/**
 * @file hl7_handler_registry.cpp
 * @brief Implementation of HL7 handler registry
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/202
 * @see https://github.com/kcenon/pacs_bridge/issues/261
 */

#include "pacs/bridge/protocol/hl7/hl7_handler_registry.h"

#include <algorithm>

namespace pacs::bridge::hl7 {

// =============================================================================
// Handler Registration
// =============================================================================

VoidResult hl7_handler_registry::register_handler(
    std::unique_ptr<IHL7Handler> handler) {
    if (!handler) {
        return kcenon::common::make_error<std::monostate>(to_error_info(
            registry_error::registration_failed, "Null handler provided"));
    }

    std::string type_name{handler->handler_type()};

    std::lock_guard<std::mutex> lock(handlers_mutex_);

    if (handlers_.contains(type_name)) {
        return kcenon::common::make_error<std::monostate>(to_error_info(
            registry_error::handler_exists,
            "Handler already registered for type: " + type_name));
    }

    handlers_[type_name] = std::move(handler);
    return std::monostate{};
}

bool hl7_handler_registry::unregister_handler(std::string_view type_name) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    return handlers_.erase(std::string(type_name)) > 0;
}

bool hl7_handler_registry::has_handler(std::string_view type_name) const {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    return handlers_.contains(std::string(type_name));
}

std::vector<std::string> hl7_handler_registry::registered_types() const {
    std::lock_guard<std::mutex> lock(handlers_mutex_);

    std::vector<std::string> types;
    types.reserve(handlers_.size());

    for (const auto& [type, _] : handlers_) {
        types.push_back(type);
    }

    std::sort(types.begin(), types.end());
    return types;
}

size_t hl7_handler_registry::handler_count() const noexcept {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    return handlers_.size();
}

void hl7_handler_registry::clear() {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_.clear();
}

// =============================================================================
// Message Processing
// =============================================================================

IHL7Handler* hl7_handler_registry::find_handler(
    const hl7_message& message) const {
    std::lock_guard<std::mutex> lock(handlers_mutex_);

    for (const auto& [_, handler] : handlers_) {
        if (handler->can_handle(message)) {
            return handler.get();
        }
    }

    return nullptr;
}

Result<handler_result> hl7_handler_registry::process(
    const hl7_message& message) {
    // Find handler
    IHL7Handler* handler = find_handler(message);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_processed++;
    }

    if (!handler) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.no_handler_count++;
        }

        auto header = message.header();
        std::string type_str = to_string(header.type);
        return kcenon::common::make_error<handler_result>(to_error_info(
            registry_error::no_handler,
            "No handler found for message type: " + type_str));
    }

    // Process message
    auto result = handler->process(message);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);

        if (result.is_ok()) {
            stats_.success_count++;
        } else {
            stats_.failure_count++;
        }

        std::string handler_type{handler->handler_type()};
        stats_.handler_counts[handler_type]++;
    }

    return result;
}

bool hl7_handler_registry::can_process(const hl7_message& message) const {
    return find_handler(message) != nullptr;
}

// =============================================================================
// Statistics
// =============================================================================

hl7_handler_registry::statistics hl7_handler_registry::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void hl7_handler_registry::reset_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = statistics{};
}

// =============================================================================
// Global Registry
// =============================================================================

hl7_handler_registry& default_registry() {
    static hl7_handler_registry registry;
    return registry;
}

}  // namespace pacs::bridge::hl7
