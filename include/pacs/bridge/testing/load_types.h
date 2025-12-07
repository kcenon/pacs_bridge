#ifndef PACS_BRIDGE_TESTING_LOAD_TYPES_H
#define PACS_BRIDGE_TESTING_LOAD_TYPES_H

/**
 * @file load_types.h
 * @brief Load and stress testing type definitions for PACS Bridge
 *
 * Provides common types for load testing including test configurations,
 * metrics collection, and result reporting structures. Supports various
 * test scenarios: sustained load, peak load, endurance, and stress tests.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/45
 */

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::testing {

// =============================================================================
// Error Codes (-960 to -969)
// =============================================================================

/**
 * @brief Load testing specific error codes
 *
 * Allocated range: -960 to -969
 * @see docs/SDS_COMPONENTS.md for error code allocation
 */
enum class load_error : int {
    /** Test configuration is invalid */
    invalid_configuration = -960,

    /** Test runner is not initialized */
    not_initialized = -961,

    /** Test is already running */
    already_running = -962,

    /** Test was cancelled by user */
    cancelled = -963,

    /** Connection to target failed */
    connection_failed = -964,

    /** Message generation failed */
    generation_failed = -965,

    /** Test timeout exceeded */
    timeout = -966,

    /** Resource exhaustion (memory, connections) */
    resource_exhausted = -967,

    /** Target system returned error */
    target_error = -968,

    /** Report generation failed */
    report_failed = -969
};

/**
 * @brief Convert load_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(load_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of load error
 */
[[nodiscard]] constexpr const char* to_string(load_error error) noexcept {
    switch (error) {
        case load_error::invalid_configuration:
            return "Invalid test configuration";
        case load_error::not_initialized:
            return "Test runner not initialized";
        case load_error::already_running:
            return "Test is already running";
        case load_error::cancelled:
            return "Test was cancelled";
        case load_error::connection_failed:
            return "Connection to target failed";
        case load_error::generation_failed:
            return "Message generation failed";
        case load_error::timeout:
            return "Test timeout exceeded";
        case load_error::resource_exhausted:
            return "Resource exhausted";
        case load_error::target_error:
            return "Target system error";
        case load_error::report_failed:
            return "Report generation failed";
        default:
            return "Unknown load test error";
    }
}

// =============================================================================
// Test Types and Scenarios
// =============================================================================

/**
 * @brief Type of load test to execute
 */
enum class test_type {
    /** Sustained load at constant rate for extended duration */
    sustained,

    /** Peak load to find system limits */
    peak,

    /** Extended duration test for memory leak detection */
    endurance,

    /** Concurrent connection stress test */
    concurrent,

    /** Queue stress with simulated downstream failure */
    queue_stress,

    /** Failover behavior verification */
    failover
};

/**
 * @brief Convert test_type to string
 */
[[nodiscard]] constexpr const char* to_string(test_type type) noexcept {
    switch (type) {
        case test_type::sustained:
            return "sustained";
        case test_type::peak:
            return "peak";
        case test_type::endurance:
            return "endurance";
        case test_type::concurrent:
            return "concurrent";
        case test_type::queue_stress:
            return "queue_stress";
        case test_type::failover:
            return "failover";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse test_type from string
 */
[[nodiscard]] inline std::optional<test_type>
parse_test_type(std::string_view str) noexcept {
    if (str == "sustained") return test_type::sustained;
    if (str == "peak") return test_type::peak;
    if (str == "endurance") return test_type::endurance;
    if (str == "concurrent") return test_type::concurrent;
    if (str == "queue_stress") return test_type::queue_stress;
    if (str == "failover") return test_type::failover;
    return std::nullopt;
}

/**
 * @brief Current test state
 */
enum class test_state {
    /** Test is configured but not started */
    idle,

    /** Test is initializing connections */
    initializing,

    /** Test is actively running */
    running,

    /** Test is stopping gracefully */
    stopping,

    /** Test completed successfully */
    completed,

    /** Test failed with errors */
    failed,

    /** Test was cancelled */
    cancelled
};

/**
 * @brief Convert test_state to string
 */
[[nodiscard]] constexpr const char* to_string(test_state state) noexcept {
    switch (state) {
        case test_state::idle:
            return "idle";
        case test_state::initializing:
            return "initializing";
        case test_state::running:
            return "running";
        case test_state::stopping:
            return "stopping";
        case test_state::completed:
            return "completed";
        case test_state::failed:
            return "failed";
        case test_state::cancelled:
            return "cancelled";
        default:
            return "unknown";
    }
}

// =============================================================================
// HL7 Message Types
// =============================================================================

/**
 * @brief HL7 message type for load generation
 */
enum class hl7_message_type {
    /** ORM - Order Message */
    ORM,

    /** ADT - Admission/Discharge/Transfer */
    ADT,

    /** SIU - Scheduling Information Unsolicited */
    SIU,

    /** ORU - Observation Result */
    ORU,

    /** MDM - Medical Document Management */
    MDM
};

/**
 * @brief Convert hl7_message_type to string
 */
[[nodiscard]] constexpr const char* to_string(hl7_message_type type) noexcept {
    switch (type) {
        case hl7_message_type::ORM:
            return "ORM";
        case hl7_message_type::ADT:
            return "ADT";
        case hl7_message_type::SIU:
            return "SIU";
        case hl7_message_type::ORU:
            return "ORU";
        case hl7_message_type::MDM:
            return "MDM";
        default:
            return "UNKNOWN";
    }
}

// =============================================================================
// Message Distribution Configuration
// =============================================================================

/**
 * @brief Message type distribution for mixed workloads
 *
 * Defines the percentage of each message type in the test workload.
 * Percentages should sum to 100.
 */
struct message_distribution {
    /** Percentage of ORM messages (0-100) */
    uint8_t orm_percent = 70;

    /** Percentage of ADT messages (0-100) */
    uint8_t adt_percent = 20;

    /** Percentage of SIU messages (0-100) */
    uint8_t siu_percent = 10;

    /** Percentage of ORU messages (0-100) */
    uint8_t oru_percent = 0;

    /** Percentage of MDM messages (0-100) */
    uint8_t mdm_percent = 0;

    /**
     * @brief Validate distribution sums to 100
     */
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return (orm_percent + adt_percent + siu_percent +
                oru_percent + mdm_percent) == 100;
    }

    /**
     * @brief Get default distribution (70% ORM, 20% ADT, 10% SIU)
     */
    [[nodiscard]] static constexpr message_distribution default_mix() noexcept {
        return {70, 20, 10, 0, 0};
    }
};

// =============================================================================
// Test Configuration
// =============================================================================

/**
 * @brief Load test configuration parameters
 */
struct load_config {
    /** Type of test to run */
    test_type type = test_type::sustained;

    /** Target hostname or IP */
    std::string target_host = "localhost";

    /** Target port */
    uint16_t target_port = 2575;

    /** Test duration */
    std::chrono::seconds duration{3600};  // 1 hour default

    /** Target message rate (messages per second) */
    uint32_t messages_per_second = 500;

    /** Number of concurrent connections */
    size_t concurrent_connections = 10;

    /** Message type distribution */
    message_distribution distribution;

    /** Enable TLS */
    bool use_tls = false;

    /** TLS certificate path (if TLS enabled) */
    std::string tls_cert_path;

    /** TLS CA path (if TLS enabled) */
    std::string tls_ca_path;

    /** Ramp-up time before full load */
    std::chrono::seconds ramp_up{30};

    /** Timeout for individual message send/receive */
    std::chrono::milliseconds message_timeout{5000};

    /** Maximum retries per message */
    size_t max_retries = 3;

    /** Collect detailed per-message timing */
    bool detailed_timing = false;

    /** Output directory for reports */
    std::string output_directory = "./load_test_results";

    /**
     * @brief Validate configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (target_host.empty()) return false;
        if (target_port == 0) return false;
        if (messages_per_second == 0) return false;
        if (concurrent_connections == 0) return false;
        if (!distribution.is_valid()) return false;
        return true;
    }

    /**
     * @brief Create sustained load test configuration
     */
    [[nodiscard]] static load_config sustained(
        std::string_view host,
        uint16_t port,
        std::chrono::seconds test_duration,
        uint32_t rate) {
        load_config config;
        config.type = test_type::sustained;
        config.target_host = std::string(host);
        config.target_port = port;
        config.duration = test_duration;
        config.messages_per_second = rate;
        return config;
    }

    /**
     * @brief Create peak load test configuration
     */
    [[nodiscard]] static load_config peak(
        std::string_view host,
        uint16_t port,
        uint32_t max_rate) {
        load_config config;
        config.type = test_type::peak;
        config.target_host = std::string(host);
        config.target_port = port;
        config.duration = std::chrono::seconds(900);  // 15 minutes
        config.messages_per_second = max_rate;
        config.ramp_up = std::chrono::seconds(60);
        return config;
    }

    /**
     * @brief Create endurance test configuration
     */
    [[nodiscard]] static load_config endurance(
        std::string_view host,
        uint16_t port) {
        load_config config;
        config.type = test_type::endurance;
        config.target_host = std::string(host);
        config.target_port = port;
        config.duration = std::chrono::seconds(86400);  // 24 hours
        config.messages_per_second = 200;
        config.detailed_timing = false;  // Save memory for long test
        return config;
    }

    /**
     * @brief Create concurrent connection test configuration
     */
    [[nodiscard]] static load_config concurrent(
        std::string_view host,
        uint16_t port,
        size_t connections,
        size_t messages_per_connection) {
        load_config config;
        config.type = test_type::concurrent;
        config.target_host = std::string(host);
        config.target_port = port;
        config.concurrent_connections = connections;
        config.messages_per_second = 1000;  // Total across all connections
        config.duration = std::chrono::seconds(
            (connections * messages_per_connection) / 1000 + 60);
        return config;
    }
};

// =============================================================================
// Latency Histogram
// =============================================================================

/**
 * @brief Latency histogram for percentile calculations
 *
 * Pre-defined buckets for efficient percentile calculation:
 * [0-1ms, 1-5ms, 5-10ms, 10-25ms, 25-50ms, 50-100ms, 100-250ms, 250-500ms,
 *  500-1000ms, 1000+ms]
 */
struct latency_histogram {
    /** Bucket boundaries in microseconds */
    static constexpr std::array<uint64_t, 10> BUCKET_BOUNDS = {
        1000, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000,
        UINT64_MAX
    };

    /** Count in each bucket */
    std::array<std::atomic<uint64_t>, 10> buckets{};

    /** Minimum latency observed (microseconds) */
    std::atomic<uint64_t> min_latency{UINT64_MAX};

    /** Maximum latency observed (microseconds) */
    std::atomic<uint64_t> max_latency{0};

    /** Sum of all latencies for mean calculation */
    std::atomic<uint64_t> total_latency{0};

    /** Total sample count */
    std::atomic<uint64_t> count{0};

    /**
     * @brief Record a latency sample
     * @param latency_us Latency in microseconds
     */
    void record(uint64_t latency_us) noexcept {
        // Update min/max atomically
        uint64_t current_min = min_latency.load(std::memory_order_relaxed);
        while (latency_us < current_min &&
               !min_latency.compare_exchange_weak(current_min, latency_us)) {
        }

        uint64_t current_max = max_latency.load(std::memory_order_relaxed);
        while (latency_us > current_max &&
               !max_latency.compare_exchange_weak(current_max, latency_us)) {
        }

        // Find bucket and increment
        for (size_t i = 0; i < BUCKET_BOUNDS.size(); ++i) {
            if (latency_us <= BUCKET_BOUNDS[i]) {
                buckets[i].fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }

        total_latency.fetch_add(latency_us, std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Get mean latency in microseconds
     */
    [[nodiscard]] double mean_us() const noexcept {
        uint64_t c = count.load(std::memory_order_relaxed);
        if (c == 0) return 0.0;
        return static_cast<double>(total_latency.load()) / static_cast<double>(c);
    }

    /**
     * @brief Get approximate percentile latency
     * @param percentile Percentile (0-100)
     * @return Approximate latency in microseconds
     */
    [[nodiscard]] uint64_t percentile_us(double percentile) const noexcept {
        uint64_t c = count.load(std::memory_order_relaxed);
        if (c == 0) return 0;

        uint64_t target = static_cast<uint64_t>(
            static_cast<double>(c) * percentile / 100.0);
        uint64_t cumulative = 0;

        for (size_t i = 0; i < buckets.size(); ++i) {
            cumulative += buckets[i].load(std::memory_order_relaxed);
            if (cumulative >= target) {
                return BUCKET_BOUNDS[i];
            }
        }

        return max_latency.load(std::memory_order_relaxed);
    }

    /**
     * @brief Reset histogram
     */
    void reset() noexcept {
        for (auto& bucket : buckets) {
            bucket.store(0, std::memory_order_relaxed);
        }
        min_latency.store(UINT64_MAX, std::memory_order_relaxed);
        max_latency.store(0, std::memory_order_relaxed);
        total_latency.store(0, std::memory_order_relaxed);
        count.store(0, std::memory_order_relaxed);
    }
};

// =============================================================================
// Real-time Metrics
// =============================================================================

/**
 * @brief Real-time test metrics (thread-safe)
 */
struct test_metrics {
    /** Messages sent successfully */
    std::atomic<uint64_t> messages_sent{0};

    /** Messages that received valid ACK */
    std::atomic<uint64_t> messages_acked{0};

    /** Messages that failed */
    std::atomic<uint64_t> messages_failed{0};

    /** Connection errors */
    std::atomic<uint64_t> connection_errors{0};

    /** Timeout errors */
    std::atomic<uint64_t> timeout_errors{0};

    /** Protocol errors (invalid ACK, etc.) */
    std::atomic<uint64_t> protocol_errors{0};

    /** Bytes sent */
    std::atomic<uint64_t> bytes_sent{0};

    /** Bytes received */
    std::atomic<uint64_t> bytes_received{0};

    /** Active connections */
    std::atomic<size_t> active_connections{0};

    /** Latency histogram */
    latency_histogram latency;

    /** Current throughput (messages/second) - updated periodically */
    std::atomic<double> current_throughput{0.0};

    /** Test start time */
    std::chrono::steady_clock::time_point start_time;

    /**
     * @brief Get total messages attempted
     */
    [[nodiscard]] uint64_t total_messages() const noexcept {
        return messages_sent.load(std::memory_order_relaxed) +
               messages_failed.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get success rate percentage
     */
    [[nodiscard]] double success_rate() const noexcept {
        uint64_t total = total_messages();
        if (total == 0) return 100.0;
        return static_cast<double>(messages_acked.load()) * 100.0 /
               static_cast<double>(total);
    }

    /**
     * @brief Get elapsed time
     */
    [[nodiscard]] std::chrono::seconds elapsed() const noexcept {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time);
    }

    /**
     * @brief Get overall throughput (messages/second)
     */
    [[nodiscard]] double overall_throughput() const noexcept {
        auto secs = elapsed().count();
        if (secs == 0) return 0.0;
        return static_cast<double>(messages_sent.load()) /
               static_cast<double>(secs);
    }

    /**
     * @brief Reset all metrics
     */
    void reset() noexcept {
        messages_sent.store(0);
        messages_acked.store(0);
        messages_failed.store(0);
        connection_errors.store(0);
        timeout_errors.store(0);
        protocol_errors.store(0);
        bytes_sent.store(0);
        bytes_received.store(0);
        active_connections.store(0);
        current_throughput.store(0.0);
        latency.reset();
        start_time = std::chrono::steady_clock::now();
    }
};

// =============================================================================
// Test Results
// =============================================================================

/**
 * @brief Test result summary
 */
struct test_result {
    /** Test type that was executed */
    test_type type;

    /** Final test state */
    test_state state;

    /** Test start time */
    std::chrono::system_clock::time_point started_at;

    /** Test end time */
    std::chrono::system_clock::time_point ended_at;

    /** Total test duration */
    std::chrono::seconds duration;

    /** Target configuration */
    std::string target_host;
    uint16_t target_port;

    /** Messages sent */
    uint64_t messages_sent;

    /** Messages acknowledged */
    uint64_t messages_acked;

    /** Messages failed */
    uint64_t messages_failed;

    /** Success rate percentage */
    double success_rate_percent;

    /** Overall throughput (msg/s) */
    double throughput;

    /** Peak throughput observed (msg/s) */
    double peak_throughput;

    /** Latency P50 (milliseconds) */
    double latency_p50_ms;

    /** Latency P95 (milliseconds) */
    double latency_p95_ms;

    /** Latency P99 (milliseconds) */
    double latency_p99_ms;

    /** Minimum latency (milliseconds) */
    double latency_min_ms;

    /** Maximum latency (milliseconds) */
    double latency_max_ms;

    /** Mean latency (milliseconds) */
    double latency_mean_ms;

    /** Total bytes sent */
    uint64_t bytes_sent;

    /** Total bytes received */
    uint64_t bytes_received;

    /** Connection errors */
    uint64_t connection_errors;

    /** Timeout errors */
    uint64_t timeout_errors;

    /** Protocol errors */
    uint64_t protocol_errors;

    /** Error message if failed */
    std::optional<std::string> error_message;

    /** Additional notes or observations */
    std::vector<std::string> notes;

    /**
     * @brief Check if test passed based on criteria
     * @param min_success_rate Minimum required success rate
     * @param max_p95_latency_ms Maximum allowed P95 latency
     */
    [[nodiscard]] bool passed(
        double min_success_rate = 100.0,
        double max_p95_latency_ms = 50.0) const noexcept {
        return state == test_state::completed &&
               success_rate_percent >= min_success_rate &&
               latency_p95_ms <= max_p95_latency_ms;
    }

    /**
     * @brief Generate summary string
     */
    [[nodiscard]] std::string summary() const;
};

// =============================================================================
// Progress Callback
// =============================================================================

/**
 * @brief Progress update information
 */
struct progress_info {
    /** Current test state */
    test_state state;

    /** Elapsed time */
    std::chrono::seconds elapsed;

    /** Remaining time (estimated) */
    std::chrono::seconds remaining;

    /** Progress percentage (0-100) */
    double progress_percent;

    /** Current metrics snapshot */
    uint64_t messages_sent;
    uint64_t messages_acked;
    uint64_t messages_failed;
    double current_throughput;
    double current_latency_p95_ms;
};

/**
 * @brief Progress callback function type
 */
using progress_callback = std::function<void(const progress_info&)>;

}  // namespace pacs::bridge::testing

#endif  // PACS_BRIDGE_TESTING_LOAD_TYPES_H
