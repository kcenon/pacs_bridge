/**
 * @file database_adapter.cpp
 * @brief Implementation of database adapter for pacs_bridge
 *
 * Provides SQLite-based implementation for standalone mode.
 *
 * @see include/pacs/bridge/integration/database_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/274
 */

#include "pacs/bridge/integration/database_adapter.h"

#include <sqlite3.h>

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <queue>

namespace pacs::bridge::integration {

// =============================================================================
// Error Message Mapping
// =============================================================================

std::string_view to_string(database_error error) noexcept {
    switch (error) {
        case database_error::connection_failed:
            return "Connection to database failed";
        case database_error::connection_timeout:
            return "Connection timeout exceeded";
        case database_error::query_failed:
            return "Query execution failed";
        case database_error::prepare_failed:
            return "Statement preparation failed";
        case database_error::bind_failed:
            return "Parameter binding failed";
        case database_error::transaction_failed:
            return "Transaction operation failed";
        case database_error::pool_exhausted:
            return "Connection pool exhausted";
        case database_error::invalid_config:
            return "Invalid configuration provided";
        case database_error::constraint_violation:
            return "Database constraint violation";
        case database_error::timeout:
            return "Operation timeout";
        case database_error::no_result:
            return "No result available";
        case database_error::invalid_column:
            return "Invalid column index";
        case database_error::type_conversion_failed:
            return "Type conversion failed";
    }
    return "Unknown database error";
}

// =============================================================================
// SQLite Row Implementation
// =============================================================================

class sqlite_row : public database_row {
public:
    explicit sqlite_row(sqlite3_stmt* stmt) : stmt_(stmt) {}

    [[nodiscard]] std::string get_string(std::size_t index) const override {
        if (index >= column_count()) {
            return "";
        }
        const auto* text = sqlite3_column_text(stmt_, static_cast<int>(index));
        if (!text) {
            return "";
        }
        return reinterpret_cast<const char*>(text);
    }

    [[nodiscard]] int64_t get_int64(std::size_t index) const override {
        if (index >= column_count()) {
            return 0;
        }
        return sqlite3_column_int64(stmt_, static_cast<int>(index));
    }

    [[nodiscard]] double get_double(std::size_t index) const override {
        if (index >= column_count()) {
            return 0.0;
        }
        return sqlite3_column_double(stmt_, static_cast<int>(index));
    }

    [[nodiscard]] std::vector<uint8_t> get_blob(std::size_t index) const override {
        if (index >= column_count()) {
            return {};
        }
        const auto* data = sqlite3_column_blob(stmt_, static_cast<int>(index));
        int size = sqlite3_column_bytes(stmt_, static_cast<int>(index));
        if (!data || size <= 0) {
            return {};
        }
        return {static_cast<const uint8_t*>(data),
                static_cast<const uint8_t*>(data) + size};
    }

    [[nodiscard]] bool is_null(std::size_t index) const override {
        if (index >= column_count()) {
            return true;
        }
        return sqlite3_column_type(stmt_, static_cast<int>(index)) == SQLITE_NULL;
    }

    [[nodiscard]] std::size_t column_count() const override {
        return static_cast<std::size_t>(sqlite3_column_count(stmt_));
    }

    [[nodiscard]] std::string column_name(std::size_t index) const override {
        if (index >= column_count()) {
            return "";
        }
        const char* name = sqlite3_column_name(stmt_, static_cast<int>(index));
        return name ? name : "";
    }

    [[nodiscard]] database_value get_value(std::size_t index) const override {
        if (index >= column_count()) {
            return std::monostate{};
        }

        int type = sqlite3_column_type(stmt_, static_cast<int>(index));
        switch (type) {
            case SQLITE_NULL:
                return std::monostate{};
            case SQLITE_INTEGER:
                return get_int64(index);
            case SQLITE_FLOAT:
                return get_double(index);
            case SQLITE_TEXT:
                return get_string(index);
            case SQLITE_BLOB:
                return get_blob(index);
            default:
                return std::monostate{};
        }
    }

private:
    sqlite3_stmt* stmt_;
};

// =============================================================================
// SQLite Result Implementation
// =============================================================================

class sqlite_result : public database_result {
public:
    sqlite_result(sqlite3_stmt* stmt, sqlite3* db, bool owns_stmt, bool has_first_row = false)
        : stmt_(stmt), db_(db), row_(stmt), has_row_(has_first_row),
          stepped_(has_first_row), owns_stmt_(owns_stmt), first_row_pending_(has_first_row) {}

    ~sqlite_result() override {
        if (stmt_ && owns_stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    // Non-copyable, non-movable
    sqlite_result(const sqlite_result&) = delete;
    sqlite_result& operator=(const sqlite_result&) = delete;
    sqlite_result(sqlite_result&&) = delete;
    sqlite_result& operator=(sqlite_result&&) = delete;

    [[nodiscard]] bool next() override {
        if (!stmt_) {
            return false;
        }
        // If we already have the first row from initial step, return it
        if (first_row_pending_) {
            first_row_pending_ = false;
            return has_row_;
        }
        // Step to get next row
        int rc = sqlite3_step(stmt_);
        stepped_ = true;
        has_row_ = (rc == SQLITE_ROW);
        return has_row_;
    }

    [[nodiscard]] const database_row& current_row() const override {
        return row_;
    }

    [[nodiscard]] std::size_t affected_rows() const override {
        return static_cast<std::size_t>(sqlite3_changes(db_));
    }

    [[nodiscard]] int64_t last_insert_id() const override {
        return sqlite3_last_insert_rowid(db_);
    }

    [[nodiscard]] bool empty() const override {
        if (!stepped_) {
            return true;
        }
        return !has_row_ && !first_row_pending_;
    }

private:
    sqlite3_stmt* stmt_;
    sqlite3* db_;
    sqlite_row row_;
    bool has_row_;
    bool stepped_;
    bool owns_stmt_;
    bool first_row_pending_;  // Track if first row hasn't been consumed yet
};

// =============================================================================
// SQLite Statement Result (Non-owning, for prepared statements)
// =============================================================================

class sqlite_stmt_result : public database_result {
public:
    sqlite_stmt_result(sqlite3_stmt* stmt, sqlite3* db, bool has_first_row)
        : stmt_(stmt), db_(db), row_(stmt), has_row_(has_first_row), first_row_(has_first_row) {}

    ~sqlite_stmt_result() override = default;

    // Non-copyable, non-movable
    sqlite_stmt_result(const sqlite_stmt_result&) = delete;
    sqlite_stmt_result& operator=(const sqlite_stmt_result&) = delete;
    sqlite_stmt_result(sqlite_stmt_result&&) = delete;
    sqlite_stmt_result& operator=(sqlite_stmt_result&&) = delete;

    [[nodiscard]] bool next() override {
        if (!stmt_) {
            return false;
        }
        // If we have the first row from initial step, return it once
        if (first_row_) {
            first_row_ = false;
            return has_row_;
        }
        // Continue stepping for additional rows
        int rc = sqlite3_step(stmt_);
        has_row_ = (rc == SQLITE_ROW);
        return has_row_;
    }

    [[nodiscard]] const database_row& current_row() const override {
        return row_;
    }

    [[nodiscard]] std::size_t affected_rows() const override {
        return static_cast<std::size_t>(sqlite3_changes(db_));
    }

    [[nodiscard]] int64_t last_insert_id() const override {
        return sqlite3_last_insert_rowid(db_);
    }

    [[nodiscard]] bool empty() const override {
        return !has_row_ && !first_row_;
    }

private:
    sqlite3_stmt* stmt_;
    sqlite3* db_;
    sqlite_row row_;
    bool has_row_;
    bool first_row_;  // Track if we haven't consumed the first row yet
};

// =============================================================================
// SQLite Statement Implementation
// =============================================================================

class sqlite_statement : public database_statement {
public:
    explicit sqlite_statement(sqlite3_stmt* stmt, sqlite3* db)
        : stmt_(stmt), db_(db) {}

    ~sqlite_statement() override {
        if (stmt_) {
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
        }
    }

    // Non-copyable, non-movable
    sqlite_statement(const sqlite_statement&) = delete;
    sqlite_statement& operator=(const sqlite_statement&) = delete;
    sqlite_statement(sqlite_statement&&) = delete;
    sqlite_statement& operator=(sqlite_statement&&) = delete;

    [[nodiscard]] std::expected<void, database_error>
    bind_string(std::size_t index, std::string_view value) override {
        if (!stmt_) {
            return std::unexpected(database_error::bind_failed);
        }
        int rc = sqlite3_bind_text(stmt_, static_cast<int>(index),
                                   value.data(), static_cast<int>(value.size()),
                                   SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error>
    bind_int64(std::size_t index, int64_t value) override {
        if (!stmt_) {
            return std::unexpected(database_error::bind_failed);
        }
        int rc = sqlite3_bind_int64(stmt_, static_cast<int>(index), value);
        if (rc != SQLITE_OK) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error>
    bind_double(std::size_t index, double value) override {
        if (!stmt_) {
            return std::unexpected(database_error::bind_failed);
        }
        int rc = sqlite3_bind_double(stmt_, static_cast<int>(index), value);
        if (rc != SQLITE_OK) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error>
    bind_blob(std::size_t index, const std::vector<uint8_t>& value) override {
        if (!stmt_) {
            return std::unexpected(database_error::bind_failed);
        }
        int rc = sqlite3_bind_blob(stmt_, static_cast<int>(index),
                                   value.data(), static_cast<int>(value.size()),
                                   SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error>
    bind_null(std::size_t index) override {
        if (!stmt_) {
            return std::unexpected(database_error::bind_failed);
        }
        int rc = sqlite3_bind_null(stmt_, static_cast<int>(index));
        if (rc != SQLITE_OK) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error> clear_bindings() override {
        if (!stmt_) {
            return std::unexpected(database_error::bind_failed);
        }
        int rc = sqlite3_clear_bindings(stmt_);
        if (rc != SQLITE_OK) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error> reset() override {
        if (!stmt_) {
            return std::unexpected(database_error::prepare_failed);
        }
        int rc = sqlite3_reset(stmt_);
        if (rc != SQLITE_OK) {
            return std::unexpected(database_error::prepare_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<std::unique_ptr<database_result>, database_error>
    execute() override {
        if (!stmt_) {
            return std::unexpected(database_error::query_failed);
        }

        // For prepared statements, we execute and create a non-owning result
        // The statement can be reset and reused
        // We need to step through non-SELECT queries here
        int rc = sqlite3_step(stmt_);
        bool has_row = (rc == SQLITE_ROW);
        bool done = (rc == SQLITE_DONE);

        if (!has_row && !done) {
            return std::unexpected(database_error::query_failed);
        }

        // Create a result that tracks the execution state
        // For SELECT: caller iterates with next()
        // For INSERT/UPDATE/DELETE: already stepped, done
        return std::make_unique<sqlite_stmt_result>(stmt_, db_, has_row);
    }

    [[nodiscard]] std::size_t parameter_count() const override {
        if (!stmt_) {
            return 0;
        }
        return static_cast<std::size_t>(sqlite3_bind_parameter_count(stmt_));
    }

private:
    sqlite3_stmt* stmt_;
    sqlite3* db_;
};

// =============================================================================
// SQLite Connection Implementation
// =============================================================================

class sqlite_connection : public database_connection {
public:
    explicit sqlite_connection(const database_config& config)
        : db_(nullptr), config_(config), in_transaction_(false) {
        open();
    }

    ~sqlite_connection() override {
        close();
    }

    // Non-copyable, non-movable
    sqlite_connection(const sqlite_connection&) = delete;
    sqlite_connection& operator=(const sqlite_connection&) = delete;
    sqlite_connection(sqlite_connection&&) = delete;
    sqlite_connection& operator=(sqlite_connection&&) = delete;

    [[nodiscard]] std::expected<std::unique_ptr<database_statement>, database_error>
    prepare(std::string_view sql) override {
        if (!db_) {
            return std::unexpected(database_error::connection_failed);
        }

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()),
                                    &stmt, nullptr);
        if (rc != SQLITE_OK || !stmt) {
            last_error_ = sqlite3_errmsg(db_);
            return std::unexpected(database_error::prepare_failed);
        }

        return std::make_unique<sqlite_statement>(stmt, db_);
    }

    [[nodiscard]] std::expected<std::unique_ptr<database_result>, database_error>
    execute(std::string_view sql) override {
        if (!db_) {
            return std::unexpected(database_error::connection_failed);
        }

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()),
                                    &stmt, nullptr);
        if (rc != SQLITE_OK || !stmt) {
            last_error_ = sqlite3_errmsg(db_);
            return std::unexpected(database_error::prepare_failed);
        }

        // Execute the first step immediately
        // This handles DDL/DML that don't return rows
        rc = sqlite3_step(stmt);
        bool has_first_row = (rc == SQLITE_ROW);
        bool done = (rc == SQLITE_DONE);

        if (!has_first_row && !done) {
            last_error_ = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return std::unexpected(database_error::query_failed);
        }

        // Result owns the statement - will finalize when destroyed
        // Pass whether we have a first row to avoid double-step
        return std::make_unique<sqlite_result>(stmt, db_, true, has_first_row);
    }

    [[nodiscard]] std::expected<void, database_error> begin_transaction() override {
        if (!db_) {
            return std::unexpected(database_error::connection_failed);
        }
        if (in_transaction_) {
            return std::unexpected(database_error::transaction_failed);
        }

        char* error_msg = nullptr;
        int rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &error_msg);
        if (rc != SQLITE_OK) {
            if (error_msg) {
                last_error_ = error_msg;
                sqlite3_free(error_msg);
            }
            return std::unexpected(database_error::transaction_failed);
        }

        in_transaction_ = true;
        return {};
    }

    [[nodiscard]] std::expected<void, database_error> commit() override {
        if (!db_) {
            return std::unexpected(database_error::connection_failed);
        }
        if (!in_transaction_) {
            return std::unexpected(database_error::transaction_failed);
        }

        char* error_msg = nullptr;
        int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &error_msg);
        in_transaction_ = false;

        if (rc != SQLITE_OK) {
            if (error_msg) {
                last_error_ = error_msg;
                sqlite3_free(error_msg);
            }
            return std::unexpected(database_error::transaction_failed);
        }

        return {};
    }

    [[nodiscard]] std::expected<void, database_error> rollback() override {
        if (!db_) {
            return std::unexpected(database_error::connection_failed);
        }
        if (!in_transaction_) {
            return {};  // No transaction to rollback
        }

        char* error_msg = nullptr;
        int rc = sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &error_msg);
        in_transaction_ = false;

        if (rc != SQLITE_OK) {
            if (error_msg) {
                last_error_ = error_msg;
                sqlite3_free(error_msg);
            }
            return std::unexpected(database_error::transaction_failed);
        }

        return {};
    }

    [[nodiscard]] bool is_valid() const override {
        return db_ != nullptr;
    }

    [[nodiscard]] std::string last_error() const override {
        if (!last_error_.empty()) {
            return last_error_;
        }
        if (db_) {
            return sqlite3_errmsg(db_);
        }
        return "No connection";
    }

    [[nodiscard]] int64_t changes() const override {
        if (!db_) {
            return 0;
        }
        return sqlite3_changes(db_);
    }

    [[nodiscard]] int64_t last_insert_rowid() const override {
        if (!db_) {
            return 0;
        }
        return sqlite3_last_insert_rowid(db_);
    }

private:
    void open() {
        int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        int rc = sqlite3_open_v2(config_.database_path.c_str(), &db_, flags, nullptr);

        if (rc != SQLITE_OK) {
            if (db_) {
                last_error_ = sqlite3_errmsg(db_);
                sqlite3_close(db_);
                db_ = nullptr;
            }
            return;
        }

        // Configure connection
        sqlite3_busy_timeout(db_, config_.busy_timeout_ms);

        // Enable WAL mode if configured
        if (config_.enable_wal) {
            char* error_msg = nullptr;
            sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, &error_msg);
            if (error_msg) {
                sqlite3_free(error_msg);
            }
            sqlite3_exec(db_, "PRAGMA synchronous=NORMAL", nullptr, nullptr, &error_msg);
            if (error_msg) {
                sqlite3_free(error_msg);
            }
        }
    }

    void close() {
        if (db_) {
            if (in_transaction_) {
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
                in_transaction_ = false;
            }
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3* db_;
    database_config config_;
    std::string last_error_;
    bool in_transaction_;
};

// =============================================================================
// SQLite Database Adapter Implementation
// =============================================================================

class sqlite_database_adapter : public database_adapter {
public:
    explicit sqlite_database_adapter(const database_config& config)
        : config_(config) {
        if (config_.pool_size == 0) {
            config_.pool_size = 1;
        }

        // Pre-create connections for the pool
        for (std::size_t i = 0; i < config_.pool_size; ++i) {
            auto conn = std::make_shared<sqlite_connection>(config_);
            if (conn->is_valid()) {
                pool_.push(conn);
            }
        }
    }

    ~sqlite_database_adapter() override {
        // Clear the pool
        std::lock_guard<std::mutex> lock(pool_mutex_);
        while (!pool_.empty()) {
            pool_.pop();
        }
    }

    // Non-copyable, non-movable
    sqlite_database_adapter(const sqlite_database_adapter&) = delete;
    sqlite_database_adapter& operator=(const sqlite_database_adapter&) = delete;
    sqlite_database_adapter(sqlite_database_adapter&&) = delete;
    sqlite_database_adapter& operator=(sqlite_database_adapter&&) = delete;

    [[nodiscard]] std::expected<std::shared_ptr<database_connection>, database_error>
    acquire_connection() override {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        if (pool_.empty()) {
            // Try to create a new connection if under limit
            if (active_count_ < config_.pool_size * 2) {
                auto conn = std::make_shared<sqlite_connection>(config_);
                if (conn->is_valid()) {
                    active_count_++;
                    stats_.connections_acquired++;
                    update_peak_locked();
                    return conn;
                }
            }
            stats_.connection_failures++;
            return std::unexpected(database_error::pool_exhausted);
        }

        auto conn = pool_.front();
        pool_.pop();
        active_count_++;

        if (!conn->is_valid()) {
            // Replace invalid connection
            conn = std::make_shared<sqlite_connection>(config_);
            if (!conn->is_valid()) {
                active_count_--;
                stats_.connection_failures++;
                return std::unexpected(database_error::connection_failed);
            }
        }

        stats_.connections_acquired++;
        update_peak_locked();
        return conn;
    }

    void release_connection(std::shared_ptr<database_connection> conn) override {
        if (!conn) {
            return;
        }

        std::lock_guard<std::mutex> lock(pool_mutex_);
        active_count_--;
        stats_.connections_released++;

        if (conn->is_valid() && pool_.size() < config_.pool_size) {
            pool_.push(std::dynamic_pointer_cast<sqlite_connection>(conn));
        }
    }

    [[nodiscard]] std::size_t available_connections() const override {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return pool_.size();
    }

    [[nodiscard]] std::size_t active_connections() const override {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return active_count_;
    }

    [[nodiscard]] bool is_healthy() const override {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return !pool_.empty() || active_count_ < config_.pool_size * 2;
    }

    [[nodiscard]] std::expected<void, database_error>
    execute_schema(std::string_view ddl) override {
        auto conn_result = acquire_connection();
        if (!conn_result) {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            stats_.schema_failures++;
            return std::unexpected(conn_result.error());
        }

        auto result = (*conn_result)->execute(ddl);

        if (!result) {
            release_connection(*conn_result);
            std::lock_guard<std::mutex> lock(pool_mutex_);
            stats_.schema_failures++;
            return std::unexpected(result.error());
        }

        // Consume the result to ensure DDL is executed
        while ((*result)->next()) {
            // DDL doesn't return rows, but we need to step through
        }

        release_connection(*conn_result);

        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            stats_.schemas_executed++;
        }
        return {};
    }

    [[nodiscard]] database_adapter_stats stats() const override {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return stats_;
    }

    [[nodiscard]] const database_config& config() const override {
        return config_;
    }

private:
    /** Update peak active connections (must be called under lock) */
    void update_peak_locked() {
        if (active_count_ > stats_.peak_active_connections) {
            stats_.peak_active_connections = active_count_;
        }
    }

    database_config config_;
    mutable std::mutex pool_mutex_;
    std::queue<std::shared_ptr<sqlite_connection>> pool_;
    std::size_t active_count_ = 0;
    database_adapter_stats stats_;
};

// =============================================================================
// Transaction Guard Implementation
// =============================================================================

std::expected<transaction_guard, database_error>
transaction_guard::begin(database_connection& conn) {
    auto result = conn.begin_transaction();
    if (!result) {
        return std::unexpected(result.error());
    }
    return transaction_guard(conn);
}

transaction_guard::transaction_guard(database_connection& conn)
    : conn_(&conn), committed_(false) {}

transaction_guard::~transaction_guard() {
    if (conn_ && !committed_) {
        conn_->rollback();
    }
}

transaction_guard::transaction_guard(transaction_guard&& other) noexcept
    : conn_(other.conn_), committed_(other.committed_) {
    other.conn_ = nullptr;
    other.committed_ = true;
}

transaction_guard& transaction_guard::operator=(transaction_guard&& other) noexcept {
    if (this != &other) {
        if (conn_ && !committed_) {
            conn_->rollback();
        }
        conn_ = other.conn_;
        committed_ = other.committed_;
        other.conn_ = nullptr;
        other.committed_ = true;
    }
    return *this;
}

std::expected<void, database_error> transaction_guard::commit() {
    if (!conn_) {
        return std::unexpected(database_error::transaction_failed);
    }
    auto result = conn_->commit();
    if (result) {
        committed_ = true;
    }
    return result;
}

std::expected<void, database_error> transaction_guard::rollback() {
    if (!conn_) {
        return std::unexpected(database_error::transaction_failed);
    }
    committed_ = true;  // Prevent double rollback in destructor
    return conn_->rollback();
}

// =============================================================================
// Connection Scope Implementation
// =============================================================================

std::expected<connection_scope, database_error>
connection_scope::acquire(database_adapter& adapter) {
    auto conn_result = adapter.acquire_connection();
    if (!conn_result) {
        return std::unexpected(conn_result.error());
    }
    return connection_scope(adapter, std::move(*conn_result));
}

connection_scope::connection_scope(database_adapter& adapter,
                                   std::shared_ptr<database_connection> conn)
    : adapter_(&adapter), conn_(std::move(conn)) {}

connection_scope::~connection_scope() {
    if (adapter_ && conn_) {
        adapter_->release_connection(std::move(conn_));
    }
}

connection_scope::connection_scope(connection_scope&& other) noexcept
    : adapter_(other.adapter_), conn_(std::move(other.conn_)) {
    other.adapter_ = nullptr;
}

connection_scope& connection_scope::operator=(connection_scope&& other) noexcept {
    if (this != &other) {
        if (adapter_ && conn_) {
            adapter_->release_connection(std::move(conn_));
        }
        adapter_ = other.adapter_;
        conn_ = std::move(other.conn_);
        other.adapter_ = nullptr;
    }
    return *this;
}

database_connection& connection_scope::connection() noexcept {
    return *conn_;
}

const database_connection& connection_scope::connection() const noexcept {
    return *conn_;
}

// =============================================================================
// Database Pool Adapter (database_system Integration)
// =============================================================================

#ifdef PACS_BRIDGE_HAS_DATABASE_SYSTEM
#include <kcenon/database/database_pool.hpp>
#include <kcenon/database/connection.hpp>

namespace pacs::bridge::integration {

/**
 * @brief Wrapper adapting database_system's row to database_row interface
 *
 * Provides access to row data from database_system's result sets,
 * implementing the pacs_bridge database_row interface.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/298
 */
class pool_row_wrapper : public database_row {
public:
    explicit pool_row_wrapper(const kcenon::database::row& row)
        : row_(row) {}

    [[nodiscard]] std::string get_string(std::size_t index) const override {
        return row_.get_string(index);
    }

    [[nodiscard]] int64_t get_int64(std::size_t index) const override {
        return row_.get_int64(index);
    }

    [[nodiscard]] double get_double(std::size_t index) const override {
        return row_.get_double(index);
    }

    [[nodiscard]] std::vector<uint8_t> get_blob(std::size_t index) const override {
        return row_.get_blob(index);
    }

    [[nodiscard]] bool is_null(std::size_t index) const override {
        return row_.is_null(index);
    }

    [[nodiscard]] std::size_t column_count() const override {
        return row_.column_count();
    }

    [[nodiscard]] std::string column_name(std::size_t index) const override {
        return row_.column_name(index);
    }

    [[nodiscard]] database_value get_value(std::size_t index) const override {
        return row_.get_value(index);
    }

private:
    const kcenon::database::row& row_;
};

/**
 * @brief Wrapper adapting database_system's result to database_result interface
 *
 * Provides iteration over query results from database_system,
 * implementing the pacs_bridge database_result interface with RAII semantics.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/298
 */
class pool_result_wrapper : public database_result {
public:
    explicit pool_result_wrapper(kcenon::database::result result)
        : result_(std::move(result)), row_(nullptr) {}

    [[nodiscard]] bool next() override {
        if (!result_.next()) {
            row_ = nullptr;
            return false;
        }
        row_ = std::make_unique<pool_row_wrapper>(result_.current_row());
        return true;
    }

    [[nodiscard]] const database_row& current_row() const override {
        if (!row_) {
            static pool_row_wrapper empty_row{result_.current_row()};
            return empty_row;
        }
        return *row_;
    }

    [[nodiscard]] std::size_t affected_rows() const override {
        return result_.affected_rows();
    }

    [[nodiscard]] int64_t last_insert_id() const override {
        return result_.last_insert_id();
    }

    [[nodiscard]] bool empty() const override {
        return result_.empty();
    }

private:
    kcenon::database::result result_;
    std::unique_ptr<pool_row_wrapper> row_;
};

/**
 * @brief Wrapper adapting database_system's statement to database_statement interface
 *
 * Provides prepared statement functionality from database_system,
 * implementing the pacs_bridge database_statement interface with proper
 * parameter binding and result iteration support.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/298
 */
class pool_statement_wrapper : public database_statement {
public:
    explicit pool_statement_wrapper(kcenon::database::statement stmt)
        : stmt_(std::move(stmt)) {}

    [[nodiscard]] std::expected<void, database_error>
    bind_string(std::size_t index, std::string_view value) override {
        if (!stmt_.bind_string(index, value)) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error>
    bind_int64(std::size_t index, int64_t value) override {
        if (!stmt_.bind_int64(index, value)) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error>
    bind_double(std::size_t index, double value) override {
        if (!stmt_.bind_double(index, value)) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error>
    bind_blob(std::size_t index, const std::vector<uint8_t>& value) override {
        if (!stmt_.bind_blob(index, value)) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error>
    bind_null(std::size_t index) override {
        if (!stmt_.bind_null(index)) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error> clear_bindings() override {
        if (!stmt_.clear_bindings()) {
            return std::unexpected(database_error::bind_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error> reset() override {
        if (!stmt_.reset()) {
            return std::unexpected(database_error::prepare_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<std::unique_ptr<database_result>, database_error>
    execute() override {
        auto result = stmt_.execute();
        if (!result) {
            return std::unexpected(database_error::query_failed);
        }
        return std::make_unique<pool_result_wrapper>(std::move(*result));
    }

    [[nodiscard]] std::size_t parameter_count() const override {
        return stmt_.parameter_count();
    }

private:
    kcenon::database::statement stmt_;
};

/**
 * @brief Wrapper adapting database_system's pooled connection to database_connection interface
 *
 * Wraps database_system's pooled connection to implement pacs_bridge's
 * database_connection interface, enabling transparent use of connection pools
 * with RAII semantics for automatic connection return.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/297
 */
class pool_connection_wrapper : public database_connection {
public:
    explicit pool_connection_wrapper(
        kcenon::database::pooled_connection conn)
        : conn_(std::move(conn)) {}

    ~pool_connection_wrapper() override = default;

    // Non-copyable, non-movable
    pool_connection_wrapper(const pool_connection_wrapper&) = delete;
    pool_connection_wrapper& operator=(const pool_connection_wrapper&) = delete;
    pool_connection_wrapper(pool_connection_wrapper&&) = delete;
    pool_connection_wrapper& operator=(pool_connection_wrapper&&) = delete;

    [[nodiscard]] std::expected<std::unique_ptr<database_statement>, database_error>
    prepare(std::string_view sql) override {
        auto stmt = conn_->prepare(sql);
        if (!stmt) {
            return std::unexpected(database_error::prepare_failed);
        }
        return std::make_unique<pool_statement_wrapper>(std::move(*stmt));
    }

    [[nodiscard]] std::expected<std::unique_ptr<database_result>, database_error>
    execute(std::string_view sql) override {
        auto result = conn_->execute(sql);
        if (!result) {
            return std::unexpected(database_error::query_failed);
        }
        return std::make_unique<pool_result_wrapper>(std::move(*result));
    }

    [[nodiscard]] std::expected<void, database_error> begin_transaction() override {
        if (!conn_->begin_transaction()) {
            return std::unexpected(database_error::transaction_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error> commit() override {
        if (!conn_->commit()) {
            return std::unexpected(database_error::transaction_failed);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, database_error> rollback() override {
        if (!conn_->rollback()) {
            return std::unexpected(database_error::transaction_failed);
        }
        return {};
    }

    [[nodiscard]] bool is_valid() const override {
        return conn_->is_valid();
    }

    [[nodiscard]] std::string last_error() const override {
        return conn_->last_error();
    }

    [[nodiscard]] int64_t changes() const override {
        return conn_->changes();
    }

    [[nodiscard]] int64_t last_insert_rowid() const override {
        return conn_->last_insert_rowid();
    }

private:
    kcenon::database::pooled_connection conn_;
};

/**
 * @brief Database adapter using database_system's connection pool
 *
 * Wraps database_system to provide advanced connection pooling,
 * query optimization, and support for multiple database backends.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/296
 */
class database_pool_adapter : public database_adapter {
public:
    explicit database_pool_adapter(
        std::shared_ptr<kcenon::database::database_pool> pool)
        : pool_(std::move(pool)) {
        if (!pool_) {
            throw std::invalid_argument("Cannot create adapter: pool is null");
        }
    }

    ~database_pool_adapter() override = default;

    // Non-copyable, non-movable
    database_pool_adapter(const database_pool_adapter&) = delete;
    database_pool_adapter& operator=(const database_pool_adapter&) = delete;
    database_pool_adapter(database_pool_adapter&&) = delete;
    database_pool_adapter& operator=(database_pool_adapter&&) = delete;

    [[nodiscard]] std::expected<std::shared_ptr<database_connection>, database_error>
    acquire_connection() override {
        auto conn = pool_->acquire();
        if (!conn) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.connection_failures++;
            return std::unexpected(database_error::pool_exhausted);
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.connections_acquired++;
            auto active = pool_->active();
            if (active > stats_.peak_active_connections) {
                stats_.peak_active_connections = active;
            }
        }
        return std::make_shared<pool_connection_wrapper>(std::move(conn));
    }

    void release_connection(std::shared_ptr<database_connection> conn) override {
        // Connection returned to pool automatically via RAII
        // No explicit action needed - pooled_connection destructor handles return
        (void)conn;  // Suppress unused parameter warning

        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.connections_released++;
    }

    [[nodiscard]] std::size_t available_connections() const override {
        return pool_->available();
    }

    [[nodiscard]] std::size_t active_connections() const override {
        return pool_->active();
    }

    [[nodiscard]] bool is_healthy() const override {
        return pool_->is_healthy();
    }

    [[nodiscard]] std::expected<void, database_error>
    execute_schema(std::string_view ddl) override {
        auto conn_result = acquire_connection();
        if (!conn_result) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.schema_failures++;
            return std::unexpected(conn_result.error());
        }

        auto result = (*conn_result)->execute(ddl);
        if (!result) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.schema_failures++;
            return std::unexpected(result.error());
        }

        // Consume result to ensure DDL is executed
        while ((*result)->next()) {
            // DDL doesn't return rows, but we need to step through
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.schemas_executed++;
        }
        return {};
    }

    [[nodiscard]] database_adapter_stats stats() const override {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_;
    }

    [[nodiscard]] const database_config& config() const override {
        return config_;
    }

private:
    std::shared_ptr<kcenon::database::database_pool> pool_;
    database_config config_;
    mutable std::mutex stats_mutex_;
    database_adapter_stats stats_;
};

}  // namespace pacs::bridge::integration
#endif  // PACS_BRIDGE_HAS_DATABASE_SYSTEM

// =============================================================================
// Factory Functions
// =============================================================================

std::shared_ptr<database_adapter> create_database_adapter(const database_config& config) {
#ifdef PACS_BRIDGE_HAS_DATABASE_SYSTEM
    // Prefer database_system when available
    kcenon::database::pool_config pool_cfg;
    pool_cfg.connection_string = config.connection_string.empty()
        ? ("sqlite://" + config.database_path)
        : config.connection_string;
    pool_cfg.pool_size = config.pool_size;
    pool_cfg.timeout = config.connection_timeout;

    auto pool = kcenon::database::database_pool::create(pool_cfg);
    if (pool) {
        return std::make_shared<database_pool_adapter>(std::move(pool));
    }

    // Log warning: database_system initialization failed, falling back to SQLite
    // TODO: Add logging when logger integration is available
#endif

    // Fallback to SQLite adapter
    return std::make_shared<sqlite_database_adapter>(config);
}

#ifdef PACS_BRIDGE_HAS_DATABASE_SYSTEM
std::shared_ptr<database_adapter>
create_database_adapter(std::shared_ptr<kcenon::database::database_pool> pool) {
    if (!pool) {
        throw std::invalid_argument("Cannot create adapter: pool is null");
    }
    return std::make_shared<database_pool_adapter>(std::move(pool));
}
#endif

}  // namespace pacs::bridge::integration
