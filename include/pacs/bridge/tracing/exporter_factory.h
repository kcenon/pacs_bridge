#ifndef PACS_BRIDGE_TRACING_EXPORTER_FACTORY_H
#define PACS_BRIDGE_TRACING_EXPORTER_FACTORY_H

/**
 * @file exporter_factory.h
 * @brief Factory for creating trace exporters
 *
 * Provides a factory for creating trace exporters based on configuration.
 * Supports multiple export formats including Jaeger, Zipkin, and OTLP.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/144
 * @see https://github.com/kcenon/pacs_bridge/issues/150
 */

#include "tracing_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pacs::bridge::tracing {

// =============================================================================
// Trace Exporter Interface
// =============================================================================

/**
 * @brief Exporter error codes
 */
enum class exporter_error : int {
    /** Exporter not initialized */
    not_initialized = -960,

    /** Connection to backend failed */
    connection_failed = -961,

    /** Export request failed */
    export_failed = -962,

    /** Invalid configuration */
    invalid_config = -963,

    /** Backend not reachable */
    backend_unavailable = -964,

    /** Export timeout */
    timeout = -965
};

/**
 * @brief Convert exporter_error to string
 */
[[nodiscard]] constexpr const char* to_string(exporter_error error) noexcept {
    switch (error) {
        case exporter_error::not_initialized:
            return "Exporter not initialized";
        case exporter_error::connection_failed:
            return "Connection to backend failed";
        case exporter_error::export_failed:
            return "Export request failed";
        case exporter_error::invalid_config:
            return "Invalid exporter configuration";
        case exporter_error::backend_unavailable:
            return "Backend unavailable";
        case exporter_error::timeout:
            return "Export timeout";
        default:
            return "Unknown exporter error";
    }
}

/**
 * @brief Interface for trace exporters
 *
 * Implementations export span data to different backends (Jaeger, Zipkin, OTLP).
 */
class trace_exporter {
public:
    virtual ~trace_exporter() = default;

    /**
     * @brief Export a batch of spans
     *
     * @param spans Spans to export
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, exporter_error> export_spans(
        const std::vector<span_data>& spans) = 0;

    /**
     * @brief Force flush any buffered spans
     *
     * @param timeout Maximum time to wait
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, exporter_error> flush(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) = 0;

    /**
     * @brief Shutdown the exporter
     */
    virtual void shutdown() = 0;

    /**
     * @brief Check if exporter is healthy
     */
    [[nodiscard]] virtual bool is_healthy() const noexcept = 0;

    /**
     * @brief Get exporter name for logging
     */
    [[nodiscard]] virtual std::string name() const = 0;
};

// =============================================================================
// Export Statistics
// =============================================================================

/**
 * @brief Exporter statistics
 */
struct exporter_statistics {
    /** Total spans exported successfully */
    size_t spans_exported = 0;

    /** Total export requests made */
    size_t export_requests = 0;

    /** Failed export attempts */
    size_t export_failures = 0;

    /** Spans dropped due to queue overflow */
    size_t spans_dropped = 0;

    /** Retry attempts */
    size_t retry_attempts = 0;

    /** Total export time (microseconds) */
    size_t total_export_time_us = 0;

    /** Average export time per batch (microseconds) */
    [[nodiscard]] size_t avg_export_time_us() const noexcept {
        return export_requests > 0 ? total_export_time_us / export_requests : 0;
    }
};

// =============================================================================
// Exporter Factory
// =============================================================================

/**
 * @brief Factory for creating trace exporters
 *
 * Creates the appropriate exporter based on configuration.
 *
 * @example
 * ```cpp
 * tracing_config config;
 * config.enabled = true;
 * config.format = trace_export_format::jaeger_thrift;
 * config.endpoint = "http://localhost:14268/api/traces";
 *
 * auto exporter = exporter_factory::create(config);
 * if (exporter) {
 *     // Use exporter
 * }
 * ```
 */
class exporter_factory {
public:
    /**
     * @brief Create an exporter based on configuration
     *
     * @param config Tracing configuration
     * @return Exporter instance or error
     */
    [[nodiscard]] static std::expected<std::unique_ptr<trace_exporter>, exporter_error>
    create(const tracing_config& config);

    /**
     * @brief Create a no-op exporter (for disabled tracing)
     */
    [[nodiscard]] static std::unique_ptr<trace_exporter> create_noop();

    /**
     * @brief Register a custom exporter factory
     *
     * @param format Export format
     * @param factory Factory function
     */
    using factory_function = std::function<
        std::expected<std::unique_ptr<trace_exporter>, exporter_error>(
            const tracing_config&)>;

    static void register_factory(trace_export_format format,
                                 factory_function factory);
};

// =============================================================================
// Batch Exporter
// =============================================================================

/**
 * @brief Configuration for batch export
 */
struct batch_config {
    /** Maximum batch size */
    size_t max_batch_size = 512;

    /** Maximum time to wait before exporting */
    std::chrono::milliseconds max_export_delay{5000};

    /** Maximum queue size before dropping spans */
    size_t max_queue_size = 2048;

    /** Number of retry attempts for failed exports */
    size_t retry_count = 3;

    /** Delay between retry attempts */
    std::chrono::milliseconds retry_delay{1000};
};

/**
 * @brief Wrapper that adds batching to any exporter
 *
 * Buffers spans and exports them in batches for efficiency.
 * Handles retry logic for failed exports.
 */
class batch_exporter : public trace_exporter {
public:
    /**
     * @brief Create a batch exporter
     *
     * @param inner Inner exporter to batch
     * @param config Batch configuration
     */
    batch_exporter(std::unique_ptr<trace_exporter> inner,
                   const batch_config& config = {});

    ~batch_exporter() override;

    batch_exporter(const batch_exporter&) = delete;
    batch_exporter& operator=(const batch_exporter&) = delete;

    // trace_exporter interface
    [[nodiscard]] std::expected<void, exporter_error> export_spans(
        const std::vector<span_data>& spans) override;

    [[nodiscard]] std::expected<void, exporter_error> flush(
        std::chrono::milliseconds timeout) override;

    void shutdown() override;

    [[nodiscard]] bool is_healthy() const noexcept override;

    [[nodiscard]] std::string name() const override;

    // Batch-specific methods

    /**
     * @brief Get current queue size
     */
    [[nodiscard]] size_t queue_size() const noexcept;

    /**
     * @brief Get export statistics
     */
    [[nodiscard]] exporter_statistics statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::tracing

#endif  // PACS_BRIDGE_TRACING_EXPORTER_FACTORY_H
