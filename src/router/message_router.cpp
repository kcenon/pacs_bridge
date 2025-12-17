/**
 * @file message_router.cpp
 * @brief HL7 message router implementation
 */

#include "pacs/bridge/router/message_router.h"

#include "pacs/bridge/tracing/trace_manager.h"

#include <kcenon/common/interfaces/global_logger_registry.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <regex>
#include <sstream>

namespace pacs::bridge::router {

namespace {

// Map message_router log_level to common log_level
kcenon::common::interfaces::log_level to_common_level(message_router::log_level level) {
    switch (level) {
        case message_router::log_level::debug:
            return kcenon::common::interfaces::log_level::debug;
        case message_router::log_level::info:
            return kcenon::common::interfaces::log_level::info;
        case message_router::log_level::warning:
            return kcenon::common::interfaces::log_level::warning;
        case message_router::log_level::error:
            return kcenon::common::interfaces::log_level::error;
        default:
            return kcenon::common::interfaces::log_level::info;
    }
}

}  // namespace

// =============================================================================
// message_pattern Implementation
// =============================================================================

message_pattern message_pattern::any() {
    return message_pattern{};
}

message_pattern message_pattern::for_type(std::string_view type) {
    message_pattern p;
    p.message_type = std::string(type);
    return p;
}

message_pattern message_pattern::for_type_trigger(std::string_view type,
                                                   std::string_view trigger) {
    message_pattern p;
    p.message_type = std::string(type);
    p.trigger_event = std::string(trigger);
    return p;
}

// =============================================================================
// handler_result Implementation
// =============================================================================

handler_result handler_result::ok(bool continue_chain) {
    handler_result r;
    r.success = true;
    r.continue_chain = continue_chain;
    return r;
}

handler_result handler_result::ok_with_response(hl7::hl7_message response) {
    handler_result r;
    r.success = true;
    r.continue_chain = false;
    r.response = std::move(response);
    return r;
}

handler_result handler_result::error(std::string_view message) {
    handler_result r;
    r.success = false;
    r.continue_chain = false;
    r.error_message = std::string(message);
    return r;
}

handler_result handler_result::stop() {
    handler_result r;
    r.success = true;
    r.continue_chain = false;
    return r;
}

// =============================================================================
// Pattern Matching Helpers
// =============================================================================

namespace {

bool match_wildcard(std::string_view pattern, std::string_view value) {
    if (pattern.empty()) return true;  // Empty pattern matches all
    if (pattern == "*") return true;   // Wildcard matches all

    // Simple wildcard matching (* at end)
    if (!pattern.empty() && pattern.back() == '*') {
        std::string_view prefix = pattern.substr(0, pattern.size() - 1);
        return value.substr(0, prefix.size()) == prefix;
    }

    return pattern == value;
}

bool match_regex(const std::string& pattern, std::string_view value) {
    if (pattern.empty()) return true;

    try {
        std::regex re(pattern, std::regex::icase);
        return std::regex_match(std::string(value), re);
    } catch (const std::regex_error&) {
        return false;
    }
}

bool pattern_matches(const std::string& pattern, std::string_view value,
                     bool use_regex) {
    if (use_regex) {
        return match_regex(pattern, value);
    }
    return match_wildcard(pattern, value);
}

}  // namespace

// =============================================================================
// route Implementation
// =============================================================================

bool route::matches(const hl7::hl7_message& message) const {
    if (!enabled) return false;

    auto header = message.header();

    // Check all pattern fields
    if (!pattern_matches(pattern.message_type, header.type_string,
                         pattern.use_regex)) {
        return false;
    }

    if (!pattern_matches(pattern.trigger_event, header.trigger_event,
                         pattern.use_regex)) {
        return false;
    }

    if (!pattern_matches(pattern.sending_application, header.sending_application,
                         pattern.use_regex)) {
        return false;
    }

    if (!pattern_matches(pattern.sending_facility, header.sending_facility,
                         pattern.use_regex)) {
        return false;
    }

    if (!pattern_matches(pattern.receiving_application, header.receiving_application,
                         pattern.use_regex)) {
        return false;
    }

    if (!pattern_matches(pattern.receiving_facility, header.receiving_facility,
                         pattern.use_regex)) {
        return false;
    }

    if (!pattern_matches(pattern.processing_id, header.processing_id,
                         pattern.use_regex)) {
        return false;
    }

    if (!pattern_matches(pattern.version, header.version_id,
                         pattern.use_regex)) {
        return false;
    }

    // Apply custom filter if present
    if (filter && !filter(message)) {
        return false;
    }

    return true;
}

// =============================================================================
// message_router::impl
// =============================================================================

class message_router::impl {
public:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, message_handler> handlers_;
    std::vector<struct route> routes_;
    message_handler default_handler_;
    mutable statistics stats_;
    logger_callback logger_;
    log_level min_log_level_ = log_level::info;

    void sort_routes() {
        std::sort(routes_.begin(), routes_.end(),
                  [](const struct route& a, const struct route& b) {
                      return a.priority < b.priority;
                  });
    }

    void log(log_level level, const std::string& message_control_id,
             const std::string& message_type, const std::string& route_id,
             const std::string& handler_id, const std::string& message,
             std::optional<int64_t> processing_time_us = std::nullopt) const {
        if (level < min_log_level_) {
            return;
        }

        // Build structured log message
        std::ostringstream oss;
        oss << "[Router] ";
        if (!message_control_id.empty()) {
            oss << "msg_id=" << message_control_id << " ";
        }
        if (!message_type.empty()) {
            oss << "type=" << message_type << " ";
        }
        if (!route_id.empty()) {
            oss << "route=" << route_id << " ";
        }
        if (!handler_id.empty()) {
            oss << "handler=" << handler_id << " ";
        }
        oss << message;
        if (processing_time_us.has_value()) {
            oss << " (duration=" << processing_time_us.value() << "us)";
        }

        // Log via GlobalLoggerRegistry
        auto logger = kcenon::common::interfaces::get_logger("message_router");
        if (logger) {
            logger->log(to_common_level(level), oss.str());
        }

        // Also call custom callback if set
        if (logger_) {
            log_entry entry;
            entry.timestamp = std::chrono::system_clock::now();
            entry.level = level;
            entry.message_control_id = message_control_id;
            entry.message_type = message_type;
            entry.route_id = route_id;
            entry.handler_id = handler_id;
            entry.message = message;
            entry.processing_time_us = processing_time_us;
            logger_(entry);
        }
    }

    void log_debug(const std::string& message_control_id,
                   const std::string& message_type,
                   const std::string& message) const {
        log(log_level::debug, message_control_id, message_type, "", "", message);
    }

    void log_info(const std::string& message_control_id,
                  const std::string& message_type,
                  const std::string& route_id,
                  const std::string& message) const {
        log(log_level::info, message_control_id, message_type, route_id, "", message);
    }

    void log_warning(const std::string& message_control_id,
                     const std::string& message_type,
                     const std::string& message) const {
        log(log_level::warning, message_control_id, message_type, "", "", message);
    }

    void log_error(const std::string& message_control_id,
                   const std::string& message_type,
                   const std::string& route_id,
                   const std::string& handler_id,
                   const std::string& message) const {
        log(log_level::error, message_control_id, message_type, route_id, handler_id, message);
    }
};

// =============================================================================
// message_router Implementation
// =============================================================================

message_router::message_router() : pimpl_(std::make_unique<impl>()) {}

message_router::~message_router() = default;

message_router::message_router(message_router&&) noexcept = default;
message_router& message_router::operator=(message_router&&) noexcept = default;

bool message_router::register_handler(std::string_view id, message_handler handler) {
    std::lock_guard lock(pimpl_->mutex_);

    auto [it, inserted] = pimpl_->handlers_.emplace(std::string(id), std::move(handler));
    return inserted;
}

bool message_router::unregister_handler(std::string_view id) {
    std::lock_guard lock(pimpl_->mutex_);
    return pimpl_->handlers_.erase(std::string(id)) > 0;
}

bool message_router::has_handler(std::string_view id) const noexcept {
    std::lock_guard lock(pimpl_->mutex_);
    return pimpl_->handlers_.find(std::string(id)) != pimpl_->handlers_.end();
}

std::vector<std::string> message_router::handler_ids() const {
    std::lock_guard lock(pimpl_->mutex_);

    std::vector<std::string> ids;
    ids.reserve(pimpl_->handlers_.size());
    for (const auto& [id, _] : pimpl_->handlers_) {
        ids.push_back(id);
    }
    return ids;
}

std::expected<void, router_error> message_router::add_route(const struct route& r) {
    if (r.id.empty()) {
        return std::unexpected(router_error::invalid_route);
    }

    std::lock_guard lock(pimpl_->mutex_);

    // Check for duplicate
    for (const auto& existing : pimpl_->routes_) {
        if (existing.id == r.id) {
            return std::unexpected(router_error::route_exists);
        }
    }

    // Validate handler references
    for (const auto& handler_id : r.handler_ids) {
        if (pimpl_->handlers_.find(handler_id) == pimpl_->handlers_.end()) {
            return std::unexpected(router_error::handler_not_found);
        }
    }

    // Validate regex patterns if used
    if (r.pattern.use_regex) {
        try {
            if (!r.pattern.message_type.empty()) {
                std::regex(r.pattern.message_type);
            }
            if (!r.pattern.trigger_event.empty()) {
                std::regex(r.pattern.trigger_event);
            }
        } catch (const std::regex_error&) {
            return std::unexpected(router_error::invalid_pattern);
        }
    }

    pimpl_->routes_.push_back(r);
    pimpl_->sort_routes();

    return {};
}

bool message_router::remove_route(std::string_view route_id) {
    std::lock_guard lock(pimpl_->mutex_);

    auto it = std::find_if(pimpl_->routes_.begin(), pimpl_->routes_.end(),
                           [route_id](const struct route& r) { return r.id == route_id; });

    if (it != pimpl_->routes_.end()) {
        pimpl_->routes_.erase(it);
        return true;
    }
    return false;
}

void message_router::set_route_enabled(std::string_view route_id, bool enabled) {
    std::lock_guard lock(pimpl_->mutex_);

    for (auto& r : pimpl_->routes_) {
        if (r.id == route_id) {
            r.enabled = enabled;
            break;
        }
    }
}

const route* message_router::get_route(std::string_view route_id) const {
    std::lock_guard lock(pimpl_->mutex_);

    for (const auto& r : pimpl_->routes_) {
        if (r.id == route_id) {
            return &r;
        }
    }
    return nullptr;
}

std::vector<route> message_router::routes() const {
    std::lock_guard lock(pimpl_->mutex_);
    return pimpl_->routes_;
}

void message_router::clear_routes() {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->routes_.clear();
}

std::expected<handler_result, router_error> message_router::route(
    const hl7::hl7_message& message) const {
    // Start tracing span
    auto span = tracing::trace_manager::instance().start_span(
        "hl7_route", tracing::span_kind::internal);

    std::lock_guard lock(pimpl_->mutex_);

    auto start_time = std::chrono::steady_clock::now();

    ++pimpl_->stats_.total_messages;

    // Extract message info for logging
    auto header = message.header();
    std::string control_id(message.control_id());
    std::string msg_type = header.full_message_type();

    // Add tracing attributes
    span.set_attribute("hl7.message_type", msg_type);
    span.set_attribute("hl7.control_id", control_id);
    span.set_attribute("router.route_count", static_cast<int64_t>(pimpl_->routes_.size()));

    pimpl_->log_debug(control_id, msg_type,
                      "Routing message started");

    handler_result final_result = handler_result::ok();
    bool any_matched = false;

    // Try each route in priority order
    for (const auto& r : pimpl_->routes_) {
        if (!r.matches(message)) {
            continue;
        }

        any_matched = true;
        ++pimpl_->stats_.matched_messages;
        ++pimpl_->stats_.route_matches[r.id];

        pimpl_->log_info(control_id, msg_type, r.id,
                         "Route matched: " + r.name);

        // Execute handler chain
        for (const auto& handler_id : r.handler_ids) {
            auto it = pimpl_->handlers_.find(handler_id);
            if (it == pimpl_->handlers_.end()) {
                pimpl_->log_warning(control_id, msg_type,
                                    "Handler not found: " + handler_id);
                continue;  // Handler removed since route was added
            }

            pimpl_->log_debug(control_id, msg_type,
                              "Executing handler: " + handler_id);

            try {
                auto handler_start = std::chrono::steady_clock::now();
                final_result = it->second(message);
                auto handler_end = std::chrono::steady_clock::now();
                auto handler_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    handler_end - handler_start).count();

                pimpl_->log(log_level::debug, control_id, msg_type, r.id, handler_id,
                            "Handler completed successfully",
                            handler_duration);
            } catch (const std::exception& e) {
                ++pimpl_->stats_.handler_errors;
                pimpl_->log_error(control_id, msg_type, r.id, handler_id,
                                  std::string("Handler threw exception: ") + e.what());
                span.set_attribute("router.matched_route", r.id);
                span.set_error(std::string("Handler exception: ") + e.what());
                return std::unexpected(router_error::handler_error);
            }

            if (!final_result.success) {
                ++pimpl_->stats_.handler_errors;
                pimpl_->log_error(control_id, msg_type, r.id, handler_id,
                                  "Handler returned error: " + final_result.error_message);
                span.set_attribute("router.matched_route", r.id);
                span.set_error("Handler error: " + final_result.error_message);
                return std::unexpected(router_error::handler_error);
            }

            if (!final_result.continue_chain) {
                pimpl_->log_debug(control_id, msg_type,
                                  "Handler chain stopped by: " + handler_id);
                break;  // Handler requested stop
            }
        }

        if (r.terminal || !final_result.continue_chain) {
            if (r.terminal) {
                pimpl_->log_debug(control_id, msg_type,
                                  "Terminal route reached: " + r.id);
            }
            break;  // Terminal route or handler requested stop
        }
    }

    // Use default handler if no matches
    if (!any_matched) {
        if (pimpl_->default_handler_) {
            ++pimpl_->stats_.default_handled;
            pimpl_->log_warning(control_id, msg_type,
                                "No matching route, using default handler");
            span.set_attribute("router.used_default", true);
            try {
                final_result = pimpl_->default_handler_(message);
            } catch (const std::exception& e) {
                ++pimpl_->stats_.handler_errors;
                pimpl_->log_error(control_id, msg_type, "", "default",
                                  std::string("Default handler threw exception: ") + e.what());
                span.set_error(std::string("Default handler exception: ") + e.what());
                return std::unexpected(router_error::handler_error);
            }
        } else {
            ++pimpl_->stats_.unhandled_messages;
            pimpl_->log_warning(control_id, msg_type,
                                "No matching route and no default handler");
            span.set_attribute("router.matched", false);
            span.set_error("No matching route");
            return std::unexpected(router_error::no_matching_route);
        }
    } else {
        span.set_attribute("router.matched", true);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();

    span.set_attribute("router.duration_us", total_duration);

    pimpl_->log(log_level::info, control_id, msg_type, "", "",
                "Routing completed successfully", total_duration);

    return final_result;
}

std::vector<std::string> message_router::find_matching_routes(
    const hl7::hl7_message& message) const {
    std::lock_guard lock(pimpl_->mutex_);

    std::vector<std::string> matches;
    for (const auto& r : pimpl_->routes_) {
        if (r.matches(message)) {
            matches.push_back(r.id);
        }
    }
    return matches;
}

bool message_router::has_matching_route(
    const hl7::hl7_message& message) const noexcept {
    std::lock_guard lock(pimpl_->mutex_);

    for (const auto& r : pimpl_->routes_) {
        if (r.matches(message)) {
            return true;
        }
    }
    return false;
}

void message_router::set_default_handler(message_handler handler) {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->default_handler_ = std::move(handler);
}

void message_router::clear_default_handler() {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->default_handler_ = nullptr;
}

message_router::statistics message_router::get_statistics() const {
    std::lock_guard lock(pimpl_->mutex_);
    return pimpl_->stats_;
}

void message_router::reset_statistics() {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->stats_ = statistics{};
}

void message_router::set_logger(logger_callback callback) {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->logger_ = std::move(callback);
}

void message_router::clear_logger() {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->logger_ = nullptr;
}

void message_router::set_log_level(log_level level) {
    std::lock_guard lock(pimpl_->mutex_);
    pimpl_->min_log_level_ = level;
}

message_router::log_level message_router::get_log_level() const noexcept {
    std::lock_guard lock(pimpl_->mutex_);
    return pimpl_->min_log_level_;
}

// =============================================================================
// route_builder Implementation
// =============================================================================

route_builder route_builder::create(std::string_view id) {
    return route_builder(id);
}

route_builder::route_builder(std::string_view id) {
    route_.id = std::string(id);
}

route_builder& route_builder::name(std::string_view n) {
    route_.name = std::string(n);
    return *this;
}

route_builder& route_builder::description(std::string_view desc) {
    route_.description = std::string(desc);
    return *this;
}

route_builder& route_builder::match_type(std::string_view type) {
    route_.pattern.message_type = std::string(type);
    return *this;
}

route_builder& route_builder::match_trigger(std::string_view trigger) {
    route_.pattern.trigger_event = std::string(trigger);
    return *this;
}

route_builder& route_builder::match_sender(std::string_view app,
                                            std::string_view facility) {
    route_.pattern.sending_application = std::string(app);
    if (!facility.empty()) {
        route_.pattern.sending_facility = std::string(facility);
    }
    return *this;
}

route_builder& route_builder::match_receiver(std::string_view app,
                                              std::string_view facility) {
    route_.pattern.receiving_application = std::string(app);
    if (!facility.empty()) {
        route_.pattern.receiving_facility = std::string(facility);
    }
    return *this;
}

route_builder& route_builder::match_any() {
    route_.pattern = message_pattern::any();
    return *this;
}

route_builder& route_builder::use_regex(bool enable) {
    route_.pattern.use_regex = enable;
    return *this;
}

route_builder& route_builder::handler(std::string_view handler_id) {
    route_.handler_ids.push_back(std::string(handler_id));
    return *this;
}

route_builder& route_builder::priority(int p) {
    route_.priority = p;
    return *this;
}

route_builder& route_builder::terminal(bool t) {
    route_.terminal = t;
    return *this;
}

route_builder& route_builder::filter(message_filter f) {
    route_.filter = std::move(f);
    return *this;
}

route route_builder::build() const {
    return route_;
}

}  // namespace pacs::bridge::router
