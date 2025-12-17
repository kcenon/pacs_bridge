#ifndef PACS_BRIDGE_TRACING_TRACE_PROPAGATION_H
#define PACS_BRIDGE_TRACING_TRACE_PROPAGATION_H

/**
 * @file trace_propagation.h
 * @brief Trace context propagation utilities for HL7 and DICOM
 *
 * Provides utilities for embedding trace context into HL7 messages
 * and propagating it through the system to PACS operations.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/144
 * @see https://github.com/kcenon/pacs_bridge/issues/149
 */

#include "tracing_types.h"

#include <optional>
#include <string>
#include <string_view>

namespace pacs::bridge::hl7 {
class hl7_message;
}

namespace pacs::bridge::tracing {

// =============================================================================
// HL7 Trace Context Propagation
// =============================================================================

/**
 * @brief Strategies for propagating trace context in HL7 messages
 */
enum class hl7_propagation_strategy {
    /** Use a custom ZTR segment for trace context */
    z_segment,

    /** Embed trace ID in MSH-10 (Message Control ID) */
    msh_control_id,

    /** Use a custom field in MSH segment */
    msh_custom_field
};

/**
 * @brief Configuration for HL7 trace context propagation
 */
struct hl7_propagation_config {
    /** Propagation strategy to use */
    hl7_propagation_strategy strategy = hl7_propagation_strategy::z_segment;

    /** Enable propagation (default: true) */
    bool enabled = true;

    /** Custom segment name for z_segment strategy (default: "ZTR") */
    std::string segment_name = "ZTR";

    /** Custom field index for msh_custom_field strategy */
    size_t msh_field_index = 25;
};

/**
 * @brief Inject trace context into an HL7 message
 *
 * Embeds the trace context into the HL7 message using the configured
 * propagation strategy.
 *
 * @param message HL7 message to inject context into
 * @param context Trace context to inject
 * @param config Propagation configuration
 * @return True if injection was successful
 */
bool inject_trace_context(hl7::hl7_message& message,
                          const trace_context& context,
                          const hl7_propagation_config& config = {});

/**
 * @brief Extract trace context from an HL7 message
 *
 * Extracts previously injected trace context from an HL7 message.
 *
 * @param message HL7 message to extract context from
 * @param config Propagation configuration
 * @return Extracted trace context, or nullopt if not found
 */
std::optional<trace_context> extract_trace_context(
    const hl7::hl7_message& message,
    const hl7_propagation_config& config = {});

/**
 * @brief Check if an HL7 message contains trace context
 *
 * @param message HL7 message to check
 * @param config Propagation configuration
 * @return True if trace context is present
 */
bool has_trace_context(const hl7::hl7_message& message,
                       const hl7_propagation_config& config = {});

// =============================================================================
// DICOM Trace Context Propagation
// =============================================================================

/**
 * @brief Key-value map for DICOM trace context
 *
 * Maps trace attributes to DICOM private tags or comment fields.
 */
struct dicom_trace_attributes {
    /** Trace ID to embed */
    std::string trace_id;

    /** Span ID to embed */
    std::string span_id;

    /** Optional parent span ID */
    std::optional<std::string> parent_span_id;

    /** Build traceparent string from attributes */
    [[nodiscard]] std::string to_traceparent() const;

    /** Parse attributes from traceparent string */
    static std::optional<dicom_trace_attributes> from_traceparent(
        std::string_view traceparent);
};

/**
 * @brief Convert trace_context to DICOM trace attributes
 */
[[nodiscard]] dicom_trace_attributes to_dicom_attributes(
    const trace_context& context);

/**
 * @brief Convert DICOM trace attributes to trace_context
 */
[[nodiscard]] std::optional<trace_context> from_dicom_attributes(
    const dicom_trace_attributes& attrs);

// =============================================================================
// Thread-Local Context Storage
// =============================================================================

/**
 * @brief Get the current thread-local trace context
 *
 * @return Current trace context, or nullopt if none set
 */
[[nodiscard]] std::optional<trace_context> get_current_trace_context();

/**
 * @brief Set the current thread-local trace context
 *
 * @param context Context to set
 */
void set_current_trace_context(const trace_context& context);

/**
 * @brief Clear the current thread-local trace context
 */
void clear_current_trace_context();

/**
 * @brief RAII guard for temporarily setting trace context
 *
 * Restores the previous context when destroyed.
 *
 * @example
 * ```cpp
 * {
 *     trace_context_guard guard(new_context);
 *     // Operations use new_context
 * }  // Previous context restored
 * ```
 */
class trace_context_guard {
public:
    explicit trace_context_guard(const trace_context& context);
    ~trace_context_guard();

    trace_context_guard(const trace_context_guard&) = delete;
    trace_context_guard& operator=(const trace_context_guard&) = delete;

private:
    std::optional<trace_context> previous_context_;
};

}  // namespace pacs::bridge::tracing

#endif  // PACS_BRIDGE_TRACING_TRACE_PROPAGATION_H
