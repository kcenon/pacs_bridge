/**
 * @file trace_propagation.cpp
 * @brief Implementation of trace context propagation utilities
 */

#include "pacs/bridge/tracing/trace_propagation.h"

#include "pacs/bridge/protocol/hl7/hl7_message.h"

#include <sstream>

namespace pacs::bridge::tracing {

// =============================================================================
// Thread-Local Context Storage
// =============================================================================

namespace {

thread_local std::optional<trace_context> current_context_;

}  // namespace

std::optional<trace_context> get_current_trace_context() {
    return current_context_;
}

void set_current_trace_context(const trace_context& context) {
    current_context_ = context;
}

void clear_current_trace_context() {
    current_context_.reset();
}

trace_context_guard::trace_context_guard(const trace_context& context)
    : previous_context_(current_context_) {
    current_context_ = context;
}

trace_context_guard::~trace_context_guard() {
    current_context_ = previous_context_;
}

// =============================================================================
// HL7 Trace Context Propagation
// =============================================================================

bool inject_trace_context(hl7::hl7_message& message,
                          const trace_context& context,
                          const hl7_propagation_config& config) {
    if (!config.enabled) {
        return false;
    }

    switch (config.strategy) {
        case hl7_propagation_strategy::z_segment: {
            // Create a ZTR segment with trace context
            // ZTR|traceparent|tracestate
            auto& ztr = message.add_segment(config.segment_name);
            ztr.set_field(1, context.to_traceparent());

            // Add optional tracestate if present
            if (!context.parent_span_id.value_or("").empty()) {
                ztr.set_field(2, "parent=" + *context.parent_span_id);
            }

            return true;
        }

        case hl7_propagation_strategy::msh_control_id: {
            // Append trace ID to existing MSH-10
            auto* msh = message.segment("MSH");
            if (!msh) {
                return false;
            }

            std::string control_id(msh->field_value(10));
            if (!control_id.empty()) {
                control_id += "_";
            }
            control_id += context.trace_id.substr(0, 16);  // Use first 16 chars

            // Note: MSH segments are read-only in current API
            // This would require a mutable segment API
            return false;  // Not fully implemented
        }

        case hl7_propagation_strategy::msh_custom_field: {
            // Use a custom MSH field
            auto* msh = message.segment("MSH");
            if (!msh) {
                return false;
            }

            // Note: Requires mutable segment API
            return false;  // Not fully implemented
        }
    }

    return false;
}

std::optional<trace_context> extract_trace_context(
    const hl7::hl7_message& message,
    const hl7_propagation_config& config) {
    if (!config.enabled) {
        return std::nullopt;
    }

    switch (config.strategy) {
        case hl7_propagation_strategy::z_segment: {
            // Look for ZTR segment
            const auto* ztr = message.segment(config.segment_name);
            if (!ztr) {
                return std::nullopt;
            }

            std::string_view traceparent = ztr->field_value(1);
            if (traceparent.empty()) {
                return std::nullopt;
            }

            return trace_context::from_traceparent(traceparent);
        }

        case hl7_propagation_strategy::msh_control_id: {
            const auto* msh = message.segment("MSH");
            if (!msh) {
                return std::nullopt;
            }

            std::string_view control_id = msh->field_value(10);
            // Try to extract trace ID from control_id
            // Format: original_id_traceid
            size_t underscore = control_id.rfind('_');
            if (underscore != std::string_view::npos &&
                underscore + 1 < control_id.size()) {
                // Could try to reconstruct, but this is limited
            }
            return std::nullopt;  // Not fully implemented
        }

        case hl7_propagation_strategy::msh_custom_field: {
            const auto* msh = message.segment("MSH");
            if (!msh) {
                return std::nullopt;
            }

            std::string_view traceparent = msh->field_value(
                static_cast<size_t>(config.msh_field_index));
            if (traceparent.empty()) {
                return std::nullopt;
            }

            return trace_context::from_traceparent(traceparent);
        }
    }

    return std::nullopt;
}

bool has_trace_context(const hl7::hl7_message& message,
                       const hl7_propagation_config& config) {
    return extract_trace_context(message, config).has_value();
}

// =============================================================================
// DICOM Trace Context Propagation
// =============================================================================

std::string dicom_trace_attributes::to_traceparent() const {
    std::ostringstream oss;
    oss << "00-" << trace_id << "-" << span_id << "-01";
    return oss.str();
}

std::optional<dicom_trace_attributes> dicom_trace_attributes::from_traceparent(
    std::string_view traceparent) {
    // Format: 00-trace_id-span_id-flags
    if (traceparent.length() < 55) {
        return std::nullopt;
    }

    if (traceparent.substr(0, 3) != "00-") {
        return std::nullopt;
    }

    // Extract trace_id (32 chars)
    size_t trace_start = 3;
    size_t trace_end = traceparent.find('-', trace_start);
    if (trace_end == std::string_view::npos || trace_end - trace_start != 32) {
        return std::nullopt;
    }

    // Extract span_id (16 chars)
    size_t span_start = trace_end + 1;
    size_t span_end = traceparent.find('-', span_start);
    if (span_end == std::string_view::npos || span_end - span_start != 16) {
        return std::nullopt;
    }

    dicom_trace_attributes attrs;
    attrs.trace_id = std::string(traceparent.substr(trace_start, 32));
    attrs.span_id = std::string(traceparent.substr(span_start, 16));

    return attrs;
}

dicom_trace_attributes to_dicom_attributes(const trace_context& context) {
    dicom_trace_attributes attrs;
    attrs.trace_id = context.trace_id;
    attrs.span_id = context.span_id;
    attrs.parent_span_id = context.parent_span_id;
    return attrs;
}

std::optional<trace_context> from_dicom_attributes(
    const dicom_trace_attributes& attrs) {
    if (attrs.trace_id.empty() || attrs.span_id.empty()) {
        return std::nullopt;
    }

    trace_context ctx;
    ctx.trace_id = attrs.trace_id;
    ctx.span_id = attrs.span_id;
    ctx.parent_span_id = attrs.parent_span_id;
    ctx.trace_flags = 0x01;  // Sampled

    return ctx;
}

}  // namespace pacs::bridge::tracing
