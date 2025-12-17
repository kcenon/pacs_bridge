#ifndef PACS_BRIDGE_TRACING_TRACE_MANAGER_H
#define PACS_BRIDGE_TRACING_TRACE_MANAGER_H

/**
 * @file trace_manager.h
 * @brief Centralized trace management for distributed tracing
 *
 * Provides a singleton trace manager for creating and managing spans
 * throughout the pacs_bridge application. Integrates with monitoring_system
 * when available, or provides no-op implementations for standalone builds.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/144
 * @see https://github.com/kcenon/pacs_bridge/issues/147
 */

#include "tracing_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::tracing {

// Forward declarations
class span_wrapper;

// =============================================================================
// Trace Manager Error Codes (-950 to -959)
// =============================================================================

/**
 * @brief Trace manager specific error codes
 *
 * Allocated range: -950 to -959
 */
enum class trace_error : int {
    /** Tracing is not initialized */
    not_initialized = -950,

    /** Invalid configuration */
    invalid_config = -951,

    /** Exporter connection failed */
    exporter_failed = -952,

    /** Span creation failed */
    span_creation_failed = -953,

    /** Context propagation failed */
    propagation_failed = -954
};

/**
 * @brief Convert trace_error to error code
 */
[[nodiscard]] constexpr int to_error_code(trace_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(trace_error error) noexcept {
    switch (error) {
        case trace_error::not_initialized:
            return "Tracing is not initialized";
        case trace_error::invalid_config:
            return "Invalid tracing configuration";
        case trace_error::exporter_failed:
            return "Trace exporter connection failed";
        case trace_error::span_creation_failed:
            return "Failed to create span";
        case trace_error::propagation_failed:
            return "Trace context propagation failed";
        default:
            return "Unknown trace error";
    }
}

// =============================================================================
// Trace Manager
// =============================================================================

/**
 * @brief Centralized manager for distributed tracing
 *
 * Manages the lifecycle of traces and spans, handles configuration,
 * and coordinates with trace exporters. Thread-safe singleton.
 *
 * @example Basic Usage
 * ```cpp
 * // Initialize tracing
 * tracing_config config;
 * config.enabled = true;
 * config.service_name = "pacs_bridge";
 * config.endpoint = "http://localhost:14268";
 * config.format = trace_export_format::jaeger_thrift;
 *
 * trace_manager::instance().initialize(config);
 *
 * // Create a root span
 * auto span = trace_manager::instance().start_span("mllp_receive");
 * span.set_attribute("mllp.port", "2575");
 *
 * // Create child span
 * auto child = span.start_child("hl7_parse");
 * // ... do work ...
 * child.end();
 *
 * span.end();
 * ```
 *
 * @example With RAII Wrapper
 * ```cpp
 * {
 *     auto span = trace_manager::instance().start_span("process_message");
 *     span.set_attribute("message.type", "ADT^A01");
 *
 *     {
 *         auto child = span.start_child("validate");
 *         // Span automatically ends when going out of scope
 *     }
 *
 *     // Parent span continues
 * }  // All spans ended
 * ```
 */
class trace_manager {
public:
    /**
     * @brief Get singleton instance
     */
    [[nodiscard]] static trace_manager& instance();

    /**
     * @brief Initialize tracing with configuration
     *
     * Must be called before creating any spans. Can be called multiple
     * times to reconfigure tracing.
     *
     * @param config Tracing configuration
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, trace_error> initialize(
        const tracing_config& config);

    /**
     * @brief Shutdown tracing and flush pending spans
     *
     * Should be called before application exit to ensure all spans
     * are exported.
     */
    void shutdown();

    /**
     * @brief Check if tracing is enabled and initialized
     */
    [[nodiscard]] bool is_enabled() const noexcept;

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const tracing_config& config() const noexcept;

    // =========================================================================
    // Span Creation
    // =========================================================================

    /**
     * @brief Start a new root span
     *
     * Creates a new trace with a root span. Use this for the beginning
     * of a new operation (e.g., receiving an MLLP message).
     *
     * @param name Span name/operation name
     * @param kind Span kind (default: server)
     * @return Span wrapper for managing the span
     */
    [[nodiscard]] span_wrapper start_span(
        std::string_view name,
        span_kind kind = span_kind::server);

    /**
     * @brief Start a child span with explicit parent context
     *
     * Creates a span that is a child of the given parent context.
     * Use this when continuing a trace from an external source.
     *
     * @param name Span name
     * @param parent Parent trace context
     * @param kind Span kind
     * @return Span wrapper
     */
    [[nodiscard]] span_wrapper start_span_with_parent(
        std::string_view name,
        const trace_context& parent,
        span_kind kind = span_kind::internal);

    /**
     * @brief Start a span from W3C traceparent header
     *
     * Parses the traceparent header and creates a child span.
     * If parsing fails, creates a new root span instead.
     *
     * @param name Span name
     * @param traceparent W3C traceparent header value
     * @param kind Span kind
     * @return Span wrapper
     */
    [[nodiscard]] span_wrapper start_span_from_traceparent(
        std::string_view name,
        std::string_view traceparent,
        span_kind kind = span_kind::server);

    // =========================================================================
    // Context Management
    // =========================================================================

    /**
     * @brief Get current active trace context
     *
     * Returns the context of the currently active span, if any.
     * Useful for propagating context across async boundaries.
     */
    [[nodiscard]] std::optional<trace_context> current_context() const;

    /**
     * @brief Set active context (for async continuation)
     *
     * @param context Context to set as active
     */
    void set_current_context(const trace_context& context);

    /**
     * @brief Clear active context
     */
    void clear_current_context();

    // =========================================================================
    // Export Control
    // =========================================================================

    /**
     * @brief Force flush pending spans to exporter
     *
     * Blocks until all pending spans are exported or timeout occurs.
     *
     * @param timeout Maximum time to wait
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, trace_error> flush(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Tracing statistics
     */
    struct statistics {
        /** Total spans created */
        size_t spans_created = 0;

        /** Spans successfully exported */
        size_t spans_exported = 0;

        /** Spans dropped (export failed) */
        size_t spans_dropped = 0;

        /** Export errors */
        size_t export_errors = 0;
    };

    /**
     * @brief Get tracing statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

private:
    trace_manager();
    ~trace_manager();

    trace_manager(const trace_manager&) = delete;
    trace_manager& operator=(const trace_manager&) = delete;

    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::tracing

#endif  // PACS_BRIDGE_TRACING_TRACE_MANAGER_H
