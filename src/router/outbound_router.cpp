/**
 * @file outbound_router.cpp
 * @brief Outbound message router implementation
 */

#include "pacs/bridge/router/outbound_router.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace pacs::bridge::router {

// =============================================================================
// outbound_router::impl
// =============================================================================

class outbound_router::impl {
public:
    outbound_router_config config_;
    std::atomic<bool> running_{false};

    // Destination management
    mutable std::mutex destinations_mutex_;
    std::vector<outbound_destination> destinations_;
    std::unordered_map<std::string, destination_health> health_status_;
    std::unordered_map<std::string, size_t> consecutive_failures_;

    // Connection pools per destination
    std::unordered_map<std::string, std::unique_ptr<mllp::mllp_connection_pool>> pools_;

    // Health checking
    std::thread health_check_thread_;
    std::atomic<bool> health_check_running_{false};
    std::condition_variable health_check_cv_;
    std::mutex health_check_mutex_;
    health_callback health_callback_;

    // Async delivery queue
    struct queued_message {
        hl7::hl7_message message;
        delivery_callback callback;
    };
    std::queue<queued_message> delivery_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> workers_running_{false};

    // Statistics
    mutable std::mutex stats_mutex_;
    statistics stats_;

    explicit impl(const outbound_router_config& config) : config_(config) {
        destinations_ = config.destinations;
        for (const auto& dest : destinations_) {
            health_status_[dest.name] = destination_health::unknown;
            consecutive_failures_[dest.name] = 0;
        }
    }

    ~impl() {
        stop();
    }

    void stop() {
        // Stop workers
        workers_running_ = false;
        queue_cv_.notify_all();
        for (auto& worker : worker_threads_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        worker_threads_.clear();

        // Stop health checking
        health_check_running_ = false;
        health_check_cv_.notify_all();
        if (health_check_thread_.joinable()) {
            health_check_thread_.join();
        }

        // Close connection pools
        {
            std::lock_guard<std::mutex> lock(destinations_mutex_);
            pools_.clear();
        }

        running_ = false;
    }

    std::expected<void, outbound_error> start() {
        if (running_) {
            return std::unexpected(outbound_error::already_running);
        }

        // Validate destinations
        for (const auto& dest : destinations_) {
            if (!dest.is_valid()) {
                return std::unexpected(outbound_error::invalid_configuration);
            }
        }

        // Create connection pools
        {
            std::lock_guard<std::mutex> lock(destinations_mutex_);
            for (const auto& dest : destinations_) {
                if (dest.enabled) {
                    mllp::mllp_pool_config pool_config;
                    pool_config.client_config = dest.to_client_config();
                    pool_config.min_connections = 1;
                    pool_config.max_connections = 5;
                    pools_[dest.name] =
                        std::make_unique<mllp::mllp_connection_pool>(pool_config);
                }
            }
        }

        running_ = true;

        // Start health checking
        if (config_.enable_health_check) {
            health_check_running_ = true;
            health_check_thread_ = std::thread([this]() { health_check_loop(); });
        }

        // Start worker threads
        if (config_.async_queue_size > 0 && config_.worker_threads > 0) {
            workers_running_ = true;
            for (size_t i = 0; i < config_.worker_threads; ++i) {
                worker_threads_.emplace_back([this]() { worker_loop(); });
            }
        }

        return {};
    }

    void health_check_loop() {
        while (health_check_running_) {
            std::unique_lock<std::mutex> lock(health_check_mutex_);
            health_check_cv_.wait_for(
                lock, config_.default_health_check_interval,
                [this]() { return !health_check_running_.load(); });

            if (!health_check_running_) break;

            check_all_health_internal();
        }
    }

    void check_all_health_internal() {
        std::vector<outbound_destination> dests_copy;
        {
            std::lock_guard<std::mutex> lock(destinations_mutex_);
            dests_copy = destinations_;
        }

        for (const auto& dest : dests_copy) {
            if (!dest.enabled) continue;

            (void)check_health_internal(dest.name);
            // Health status is updated inside check_health_internal
        }
    }

    std::expected<destination_health, outbound_error>
    check_health_internal(std::string_view name) {
        std::lock_guard<std::mutex> lock(destinations_mutex_);

        auto it = std::find_if(destinations_.begin(), destinations_.end(),
                               [&](const auto& d) { return d.name == name; });
        if (it == destinations_.end()) {
            return std::unexpected(outbound_error::destination_not_found);
        }

        const auto& dest = *it;
        destination_health old_health = health_status_[dest.name];
        destination_health new_health = destination_health::unknown;

        try {
            // Try to connect and send a simple message or just connect
            mllp::mllp_client client(dest.to_client_config());
            auto connect_result = client.connect();

            if (connect_result) {
                new_health = destination_health::healthy;
                consecutive_failures_[dest.name] = 0;
            } else {
                consecutive_failures_[dest.name]++;
                if (consecutive_failures_[dest.name] >= dest.max_consecutive_failures) {
                    new_health = destination_health::unavailable;
                } else {
                    new_health = destination_health::degraded;
                }
            }
        } catch (...) {
            consecutive_failures_[dest.name]++;
            if (consecutive_failures_[dest.name] >= dest.max_consecutive_failures) {
                new_health = destination_health::unavailable;
            } else {
                new_health = destination_health::degraded;
            }
        }

        health_status_[dest.name] = new_health;

        // Notify callback if health changed
        if (old_health != new_health && health_callback_) {
            health_callback_(dest.name, old_health, new_health);
        }

        return new_health;
    }

    void worker_loop() {
        while (workers_running_) {
            queued_message item;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this]() {
                    return !delivery_queue_.empty() || !workers_running_;
                });

                if (!workers_running_ && delivery_queue_.empty()) {
                    break;
                }

                if (delivery_queue_.empty()) continue;

                item = std::move(delivery_queue_.front());
                delivery_queue_.pop();
            }

            // Process the message
            auto result = route_internal(item.message);
            if (item.callback) {
                if (result) {
                    item.callback(*result, item.message);
                } else {
                    delivery_result failed;
                    failed.success = false;
                    failed.error_message = to_string(result.error());
                    failed.timestamp = std::chrono::system_clock::now();
                    item.callback(failed, item.message);
                }
            }
        }
    }

    std::expected<delivery_result, outbound_error>
    route_internal(const hl7::hl7_message& message) {
        auto header = message.header();
        std::string message_type = header.full_message_type();

        // Get destinations for this message type in priority order
        auto dest_names = get_destinations_for_type(message_type);
        if (dest_names.empty()) {
            update_stats_failure();
            return std::unexpected(outbound_error::no_destination);
        }

        // Serialize message
        std::string serialized = message.serialize();

        delivery_result result;
        result.timestamp = std::chrono::system_clock::now();

        // Try each destination in priority order
        for (size_t i = 0; i < dest_names.size(); ++i) {
            const auto& dest_name = dest_names[i];

            // Check health status
            {
                std::lock_guard<std::mutex> lock(destinations_mutex_);
                auto health_it = health_status_.find(dest_name);
                if (health_it != health_status_.end() &&
                    health_it->second == destination_health::unavailable) {
                    result.failover_count++;
                    continue;  // Skip unavailable destinations
                }
            }

            auto send_result = send_to_destination(dest_name, serialized);
            if (send_result) {
                result.success = true;
                result.destination_name = dest_name;
                result.response = send_result->response;
                result.round_trip_time = send_result->round_trip_time;
                result.retry_count = send_result->retry_count;

                update_stats_success(dest_name, result.round_trip_time);
                return result;
            }

            // Failed, try next destination
            result.failover_count++;
            update_destination_failure(dest_name);

            if (i == 0) {
                update_stats_failover();
            }
        }

        // All destinations failed
        result.success = false;
        result.error_message = "All destinations failed";
        update_stats_failure();
        return std::unexpected(outbound_error::all_destinations_failed);
    }

    std::expected<mllp::mllp_client::send_result, mllp::mllp_error>
    send_to_destination(const std::string& dest_name, const std::string& serialized) {
        std::lock_guard<std::mutex> lock(destinations_mutex_);

        auto pool_it = pools_.find(dest_name);
        if (pool_it == pools_.end() || !pool_it->second) {
            // No pool, try creating a direct client
            auto dest_it = std::find_if(destinations_.begin(), destinations_.end(),
                                        [&](const auto& d) { return d.name == dest_name; });
            if (dest_it == destinations_.end()) {
                return std::unexpected(mllp::mllp_error::connection_failed);
            }

            mllp::mllp_client client(dest_it->to_client_config());
            auto connect_result = client.connect();
            if (!connect_result) {
                return std::unexpected(connect_result.error());
            }

            return client.send(serialized);
        }

        return pool_it->second->send(mllp::mllp_message::from_string(serialized));
    }

    std::vector<std::string> get_destinations_for_type(std::string_view message_type) const {
        std::vector<std::pair<std::string, int>> matches;

        std::lock_guard<std::mutex> lock(destinations_mutex_);
        for (const auto& dest : destinations_) {
            if (!dest.enabled) continue;

            bool matches_type = false;
            for (const auto& type : dest.message_types) {
                if (type == message_type || type == "*") {
                    matches_type = true;
                    break;
                }
                // Support partial matching (e.g., "ORM" matches "ORM^O01")
                if (message_type.find(type) == 0) {
                    matches_type = true;
                    break;
                }
            }

            if (matches_type || dest.message_types.empty()) {
                matches.emplace_back(dest.name, dest.priority);
            }
        }

        // Sort by priority (lower = higher priority)
        std::sort(matches.begin(), matches.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        std::vector<std::string> result;
        result.reserve(matches.size());
        for (const auto& [name, priority] : matches) {
            result.push_back(name);
        }
        return result;
    }

    void update_stats_success(const std::string& dest_name,
                              std::chrono::milliseconds rtt) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_messages++;
        stats_.successful_deliveries++;

        auto& dest_stats = stats_.destination_stats[dest_name];
        dest_stats.messages_sent++;
        dest_stats.last_success = std::chrono::system_clock::now();

        // Update average delivery time
        double total = stats_.avg_delivery_time_ms * (stats_.successful_deliveries - 1);
        stats_.avg_delivery_time_ms =
            (total + static_cast<double>(rtt.count())) / stats_.successful_deliveries;

        // Update destination average
        double dest_total = dest_stats.avg_response_time_ms * (dest_stats.messages_sent - 1);
        dest_stats.avg_response_time_ms =
            (dest_total + static_cast<double>(rtt.count())) / dest_stats.messages_sent;
    }

    void update_stats_failure() {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_messages++;
        stats_.failed_deliveries++;
    }

    void update_stats_failover() {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.failover_events++;
    }

    void update_destination_failure(const std::string& dest_name) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            auto& dest_stats = stats_.destination_stats[dest_name];
            dest_stats.messages_failed++;
            dest_stats.last_failure = std::chrono::system_clock::now();
            dest_stats.consecutive_failures++;
        }

        {
            std::lock_guard<std::mutex> lock(destinations_mutex_);
            consecutive_failures_[dest_name]++;

            auto it = std::find_if(destinations_.begin(), destinations_.end(),
                                   [&](const auto& d) { return d.name == dest_name; });
            if (it != destinations_.end()) {
                if (consecutive_failures_[dest_name] >= it->max_consecutive_failures) {
                    destination_health old_health = health_status_[dest_name];
                    health_status_[dest_name] = destination_health::unavailable;
                    if (old_health != destination_health::unavailable && health_callback_) {
                        health_callback_(dest_name, old_health,
                                         destination_health::unavailable);
                    }
                }
            }
        }
    }
};

// =============================================================================
// outbound_router Implementation
// =============================================================================

outbound_router::outbound_router()
    : pimpl_(std::make_unique<impl>(outbound_router_config{})) {}

outbound_router::outbound_router(const outbound_router_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

outbound_router::~outbound_router() = default;

outbound_router::outbound_router(outbound_router&&) noexcept = default;
outbound_router& outbound_router::operator=(outbound_router&&) noexcept = default;

std::expected<void, outbound_error> outbound_router::start() {
    return pimpl_->start();
}

void outbound_router::stop() {
    pimpl_->stop();
}

bool outbound_router::is_running() const noexcept {
    return pimpl_->running_;
}

std::expected<delivery_result, outbound_error>
outbound_router::route(const hl7::hl7_message& message) {
    if (!pimpl_->running_) {
        return std::unexpected(outbound_error::not_running);
    }
    return pimpl_->route_internal(message);
}

std::future<std::expected<delivery_result, outbound_error>>
outbound_router::route_async(const hl7::hl7_message& message) {
    return std::async(std::launch::async, [this, msg = message]() {
        return this->route(msg);
    });
}

std::expected<void, outbound_error>
outbound_router::route_with_callback(const hl7::hl7_message& message,
                                      delivery_callback callback) {
    if (!pimpl_->running_) {
        return std::unexpected(outbound_error::not_running);
    }

    {
        std::lock_guard<std::mutex> lock(pimpl_->queue_mutex_);
        if (pimpl_->config_.async_queue_size > 0 &&
            pimpl_->delivery_queue_.size() >= pimpl_->config_.async_queue_size) {
            return std::unexpected(outbound_error::queue_full);
        }
        pimpl_->delivery_queue_.push({message, std::move(callback)});
    }
    pimpl_->queue_cv_.notify_one();
    return {};
}

std::expected<delivery_result, outbound_error>
outbound_router::route(std::string_view hl7_content) {
    auto parse_result = hl7::hl7_message::parse(hl7_content);
    if (!parse_result) {
        return std::unexpected(outbound_error::delivery_failed);
    }
    return route(*parse_result);
}

std::vector<std::string>
outbound_router::get_destinations(std::string_view message_type) const {
    return pimpl_->get_destinations_for_type(message_type);
}

std::vector<outbound_destination> outbound_router::destinations() const {
    std::lock_guard<std::mutex> lock(pimpl_->destinations_mutex_);
    return pimpl_->destinations_;
}

const outbound_destination*
outbound_router::get_destination(std::string_view name) const {
    std::lock_guard<std::mutex> lock(pimpl_->destinations_mutex_);
    auto it = std::find_if(pimpl_->destinations_.begin(), pimpl_->destinations_.end(),
                           [&](const auto& d) { return d.name == name; });
    if (it != pimpl_->destinations_.end()) {
        return &(*it);
    }
    return nullptr;
}

bool outbound_router::set_destination_enabled(std::string_view name, bool enabled) {
    std::lock_guard<std::mutex> lock(pimpl_->destinations_mutex_);
    auto it = std::find_if(pimpl_->destinations_.begin(), pimpl_->destinations_.end(),
                           [&](const auto& d) { return d.name == name; });
    if (it != pimpl_->destinations_.end()) {
        it->enabled = enabled;
        return true;
    }
    return false;
}

std::expected<void, outbound_error>
outbound_router::add_destination(const outbound_destination& destination) {
    if (!destination.is_valid()) {
        return std::unexpected(outbound_error::invalid_configuration);
    }

    std::lock_guard<std::mutex> lock(pimpl_->destinations_mutex_);

    // Check for duplicate
    auto it = std::find_if(pimpl_->destinations_.begin(), pimpl_->destinations_.end(),
                           [&](const auto& d) { return d.name == destination.name; });
    if (it != pimpl_->destinations_.end()) {
        return std::unexpected(outbound_error::invalid_configuration);
    }

    pimpl_->destinations_.push_back(destination);
    pimpl_->health_status_[destination.name] = destination_health::unknown;
    pimpl_->consecutive_failures_[destination.name] = 0;

    // Create connection pool if router is running
    if (pimpl_->running_ && destination.enabled) {
        mllp::mllp_pool_config pool_config;
        pool_config.client_config = destination.to_client_config();
        pool_config.min_connections = 1;
        pool_config.max_connections = 5;
        pimpl_->pools_[destination.name] =
            std::make_unique<mllp::mllp_connection_pool>(pool_config);
    }

    return {};
}

bool outbound_router::remove_destination(std::string_view name) {
    std::lock_guard<std::mutex> lock(pimpl_->destinations_mutex_);

    auto it = std::find_if(pimpl_->destinations_.begin(), pimpl_->destinations_.end(),
                           [&](const auto& d) { return d.name == name; });
    if (it != pimpl_->destinations_.end()) {
        pimpl_->destinations_.erase(it);
        pimpl_->health_status_.erase(std::string(name));
        pimpl_->consecutive_failures_.erase(std::string(name));
        pimpl_->pools_.erase(std::string(name));
        return true;
    }
    return false;
}

destination_health
outbound_router::get_destination_health(std::string_view name) const {
    std::lock_guard<std::mutex> lock(pimpl_->destinations_mutex_);
    auto it = pimpl_->health_status_.find(std::string(name));
    if (it != pimpl_->health_status_.end()) {
        return it->second;
    }
    return destination_health::unknown;
}

std::unordered_map<std::string, destination_health>
outbound_router::get_all_health() const {
    std::lock_guard<std::mutex> lock(pimpl_->destinations_mutex_);
    return pimpl_->health_status_;
}

std::expected<destination_health, outbound_error>
outbound_router::check_health(std::string_view name) {
    return pimpl_->check_health_internal(name);
}

void outbound_router::check_all_health() {
    pimpl_->check_all_health_internal();
}

void outbound_router::set_health_callback(health_callback callback) {
    pimpl_->health_callback_ = std::move(callback);
}

void outbound_router::clear_health_callback() {
    pimpl_->health_callback_ = nullptr;
}

outbound_router::statistics outbound_router::get_statistics() const {
    std::lock_guard<std::mutex> lock(pimpl_->stats_mutex_);
    statistics stats = pimpl_->stats_;

    // Update queue pending
    {
        std::lock_guard<std::mutex> queue_lock(pimpl_->queue_mutex_);
        stats.queue_pending = pimpl_->delivery_queue_.size();
    }

    // Update health status in destination stats
    {
        std::lock_guard<std::mutex> dest_lock(pimpl_->destinations_mutex_);
        for (auto& [name, dest_stats] : stats.destination_stats) {
            auto health_it = pimpl_->health_status_.find(name);
            if (health_it != pimpl_->health_status_.end()) {
                dest_stats.health = health_it->second;
            }
        }
    }

    return stats;
}

void outbound_router::reset_statistics() {
    std::lock_guard<std::mutex> lock(pimpl_->stats_mutex_);
    pimpl_->stats_ = statistics{};
}

const outbound_router_config& outbound_router::config() const noexcept {
    return pimpl_->config_;
}

void outbound_router::set_config(const outbound_router_config& config) {
    pimpl_->config_ = config;
}

// =============================================================================
// destination_builder Implementation
// =============================================================================

destination_builder destination_builder::create(std::string_view name) {
    return destination_builder(name);
}

destination_builder::destination_builder(std::string_view name) {
    dest_.name = std::string(name);
}

destination_builder& destination_builder::host(std::string_view h) {
    dest_.host = std::string(h);
    return *this;
}

destination_builder& destination_builder::port(uint16_t p) {
    dest_.port = p;
    return *this;
}

destination_builder& destination_builder::message_type(std::string_view type) {
    dest_.message_types.emplace_back(type);
    return *this;
}

destination_builder& destination_builder::message_types(std::vector<std::string> types) {
    dest_.message_types = std::move(types);
    return *this;
}

destination_builder& destination_builder::priority(int p) {
    dest_.priority = p;
    return *this;
}

destination_builder& destination_builder::enabled(bool e) {
    dest_.enabled = e;
    return *this;
}

destination_builder& destination_builder::tls_enabled(bool enable) {
    dest_.tls.enabled = enable;
    return *this;
}

destination_builder& destination_builder::tls_ca(std::string_view ca_path) {
    dest_.tls.ca_path = std::string(ca_path);
    return *this;
}

destination_builder& destination_builder::tls_cert(std::string_view cert_path,
                                                    std::string_view key_path) {
    dest_.tls.cert_path = std::string(cert_path);
    dest_.tls.key_path = std::string(key_path);
    return *this;
}

destination_builder& destination_builder::connect_timeout(std::chrono::milliseconds timeout) {
    dest_.connect_timeout = timeout;
    return *this;
}

destination_builder& destination_builder::io_timeout(std::chrono::milliseconds timeout) {
    dest_.io_timeout = timeout;
    return *this;
}

destination_builder& destination_builder::retry(size_t count,
                                                 std::chrono::milliseconds delay) {
    dest_.retry_count = count;
    dest_.retry_delay = delay;
    return *this;
}

destination_builder& destination_builder::health_check_interval(std::chrono::seconds interval) {
    dest_.health_check_interval = interval;
    return *this;
}

destination_builder& destination_builder::description(std::string_view desc) {
    dest_.description = std::string(desc);
    return *this;
}

outbound_destination destination_builder::build() const {
    return dest_;
}

}  // namespace pacs::bridge::router
