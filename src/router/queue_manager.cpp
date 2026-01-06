/**
 * @file queue_manager.cpp
 * @brief Persistent message queue implementation with SQLite storage
 */

#include "pacs/bridge/router/queue_manager.h"

#include "pacs/bridge/monitoring/bridge_metrics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <future>
#include <iomanip>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <vector>

// SQLite header - using sqlite3 from vcpkg
#include <sqlite3.h>

namespace pacs::bridge::router {

// =============================================================================
// IExecutor Job Implementations (when available)
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD

/**
 * @brief Job implementation for queue worker execution
 *
 * Wraps a single worker iteration for execution via IExecutor.
 */
class queue_worker_job : public kcenon::common::interfaces::IJob {
public:
    explicit queue_worker_job(std::function<void()> work_func)
        : work_func_(std::move(work_func)) {}

    kcenon::common::VoidResult execute() override {
        if (work_func_) {
            work_func_();
        }
        return {};
    }

    std::string get_name() const override { return "queue_worker"; }

private:
    std::function<void()> work_func_;
};

/**
 * @brief Job implementation for cleanup task execution
 *
 * Wraps periodic cleanup operations for execution via IExecutor.
 */
class queue_cleanup_job : public kcenon::common::interfaces::IJob {
public:
    explicit queue_cleanup_job(std::function<void()> cleanup_func)
        : cleanup_func_(std::move(cleanup_func)) {}

    kcenon::common::VoidResult execute() override {
        if (cleanup_func_) {
            cleanup_func_();
        }
        return {};
    }

    std::string get_name() const override { return "queue_cleanup"; }

private:
    std::function<void()> cleanup_func_;
};

#endif  // PACS_BRIDGE_STANDALONE_BUILD

namespace {

/**
 * @brief Generate a unique message ID
 */
std::string generate_message_id() {
    static std::atomic<uint64_t> counter{0};
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(12) << millis
        << std::setw(4) << (counter++ & 0xFFFF)
        << std::setw(8) << (dis(gen) & 0xFFFFFFFF);
    return oss.str();
}

/**
 * @brief Convert time_point to SQLite timestamp string
 */
std::string to_sqlite_timestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                      tp.time_since_epoch())
                      .count() %
                  1000;
    std::tm tm_val{};
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << millis;
    return oss.str();
}

/**
 * @brief Parse SQLite timestamp string to time_point
 */
std::chrono::system_clock::time_point from_sqlite_timestamp(const std::string& str) {
    if (str.empty()) {
        return std::chrono::system_clock::now();
    }

    std::tm tm_val{};
    int millis = 0;

    // Parse: "YYYY-MM-DD HH:MM:SS.mmm"
    std::istringstream iss(str);
    iss >> std::get_time(&tm_val, "%Y-%m-%d %H:%M:%S");
    if (iss.peek() == '.') {
        iss.ignore();
        iss >> millis;
    }

#ifdef _WIN32
    auto time_t_val = _mkgmtime(&tm_val);
#else
    auto time_t_val = timegm(&tm_val);
#endif

    return std::chrono::system_clock::from_time_t(time_t_val) +
           std::chrono::milliseconds(millis);
}

/**
 * @brief Calculate next retry delay with exponential backoff
 */
std::chrono::seconds calculate_retry_delay(int attempt_count,
                                            std::chrono::seconds initial_delay,
                                            double multiplier,
                                            std::chrono::seconds max_delay) {
    if (attempt_count <= 0) {
        return std::chrono::seconds{0};
    }

    double delay_seconds = static_cast<double>(initial_delay.count());
    for (int i = 1; i < attempt_count; ++i) {
        delay_seconds *= multiplier;
    }

    auto calculated = std::chrono::seconds{static_cast<int64_t>(delay_seconds)};
    return std::min(calculated, max_delay);
}

}  // namespace

// =============================================================================
// queue_manager::impl
// =============================================================================

class queue_manager::impl {
public:
    queue_config config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> workers_running_{false};

    // SQLite database
    sqlite3* db_ = nullptr;
    mutable std::shared_mutex db_mutex_;

    // Worker threads
    std::vector<std::thread> worker_threads_;
    std::condition_variable_any worker_cv_;
    sender_function sender_;

    // Callbacks
    dead_letter_callback dead_letter_callback_;
    delivery_callback delivery_callback_;

    // Statistics
    mutable std::mutex stats_mutex_;
    queue_statistics stats_;

    // Cleanup thread
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_{false};
    std::condition_variable cleanup_cv_;
    std::mutex cleanup_mutex_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    // Futures for tracking executor-based jobs
    std::vector<std::future<void>> worker_futures_;
    std::future<void> cleanup_future_;
#endif

    explicit impl(const queue_config& config) : config_(config) {}

    ~impl() { stop(); }

    void stop() {
        // Stop cleanup thread
        cleanup_running_ = false;
        cleanup_cv_.notify_all();

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        // Wait for executor-based cleanup job to complete
        if (config_.executor && cleanup_future_.valid()) {
            cleanup_future_.wait_for(std::chrono::seconds{5});
        }
#endif

        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }

        // Stop workers
        stop_workers();

        // Close database
        {
            std::unique_lock<std::shared_mutex> lock(db_mutex_);
            if (db_) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
        }

        running_ = false;
    }

    void stop_workers() {
        workers_running_ = false;
        worker_cv_.notify_all();

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        // Wait for executor-based worker jobs to complete
        if (config_.executor) {
            for (auto& future : worker_futures_) {
                if (future.valid()) {
                    future.wait_for(std::chrono::seconds{5});
                }
            }
            worker_futures_.clear();
        }
#endif

        for (auto& worker : worker_threads_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        worker_threads_.clear();
    }

    std::expected<void, queue_error> start() {
        if (running_) {
            return std::unexpected(queue_error::already_running);
        }

        if (!config_.is_valid()) {
            return std::unexpected(queue_error::invalid_message);
        }

        // Open database
        auto db_result = open_database();
        if (!db_result) {
            return db_result;
        }

        running_ = true;

        // Recover in-progress messages
        recover_internal();

        // Start cleanup thread
        cleanup_running_ = true;
#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (config_.executor) {
            schedule_cleanup_job();
        } else {
            cleanup_thread_ = std::thread([this]() { cleanup_loop(); });
        }
#else
        cleanup_thread_ = std::thread([this]() { cleanup_loop(); });
#endif

        return {};
    }

    std::expected<void, queue_error> open_database() {
        std::unique_lock<std::shared_mutex> lock(db_mutex_);

        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        int rc = sqlite3_open_v2(config_.database_path.c_str(), &db_, flags, nullptr);
        if (rc != SQLITE_OK) {
            if (db_) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
            return std::unexpected(queue_error::database_error);
        }

        // Enable WAL mode for better concurrent access
        if (config_.enable_wal_mode) {
            execute_sql("PRAGMA journal_mode=WAL");
            execute_sql("PRAGMA synchronous=NORMAL");
        }

        // Set busy timeout
        sqlite3_busy_timeout(db_, 5000);

        // Create tables
        auto create_result = create_tables();
        if (!create_result) {
            sqlite3_close(db_);
            db_ = nullptr;
            return create_result;
        }

        return {};
    }

    std::expected<void, queue_error> create_tables() {
        // Main queue table
        const char* queue_table = R"(
            CREATE TABLE IF NOT EXISTS message_queue (
                id TEXT PRIMARY KEY,
                destination TEXT NOT NULL,
                payload TEXT NOT NULL,
                priority INTEGER DEFAULT 0,
                state INTEGER DEFAULT 0,
                created_at TEXT NOT NULL,
                scheduled_at TEXT NOT NULL,
                attempt_count INTEGER DEFAULT 0,
                last_error TEXT,
                correlation_id TEXT,
                message_type TEXT
            )
        )";

        // Dead letter table
        const char* dead_letter_table = R"(
            CREATE TABLE IF NOT EXISTS dead_letter_queue (
                id TEXT PRIMARY KEY,
                destination TEXT NOT NULL,
                payload TEXT NOT NULL,
                priority INTEGER DEFAULT 0,
                created_at TEXT NOT NULL,
                attempt_count INTEGER DEFAULT 0,
                reason TEXT NOT NULL,
                dead_lettered_at TEXT NOT NULL,
                error_history TEXT,
                correlation_id TEXT,
                message_type TEXT
            )
        )";

        // Indexes for efficient querying
        const char* indexes[] = {
            "CREATE INDEX IF NOT EXISTS idx_queue_state ON message_queue(state)",
            "CREATE INDEX IF NOT EXISTS idx_queue_destination ON message_queue(destination)",
            "CREATE INDEX IF NOT EXISTS idx_queue_scheduled ON message_queue(scheduled_at)",
            "CREATE INDEX IF NOT EXISTS idx_queue_priority ON message_queue(priority)",
            "CREATE INDEX IF NOT EXISTS idx_dlq_destination ON dead_letter_queue(destination)"};

        if (!execute_sql(queue_table)) {
            return std::unexpected(queue_error::database_error);
        }

        if (!execute_sql(dead_letter_table)) {
            return std::unexpected(queue_error::database_error);
        }

        for (const auto* idx : indexes) {
            execute_sql(idx);  // Indexes are optional, don't fail on error
        }

        return {};
    }

    bool execute_sql(const char* sql) {
        char* error_msg = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error_msg);
        if (rc != SQLITE_OK) {
            if (error_msg) {
                sqlite3_free(error_msg);
            }
            return false;
        }
        return true;
    }

    void cleanup_loop() {
        // Queue depth update interval (5 seconds as per issue requirement)
        constexpr auto metrics_update_interval = std::chrono::seconds{5};
        auto last_cleanup = std::chrono::steady_clock::now();

        while (cleanup_running_) {
            std::unique_lock<std::mutex> lock(cleanup_mutex_);
            cleanup_cv_.wait_for(lock, metrics_update_interval,
                                  [this]() { return !cleanup_running_.load(); });

            if (!cleanup_running_) break;

            // Update queue depth metrics for each destination
            update_queue_depth_metrics();

            // Check if cleanup interval has passed
            auto now = std::chrono::steady_clock::now();
            if (now - last_cleanup >= config_.cleanup_interval) {
                cleanup_expired_internal();
                last_cleanup = now;
            }
        }
    }

    void update_queue_depth_metrics() {
        auto destinations = destinations_internal();
        auto& metrics = monitoring::bridge_metrics_collector::instance();

        // Update total queue depth
        size_t total_depth = queue_depth_internal("");
        metrics.set_queue_depth("total", total_depth);

        // Update depth per destination
        for (const auto& dest : destinations) {
            size_t depth = queue_depth_internal(dest);
            metrics.set_queue_depth(dest, depth);
        }
    }

    size_t cleanup_expired_internal() {
        if (config_.message_ttl.count() == 0) {
            return 0;  // No TTL configured
        }

        std::unique_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) return 0;

        auto cutoff = std::chrono::system_clock::now() - config_.message_ttl;
        std::string cutoff_str = to_sqlite_timestamp(cutoff);

        // Move expired messages to dead letter
        const char* select_sql =
            "SELECT id FROM message_queue WHERE created_at < ? AND state != ?";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return 0;

        sqlite3_bind_text(stmt, 1, cutoff_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, static_cast<int>(message_state::delivered));

        std::vector<std::string> expired_ids;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (id) {
                expired_ids.emplace_back(id);
            }
        }
        sqlite3_finalize(stmt);

        size_t count = 0;
        for (const auto& id : expired_ids) {
            // Get the message first
            auto msg = get_message_internal(id);
            if (msg) {
                // Move to dead letter
                dead_letter_internal(id, "Message expired (TTL exceeded)");
                count++;
            }
        }

        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.expired_count += count;
        }

        return count;
    }

    size_t recover_internal() {
        std::unique_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) return 0;

        // Reset processing state to pending
        const char* sql =
            "UPDATE message_queue SET state = ?, scheduled_at = ? "
            "WHERE state = ?";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return 0;

        std::string now_str = to_sqlite_timestamp(std::chrono::system_clock::now());
        sqlite3_bind_int(stmt, 1, static_cast<int>(message_state::pending));
        sqlite3_bind_text(stmt, 2, now_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, static_cast<int>(message_state::processing));

        rc = sqlite3_step(stmt);
        int changes = sqlite3_changes(db_);
        sqlite3_finalize(stmt);

        return static_cast<size_t>(changes);
    }

    std::expected<std::string, queue_error> enqueue_internal(std::string_view destination,
                                                              std::string_view payload,
                                                              int priority,
                                                              std::string_view correlation_id,
                                                              std::string_view message_type) {
        if (destination.empty() || payload.empty()) {
            return std::unexpected(queue_error::invalid_message);
        }

        // Check queue size
        if (queue_depth_internal("") >= config_.max_queue_size) {
            return std::unexpected(queue_error::queue_full);
        }

        std::string id = generate_message_id();
        auto now = std::chrono::system_clock::now();
        std::string now_str = to_sqlite_timestamp(now);

        std::unique_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) {
            return std::unexpected(queue_error::not_running);
        }

        const char* sql =
            "INSERT INTO message_queue "
            "(id, destination, payload, priority, state, created_at, scheduled_at, "
            "attempt_count, correlation_id, message_type) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, ?)";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return std::unexpected(queue_error::database_error);
        }

        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, std::string(destination).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, std::string(payload).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, priority);
        sqlite3_bind_int(stmt, 5, static_cast<int>(message_state::pending));
        sqlite3_bind_text(stmt, 6, now_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, now_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, std::string(correlation_id).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, std::string(message_type).c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            return std::unexpected(queue_error::database_error);
        }

        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_enqueued++;
            stats_.pending_count++;
        }

        // Record metrics
        monitoring::bridge_metrics_collector::instance().record_message_enqueued(
            std::string(destination));

        // Notify workers
        worker_cv_.notify_one();

        return id;
    }

    std::optional<queued_message> dequeue_internal(std::string_view destination) {
        std::unique_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) return std::nullopt;

        auto now = std::chrono::system_clock::now();
        std::string now_str = to_sqlite_timestamp(now);

        // Select highest priority message that is ready
        std::string sql =
            "SELECT id, destination, payload, priority, state, created_at, "
            "scheduled_at, attempt_count, last_error, correlation_id, message_type "
            "FROM message_queue "
            "WHERE (state = ? OR (state = ? AND scheduled_at <= ?))";

        if (!destination.empty()) {
            sql += " AND destination = ?";
        }

        sql += " ORDER BY priority ASC, scheduled_at ASC LIMIT 1";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return std::nullopt;

        int param = 1;
        sqlite3_bind_int(stmt, param++, static_cast<int>(message_state::pending));
        sqlite3_bind_int(stmt, param++, static_cast<int>(message_state::retry_scheduled));
        sqlite3_bind_text(stmt, param++, now_str.c_str(), -1, SQLITE_STATIC);
        if (!destination.empty()) {
            sqlite3_bind_text(stmt, param++, std::string(destination).c_str(), -1,
                              SQLITE_TRANSIENT);
        }

        std::optional<queued_message> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            queued_message msg;
            msg.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            msg.destination = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            msg.payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            msg.priority = sqlite3_column_int(stmt, 3);
            msg.state = static_cast<message_state>(sqlite3_column_int(stmt, 4));
            msg.created_at =
                from_sqlite_timestamp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
            msg.scheduled_at =
                from_sqlite_timestamp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
            msg.attempt_count = sqlite3_column_int(stmt, 7);

            const char* last_error =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            if (last_error) msg.last_error = last_error;

            const char* corr_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            if (corr_id) msg.correlation_id = corr_id;

            const char* msg_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            if (msg_type) msg.message_type = msg_type;

            result = msg;
        }
        sqlite3_finalize(stmt);

        if (result) {
            // Update state to processing
            const char* update_sql =
                "UPDATE message_queue SET state = ?, attempt_count = attempt_count + 1 "
                "WHERE id = ?";

            rc = sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr);
            if (rc == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, static_cast<int>(message_state::processing));
                sqlite3_bind_text(stmt, 2, result->id.c_str(), -1, SQLITE_STATIC);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);

                result->state = message_state::processing;
                result->attempt_count++;
            }

            // Update statistics
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                if (stats_.pending_count > 0) stats_.pending_count--;
                stats_.processing_count++;
            }
        }

        return result;
    }

    std::expected<void, queue_error> ack_internal(std::string_view message_id) {
        // Get message destination before deletion for metrics
        std::string destination;
        {
            auto msg = get_message_internal(std::string(message_id));
            if (msg) {
                destination = msg->destination;
            }
        }

        std::unique_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) {
            return std::unexpected(queue_error::not_running);
        }

        const char* sql = "DELETE FROM message_queue WHERE id = ?";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return std::unexpected(queue_error::database_error);
        }

        sqlite3_bind_text(stmt, 1, std::string(message_id).c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        int changes = sqlite3_changes(db_);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            return std::unexpected(queue_error::database_error);
        }

        if (changes == 0) {
            return std::unexpected(queue_error::message_not_found);
        }

        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_delivered++;
            if (stats_.processing_count > 0) stats_.processing_count--;
        }

        // Record metrics
        if (!destination.empty()) {
            monitoring::bridge_metrics_collector::instance().record_message_delivered(
                destination);
        }

        return {};
    }

    std::expected<void, queue_error> nack_internal(std::string_view message_id,
                                                    std::string_view error) {
        // Get current message to check attempt count
        auto msg = get_message_internal(std::string(message_id));
        if (!msg) {
            return std::unexpected(queue_error::message_not_found);
        }

        // Record delivery failure metric
        monitoring::bridge_metrics_collector::instance().record_delivery_failure(
            msg->destination);

        // Check if max retries exceeded
        if (static_cast<size_t>(msg->attempt_count) >= config_.max_retry_count) {
            return dead_letter_internal(message_id, std::string("Max retries exceeded: ") +
                                                        std::string(error));
        }

        // Calculate next retry time
        auto delay = calculate_retry_delay(msg->attempt_count, config_.initial_retry_delay,
                                            config_.retry_backoff_multiplier,
                                            config_.max_retry_delay);
        auto next_retry = std::chrono::system_clock::now() + delay;
        std::string next_retry_str = to_sqlite_timestamp(next_retry);

        std::unique_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) {
            return std::unexpected(queue_error::not_running);
        }

        const char* sql =
            "UPDATE message_queue SET state = ?, scheduled_at = ?, last_error = ? "
            "WHERE id = ?";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return std::unexpected(queue_error::database_error);
        }

        sqlite3_bind_int(stmt, 1, static_cast<int>(message_state::retry_scheduled));
        sqlite3_bind_text(stmt, 2, next_retry_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, std::string(error).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, std::string(message_id).c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            return std::unexpected(queue_error::database_error);
        }

        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_retries++;
            if (stats_.processing_count > 0) stats_.processing_count--;
            stats_.retry_scheduled_count++;
        }

        return {};
    }

    std::expected<void, queue_error> dead_letter_internal(std::string_view message_id,
                                                           std::string_view reason) {
        auto msg = get_message_internal(std::string(message_id));
        if (!msg) {
            return std::unexpected(queue_error::message_not_found);
        }

        auto now = std::chrono::system_clock::now();
        std::string now_str = to_sqlite_timestamp(now);

        std::unique_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) {
            return std::unexpected(queue_error::not_running);
        }

        // Begin transaction
        execute_sql("BEGIN TRANSACTION");

        // Insert into dead letter queue
        const char* insert_sql =
            "INSERT INTO dead_letter_queue "
            "(id, destination, payload, priority, created_at, attempt_count, "
            "reason, dead_lettered_at, error_history, correlation_id, message_type) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            execute_sql("ROLLBACK");
            return std::unexpected(queue_error::database_error);
        }

        std::string created_str = to_sqlite_timestamp(msg->created_at);
        sqlite3_bind_text(stmt, 1, msg->id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, msg->destination.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, msg->payload.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, msg->priority);
        sqlite3_bind_text(stmt, 5, created_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 6, msg->attempt_count);
        sqlite3_bind_text(stmt, 7, std::string(reason).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, now_str.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, msg->last_error.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 10, msg->correlation_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 11, msg->message_type.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            execute_sql("ROLLBACK");
            return std::unexpected(queue_error::database_error);
        }

        // Delete from main queue
        const char* delete_sql = "DELETE FROM message_queue WHERE id = ?";
        rc = sqlite3_prepare_v2(db_, delete_sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            execute_sql("ROLLBACK");
            return std::unexpected(queue_error::database_error);
        }

        sqlite3_bind_text(stmt, 1, std::string(message_id).c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            execute_sql("ROLLBACK");
            return std::unexpected(queue_error::database_error);
        }

        execute_sql("COMMIT");

        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.total_dead_lettered++;
            if (stats_.processing_count > 0) stats_.processing_count--;
            stats_.dead_letter_count++;
        }

        // Record dead letter metric
        monitoring::bridge_metrics_collector::instance().record_dead_letter(
            msg->destination);

        // Notify callback
        if (dead_letter_callback_) {
            dead_letter_entry entry;
            entry.message = *msg;
            entry.reason = std::string(reason);
            entry.dead_lettered_at = now;
            entry.error_history.push_back(msg->last_error);
            dead_letter_callback_(entry);
        }

        return {};
    }

    std::optional<queued_message> get_message_internal(const std::string& message_id) const {
        std::shared_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) return std::nullopt;

        const char* sql =
            "SELECT id, destination, payload, priority, state, created_at, "
            "scheduled_at, attempt_count, last_error, correlation_id, message_type "
            "FROM message_queue WHERE id = ?";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return std::nullopt;

        sqlite3_bind_text(stmt, 1, message_id.c_str(), -1, SQLITE_STATIC);

        std::optional<queued_message> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            queued_message msg;
            msg.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            msg.destination = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            msg.payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            msg.priority = sqlite3_column_int(stmt, 3);
            msg.state = static_cast<message_state>(sqlite3_column_int(stmt, 4));
            msg.created_at =
                from_sqlite_timestamp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
            msg.scheduled_at =
                from_sqlite_timestamp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
            msg.attempt_count = sqlite3_column_int(stmt, 7);

            const char* last_error =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            if (last_error) msg.last_error = last_error;

            const char* corr_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            if (corr_id) msg.correlation_id = corr_id;

            const char* msg_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            if (msg_type) msg.message_type = msg_type;

            result = msg;
        }
        sqlite3_finalize(stmt);

        return result;
    }

    size_t queue_depth_internal(std::string_view destination) const {
        std::shared_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) return 0;

        // Count only pending and retry_scheduled messages (not processing or delivered)
        std::string sql = "SELECT COUNT(*) FROM message_queue WHERE state IN (?, ?)";
        if (!destination.empty()) {
            sql += " AND destination = ?";
        }

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return 0;

        sqlite3_bind_int(stmt, 1, static_cast<int>(message_state::pending));
        sqlite3_bind_int(stmt, 2, static_cast<int>(message_state::retry_scheduled));
        if (!destination.empty()) {
            sqlite3_bind_text(stmt, 3, std::string(destination).c_str(), -1, SQLITE_TRANSIENT);
        }

        size_t count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);

        return count;
    }

    void worker_loop() {
        while (workers_running_) {
            std::optional<queued_message> msg;

            {
                std::shared_lock<std::shared_mutex> lock(db_mutex_);
                worker_cv_.wait(lock, [this]() {
                    return !workers_running_ || queue_depth_internal("") > 0;
                });

                if (!workers_running_) break;
            }

            msg = dequeue_internal("");
            if (!msg) {
                // No message ready, wait a bit
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // Process the message
            if (sender_) {
                auto start = std::chrono::steady_clock::now();
                auto result = sender_(*msg);
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                if (result) {
                    ack_internal(msg->id);

                    // Update average delivery time
                    {
                        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                        double total = stats_.avg_delivery_time_ms *
                                       (stats_.total_delivered > 0 ? stats_.total_delivered - 1 : 0);
                        stats_.avg_delivery_time_ms =
                            (total + static_cast<double>(duration.count())) /
                            std::max(stats_.total_delivered, size_t{1});
                    }

                    if (delivery_callback_) {
                        delivery_callback_(*msg, true, "");
                    }
                } else {
                    nack_internal(msg->id, result.error());

                    if (delivery_callback_) {
                        delivery_callback_(*msg, false, result.error());
                    }
                }
            }
        }
    }

    void start_workers_internal() {
        if (workers_running_) return;

        workers_running_ = true;
#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (config_.executor) {
            for (size_t i = 0; i < config_.worker_count; ++i) {
                schedule_worker_job();
            }
        } else {
            for (size_t i = 0; i < config_.worker_count; ++i) {
                worker_threads_.emplace_back([this]() { worker_loop(); });
            }
        }
#else
        for (size_t i = 0; i < config_.worker_count; ++i) {
            worker_threads_.emplace_back([this]() { worker_loop(); });
        }
#endif
    }

    std::vector<dead_letter_entry> get_dead_letters_internal(size_t limit, size_t offset) const {
        std::shared_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) return {};

        const char* sql =
            "SELECT id, destination, payload, priority, created_at, attempt_count, "
            "reason, dead_lettered_at, error_history, correlation_id, message_type "
            "FROM dead_letter_queue ORDER BY dead_lettered_at DESC LIMIT ? OFFSET ?";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return {};

        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(limit));
        sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(offset));

        std::vector<dead_letter_entry> results;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            dead_letter_entry entry;
            entry.message.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            entry.message.destination =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            entry.message.payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            entry.message.priority = sqlite3_column_int(stmt, 3);
            entry.message.created_at = from_sqlite_timestamp(
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
            entry.message.attempt_count = sqlite3_column_int(stmt, 5);
            entry.reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            entry.dead_lettered_at = from_sqlite_timestamp(
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));

            const char* error_history =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            if (error_history) {
                entry.error_history.push_back(error_history);
            }

            const char* corr_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            if (corr_id) entry.message.correlation_id = corr_id;

            const char* msg_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
            if (msg_type) entry.message.message_type = msg_type;

            entry.message.state = message_state::dead_letter;
            results.push_back(std::move(entry));
        }
        sqlite3_finalize(stmt);

        return results;
    }

    size_t dead_letter_count_internal() const {
        std::shared_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) return 0;

        const char* sql = "SELECT COUNT(*) FROM dead_letter_queue";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return 0;

        size_t count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);

        return count;
    }

    std::vector<std::string> destinations_internal() const {
        std::shared_lock<std::shared_mutex> lock(db_mutex_);
        if (!db_) return {};

        const char* sql = "SELECT DISTINCT destination FROM message_queue WHERE state != ?";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return {};

        sqlite3_bind_int(stmt, 1, static_cast<int>(message_state::delivered));

        std::vector<std::string> results;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* dest = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (dest) {
                results.emplace_back(dest);
            }
        }
        sqlite3_finalize(stmt);

        return results;
    }

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    /**
     * @brief Schedule worker job using IExecutor
     *
     * Processes messages and reschedules itself for continuous operation.
     */
    void schedule_worker_job() {
        if (!workers_running_ || !config_.executor) {
            return;
        }

        auto job = std::make_unique<queue_worker_job>([this]() {
            if (!workers_running_) {
                return;
            }

            std::optional<queued_message> msg;

            {
                std::shared_lock<std::shared_mutex> lock(db_mutex_);
                worker_cv_.wait_for(lock, std::chrono::milliseconds{100}, [this]() {
                    return !workers_running_ || queue_depth_internal("") > 0;
                });

                if (!workers_running_) {
                    return;
                }
            }

            msg = dequeue_internal("");
            if (msg && sender_) {
                auto start = std::chrono::steady_clock::now();
                auto result = sender_(*msg);
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

                if (result) {
                    ack_internal(msg->id);

                    // Update average delivery time
                    {
                        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                        double total = stats_.avg_delivery_time_ms *
                                       (stats_.total_delivered > 0 ? stats_.total_delivered - 1 : 0);
                        stats_.avg_delivery_time_ms =
                            (total + static_cast<double>(duration.count())) /
                            std::max(stats_.total_delivered, size_t{1});
                    }

                    if (delivery_callback_) {
                        delivery_callback_(*msg, true, "");
                    }
                } else {
                    nack_internal(msg->id, result.error());

                    if (delivery_callback_) {
                        delivery_callback_(*msg, false, result.error());
                    }
                }
            }

            // Reschedule for next iteration
            schedule_worker_job();
        });

        auto result = config_.executor->execute(std::move(job));
        if (result) {
            worker_futures_.push_back(std::move(*result));
        }
    }

    /**
     * @brief Schedule cleanup job using IExecutor with delayed execution
     *
     * Performs periodic cleanup and reschedules itself.
     */
    void schedule_cleanup_job() {
        if (!cleanup_running_ || !config_.executor) {
            return;
        }

        // Queue depth update interval (5 seconds as per issue requirement)
        constexpr auto metrics_update_interval = std::chrono::milliseconds{5000};

        auto job = std::make_unique<queue_cleanup_job>([this]() {
            if (!cleanup_running_) {
                return;
            }

            // Update queue depth metrics
            update_queue_depth_metrics();

            // Check if cleanup interval has passed (handled by delayed scheduling)
            static auto last_cleanup = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - last_cleanup >= config_.cleanup_interval) {
                cleanup_expired_internal();
                last_cleanup = now;
            }

            // Reschedule for next iteration
            schedule_cleanup_job();
        });

        auto result = config_.executor->execute_delayed(std::move(job), metrics_update_interval);
        if (result) {
            cleanup_future_ = std::move(*result);
        }
    }
#endif  // PACS_BRIDGE_STANDALONE_BUILD
};

// =============================================================================
// queue_manager Implementation
// =============================================================================

queue_manager::queue_manager() : pimpl_(std::make_unique<impl>(queue_config{})) {}

queue_manager::queue_manager(const queue_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

queue_manager::~queue_manager() = default;

queue_manager::queue_manager(queue_manager&&) noexcept = default;
queue_manager& queue_manager::operator=(queue_manager&&) noexcept = default;

std::expected<void, queue_error> queue_manager::start() {
    return pimpl_->start();
}

void queue_manager::stop() {
    pimpl_->stop();
}

bool queue_manager::is_running() const noexcept {
    return pimpl_->running_;
}

std::expected<std::string, queue_error> queue_manager::enqueue(std::string_view destination,
                                                                std::string_view payload,
                                                                int priority) {
    if (!pimpl_->running_) {
        return std::unexpected(queue_error::not_running);
    }
    return pimpl_->enqueue_internal(destination, payload, priority, "", "");
}

std::expected<std::string, queue_error> queue_manager::enqueue(std::string_view destination,
                                                                std::string_view payload,
                                                                int priority,
                                                                std::string_view correlation_id,
                                                                std::string_view message_type) {
    if (!pimpl_->running_) {
        return std::unexpected(queue_error::not_running);
    }
    return pimpl_->enqueue_internal(destination, payload, priority, correlation_id, message_type);
}

std::optional<queued_message> queue_manager::dequeue(std::string_view destination) {
    if (!pimpl_->running_) {
        return std::nullopt;
    }
    return pimpl_->dequeue_internal(destination);
}

std::vector<queued_message> queue_manager::dequeue_batch(size_t count,
                                                          std::string_view destination) {
    if (!pimpl_->running_) {
        return {};
    }

    std::vector<queued_message> results;
    results.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto msg = pimpl_->dequeue_internal(destination);
        if (!msg) break;
        results.push_back(std::move(*msg));
    }
    return results;
}

std::expected<void, queue_error> queue_manager::ack(std::string_view message_id) {
    if (!pimpl_->running_) {
        return std::unexpected(queue_error::not_running);
    }
    return pimpl_->ack_internal(message_id);
}

std::expected<void, queue_error> queue_manager::nack(std::string_view message_id,
                                                      std::string_view error) {
    if (!pimpl_->running_) {
        return std::unexpected(queue_error::not_running);
    }
    return pimpl_->nack_internal(message_id, error);
}

std::expected<void, queue_error> queue_manager::dead_letter(std::string_view message_id,
                                                             std::string_view reason) {
    if (!pimpl_->running_) {
        return std::unexpected(queue_error::not_running);
    }
    return pimpl_->dead_letter_internal(message_id, reason);
}

void queue_manager::start_workers(sender_function sender) {
    pimpl_->sender_ = std::move(sender);
    pimpl_->start_workers_internal();
}

void queue_manager::stop_workers() {
    pimpl_->stop_workers();
}

bool queue_manager::workers_running() const noexcept {
    return pimpl_->workers_running_;
}

std::vector<dead_letter_entry> queue_manager::get_dead_letters(size_t limit,
                                                                size_t offset) const {
    return pimpl_->get_dead_letters_internal(limit, offset);
}

size_t queue_manager::dead_letter_count() const {
    return pimpl_->dead_letter_count_internal();
}

std::expected<void, queue_error> queue_manager::retry_dead_letter(std::string_view message_id) {
    if (!pimpl_->running_) {
        return std::unexpected(queue_error::not_running);
    }

    // Get dead letter entry
    auto entries = pimpl_->get_dead_letters_internal(1, 0);
    std::optional<dead_letter_entry> target;
    for (const auto& entry : entries) {
        if (entry.message.id == message_id) {
            target = entry;
            break;
        }
    }

    // Search in all dead letters for the specific ID
    std::unique_lock<std::shared_mutex> lock(pimpl_->db_mutex_);
    if (!pimpl_->db_) {
        return std::unexpected(queue_error::not_running);
    }

    // Get the dead letter message
    const char* select_sql =
        "SELECT id, destination, payload, priority, created_at, correlation_id, message_type "
        "FROM dead_letter_queue WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(pimpl_->db_, select_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::unexpected(queue_error::database_error);
    }

    sqlite3_bind_text(stmt, 1, std::string(message_id).c_str(), -1, SQLITE_TRANSIENT);

    queued_message msg;
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        msg.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        msg.destination = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        msg.payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        msg.priority = sqlite3_column_int(stmt, 3);
        msg.created_at = from_sqlite_timestamp(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));

        const char* corr_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (corr_id) msg.correlation_id = corr_id;

        const char* msg_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (msg_type) msg.message_type = msg_type;

        found = true;
    }
    sqlite3_finalize(stmt);

    if (!found) {
        return std::unexpected(queue_error::message_not_found);
    }

    // Begin transaction
    pimpl_->execute_sql("BEGIN TRANSACTION");

    // Insert back into main queue with reset attempt count
    auto now = std::chrono::system_clock::now();
    std::string now_str = to_sqlite_timestamp(now);
    std::string created_str = to_sqlite_timestamp(msg.created_at);

    const char* insert_sql =
        "INSERT INTO message_queue "
        "(id, destination, payload, priority, state, created_at, scheduled_at, "
        "attempt_count, correlation_id, message_type) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, ?)";

    rc = sqlite3_prepare_v2(pimpl_->db_, insert_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        pimpl_->execute_sql("ROLLBACK");
        return std::unexpected(queue_error::database_error);
    }

    sqlite3_bind_text(stmt, 1, msg.id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, msg.destination.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, msg.payload.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, msg.priority);
    sqlite3_bind_int(stmt, 5, static_cast<int>(message_state::pending));
    sqlite3_bind_text(stmt, 6, created_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, now_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, msg.correlation_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, msg.message_type.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        pimpl_->execute_sql("ROLLBACK");
        return std::unexpected(queue_error::database_error);
    }

    // Delete from dead letter queue
    const char* delete_sql = "DELETE FROM dead_letter_queue WHERE id = ?";
    rc = sqlite3_prepare_v2(pimpl_->db_, delete_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        pimpl_->execute_sql("ROLLBACK");
        return std::unexpected(queue_error::database_error);
    }

    sqlite3_bind_text(stmt, 1, std::string(message_id).c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        pimpl_->execute_sql("ROLLBACK");
        return std::unexpected(queue_error::database_error);
    }

    pimpl_->execute_sql("COMMIT");

    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(pimpl_->stats_mutex_);
        if (pimpl_->stats_.dead_letter_count > 0) pimpl_->stats_.dead_letter_count--;
        pimpl_->stats_.pending_count++;
    }

    // Notify workers
    pimpl_->worker_cv_.notify_one();

    return {};
}

std::expected<void, queue_error> queue_manager::delete_dead_letter(std::string_view message_id) {
    if (!pimpl_->running_) {
        return std::unexpected(queue_error::not_running);
    }

    std::unique_lock<std::shared_mutex> lock(pimpl_->db_mutex_);
    if (!pimpl_->db_) {
        return std::unexpected(queue_error::not_running);
    }

    const char* sql = "DELETE FROM dead_letter_queue WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(pimpl_->db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::unexpected(queue_error::database_error);
    }

    sqlite3_bind_text(stmt, 1, std::string(message_id).c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(pimpl_->db_);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return std::unexpected(queue_error::database_error);
    }

    if (changes == 0) {
        return std::unexpected(queue_error::message_not_found);
    }

    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(pimpl_->stats_mutex_);
        if (pimpl_->stats_.dead_letter_count > 0) pimpl_->stats_.dead_letter_count--;
    }

    return {};
}

size_t queue_manager::purge_dead_letters() {
    if (!pimpl_->running_) {
        return 0;
    }

    std::unique_lock<std::shared_mutex> lock(pimpl_->db_mutex_);
    if (!pimpl_->db_) {
        return 0;
    }

    size_t count = pimpl_->dead_letter_count_internal();
    pimpl_->execute_sql("DELETE FROM dead_letter_queue");

    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(pimpl_->stats_mutex_);
        pimpl_->stats_.dead_letter_count = 0;
    }

    return count;
}

void queue_manager::set_dead_letter_callback(dead_letter_callback callback) {
    pimpl_->dead_letter_callback_ = std::move(callback);
}

void queue_manager::clear_dead_letter_callback() {
    pimpl_->dead_letter_callback_ = nullptr;
}

std::optional<queued_message> queue_manager::get_message(std::string_view message_id) const {
    return pimpl_->get_message_internal(std::string(message_id));
}

std::vector<queued_message> queue_manager::get_pending(std::string_view destination,
                                                        size_t limit) const {
    if (!pimpl_->running_) {
        return {};
    }

    std::shared_lock<std::shared_mutex> lock(pimpl_->db_mutex_);
    if (!pimpl_->db_) return {};

    std::string sql =
        "SELECT id, destination, payload, priority, state, created_at, "
        "scheduled_at, attempt_count, last_error, correlation_id, message_type "
        "FROM message_queue WHERE state IN (?, ?)";

    if (!destination.empty()) {
        sql += " AND destination = ?";
    }

    sql += " ORDER BY priority ASC, scheduled_at ASC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(pimpl_->db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return {};

    int param = 1;
    sqlite3_bind_int(stmt, param++, static_cast<int>(message_state::pending));
    sqlite3_bind_int(stmt, param++, static_cast<int>(message_state::retry_scheduled));
    if (!destination.empty()) {
        sqlite3_bind_text(stmt, param++, std::string(destination).c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int64(stmt, param++, static_cast<int64_t>(limit));

    std::vector<queued_message> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        queued_message msg;
        msg.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        msg.destination = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        msg.payload = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        msg.priority = sqlite3_column_int(stmt, 3);
        msg.state = static_cast<message_state>(sqlite3_column_int(stmt, 4));
        msg.created_at =
            from_sqlite_timestamp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        msg.scheduled_at =
            from_sqlite_timestamp(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
        msg.attempt_count = sqlite3_column_int(stmt, 7);

        const char* last_error = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        if (last_error) msg.last_error = last_error;

        const char* corr_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (corr_id) msg.correlation_id = corr_id;

        const char* msg_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        if (msg_type) msg.message_type = msg_type;

        results.push_back(std::move(msg));
    }
    sqlite3_finalize(stmt);

    return results;
}

size_t queue_manager::queue_depth() const {
    return pimpl_->queue_depth_internal("");
}

size_t queue_manager::queue_depth(std::string_view destination) const {
    return pimpl_->queue_depth_internal(destination);
}

std::vector<std::string> queue_manager::destinations() const {
    return pimpl_->destinations_internal();
}

queue_statistics queue_manager::get_statistics() const {
    std::lock_guard<std::mutex> lock(pimpl_->stats_mutex_);
    queue_statistics stats = pimpl_->stats_;

    // Update current counts
    stats.pending_count = 0;
    stats.processing_count = 0;
    stats.retry_scheduled_count = 0;

    // Query actual counts from database
    std::shared_lock<std::shared_mutex> db_lock(pimpl_->db_mutex_);
    if (pimpl_->db_) {
        const char* sql =
            "SELECT state, COUNT(*) FROM message_queue GROUP BY state";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(pimpl_->db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                auto state = static_cast<message_state>(sqlite3_column_int(stmt, 0));
                size_t count = static_cast<size_t>(sqlite3_column_int64(stmt, 1));

                switch (state) {
                    case message_state::pending:
                        stats.pending_count = count;
                        break;
                    case message_state::processing:
                        stats.processing_count = count;
                        break;
                    case message_state::retry_scheduled:
                        stats.retry_scheduled_count = count;
                        break;
                    default:
                        break;
                }
            }
            sqlite3_finalize(stmt);
        }

        // Get depth by destination
        const char* dest_sql =
            "SELECT destination, COUNT(*) FROM message_queue WHERE state != ? "
            "GROUP BY destination";

        if (sqlite3_prepare_v2(pimpl_->db_, dest_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, static_cast<int>(message_state::delivered));
            stats.depth_by_destination.clear();
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* dest = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                size_t count = static_cast<size_t>(sqlite3_column_int64(stmt, 1));
                if (dest) {
                    stats.depth_by_destination.emplace_back(dest, count);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    stats.dead_letter_count = pimpl_->dead_letter_count_internal();

    return stats;
}

void queue_manager::reset_statistics() {
    std::lock_guard<std::mutex> lock(pimpl_->stats_mutex_);
    pimpl_->stats_ = queue_statistics{};
}

size_t queue_manager::cleanup_expired() {
    return pimpl_->cleanup_expired_internal();
}

void queue_manager::compact() {
    std::unique_lock<std::shared_mutex> lock(pimpl_->db_mutex_);
    if (pimpl_->db_) {
        pimpl_->execute_sql("VACUUM");
    }
}

size_t queue_manager::recover() {
    return pimpl_->recover_internal();
}

const queue_config& queue_manager::config() const noexcept {
    return pimpl_->config_;
}

void queue_manager::set_delivery_callback(delivery_callback callback) {
    pimpl_->delivery_callback_ = std::move(callback);
}

void queue_manager::clear_delivery_callback() {
    pimpl_->delivery_callback_ = nullptr;
}

// =============================================================================
// queue_config_builder Implementation
// =============================================================================

queue_config_builder queue_config_builder::create() {
    return queue_config_builder();
}

queue_config_builder::queue_config_builder() : config_() {}

queue_config_builder& queue_config_builder::database(std::string_view path) {
    config_.database_path = std::string(path);
    return *this;
}

queue_config_builder& queue_config_builder::max_size(size_t size) {
    config_.max_queue_size = size;
    return *this;
}

queue_config_builder& queue_config_builder::workers(size_t count) {
    config_.worker_count = count;
    return *this;
}

queue_config_builder& queue_config_builder::retry_policy(size_t max_retries,
                                                          std::chrono::seconds initial_delay,
                                                          double backoff_multiplier) {
    config_.max_retry_count = max_retries;
    config_.initial_retry_delay = initial_delay;
    config_.retry_backoff_multiplier = backoff_multiplier;
    return *this;
}

queue_config_builder& queue_config_builder::max_retry_delay(std::chrono::seconds delay) {
    config_.max_retry_delay = delay;
    return *this;
}

queue_config_builder& queue_config_builder::ttl(std::chrono::hours ttl) {
    config_.message_ttl = ttl;
    return *this;
}

queue_config_builder& queue_config_builder::batch_size(size_t size) {
    config_.batch_size = size;
    return *this;
}

queue_config_builder& queue_config_builder::cleanup_interval(std::chrono::minutes interval) {
    config_.cleanup_interval = interval;
    return *this;
}

queue_config_builder& queue_config_builder::wal_mode(bool enable) {
    config_.enable_wal_mode = enable;
    return *this;
}

queue_config queue_config_builder::build() const {
    return config_;
}

}  // namespace pacs::bridge::router
