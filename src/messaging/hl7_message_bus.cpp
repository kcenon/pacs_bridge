/**
 * @file hl7_message_bus.cpp
 * @brief Implementation of HL7 message Pub/Sub pattern
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 * @see https://github.com/kcenon/pacs_bridge/issues/153
 */

#include "pacs/bridge/messaging/hl7_message_bus.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <map>

#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include <kcenon/messaging/core/message_bus.h>
#include <kcenon/messaging/core/message.h>
#include <kcenon/messaging/backends/standalone_backend.h>
#include <core/container.h>
#endif

namespace pacs::bridge::messaging {

// =============================================================================
// Topic Utilities Implementation
// =============================================================================

namespace topics {

std::string build_topic(std::string_view message_type, std::string_view trigger_event) {
    std::string topic = "hl7.";

    // Convert to lowercase
    for (char c : message_type) {
        topic += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (!trigger_event.empty()) {
        topic += '.';
        for (char c : trigger_event) {
            topic += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    return topic;
}

std::string build_topic(const hl7::hl7_message& message) {
    // Get message type from MSH.9.1
    auto type_str = message.get_value("MSH.9.1");
    auto trigger = message.trigger_event();
    return build_topic(type_str, trigger);
}

}  // namespace topics

// =============================================================================
// HL7 Message Bus Implementation
// =============================================================================

class hl7_message_bus::impl {
public:
    explicit impl(const hl7_message_bus_config& config)
        : config_(config)
        , running_(false)
        , next_subscription_id_(1) {
    }

    ~impl() {
        stop();
    }

    std::expected<void, message_bus_error> start() {
        std::unique_lock lock(mutex_);

        if (running_.load()) {
            return std::unexpected(message_bus_error::already_started);
        }

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        try {
            // Create backend with configured worker threads
            size_t workers = config_.worker_threads;
            if (workers == 0) {
                workers = std::thread::hardware_concurrency();
                if (workers == 0) workers = 2;
            }

            backend_ = std::make_shared<kcenon::messaging::standalone_backend>(workers);

            // Configure message bus
            kcenon::messaging::message_bus_config bus_config;
            bus_config.queue_capacity = config_.queue_capacity;

            message_bus_ = std::make_shared<kcenon::messaging::message_bus>(
                backend_, bus_config);

            auto result = message_bus_->start();
            if (result.is_err()) {
                return std::unexpected(message_bus_error::backend_init_failed);
            }
        } catch (const std::exception&) {
            return std::unexpected(message_bus_error::backend_init_failed);
        }
#endif

        running_.store(true);
        return {};
    }

    void stop() {
        std::unique_lock lock(mutex_);

        if (!running_.load()) {
            return;
        }

        running_.store(false);

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (message_bus_) {
            message_bus_->stop();
            message_bus_.reset();
        }
        backend_.reset();
#endif

        // Clear subscriptions
        subscriptions_.clear();
    }

    bool is_running() const noexcept {
        return running_.load();
    }

    std::expected<void, message_bus_error> publish(
        const hl7::hl7_message& message,
        message_priority priority) {

        auto topic = topics::build_topic(message);
        return publish(topic, message, priority);
    }

    std::expected<void, message_bus_error> publish(
        std::string_view topic,
        const hl7::hl7_message& message,
        message_priority priority) {

        if (!running_.load()) {
            return std::unexpected(message_bus_error::not_started);
        }

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        try {
            // Convert HL7 message to messaging_system message
            kcenon::messaging::message msg(std::string(topic),
                                           kcenon::messaging::message_type::event);

            // Store HL7 message data in payload
            auto& payload = msg.payload();
            payload.set("raw_message", message.serialize());
            payload.set("message_type", std::string(message.get_value("MSH.9.1")));
            payload.set("trigger_event", std::string(message.trigger_event()));
            payload.set("control_id", std::string(message.control_id()));

            // Set priority
            switch (priority) {
                case message_priority::low:
                    msg.metadata().priority = kcenon::messaging::message_priority::low;
                    break;
                case message_priority::high:
                    msg.metadata().priority = kcenon::messaging::message_priority::high;
                    break;
                default:
                    msg.metadata().priority = kcenon::messaging::message_priority::normal;
                    break;
            }

            auto result = message_bus_->publish(msg);
            if (result.is_err()) {
                return std::unexpected(message_bus_error::publish_failed);
            }

            // Update statistics
            if (config_.enable_statistics) {
                std::unique_lock lock(stats_mutex_);
                stats_.messages_published++;
                topic_counts_[std::string(topic)]++;
            }

            return {};
        } catch (const std::exception&) {
            return std::unexpected(message_bus_error::publish_failed);
        }
#else
        // Standalone mode: direct delivery to local subscribers
        std::shared_lock lock(mutex_);

        for (const auto& [id, sub] : subscriptions_) {
            if (!sub.active) continue;

            if (matches_pattern(topic, sub.topic_pattern)) {
                // Apply filter if present
                if (sub.filter && !sub.filter(message)) {
                    continue;
                }

                // Invoke callback
                auto result = sub.callback(message);

                if (config_.enable_statistics) {
                    std::unique_lock stats_lock(stats_mutex_);
                    stats_.messages_delivered++;
                }

                if (result.stop_propagation) {
                    break;
                }
            }
        }

        if (config_.enable_statistics) {
            std::unique_lock stats_lock(stats_mutex_);
            stats_.messages_published++;
            topic_counts_[std::string(topic)]++;
        }

        return {};
#endif
    }

    std::expected<subscription_handle, message_bus_error> subscribe(
        std::string_view topic_pattern,
        message_callback callback,
        message_filter filter,
        int priority) {

        if (!running_.load()) {
            return std::unexpected(message_bus_error::not_started);
        }

        if (topic_pattern.empty()) {
            return std::unexpected(message_bus_error::invalid_topic);
        }

        std::unique_lock lock(mutex_);

        subscription_info sub;
        sub.id = next_subscription_id_++;
        sub.topic_pattern = std::string(topic_pattern);
        sub.callback = std::move(callback);
        sub.filter = std::move(filter);
        sub.priority = priority;
        sub.active = true;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        // Subscribe to messaging_system
        auto result = message_bus_->subscribe(
            std::string(topic_pattern),
            [this, sub_id = sub.id](const kcenon::messaging::message& msg)
                -> kcenon::common::VoidResult {

                std::shared_lock lock(mutex_);
                auto it = subscriptions_.find(sub_id);
                if (it == subscriptions_.end() || !it->second.active) {
                    return kcenon::common::ok();
                }

                // Convert messaging_system message to HL7 message
                auto raw_opt = msg.payload().get_value("raw_message");
                if (!raw_opt || raw_opt->type != container_module::value_types::string_value) {
                    return kcenon::common::ok();
                }
                auto raw = std::get<std::string>(raw_opt->data);

                hl7::hl7_parser parser;
                auto parse_result = parser.parse(raw);
                if (!parse_result.is_ok()) {
                    return kcenon::common::ok();
                }

                const auto& sub_info = it->second;

                // Apply filter
                if (sub_info.filter && !sub_info.filter(parse_result.value())) {
                    return kcenon::common::ok();
                }

                // Invoke callback
                auto cb_result = sub_info.callback(parse_result.value());

                if (config_.enable_statistics) {
                    std::unique_lock stats_lock(stats_mutex_);
                    stats_.messages_delivered++;
                }

                return kcenon::common::ok();
            },
            nullptr,
            priority);

        if (result.is_err()) {
            return std::unexpected(message_bus_error::subscribe_failed);
        }

        sub.internal_id = result.value();
#endif

        subscriptions_[sub.id] = std::move(sub);

        subscription_handle handle;
        handle.id = sub.id;
        handle.topic_pattern = std::string(topic_pattern);
        handle.active = true;

        return handle;
    }

    std::expected<void, message_bus_error> unsubscribe(
        const subscription_handle& handle) {

        std::unique_lock lock(mutex_);

        auto it = subscriptions_.find(handle.id);
        if (it == subscriptions_.end()) {
            return std::unexpected(message_bus_error::subscription_not_found);
        }

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (message_bus_) {
            message_bus_->unsubscribe(it->second.internal_id);
        }
#endif

        subscriptions_.erase(it);
        return {};
    }

    void unsubscribe_all() {
        std::unique_lock lock(mutex_);

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (message_bus_) {
            for (const auto& [id, sub] : subscriptions_) {
                message_bus_->unsubscribe(sub.internal_id);
            }
        }
#endif

        subscriptions_.clear();
    }

    size_t subscription_count() const noexcept {
        std::shared_lock lock(mutex_);
        return subscriptions_.size();
    }

    bool has_subscribers(std::string_view topic) const noexcept {
        std::shared_lock lock(mutex_);

        for (const auto& [id, sub] : subscriptions_) {
            if (sub.active && matches_pattern(topic, sub.topic_pattern)) {
                return true;
            }
        }
        return false;
    }

    hl7_message_bus::statistics get_statistics() const {
        std::shared_lock lock(stats_mutex_);

        statistics stats;
        stats.messages_published = stats_.messages_published;
        stats.messages_delivered = stats_.messages_delivered;
        stats.messages_failed = stats_.messages_failed;
        stats.dead_letter_count = stats_.dead_letter_count;
        stats.active_subscriptions = subscriptions_.size();

        for (const auto& [topic, count] : topic_counts_) {
            stats.topic_counts.emplace_back(topic, count);
        }

        return stats;
    }

    void reset_statistics() {
        std::unique_lock lock(stats_mutex_);
        stats_ = internal_statistics{};
        topic_counts_.clear();
    }

    const hl7_message_bus_config& config() const noexcept {
        return config_;
    }

private:
    struct subscription_info {
        uint64_t id = 0;
        uint64_t internal_id = 0;  // messaging_system subscription ID
        std::string topic_pattern;
        message_callback callback;
        message_filter filter;
        int priority = 5;
        bool active = false;
    };

    struct internal_statistics {
        uint64_t messages_published = 0;
        uint64_t messages_delivered = 0;
        uint64_t messages_failed = 0;
        uint64_t dead_letter_count = 0;
    };

    static bool matches_pattern(std::string_view topic,
                                 std::string_view pattern) {
        // Simple pattern matching with * and #
        if (pattern == topic) return true;
        if (pattern.ends_with("#")) {
            auto prefix = pattern.substr(0, pattern.size() - 1);
            return topic.starts_with(prefix);
        }
        if (pattern.ends_with("*")) {
            auto prefix = pattern.substr(0, pattern.size() - 1);
            if (!topic.starts_with(prefix)) return false;
            auto remainder = topic.substr(prefix.size());
            return remainder.find('.') == std::string_view::npos;
        }
        return false;
    }

    hl7_message_bus_config config_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> next_subscription_id_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, subscription_info> subscriptions_;

    mutable std::shared_mutex stats_mutex_;
    internal_statistics stats_;
    std::unordered_map<std::string, uint64_t> topic_counts_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    std::shared_ptr<kcenon::messaging::standalone_backend> backend_;
    std::shared_ptr<kcenon::messaging::message_bus> message_bus_;
#endif
};

// =============================================================================
// hl7_message_bus Public Interface
// =============================================================================

hl7_message_bus::hl7_message_bus()
    : pimpl_(std::make_unique<impl>(hl7_message_bus_config::defaults())) {
}

hl7_message_bus::hl7_message_bus(const hl7_message_bus_config& config)
    : pimpl_(std::make_unique<impl>(config)) {
}

hl7_message_bus::~hl7_message_bus() = default;

hl7_message_bus::hl7_message_bus(hl7_message_bus&&) noexcept = default;
hl7_message_bus& hl7_message_bus::operator=(hl7_message_bus&&) noexcept = default;

std::expected<void, message_bus_error> hl7_message_bus::start() {
    return pimpl_->start();
}

void hl7_message_bus::stop() {
    pimpl_->stop();
}

bool hl7_message_bus::is_running() const noexcept {
    return pimpl_->is_running();
}

std::expected<void, message_bus_error> hl7_message_bus::publish(
    const hl7::hl7_message& message,
    message_priority priority) {
    return pimpl_->publish(message, priority);
}

std::expected<void, message_bus_error> hl7_message_bus::publish(
    std::string_view topic,
    const hl7::hl7_message& message,
    message_priority priority) {
    return pimpl_->publish(topic, message, priority);
}

std::expected<subscription_handle, message_bus_error> hl7_message_bus::subscribe(
    std::string_view topic_pattern,
    message_callback callback,
    message_filter filter,
    int priority) {
    return pimpl_->subscribe(topic_pattern, std::move(callback),
                              std::move(filter), priority);
}

std::expected<subscription_handle, message_bus_error>
hl7_message_bus::subscribe_to_type(std::string_view message_type,
                                    message_callback callback) {
    auto topic = std::string(topics::HL7_BASE) + "." +
                 std::string(message_type) + ".*";
    // Convert to lowercase
    for (auto& c : topic) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return subscribe(topic, std::move(callback));
}

std::expected<subscription_handle, message_bus_error>
hl7_message_bus::subscribe_to_event(std::string_view message_type,
                                     std::string_view trigger_event,
                                     message_callback callback) {
    auto topic = topics::build_topic(message_type, trigger_event);
    return subscribe(topic, std::move(callback));
}

std::expected<void, message_bus_error> hl7_message_bus::unsubscribe(
    const subscription_handle& handle) {
    return pimpl_->unsubscribe(handle);
}

void hl7_message_bus::unsubscribe_all() {
    pimpl_->unsubscribe_all();
}

size_t hl7_message_bus::subscription_count() const noexcept {
    return pimpl_->subscription_count();
}

bool hl7_message_bus::has_subscribers(std::string_view topic) const noexcept {
    return pimpl_->has_subscribers(topic);
}

hl7_message_bus::statistics hl7_message_bus::get_statistics() const {
    return pimpl_->get_statistics();
}

void hl7_message_bus::reset_statistics() {
    pimpl_->reset_statistics();
}

const hl7_message_bus_config& hl7_message_bus::config() const noexcept {
    return pimpl_->config();
}

// =============================================================================
// hl7_publisher Implementation
// =============================================================================

hl7_publisher::hl7_publisher(std::shared_ptr<hl7_message_bus> bus)
    : bus_(std::move(bus)) {
}

std::expected<void, message_bus_error> hl7_publisher::publish(
    const hl7::hl7_message& message) {
    if (!bus_) {
        return std::unexpected(message_bus_error::not_started);
    }
    return bus_->publish(message, default_priority_);
}

std::expected<void, message_bus_error> hl7_publisher::publish(
    std::string_view topic,
    const hl7::hl7_message& message) {
    if (!bus_) {
        return std::unexpected(message_bus_error::not_started);
    }
    return bus_->publish(topic, message, default_priority_);
}

void hl7_publisher::set_default_priority(message_priority priority) {
    default_priority_ = priority;
}

bool hl7_publisher::is_ready() const noexcept {
    return bus_ && bus_->is_running();
}

// =============================================================================
// hl7_subscriber Implementation
// =============================================================================

hl7_subscriber::hl7_subscriber(std::shared_ptr<hl7_message_bus> bus)
    : bus_(std::move(bus)) {
}

hl7_subscriber::~hl7_subscriber() {
    unsubscribe_all();
}

hl7_subscriber::hl7_subscriber(hl7_subscriber&&) noexcept = default;
hl7_subscriber& hl7_subscriber::operator=(hl7_subscriber&&) noexcept = default;

std::expected<void, message_bus_error> hl7_subscriber::on_adt(
    message_callback callback) {
    return on(topics::HL7_ADT_ALL, std::move(callback));
}

std::expected<void, message_bus_error> hl7_subscriber::on_orm(
    message_callback callback) {
    return on(topics::HL7_ORM_ALL, std::move(callback));
}

std::expected<void, message_bus_error> hl7_subscriber::on_oru(
    message_callback callback) {
    return on(topics::HL7_ORU_ALL, std::move(callback));
}

std::expected<void, message_bus_error> hl7_subscriber::on_siu(
    message_callback callback) {
    return on("hl7.siu.*", std::move(callback));
}

std::expected<void, message_bus_error> hl7_subscriber::on(
    std::string_view topic_pattern,
    message_callback callback,
    message_filter filter) {
    if (!bus_) {
        return std::unexpected(message_bus_error::not_started);
    }

    auto result = bus_->subscribe(topic_pattern, std::move(callback),
                                   std::move(filter));
    if (!result) {
        return std::unexpected(result.error());
    }

    handles_.push_back(*result);
    return {};
}

void hl7_subscriber::unsubscribe_all() {
    if (bus_) {
        for (const auto& handle : handles_) {
            (void)bus_->unsubscribe(handle);
        }
    }
    handles_.clear();
}

size_t hl7_subscriber::subscription_count() const noexcept {
    return handles_.size();
}

}  // namespace pacs::bridge::messaging
