/**
 * @file reliable_outbound_sender.cpp
 * @brief Implementation of reliable outbound message delivery
 */

#include "pacs/bridge/router/reliable_outbound_sender.h"

#include "pacs/bridge/monitoring/bridge_metrics.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace pacs::bridge::router {

// =============================================================================
// reliable_outbound_sender::impl
// =============================================================================

class reliable_outbound_sender::impl {
public:
    reliable_sender_config config_;
    std::atomic<bool> running_{false};

    // Components
    std::unique_ptr<queue_manager> queue_;
    std::unique_ptr<outbound_router> router_;

    // Callbacks
    delivery_callback delivery_callback_;
    dead_letter_callback dead_letter_callback_;

    // Shutdown synchronization
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;

    // Statistics
    mutable std::mutex stats_mutex_;
    size_t total_enqueued_ = 0;
    size_t total_delivered_ = 0;
    size_t total_failed_ = 0;
    double total_delivery_time_ms_ = 0.0;

    explicit impl(const reliable_sender_config& config)
        : config_(config),
          queue_(std::make_unique<queue_manager>(config.queue)),
          router_(std::make_unique<outbound_router>(config.router)) {}

    ~impl() { stop(); }

    std::expected<void, reliable_sender_error> start() {
        if (running_) {
            return std::unexpected(reliable_sender_error::already_running);
        }

        if (!config_.is_valid()) {
            return std::unexpected(reliable_sender_error::invalid_configuration);
        }

        // Start the queue manager (this also recovers in-progress messages)
        auto queue_result = queue_->start();
        if (!queue_result) {
            return std::unexpected(reliable_sender_error::queue_init_failed);
        }

        // Start the outbound router
        auto router_result = router_->start();
        if (!router_result) {
            queue_->stop();
            return std::unexpected(reliable_sender_error::router_init_failed);
        }

        running_ = true;

        // Start workers with sender function that uses the router
        if (config_.auto_start_workers) {
            start_workers();
        }

        return {};
    }

    void start_workers() {
        queue_->start_workers([this](const queued_message& msg)
                                  -> std::expected<void, std::string> {
            return send_message(msg);
        });

        // Set up delivery callback forwarding
        queue_->set_delivery_callback([this](const queued_message& msg, bool success,
                                              const std::string& error_message) {
            on_delivery_complete(msg, success, error_message);
        });

        // Set up dead letter callback forwarding
        queue_->set_dead_letter_callback([this](const dead_letter_entry& entry) {
            on_dead_letter(entry);
        });
    }

    void stop() {
        if (!running_) {
            return;
        }

        running_ = false;

        // Stop workers first
        queue_->stop_workers();

        // Stop router
        router_->stop();

        // Stop queue (this persists in-progress state)
        queue_->stop();

        // Notify shutdown waiters
        shutdown_cv_.notify_all();
    }

    void wait_for_shutdown() {
        std::unique_lock<std::mutex> lock(shutdown_mutex_);
        shutdown_cv_.wait(lock, [this]() { return !running_.load(); });
    }

    std::expected<void, std::string> send_message(const queued_message& msg) {
        auto start_time = std::chrono::steady_clock::now();

        // Verify destination exists
        auto dest = router_->get_destination(msg.destination);
        if (!dest) {
            return std::unexpected("Destination not found: " + msg.destination);
        }

        // Send via router
        auto result = router_->route(msg.payload);
        if (!result) {
            return std::unexpected(to_string(result.error()));
        }

        if (!result->success) {
            return std::unexpected(result->error_message);
        }

        // Record timing
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            total_delivered_++;
            total_delivery_time_ms_ += static_cast<double>(duration.count());
        }

        return {};
    }

    void on_delivery_complete(const queued_message& msg, bool success,
                               const std::string& error_message) {
        if (delivery_callback_) {
            delivery_event event;
            event.message_id = msg.id;
            event.destination = msg.destination;
            event.correlation_id = msg.correlation_id;
            event.message_type = msg.message_type;
            event.success = success;
            event.error = error_message;
            event.attempt_count = msg.attempt_count;
            event.timestamp = std::chrono::system_clock::now();

            delivery_callback_(event);
        }

        // Update metrics
        if (success) {
            monitoring::bridge_metrics_collector::instance().record_message_delivered(
                msg.destination);
        } else {
            monitoring::bridge_metrics_collector::instance().record_delivery_failure(
                msg.destination);
        }
    }

    void on_dead_letter(const dead_letter_entry& entry) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            total_failed_++;
        }

        if (dead_letter_callback_) {
            dead_letter_callback_(entry);
        }
    }

    std::expected<std::string, reliable_sender_error> enqueue(
        std::string_view destination,
        std::string_view payload,
        int priority,
        std::string_view correlation_id,
        std::string_view message_type) {
        if (!running_) {
            return std::unexpected(reliable_sender_error::not_running);
        }

        if (destination.empty() || payload.empty()) {
            return std::unexpected(reliable_sender_error::enqueue_failed);
        }

        // Verify destination exists (if router is configured with destinations)
        if (!router_->destinations().empty()) {
            auto dest = router_->get_destination(destination);
            if (!dest) {
                return std::unexpected(reliable_sender_error::destination_not_found);
            }
        }

        auto result = queue_->enqueue(destination, payload, priority,
                                       correlation_id, message_type);
        if (!result) {
            return std::unexpected(reliable_sender_error::enqueue_failed);
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            total_enqueued_++;
        }

        return *result;
    }

    reliable_sender_statistics get_statistics() const {
        reliable_sender_statistics stats;

        stats.queue_stats = queue_->get_statistics();
        stats.router_stats = router_->get_statistics();

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats.total_enqueued = total_enqueued_;
            stats.total_delivered = total_delivered_;
            stats.total_failed = total_failed_;

            if (total_delivered_ > 0) {
                stats.avg_delivery_latency_ms = total_delivery_time_ms_ / total_delivered_;
            }
        }

        stats.queue_depth = queue_->queue_depth();
        stats.dlq_depth = queue_->dead_letter_count();

        return stats;
    }

    void reset_statistics() {
        queue_->reset_statistics();
        router_->reset_statistics();

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            total_enqueued_ = 0;
            total_delivered_ = 0;
            total_failed_ = 0;
            total_delivery_time_ms_ = 0.0;
        }
    }
};

// =============================================================================
// reliable_outbound_sender Implementation
// =============================================================================

reliable_outbound_sender::reliable_outbound_sender()
    : pimpl_(std::make_unique<impl>(reliable_sender_config{})) {}

reliable_outbound_sender::reliable_outbound_sender(const reliable_sender_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

reliable_outbound_sender::~reliable_outbound_sender() = default;

reliable_outbound_sender::reliable_outbound_sender(reliable_outbound_sender&&) noexcept = default;
reliable_outbound_sender& reliable_outbound_sender::operator=(
    reliable_outbound_sender&&) noexcept = default;

std::expected<void, reliable_sender_error> reliable_outbound_sender::start() {
    return pimpl_->start();
}

void reliable_outbound_sender::stop() {
    pimpl_->stop();
}

bool reliable_outbound_sender::is_running() const noexcept {
    return pimpl_->running_;
}

void reliable_outbound_sender::wait_for_shutdown() {
    pimpl_->wait_for_shutdown();
}

std::expected<std::string, reliable_sender_error>
reliable_outbound_sender::enqueue(const enqueue_request& request) {
    if (!request.is_valid()) {
        return std::unexpected(reliable_sender_error::enqueue_failed);
    }

    return pimpl_->enqueue(request.destination, request.payload, request.priority,
                            request.correlation_id, request.message_type);
}

std::expected<std::string, reliable_sender_error>
reliable_outbound_sender::enqueue(std::string_view destination,
                                   std::string_view payload,
                                   int priority,
                                   std::string_view correlation_id,
                                   std::string_view message_type) {
    return pimpl_->enqueue(destination, payload, priority, correlation_id, message_type);
}

std::expected<void, reliable_sender_error>
reliable_outbound_sender::add_destination(const outbound_destination& destination) {
    auto result = pimpl_->router_->add_destination(destination);
    if (!result) {
        return std::unexpected(reliable_sender_error::invalid_configuration);
    }
    return {};
}

bool reliable_outbound_sender::remove_destination(std::string_view name) {
    return pimpl_->router_->remove_destination(name);
}

std::vector<outbound_destination> reliable_outbound_sender::destinations() const {
    return pimpl_->router_->destinations();
}

bool reliable_outbound_sender::has_destination(std::string_view name) const {
    return pimpl_->router_->get_destination(name) != nullptr;
}

destination_health reliable_outbound_sender::get_destination_health(
    std::string_view name) const {
    return pimpl_->router_->get_destination_health(name);
}

size_t reliable_outbound_sender::queue_depth() const {
    return pimpl_->queue_->queue_depth();
}

size_t reliable_outbound_sender::queue_depth(std::string_view destination) const {
    return pimpl_->queue_->queue_depth(destination);
}

std::vector<queued_message> reliable_outbound_sender::get_pending(
    std::string_view destination,
    size_t limit) const {
    return pimpl_->queue_->get_pending(destination, limit);
}

std::vector<dead_letter_entry> reliable_outbound_sender::get_dead_letters(
    size_t limit,
    size_t offset) const {
    return pimpl_->queue_->get_dead_letters(limit, offset);
}

size_t reliable_outbound_sender::dead_letter_count() const {
    return pimpl_->queue_->dead_letter_count();
}

std::expected<void, reliable_sender_error>
reliable_outbound_sender::retry_dead_letter(std::string_view message_id) {
    auto result = pimpl_->queue_->retry_dead_letter(message_id);
    if (!result) {
        return std::unexpected(reliable_sender_error::internal_error);
    }
    return {};
}

std::expected<void, reliable_sender_error>
reliable_outbound_sender::delete_dead_letter(std::string_view message_id) {
    auto result = pimpl_->queue_->delete_dead_letter(message_id);
    if (!result) {
        return std::unexpected(reliable_sender_error::internal_error);
    }
    return {};
}

size_t reliable_outbound_sender::purge_dead_letters() {
    return pimpl_->queue_->purge_dead_letters();
}

reliable_sender_statistics reliable_outbound_sender::get_statistics() const {
    return pimpl_->get_statistics();
}

void reliable_outbound_sender::reset_statistics() {
    pimpl_->reset_statistics();
}

void reliable_outbound_sender::set_delivery_callback(delivery_callback callback) {
    pimpl_->delivery_callback_ = std::move(callback);
}

void reliable_outbound_sender::clear_delivery_callback() {
    pimpl_->delivery_callback_ = nullptr;
}

void reliable_outbound_sender::set_dead_letter_callback(dead_letter_callback callback) {
    pimpl_->dead_letter_callback_ = std::move(callback);
}

void reliable_outbound_sender::clear_dead_letter_callback() {
    pimpl_->dead_letter_callback_ = nullptr;
}

const reliable_sender_config& reliable_outbound_sender::config() const noexcept {
    return pimpl_->config_;
}

queue_manager& reliable_outbound_sender::get_queue_manager() {
    return *pimpl_->queue_;
}

outbound_router& reliable_outbound_sender::get_outbound_router() {
    return *pimpl_->router_;
}

// =============================================================================
// reliable_sender_config_builder Implementation
// =============================================================================

reliable_sender_config_builder reliable_sender_config_builder::create() {
    return reliable_sender_config_builder();
}

reliable_sender_config_builder::reliable_sender_config_builder() : config_() {}

reliable_sender_config_builder& reliable_sender_config_builder::database(
    std::string_view path) {
    config_.queue.database_path = std::string(path);
    return *this;
}

reliable_sender_config_builder& reliable_sender_config_builder::max_queue_size(
    size_t size) {
    config_.queue.max_queue_size = size;
    return *this;
}

reliable_sender_config_builder& reliable_sender_config_builder::workers(size_t count) {
    config_.queue.worker_count = count;
    return *this;
}

reliable_sender_config_builder& reliable_sender_config_builder::retry_policy(
    size_t max_retries,
    std::chrono::seconds initial_delay,
    double backoff_multiplier) {
    config_.queue.max_retry_count = max_retries;
    config_.queue.initial_retry_delay = initial_delay;
    config_.queue.retry_backoff_multiplier = backoff_multiplier;
    return *this;
}

reliable_sender_config_builder& reliable_sender_config_builder::ttl(
    std::chrono::hours ttl) {
    config_.queue.message_ttl = ttl;
    return *this;
}

reliable_sender_config_builder& reliable_sender_config_builder::add_destination(
    const outbound_destination& dest) {
    config_.router.destinations.push_back(dest);
    return *this;
}

reliable_sender_config_builder& reliable_sender_config_builder::health_check(
    bool enable) {
    config_.router.enable_health_check = enable;
    return *this;
}

reliable_sender_config_builder& reliable_sender_config_builder::auto_start_workers(
    bool enable) {
    config_.auto_start_workers = enable;
    return *this;
}

reliable_sender_config build() {
    return reliable_sender_config{};
}

reliable_sender_config reliable_sender_config_builder::build() const {
    return config_;
}

}  // namespace pacs::bridge::router
