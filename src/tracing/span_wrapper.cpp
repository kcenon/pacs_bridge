/**
 * @file span_wrapper.cpp
 * @brief Implementation of RAII span wrapper
 */

#include "pacs/bridge/tracing/span_wrapper.h"
#include "pacs/bridge/tracing/trace_manager.h"
#include "span_impl.h"

#include <sstream>

namespace pacs::bridge::tracing {

// =============================================================================
// span_wrapper Implementation
// =============================================================================

span_wrapper::span_wrapper() : pimpl_(nullptr) {}

span_wrapper::~span_wrapper() {
    if (pimpl_ && pimpl_->is_active()) {
        pimpl_->end();
    }
}

span_wrapper::span_wrapper(span_wrapper&& other) noexcept
    : pimpl_(std::move(other.pimpl_)) {}

span_wrapper& span_wrapper::operator=(span_wrapper&& other) noexcept {
    if (this != &other) {
        // End current span if active
        if (pimpl_ && pimpl_->is_active()) {
            pimpl_->end();
        }
        pimpl_ = std::move(other.pimpl_);
    }
    return *this;
}

span_wrapper::span_wrapper(std::unique_ptr<span_impl> impl)
    : pimpl_(std::move(impl)) {}

bool span_wrapper::is_active() const noexcept {
    return pimpl_ && pimpl_->is_active();
}

bool span_wrapper::is_valid() const noexcept {
    return pimpl_ && pimpl_->is_valid();
}

std::string_view span_wrapper::name() const noexcept {
    if (pimpl_) {
        return pimpl_->name();
    }
    return "";
}

const trace_context& span_wrapper::context() const noexcept {
    if (pimpl_) {
        return pimpl_->context();
    }
    static trace_context empty;
    return empty;
}

span_wrapper& span_wrapper::set_attribute(std::string_view key,
                                          std::string_view value) {
    if (pimpl_) {
        pimpl_->set_attribute(key, value);
    }
    return *this;
}

span_wrapper& span_wrapper::set_attribute(std::string_view key,
                                          const char* value) {
    return set_attribute(key, std::string_view{value});
}

span_wrapper& span_wrapper::set_attribute(std::string_view key, int64_t value) {
    if (pimpl_) {
        pimpl_->set_attribute(key, std::to_string(value));
    }
    return *this;
}

span_wrapper& span_wrapper::set_attribute(std::string_view key, double value) {
    if (pimpl_) {
        std::ostringstream ss;
        ss << value;
        pimpl_->set_attribute(key, ss.str());
    }
    return *this;
}

span_wrapper& span_wrapper::set_attribute(std::string_view key, bool value) {
    if (pimpl_) {
        pimpl_->set_attribute(key, value ? "true" : "false");
    }
    return *this;
}

span_wrapper& span_wrapper::set_status(span_status status,
                                       std::string_view message) {
    if (pimpl_) {
        pimpl_->set_status(status, message);
    }
    return *this;
}

span_wrapper& span_wrapper::record_exception(const std::exception& exception) {
    if (pimpl_) {
        pimpl_->set_status(span_status::error, exception.what());
        pimpl_->set_attribute("exception.type", typeid(exception).name());
        pimpl_->set_attribute("exception.message", exception.what());
    }
    return *this;
}

span_wrapper& span_wrapper::set_error(std::string_view error_message) {
    if (pimpl_) {
        pimpl_->set_status(span_status::error, error_message);
    }
    return *this;
}

span_wrapper& span_wrapper::add_event(std::string_view name) {
    if (pimpl_) {
        pimpl_->add_event(name, {});
    }
    return *this;
}

span_wrapper& span_wrapper::add_event(
    std::string_view name,
    const std::unordered_map<std::string, std::string>& attributes) {
    if (pimpl_) {
        pimpl_->add_event(name, attributes);
    }
    return *this;
}

span_wrapper span_wrapper::start_child(std::string_view name, span_kind kind) {
    if (!pimpl_ || !pimpl_->is_active()) {
        return span_wrapper{};  // No-op span
    }

    return trace_manager::instance().start_span_with_parent(
        name, pimpl_->context(), kind);
}

void span_wrapper::end() {
    if (pimpl_) {
        pimpl_->end();
    }
}

void span_wrapper::end(std::chrono::system_clock::time_point end_time) {
    if (pimpl_) {
        pimpl_->end(end_time);
    }
}

std::string span_wrapper::get_traceparent() const {
    if (pimpl_ && pimpl_->is_valid()) {
        return pimpl_->context().to_traceparent();
    }
    return "";
}

void span_wrapper::inject_context(
    std::unordered_map<std::string, std::string>& headers) const {
    if (pimpl_ && pimpl_->is_valid()) {
        headers["traceparent"] = pimpl_->context().to_traceparent();
        // tracestate could be added here if needed
    }
}

span_wrapper span_wrapper::create_internal(
    std::string_view name,
    const trace_context& ctx,
    span_kind kind,
    std::string_view service_name) {

    auto impl_ptr = std::make_unique<span_impl>(
        name, ctx, kind, std::string(service_name));
    return span_wrapper(std::move(impl_ptr));
}

}  // namespace pacs::bridge::tracing
