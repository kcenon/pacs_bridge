#ifndef PACS_BRIDGE_TRACING_SPAN_WRAPPER_H
#define PACS_BRIDGE_TRACING_SPAN_WRAPPER_H

/**
 * @file span_wrapper.h
 * @brief RAII wrapper for distributed tracing spans
 *
 * Provides an RAII wrapper that automatically manages span lifecycle.
 * Spans are automatically ended when the wrapper goes out of scope.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/144
 * @see https://github.com/kcenon/pacs_bridge/issues/147
 */

#include "tracing_types.h"

#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::tracing {

/**
 * @brief RAII wrapper for managing span lifecycle
 *
 * Automatically ends the span when the wrapper is destroyed,
 * ensuring proper cleanup even in case of exceptions.
 *
 * @example Basic Usage
 * ```cpp
 * {
 *     auto span = trace_manager::instance().start_span("operation");
 *     span.set_attribute("key", "value");
 *
 *     // Do work...
 *
 * }  // Span automatically ended here
 * ```
 *
 * @example Error Handling
 * ```cpp
 * {
 *     auto span = trace_manager::instance().start_span("risky_operation");
 *
 *     try {
 *         do_something_risky();
 *     } catch (const std::exception& e) {
 *         span.set_status(span_status::error, e.what());
 *         throw;  // Span still ends properly
 *     }
 * }
 * ```
 *
 * @example Child Spans
 * ```cpp
 * auto parent = trace_manager::instance().start_span("parent");
 *
 * {
 *     auto child = parent.start_child("child_operation");
 *     // Child work...
 * }  // Child span ends
 *
 * // Parent continues...
 * parent.end();
 * ```
 */
class span_wrapper {
public:
    /**
     * @brief Default constructor creates an inactive/no-op span
     */
    span_wrapper();

    /**
     * @brief Destructor - ends span if still active
     */
    ~span_wrapper();

    // Movable
    span_wrapper(span_wrapper&& other) noexcept;
    span_wrapper& operator=(span_wrapper&& other) noexcept;

    // Non-copyable
    span_wrapper(const span_wrapper&) = delete;
    span_wrapper& operator=(const span_wrapper&) = delete;

    // =========================================================================
    // Span State
    // =========================================================================

    /**
     * @brief Check if span is active (not yet ended)
     */
    [[nodiscard]] bool is_active() const noexcept;

    /**
     * @brief Check if this is a valid span (not no-op)
     */
    [[nodiscard]] bool is_valid() const noexcept;

    /**
     * @brief Get the span name
     */
    [[nodiscard]] std::string_view name() const noexcept;

    /**
     * @brief Get the trace context for this span
     */
    [[nodiscard]] const trace_context& context() const noexcept;

    // =========================================================================
    // Span Attributes
    // =========================================================================

    /**
     * @brief Set a string attribute
     *
     * @param key Attribute key
     * @param value Attribute value
     * @return Reference to this for chaining
     */
    span_wrapper& set_attribute(std::string_view key, std::string_view value);

    /**
     * @brief Set a string attribute (const char* overload)
     *
     * @param key Attribute key
     * @param value Attribute value
     * @return Reference to this for chaining
     */
    span_wrapper& set_attribute(std::string_view key, const char* value);

    /**
     * @brief Set an integer attribute
     *
     * @param key Attribute key
     * @param value Attribute value
     * @return Reference to this for chaining
     */
    span_wrapper& set_attribute(std::string_view key, int64_t value);

    /**
     * @brief Set a double attribute
     *
     * @param key Attribute key
     * @param value Attribute value
     * @return Reference to this for chaining
     */
    span_wrapper& set_attribute(std::string_view key, double value);

    /**
     * @brief Set a boolean attribute
     *
     * @param key Attribute key
     * @param value Attribute value
     * @return Reference to this for chaining
     */
    span_wrapper& set_attribute(std::string_view key, bool value);

    // =========================================================================
    // Span Status
    // =========================================================================

    /**
     * @brief Set span status
     *
     * @param status Status code
     * @param message Optional status message (typically for errors)
     * @return Reference to this for chaining
     */
    span_wrapper& set_status(span_status status, std::string_view message = "");

    /**
     * @brief Mark span as error with exception info
     *
     * Sets status to error and records exception details.
     *
     * @param exception Exception that occurred
     * @return Reference to this for chaining
     */
    span_wrapper& record_exception(const std::exception& exception);

    /**
     * @brief Mark span as error with message
     *
     * @param error_message Error description
     * @return Reference to this for chaining
     */
    span_wrapper& set_error(std::string_view error_message);

    // =========================================================================
    // Events/Logs
    // =========================================================================

    /**
     * @brief Add an event to the span
     *
     * Events are timestamped annotations within the span.
     *
     * @param name Event name
     * @return Reference to this for chaining
     */
    span_wrapper& add_event(std::string_view name);

    /**
     * @brief Add an event with attributes
     *
     * @param name Event name
     * @param attributes Event attributes
     * @return Reference to this for chaining
     */
    span_wrapper& add_event(
        std::string_view name,
        const std::unordered_map<std::string, std::string>& attributes);

    // =========================================================================
    // Child Spans
    // =========================================================================

    /**
     * @brief Create a child span
     *
     * Creates a new span that is a child of this span.
     *
     * @param name Child span name
     * @param kind Child span kind
     * @return Child span wrapper
     */
    [[nodiscard]] span_wrapper start_child(
        std::string_view name,
        span_kind kind = span_kind::internal);

    // =========================================================================
    // Span Lifecycle
    // =========================================================================

    /**
     * @brief Explicitly end the span
     *
     * After calling end(), the span becomes inactive and further
     * operations are no-ops. If not called explicitly, the span
     * is ended in the destructor.
     */
    void end();

    /**
     * @brief End span with specific timestamp
     *
     * @param end_time Custom end timestamp
     */
    void end(std::chrono::system_clock::time_point end_time);

    // =========================================================================
    // Context Propagation
    // =========================================================================

    /**
     * @brief Get W3C traceparent header value for propagation
     *
     * @return Traceparent header value or empty string if invalid
     */
    [[nodiscard]] std::string get_traceparent() const;

    /**
     * @brief Inject trace context into a map (for headers)
     *
     * Adds traceparent and tracestate to the provided map.
     *
     * @param headers Map to inject headers into
     */
    void inject_context(std::unordered_map<std::string, std::string>& headers) const;

private:
    friend class trace_manager;

    class span_impl;
    std::unique_ptr<span_impl> pimpl_;

    // Private constructor for trace_manager
    explicit span_wrapper(std::unique_ptr<span_impl> impl);

public:
    /**
     * @brief Create span wrapper (internal use)
     *
     * This is used internally by trace_manager. Do not call directly.
     */
    static span_wrapper create_internal(
        std::string_view name,
        const trace_context& ctx,
        span_kind kind,
        std::string_view service_name);
};

// =============================================================================
// Convenience Macros
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD

/**
 * @brief Create a scoped span with automatic naming
 *
 * Creates a span named after the current function.
 */
#define PACS_TRACE_SPAN() \
    auto _pacs_trace_span_ = \
        ::pacs::bridge::tracing::trace_manager::instance().start_span(__FUNCTION__)

/**
 * @brief Create a named scoped span
 */
#define PACS_TRACE_SPAN_NAMED(name) \
    auto _pacs_trace_span_ = \
        ::pacs::bridge::tracing::trace_manager::instance().start_span(name)

/**
 * @brief Create a child span from current context
 */
#define PACS_TRACE_CHILD(name) \
    auto _pacs_trace_child_ = _pacs_trace_span_.start_child(name)

#else

// No-op macros for standalone builds
#define PACS_TRACE_SPAN() ((void)0)
#define PACS_TRACE_SPAN_NAMED(name) ((void)0)
#define PACS_TRACE_CHILD(name) ((void)0)

#endif

}  // namespace pacs::bridge::tracing

#endif  // PACS_BRIDGE_TRACING_SPAN_WRAPPER_H
