#ifndef PACS_BRIDGE_TRACING_TRACING_TYPES_H
#define PACS_BRIDGE_TRACING_TRACING_TYPES_H

/**
 * @file tracing_types.h
 * @brief Type definitions for distributed tracing support
 *
 * Provides core types for distributed tracing in pacs_bridge.
 * When monitoring_system is available, these types wrap its tracing
 * capabilities. In standalone builds, they provide no-op implementations.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/144
 * @see https://www.w3.org/TR/trace-context/
 */

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

namespace pacs::bridge::tracing {

// =============================================================================
// Span Status
// =============================================================================

/**
 * @brief Span status codes following OpenTelemetry conventions
 */
enum class span_status {
    /** Default status, indicates span completed without error */
    ok,

    /** Span completed with an error */
    error,

    /** Span was cancelled */
    cancelled
};

/**
 * @brief Convert span_status to string
 */
[[nodiscard]] constexpr const char* to_string(span_status status) noexcept {
    switch (status) {
        case span_status::ok:
            return "OK";
        case span_status::error:
            return "ERROR";
        case span_status::cancelled:
            return "CANCELLED";
        default:
            return "UNKNOWN";
    }
}

// =============================================================================
// Span Kind
// =============================================================================

/**
 * @brief Span kind following OpenTelemetry conventions
 */
enum class span_kind {
    /** Internal operation within application */
    internal,

    /** Server-side handling of request */
    server,

    /** Client-side request to external service */
    client,

    /** Producer of asynchronous message */
    producer,

    /** Consumer of asynchronous message */
    consumer
};

/**
 * @brief Convert span_kind to string
 */
[[nodiscard]] constexpr const char* to_string(span_kind kind) noexcept {
    switch (kind) {
        case span_kind::internal:
            return "INTERNAL";
        case span_kind::server:
            return "SERVER";
        case span_kind::client:
            return "CLIENT";
        case span_kind::producer:
            return "PRODUCER";
        case span_kind::consumer:
            return "CONSUMER";
        default:
            return "INTERNAL";
    }
}

// =============================================================================
// Trace Context
// =============================================================================

/**
 * @brief W3C Trace Context for distributed tracing
 *
 * Holds the identifiers needed to correlate spans across service boundaries.
 * Compatible with W3C Trace Context specification.
 *
 * @see https://www.w3.org/TR/trace-context/
 */
struct trace_context {
    /** Trace ID - identifies entire distributed trace (32 hex chars) */
    std::string trace_id;

    /** Span ID - identifies current span (16 hex chars) */
    std::string span_id;

    /** Parent span ID - identifies parent span (optional) */
    std::optional<std::string> parent_span_id;

    /** Trace flags (e.g., sampled) */
    uint8_t trace_flags = 0x01;

    /**
     * @brief Check if context is valid
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !trace_id.empty() && !span_id.empty();
    }

    /**
     * @brief Format as W3C traceparent header value
     *
     * Format: {version}-{trace-id}-{parent-id}-{trace-flags}
     * Example: 00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01
     */
    [[nodiscard]] std::string to_traceparent() const;

    /**
     * @brief Parse from W3C traceparent header value
     */
    [[nodiscard]] static std::optional<trace_context> from_traceparent(
        std::string_view traceparent);
};

// =============================================================================
// Span Data
// =============================================================================

/**
 * @brief Completed span data for export
 *
 * Contains all information about a completed span, ready for export
 * to tracing backends like Jaeger or Zipkin.
 */
struct span_data {
    /** Span name/operation name */
    std::string name;

    /** Trace context */
    trace_context context;

    /** Service name */
    std::string service_name;

    /** Span kind */
    span_kind kind = span_kind::internal;

    /** Start timestamp */
    std::chrono::system_clock::time_point start_time;

    /** End timestamp */
    std::chrono::system_clock::time_point end_time;

    /** Span status */
    span_status status = span_status::ok;

    /** Status message (for errors) */
    std::string status_message;

    /** Span attributes/tags */
    std::unordered_map<std::string, std::string> attributes;

    /**
     * @brief Calculate span duration
     */
    [[nodiscard]] std::chrono::microseconds duration() const noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
    }
};

// =============================================================================
// Tracing Configuration
// =============================================================================

/**
 * @brief Trace export format
 */
enum class trace_export_format {
    /** Jaeger Thrift over HTTP */
    jaeger_thrift,

    /** Jaeger gRPC */
    jaeger_grpc,

    /** Zipkin JSON v2 */
    zipkin_json,

    /** OpenTelemetry Protocol gRPC */
    otlp_grpc,

    /** OpenTelemetry Protocol HTTP/JSON */
    otlp_http_json
};

/**
 * @brief Convert trace_export_format to string
 */
[[nodiscard]] constexpr const char* to_string(trace_export_format format) noexcept {
    switch (format) {
        case trace_export_format::jaeger_thrift:
            return "jaeger_thrift";
        case trace_export_format::jaeger_grpc:
            return "jaeger_grpc";
        case trace_export_format::zipkin_json:
            return "zipkin_json";
        case trace_export_format::otlp_grpc:
            return "otlp_grpc";
        case trace_export_format::otlp_http_json:
            return "otlp_http_json";
        default:
            return "unknown";
    }
}

/**
 * @brief Configuration for distributed tracing
 */
struct tracing_config {
    /** Enable tracing */
    bool enabled = false;

    /** Service name for spans */
    std::string service_name = "pacs_bridge";

    /** Exporter endpoint URL */
    std::string endpoint;

    /** Export format */
    trace_export_format format = trace_export_format::otlp_grpc;

    /** Sampling rate (0.0 to 1.0) */
    double sampling_rate = 1.0;

    /** Maximum batch size for export */
    size_t max_batch_size = 512;

    /** Batch export timeout */
    std::chrono::milliseconds batch_timeout{5000};

    /** Custom headers for exporter */
    std::unordered_map<std::string, std::string> headers;
};

}  // namespace pacs::bridge::tracing

#endif  // PACS_BRIDGE_TRACING_TRACING_TYPES_H
