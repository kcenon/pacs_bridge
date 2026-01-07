/**
 * @file hl7_request_handler.cpp
 * @brief Implementation of HL7 Request/Reply pattern
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 * @see https://github.com/kcenon/pacs_bridge/issues/154
 */

#include "pacs/bridge/messaging/hl7_request_handler.h"
#include "pacs/bridge/messaging/hl7_message_bus.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace pacs::bridge::messaging {

// =============================================================================
// ACK Builder Utilities
// =============================================================================

namespace {
    // Convert messaging::ack_code to hl7::ack_code
    hl7::ack_code to_hl7_ack_code(ack_code code) {
        switch (code) {
            case ack_code::AA: return hl7::ack_code::AA;
            case ack_code::AE: return hl7::ack_code::AE;
            case ack_code::AR: return hl7::ack_code::AR;
            case ack_code::CA: return hl7::ack_code::CA;
            case ack_code::CE: return hl7::ack_code::CE;
            case ack_code::CR: return hl7::ack_code::CR;
            default: return hl7::ack_code::AA;
        }
    }
}  // anonymous namespace

namespace ack_builder {

hl7::hl7_message generate_ack(
    const hl7::hl7_message& request,
    ack_code code,
    std::string_view text_message,
    std::string_view sending_app,
    std::string_view sending_facility) {

    // Use hl7_builder::create_ack to generate the ACK message
    auto ack = hl7::hl7_builder::create_ack(request, to_hl7_ack_code(code), text_message);

    // Override sending application/facility if provided
    if (!sending_app.empty()) {
        ack.set_value("MSH.3", std::string(sending_app));
    }
    if (!sending_facility.empty()) {
        ack.set_value("MSH.4", std::string(sending_facility));
    }

    return ack;
}

hl7::hl7_message generate_nak(
    const hl7::hl7_message& request,
    std::string_view error_message,
    std::string_view error_code,
    std::string_view sending_app,
    std::string_view sending_facility) {

    ack_code code = ack_code::AE;
    if (error_code == "AR" || error_code == "CR") {
        code = ack_code::AR;
    }

    auto nak = generate_ack(request, code, error_message,
                             sending_app, sending_facility);

    // Add ERR segment if error details provided
    if (!error_message.empty()) {
        nak.set_value("ERR.1", "0");
        nak.set_value("ERR.3", std::string(error_code));
        nak.set_value("ERR.4", std::string(error_message));
    }

    return nak;
}

bool is_ack(const hl7::hl7_message& message) {
    auto type = message.get_value("MSH.9.1");
    return type == "ACK" || type == "MCF";
}

std::optional<ack_code> get_ack_code(const hl7::hl7_message& ack) {
    auto code_str = ack.get_value("MSA.1");
    if (code_str.empty()) return std::nullopt;

    if (code_str == "AA") return ack_code::AA;
    if (code_str == "AE") return ack_code::AE;
    if (code_str == "AR") return ack_code::AR;
    if (code_str == "CA") return ack_code::CA;
    if (code_str == "CE") return ack_code::CE;
    if (code_str == "CR") return ack_code::CR;

    return std::nullopt;
}

bool is_ack_success(const hl7::hl7_message& ack) {
    auto code = get_ack_code(ack);
    if (!code) return false;
    return *code == ack_code::AA || *code == ack_code::CA;
}

}  // namespace ack_builder

// =============================================================================
// hl7_request_client Implementation
// =============================================================================

class hl7_request_client::impl {
public:
    impl(std::shared_ptr<hl7_message_bus> bus, const request_handler_config& config)
        : bus_(std::move(bus))
        , config_(config)
        , next_correlation_id_(1) {
    }

    ~impl() {
        cancel_all();
    }

    std::expected<request_result, request_error> request(
        const hl7::hl7_message& request,
        std::chrono::milliseconds timeout) {

        if (!bus_ || !bus_->is_running()) {
            return std::unexpected(request_error::service_unavailable);
        }

        if (timeout.count() == 0) {
            timeout = config_.default_timeout;
        }

        // Generate correlation ID
        auto correlation_id = generate_correlation_id();

        // Create promise for response
        auto promise = std::make_shared<std::promise<hl7::hl7_message>>();
        auto future = promise->get_future();

        // Register pending request
        {
            std::unique_lock lock(mutex_);
            if (pending_.size() >= config_.max_pending_requests) {
                return std::unexpected(request_error::service_unavailable);
            }
            pending_[correlation_id] = {
                .promise = promise,
                .start_time = std::chrono::steady_clock::now()
            };
        }

        auto start_time = std::chrono::steady_clock::now();

        // Subscribe to reply topic
        auto reply_topic = config_.reply_topic.empty()
            ? "hl7.reply." + correlation_id
            : config_.reply_topic;

        auto sub_result = bus_->subscribe(reply_topic,
            [this, correlation_id](const hl7::hl7_message& msg) {
                handle_response(correlation_id, msg);
                return subscription_result::ok();
            });

        if (!sub_result) {
            std::unique_lock lock(mutex_);
            pending_.erase(correlation_id);
            return std::unexpected(request_error::service_unavailable);
        }

        // Publish request with correlation ID
        auto modified_request = request;
        modified_request.set_value("MSH.10", correlation_id);

        auto pub_result = bus_->publish(config_.service_topic, modified_request);
        if (!pub_result) {
            std::unique_lock lock(mutex_);
            pending_.erase(correlation_id);
            (void)bus_->unsubscribe(*sub_result);
            return std::unexpected(request_error::service_unavailable);
        }

        // Wait for response
        auto status = future.wait_for(timeout);

        // Cleanup subscription
        (void)bus_->unsubscribe(*sub_result);

        // Remove pending request
        {
            std::unique_lock lock(mutex_);
            pending_.erase(correlation_id);
        }

        if (status == std::future_status::timeout) {
            return std::unexpected(request_error::timeout);
        }

        auto end_time = std::chrono::steady_clock::now();
        auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        try {
            request_result result;
            result.response = future.get();
            result.round_trip_time = rtt;
            return result;
        } catch (...) {
            return std::unexpected(request_error::connection_lost);
        }
    }

    std::expected<request_result, request_error> send(
        const hl7::hl7_message& request) {
        return this->request(request, std::chrono::milliseconds{0});
    }

    bool cancel(std::string_view correlation_id) {
        std::unique_lock lock(mutex_);

        auto it = pending_.find(std::string(correlation_id));
        if (it == pending_.end()) {
            return false;
        }

        try {
            it->second.promise->set_exception(
                std::make_exception_ptr(
                    std::runtime_error("Request cancelled")));
        } catch (...) {
            // Promise may already be satisfied
        }

        pending_.erase(it);
        return true;
    }

    void cancel_all() {
        std::unique_lock lock(mutex_);

        for (auto& [id, req] : pending_) {
            try {
                req.promise->set_exception(
                    std::make_exception_ptr(
                        std::runtime_error("All requests cancelled")));
            } catch (...) {
                // Ignore
            }
        }

        pending_.clear();
    }

    size_t pending_count() const noexcept {
        std::shared_lock lock(mutex_);
        return pending_.size();
    }

    bool is_ready() const noexcept {
        return bus_ && bus_->is_running();
    }

    const std::string& service_topic() const noexcept {
        return config_.service_topic;
    }

private:
    struct pending_request {
        std::shared_ptr<std::promise<hl7::hl7_message>> promise;
        std::chrono::steady_clock::time_point start_time;
    };

    std::string generate_correlation_id() {
        auto id = next_correlation_id_.fetch_add(1);
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        return std::to_string(ts) + "_" + std::to_string(id);
    }

    void handle_response(const std::string& correlation_id,
                         const hl7::hl7_message& response) {
        std::unique_lock lock(mutex_);

        auto it = pending_.find(correlation_id);
        if (it == pending_.end()) {
            return;  // Request may have timed out
        }

        try {
            it->second.promise->set_value(response);
        } catch (...) {
            // Promise may already be satisfied
        }
    }

    std::shared_ptr<hl7_message_bus> bus_;
    request_handler_config config_;
    std::atomic<uint64_t> next_correlation_id_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, pending_request> pending_;
};

hl7_request_client::hl7_request_client(
    std::shared_ptr<hl7_message_bus> bus,
    std::string_view service_topic)
    : pimpl_(std::make_unique<impl>(
          std::move(bus),
          request_handler_config{.service_topic = std::string(service_topic)})) {
}

hl7_request_client::hl7_request_client(
    std::shared_ptr<hl7_message_bus> bus,
    const request_handler_config& config)
    : pimpl_(std::make_unique<impl>(std::move(bus), config)) {
}

hl7_request_client::~hl7_request_client() = default;
hl7_request_client::hl7_request_client(hl7_request_client&&) noexcept = default;
hl7_request_client& hl7_request_client::operator=(hl7_request_client&&) noexcept = default;

std::expected<request_result, request_error> hl7_request_client::request(
    const hl7::hl7_message& request,
    std::chrono::milliseconds timeout) {
    return pimpl_->request(request, timeout);
}

std::expected<request_result, request_error> hl7_request_client::send(
    const hl7::hl7_message& request) {
    return pimpl_->send(request);
}

bool hl7_request_client::cancel(std::string_view correlation_id) {
    return pimpl_->cancel(correlation_id);
}

void hl7_request_client::cancel_all() {
    pimpl_->cancel_all();
}

size_t hl7_request_client::pending_count() const noexcept {
    return pimpl_->pending_count();
}

bool hl7_request_client::is_ready() const noexcept {
    return pimpl_->is_ready();
}

const std::string& hl7_request_client::service_topic() const noexcept {
    return pimpl_->service_topic();
}

// =============================================================================
// hl7_request_server Implementation
// =============================================================================

class hl7_request_server::impl {
public:
    impl(std::shared_ptr<hl7_message_bus> bus, const request_handler_config& config)
        : bus_(std::move(bus))
        , config_(config)
        , running_(false) {
    }

    ~impl() {
        stop();
    }

    std::expected<void, request_error> register_handler(request_processor handler) {
        std::unique_lock lock(mutex_);
        handler_ = std::move(handler);
        return {};
    }

    void unregister_handler() {
        std::unique_lock lock(mutex_);
        handler_ = nullptr;
    }

    bool has_handler() const noexcept {
        std::shared_lock lock(mutex_);
        return handler_ != nullptr;
    }

    std::expected<void, request_error> start() {
        if (!bus_ || !bus_->is_running()) {
            return std::unexpected(request_error::service_unavailable);
        }

        if (running_.load()) {
            return std::unexpected(request_error::service_unavailable);
        }

        // Subscribe to service topic
        auto result = bus_->subscribe(config_.service_topic,
            [this](const hl7::hl7_message& msg) {
                handle_request(msg);
                return subscription_result::ok();
            });

        if (!result) {
            return std::unexpected(request_error::service_unavailable);
        }

        subscription_ = *result;
        running_.store(true);
        return {};
    }

    void stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);

        if (bus_) {
            (void)bus_->unsubscribe(subscription_);
        }
    }

    bool is_running() const noexcept {
        return running_.load();
    }

    const std::string& service_topic() const noexcept {
        return config_.service_topic;
    }

    statistics get_statistics() const {
        std::shared_lock lock(stats_mutex_);
        statistics stats;
        stats.requests_received = stats_.requests_received;
        stats.requests_succeeded = stats_.requests_succeeded;
        stats.requests_failed = stats_.requests_failed;

        if (stats_.requests_received > 0) {
            stats.avg_processing_time_us =
                static_cast<double>(stats_.total_processing_time_us) /
                static_cast<double>(stats_.requests_received);
        }

        return stats;
    }

    void reset_statistics() {
        std::unique_lock lock(stats_mutex_);
        stats_ = internal_stats{};
    }

private:
    struct internal_stats {
        uint64_t requests_received = 0;
        uint64_t requests_succeeded = 0;
        uint64_t requests_failed = 0;
        uint64_t total_processing_time_us = 0;
    };

    void handle_request(const hl7::hl7_message& request) {
        auto start = std::chrono::steady_clock::now();

        {
            std::unique_lock lock(stats_mutex_);
            stats_.requests_received++;
        }

        // Get handler
        request_processor handler;
        {
            std::shared_lock lock(mutex_);
            handler = handler_;
        }

        if (!handler) {
            // Auto-generate NAK if no handler
            if (config_.auto_ack) {
                auto nak = ack_builder::generate_nak(
                    request, "No handler registered",
                    "AR",
                    config_.sending_application,
                    config_.sending_facility);

                send_response(request, nak);
            }

            {
                std::unique_lock lock(stats_mutex_);
                stats_.requests_failed++;
            }
            return;
        }

        // Process request
        auto result = handler(request);

        auto end = std::chrono::steady_clock::now();
        auto processing_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count();

        if (result) {
            send_response(request, *result);

            std::unique_lock lock(stats_mutex_);
            stats_.requests_succeeded++;
            stats_.total_processing_time_us += processing_time;
        } else {
            // Generate NAK on error
            if (config_.auto_ack) {
                auto nak = ack_builder::generate_nak(
                    request, to_string(result.error()),
                    "AE",
                    config_.sending_application,
                    config_.sending_facility);

                send_response(request, nak);
            }

            std::unique_lock lock(stats_mutex_);
            stats_.requests_failed++;
            stats_.total_processing_time_us += processing_time;
        }
    }

    void send_response(const hl7::hl7_message& request,
                       const hl7::hl7_message& response) {
        // Determine reply topic
        auto reply_topic = config_.reply_topic.empty()
            ? "hl7.reply." + std::string(request.control_id())
            : config_.reply_topic;

        (void)bus_->publish(reply_topic, response);
    }

    std::shared_ptr<hl7_message_bus> bus_;
    request_handler_config config_;
    std::atomic<bool> running_;
    subscription_handle subscription_;

    mutable std::shared_mutex mutex_;
    request_processor handler_;

    mutable std::shared_mutex stats_mutex_;
    internal_stats stats_;
};

hl7_request_server::hl7_request_server(
    std::shared_ptr<hl7_message_bus> bus,
    std::string_view service_topic)
    : pimpl_(std::make_unique<impl>(
          std::move(bus),
          request_handler_config{.service_topic = std::string(service_topic)})) {
}

hl7_request_server::hl7_request_server(
    std::shared_ptr<hl7_message_bus> bus,
    const request_handler_config& config)
    : pimpl_(std::make_unique<impl>(std::move(bus), config)) {
}

hl7_request_server::~hl7_request_server() = default;
hl7_request_server::hl7_request_server(hl7_request_server&&) noexcept = default;
hl7_request_server& hl7_request_server::operator=(hl7_request_server&&) noexcept = default;

std::expected<void, request_error> hl7_request_server::register_handler(
    request_processor handler) {
    return pimpl_->register_handler(std::move(handler));
}

void hl7_request_server::unregister_handler() {
    pimpl_->unregister_handler();
}

bool hl7_request_server::has_handler() const noexcept {
    return pimpl_->has_handler();
}

std::expected<void, request_error> hl7_request_server::start() {
    return pimpl_->start();
}

void hl7_request_server::stop() {
    pimpl_->stop();
}

bool hl7_request_server::is_running() const noexcept {
    return pimpl_->is_running();
}

const std::string& hl7_request_server::service_topic() const noexcept {
    return pimpl_->service_topic();
}

hl7_request_server::statistics hl7_request_server::get_statistics() const {
    return pimpl_->get_statistics();
}

void hl7_request_server::reset_statistics() {
    pimpl_->reset_statistics();
}

}  // namespace pacs::bridge::messaging
