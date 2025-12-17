/**
 * @file span_impl.h
 * @brief Internal implementation of span_wrapper::span_impl
 *
 * This header is internal and should not be included by external code.
 */

#ifndef PACS_BRIDGE_TRACING_SPAN_IMPL_H
#define PACS_BRIDGE_TRACING_SPAN_IMPL_H

#include "pacs/bridge/tracing/span_wrapper.h"
#include "pacs/bridge/tracing/tracing_types.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::tracing {

/**
 * @brief Internal implementation of span_wrapper
 */
class span_wrapper::span_impl {
public:
    span_impl() = default;

    span_impl(std::string_view name, const trace_context& ctx, span_kind kind,
              std::string service_name = "")
        : name_(name)
        , context_(ctx)
        , kind_(kind)
        , service_name_(std::move(service_name))
        , start_time_(std::chrono::system_clock::now())
        , active_(true) {}

    ~span_impl() {
        if (active_) {
            end();
        }
    }

    // Non-copyable
    span_impl(const span_impl&) = delete;
    span_impl& operator=(const span_impl&) = delete;

    // Movable
    span_impl(span_impl&&) = default;
    span_impl& operator=(span_impl&&) = default;

    bool is_active() const noexcept { return active_; }
    bool is_valid() const noexcept { return context_.is_valid(); }
    std::string_view name() const noexcept { return name_; }
    const trace_context& context() const noexcept { return context_; }

    void set_attribute(std::string_view key, std::string_view value) {
        if (active_) {
            attributes_[std::string(key)] = std::string(value);
        }
    }

    void set_status(span_status status, std::string_view message) {
        if (active_) {
            status_ = status;
            status_message_ = std::string(message);
        }
    }

    void add_event(std::string_view name,
                   const std::unordered_map<std::string, std::string>& attrs) {
        if (active_) {
            events_.emplace_back(std::string(name), attrs);
        }
    }

    void end() {
        if (active_) {
            end_time_ = std::chrono::system_clock::now();
            active_ = false;
            export_span();
        }
    }

    void end(std::chrono::system_clock::time_point end_time) {
        if (active_) {
            end_time_ = end_time;
            active_ = false;
            export_span();
        }
    }

    void set_service_name(std::string_view name) {
        service_name_ = std::string(name);
    }

    span_data get_span_data() const {
        span_data data;
        data.name = name_;
        data.context = context_;
        data.service_name = service_name_;
        data.kind = kind_;
        data.start_time = start_time_;
        data.end_time = end_time_;
        data.status = status_;
        data.status_message = status_message_;
        data.attributes = attributes_;
        return data;
    }

private:
    void export_span() {
        // Span export is handled by trace_manager
        // This is called to notify that span is complete
    }

    std::string name_;
    trace_context context_;
    span_kind kind_ = span_kind::internal;
    std::string service_name_;
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    span_status status_ = span_status::ok;
    std::string status_message_;
    std::unordered_map<std::string, std::string> attributes_;
    std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>> events_;
    bool active_ = false;
};

}  // namespace pacs::bridge::tracing

#endif  // PACS_BRIDGE_TRACING_SPAN_IMPL_H
