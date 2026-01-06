#ifndef PACS_BRIDGE_MONITORING_BRIDGE_METRICS_H
#define PACS_BRIDGE_MONITORING_BRIDGE_METRICS_H

/**
 * @file bridge_metrics.h
 * @brief Metrics collection for PACS Bridge using monitoring_system
 *
 * Provides comprehensive metrics collection for all PACS Bridge components
 * including HL7 messages, MWL operations, queue status, and connections.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/40
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
#include <kcenon/monitoring/core/performance_monitor.h>
#include <kcenon/monitoring/exporters/metric_exporters.h>
#include <kcenon/monitoring/interfaces/monitoring_core.h>
#endif

namespace pacs::bridge::monitoring {

// ═══════════════════════════════════════════════════════════════════════════
// Metric Labels
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Common metric labels for PACS Bridge
 */
struct metric_labels {
    std::string message_type;  // ADT, ORM, ORU, SIU, etc.
    std::string destination;   // Queue destination name
    std::string error_type;    // Error category
    std::string method;        // HTTP method for FHIR
    std::string resource;      // FHIR resource type
};

// ═══════════════════════════════════════════════════════════════════════════
// Histogram Buckets
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Default histogram bucket boundaries for latency metrics (in seconds)
 */
inline std::vector<double> default_latency_buckets() {
    return {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
}

/**
 * @brief Default histogram bucket boundaries for queue depth metrics
 */
inline std::vector<double> default_queue_depth_buckets() {
    return {10, 50, 100, 500, 1000, 5000, 10000, 50000};
}

// ═══════════════════════════════════════════════════════════════════════════
// Bridge Metrics Collector
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Metrics collector for PACS Bridge components
 *
 * Thread-safe: All public methods are thread-safe.
 *
 * @example
 * ```cpp
 * auto& metrics = bridge_metrics_collector::instance();
 *
 * // Record HL7 message
 * metrics.record_hl7_message_received("ADT");
 *
 * // Record processing duration
 * auto timer = metrics.start_hl7_processing_timer();
 * // ... process message ...
 * timer.stop();
 *
 * // Get Prometheus metrics
 * std::string output = metrics.get_prometheus_metrics();
 * ```
 */
class bridge_metrics_collector {
public:
    /**
     * @brief Get singleton instance
     */
    static bridge_metrics_collector& instance();

    /**
     * @brief Initialize the metrics collector
     * @param service_name Name of this service instance
     * @param prometheus_port Port for Prometheus metrics endpoint (0 to disable)
     * @return true if initialization succeeded
     */
    bool initialize(const std::string& service_name = "pacs_bridge",
                    uint16_t prometheus_port = 9090);

    /**
     * @brief Shutdown the metrics collector
     */
    void shutdown();

    /**
     * @brief Check if metrics collection is enabled
     */
    [[nodiscard]] bool is_enabled() const noexcept { return enabled_.load(); }

    /**
     * @brief Enable or disable metrics collection
     */
    void set_enabled(bool enabled) { enabled_.store(enabled); }

    // ═══════════════════════════════════════════════════════════════════════
    // HL7 Message Metrics
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Record an HL7 message received
     * @param message_type HL7 message type (ADT, ORM, ORU, SIU, etc.)
     */
    void record_hl7_message_received(const std::string& message_type);

    /**
     * @brief Record an HL7 message sent
     * @param message_type HL7 message type
     */
    void record_hl7_message_sent(const std::string& message_type);

    /**
     * @brief Record HL7 message processing duration
     * @param message_type HL7 message type
     * @param duration Processing duration
     */
    void record_hl7_processing_duration(
        const std::string& message_type,
        std::chrono::nanoseconds duration);

    /**
     * @brief Record an HL7 message error
     * @param message_type HL7 message type
     * @param error_type Error category (parse_error, validation_error, etc.)
     */
    void record_hl7_error(const std::string& message_type,
                          const std::string& error_type);

    // ═══════════════════════════════════════════════════════════════════════
    // MWL Metrics
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Record MWL entry created
     */
    void record_mwl_entry_created();

    /**
     * @brief Record MWL entry updated
     */
    void record_mwl_entry_updated();

    /**
     * @brief Record MWL entry cancelled
     */
    void record_mwl_entry_cancelled();

    /**
     * @brief Record MWL query duration
     * @param duration Query duration
     */
    void record_mwl_query_duration(std::chrono::nanoseconds duration);

    // ═══════════════════════════════════════════════════════════════════════
    // Queue Metrics
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Set current queue depth
     * @param destination Queue destination name
     * @param depth Current queue depth
     */
    void set_queue_depth(const std::string& destination, size_t depth);

    /**
     * @brief Record message enqueued
     * @param destination Queue destination name
     */
    void record_message_enqueued(const std::string& destination);

    /**
     * @brief Record message delivered
     * @param destination Queue destination name
     */
    void record_message_delivered(const std::string& destination);

    /**
     * @brief Record delivery failure
     * @param destination Queue destination name
     */
    void record_delivery_failure(const std::string& destination);

    /**
     * @brief Record dead letter
     * @param destination Queue destination name
     */
    void record_dead_letter(const std::string& destination);

    // ═══════════════════════════════════════════════════════════════════════
    // Connection Metrics
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Set active MLLP connections count
     * @param count Current active connections
     */
    void set_mllp_active_connections(size_t count);

    /**
     * @brief Record new MLLP connection
     */
    void record_mllp_connection();

    /**
     * @brief Set active FHIR requests count
     * @param count Current active requests
     */
    void set_fhir_active_requests(size_t count);

    /**
     * @brief Record FHIR request
     * @param method HTTP method
     * @param resource FHIR resource type
     */
    void record_fhir_request(const std::string& method,
                             const std::string& resource);

    // ═══════════════════════════════════════════════════════════════════════
    // System Metrics
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Update system metrics (CPU, memory, etc.)
     *
     * This is typically called periodically by a background thread.
     */
    void update_system_metrics();

    // ═══════════════════════════════════════════════════════════════════════
    // Prometheus Export
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get metrics in Prometheus text format
     * @return Prometheus text exposition format string
     */
    [[nodiscard]] std::string get_prometheus_metrics() const;

    /**
     * @brief Get the Prometheus exporter port
     * @return Port number (0 if disabled)
     */
    [[nodiscard]] uint16_t get_prometheus_port() const noexcept {
        return prometheus_port_;
    }

private:
    bridge_metrics_collector();
    ~bridge_metrics_collector();

    // Non-copyable
    bridge_metrics_collector(const bridge_metrics_collector&) = delete;
    bridge_metrics_collector& operator=(const bridge_metrics_collector&) = delete;

    // Internal metric storage
    struct metrics_data;
    std::unique_ptr<metrics_data> data_;

    std::atomic<bool> enabled_{false};
    std::atomic<bool> initialized_{false};
    std::string service_name_;
    uint16_t prometheus_port_{0};
    mutable std::mutex mutex_;

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
    std::unique_ptr<kcenon::monitoring::prometheus_exporter> prometheus_exporter_;
    kcenon::monitoring::performance_monitor performance_monitor_;
#endif
};

// ═══════════════════════════════════════════════════════════════════════════
// Scoped Timer Helper
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief RAII timer for measuring operation duration
 *
 * @example
 * ```cpp
 * {
 *     scoped_metrics_timer timer([](auto duration) {
 *         bridge_metrics_collector::instance()
 *             .record_hl7_processing_duration("ADT", duration);
 *     });
 *     // ... operation to measure ...
 * }
 * ```
 */
class scoped_metrics_timer {
public:
    using callback_type = std::function<void(std::chrono::nanoseconds)>;

    explicit scoped_metrics_timer(callback_type callback)
        : callback_(std::move(callback)),
          start_time_(std::chrono::high_resolution_clock::now()) {}

    ~scoped_metrics_timer() {
        if (callback_) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end_time - start_time_);
            callback_(duration);
        }
    }

    /**
     * @brief Cancel the timer (callback will not be invoked)
     */
    void cancel() { callback_ = nullptr; }

    /**
     * @brief Get elapsed time without stopping
     */
    [[nodiscard]] std::chrono::nanoseconds elapsed() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - start_time_);
    }

private:
    callback_type callback_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Convenience Macros
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Macro to time HL7 message processing
 */
#define PACS_BRIDGE_TIME_HL7_PROCESSING(message_type)                         \
    pacs::bridge::monitoring::scoped_metrics_timer _hl7_timer([](auto d) {    \
        pacs::bridge::monitoring::bridge_metrics_collector::instance()        \
            .record_hl7_processing_duration(message_type, d);                 \
    })

/**
 * @brief Macro to time MWL query
 */
#define PACS_BRIDGE_TIME_MWL_QUERY()                                          \
    pacs::bridge::monitoring::scoped_metrics_timer _mwl_timer([](auto d) {    \
        pacs::bridge::monitoring::bridge_metrics_collector::instance()        \
            .record_mwl_query_duration(d);                                    \
    })

}  // namespace pacs::bridge::monitoring

#endif  // PACS_BRIDGE_MONITORING_BRIDGE_METRICS_H
