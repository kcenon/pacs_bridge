#ifndef PACS_BRIDGE_PERFORMANCE_PERFORMANCE_TYPES_H
#define PACS_BRIDGE_PERFORMANCE_PERFORMANCE_TYPES_H

/**
 * @file performance_types.h
 * @brief Performance optimization type definitions and configuration
 *
 * Defines error codes, configuration structures, and metrics types for
 * the performance optimization layer. Provides targets based on NFR
 * requirements and integration with the kcenon ecosystem.
 *
 * Performance Targets (from SRS):
 *   - Throughput: ≥500 messages/second
 *   - Latency P95: <50 ms
 *   - Memory baseline: <200 MB
 *   - Concurrent connections: ≥50
 *
 * @see docs/SRS.md NFR-1.1 to NFR-1.6, SRS-PERF-001 to SRS-PERF-006
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace pacs::bridge::performance {

// =============================================================================
// Error Codes (-940 to -949)
// =============================================================================

/**
 * @brief Performance module error codes
 *
 * Allocated range: -940 to -949
 */
enum class performance_error : int {
    /** Thread pool initialization failed */
    thread_pool_init_failed = -940,

    /** Object pool exhausted */
    pool_exhausted = -941,

    /** Queue is full */
    queue_full = -942,

    /** Invalid configuration */
    invalid_configuration = -943,

    /** Resource allocation failed */
    allocation_failed = -944,

    /** Operation timed out */
    timeout = -945,

    /** Component not initialized */
    not_initialized = -946,

    /** Benchmark execution failed */
    benchmark_failed = -947,

    /** Parser error */
    parser_error = -948,

    /** Memory limit exceeded */
    memory_limit_exceeded = -949
};

/**
 * @brief Convert performance_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(performance_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of performance error
 */
[[nodiscard]] constexpr const char* to_string(performance_error error) noexcept {
    switch (error) {
        case performance_error::thread_pool_init_failed:
            return "Thread pool initialization failed";
        case performance_error::pool_exhausted:
            return "Object pool exhausted";
        case performance_error::queue_full:
            return "Queue is full";
        case performance_error::invalid_configuration:
            return "Invalid performance configuration";
        case performance_error::allocation_failed:
            return "Resource allocation failed";
        case performance_error::timeout:
            return "Operation timed out";
        case performance_error::not_initialized:
            return "Component not initialized";
        case performance_error::benchmark_failed:
            return "Benchmark execution failed";
        case performance_error::parser_error:
            return "Parser error";
        case performance_error::memory_limit_exceeded:
            return "Memory limit exceeded";
        default:
            return "Unknown performance error";
    }
}

// =============================================================================
// Performance Targets
// =============================================================================

/**
 * @brief Performance target constants from SRS requirements
 */
struct performance_targets {
    /** Minimum throughput: 500 messages per second (NFR-1.1) */
    static constexpr size_t MIN_THROUGHPUT_MSG_PER_SEC = 500;

    /** Maximum P95 latency: 50 milliseconds (NFR-1.2) */
    static constexpr std::chrono::milliseconds MAX_P95_LATENCY{50};

    /** Maximum MWL creation latency: 100 milliseconds (NFR-1.3) */
    static constexpr std::chrono::milliseconds MAX_MWL_LATENCY{100};

    /** Minimum concurrent connections: 50 (NFR-1.4) */
    static constexpr size_t MIN_CONCURRENT_CONNECTIONS = 50;

    /** Maximum memory baseline: 200 MB (NFR-1.5) */
    static constexpr size_t MAX_MEMORY_BASELINE_MB = 200;

    /** Maximum CPU idle usage: 20% (NFR-1.6) */
    static constexpr double MAX_CPU_IDLE_PERCENT = 20.0;
};

// =============================================================================
// Thread Pool Configuration
// =============================================================================

/**
 * @brief Thread pool configuration for optimal performance
 *
 * Integrates with thread_system from kcenon ecosystem.
 * Uses work-stealing algorithm for load balancing.
 */
struct thread_pool_config {
    /** Minimum number of worker threads */
    size_t min_threads = 4;

    /** Maximum number of worker threads (0 = hardware_concurrency) */
    size_t max_threads = 0;

    /** Enable work-stealing for load balancing */
    bool enable_work_stealing = true;

    /** Task queue capacity per thread */
    size_t queue_capacity = 1024;

    /** Thread idle timeout before reduction */
    std::chrono::seconds idle_timeout{60};

    /** Thread name prefix for debugging */
    std::string thread_name_prefix = "pacs_worker";

    /** Enable thread affinity (pin to CPU cores) */
    bool enable_affinity = false;

    /** Priority boost for time-sensitive tasks */
    bool enable_priority_scheduling = true;

    /**
     * @brief Create default configuration for server workload
     */
    [[nodiscard]] static thread_pool_config for_server() noexcept {
        thread_pool_config config;
        config.min_threads = 4;
        config.max_threads = 0;  // auto-detect
        config.enable_work_stealing = true;
        config.queue_capacity = 2048;
        return config;
    }

    /**
     * @brief Create configuration for client workload
     */
    [[nodiscard]] static thread_pool_config for_client() noexcept {
        thread_pool_config config;
        config.min_threads = 2;
        config.max_threads = 8;
        config.enable_work_stealing = true;
        config.queue_capacity = 512;
        return config;
    }

    /**
     * @brief Create configuration for benchmarking
     */
    [[nodiscard]] static thread_pool_config for_benchmark() noexcept {
        thread_pool_config config;
        config.min_threads = 1;
        config.max_threads = 0;  // use all cores
        config.enable_work_stealing = true;
        config.enable_affinity = true;
        config.queue_capacity = 4096;
        return config;
    }
};

// =============================================================================
// Object Pool Configuration
// =============================================================================

/**
 * @brief Object pool configuration for memory optimization
 *
 * Reduces allocation overhead by pre-allocating and reusing objects.
 */
struct object_pool_config {
    /** Initial number of pre-allocated objects */
    size_t initial_size = 64;

    /** Maximum pool size (0 = unlimited) */
    size_t max_size = 1024;

    /** Grow by this many objects when pool is exhausted */
    size_t grow_size = 32;

    /** Shrink threshold (shrink when usage drops below this percentage) */
    double shrink_threshold = 0.25;

    /** Enable pool statistics collection */
    bool enable_statistics = true;

    /** Thread-safe access mode */
    bool thread_safe = true;
};

// =============================================================================
// Lock-Free Queue Configuration
// =============================================================================

/**
 * @brief Lock-free queue configuration
 *
 * Uses MPMC (multi-producer, multi-consumer) lock-free queue
 * from thread_system.
 */
struct lockfree_queue_config {
    /** Queue capacity (must be power of 2) */
    size_t capacity = 4096;

    /** Enable bounded mode (block/fail when full) */
    bool bounded = true;

    /** Spin count before yielding on contention */
    size_t spin_count = 100;

    /** Enable backoff on contention */
    bool enable_backoff = true;

    /**
     * @brief Validate configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        // Capacity must be power of 2
        return capacity > 0 && (capacity & (capacity - 1)) == 0;
    }
};

// =============================================================================
// Memory Optimization Configuration
// =============================================================================

/**
 * @brief Memory optimization settings
 */
struct memory_config {
    /** Maximum memory usage in bytes (0 = unlimited) */
    size_t max_memory_bytes = 200 * 1024 * 1024;  // 200 MB

    /** Enable memory pool for small allocations */
    bool enable_small_object_pool = true;

    /** Small object threshold (bytes) */
    size_t small_object_threshold = 256;

    /** Enable buffer pool for message buffers */
    bool enable_buffer_pool = true;

    /** Default buffer size for pooling */
    size_t default_buffer_size = 4096;

    /** Number of pre-allocated message buffers */
    size_t message_buffer_pool_size = 128;

    /** Enable memory usage tracking */
    bool enable_tracking = true;
};

// =============================================================================
// Zero-Copy Parser Configuration
// =============================================================================

/**
 * @brief Zero-copy parser configuration for HL7 messages
 *
 * Enables efficient parsing without copying message data.
 */
struct zero_copy_config {
    /** Enable zero-copy string views */
    bool enable_string_views = true;

    /** Enable lazy parsing (parse only requested fields) */
    bool enable_lazy_parsing = true;

    /** Pre-allocate segment index capacity */
    size_t segment_index_capacity = 32;

    /** Pre-allocate field index capacity per segment */
    size_t field_index_capacity = 32;

    /** Cache parsed segments for repeated access */
    bool enable_segment_cache = true;
};

// =============================================================================
// Connection Pool Configuration
// =============================================================================

/**
 * @brief Connection pool configuration for MLLP clients
 */
struct connection_pool_config {
    /** Minimum idle connections per target */
    size_t min_idle_connections = 2;

    /** Maximum connections per target */
    size_t max_connections_per_target = 10;

    /** Maximum total connections */
    size_t max_total_connections = 100;

    /** Connection idle timeout */
    std::chrono::seconds idle_timeout{300};

    /** Connection validation interval */
    std::chrono::seconds validation_interval{60};

    /** Enable connection keep-alive */
    bool enable_keep_alive = true;

    /** Maximum connection age before recycling */
    std::chrono::minutes max_connection_age{30};

    /** Enable connection pre-warming */
    bool enable_pre_warming = true;
};

// =============================================================================
// Performance Metrics (Real-time)
// =============================================================================

/**
 * @brief Real-time performance metrics
 *
 * Thread-safe atomic metrics for monitoring system performance.
 */
struct performance_metrics {
    /** Messages processed per second (current) */
    std::atomic<double> current_throughput{0.0};

    /** Peak throughput observed */
    std::atomic<double> peak_throughput{0.0};

    /** Average latency in microseconds */
    std::atomic<uint64_t> avg_latency_us{0};

    /** P50 latency in microseconds */
    std::atomic<uint64_t> p50_latency_us{0};

    /** P95 latency in microseconds */
    std::atomic<uint64_t> p95_latency_us{0};

    /** P99 latency in microseconds */
    std::atomic<uint64_t> p99_latency_us{0};

    /** Total messages processed */
    std::atomic<uint64_t> total_messages{0};

    /** Active connections */
    std::atomic<size_t> active_connections{0};

    /** Current memory usage in bytes */
    std::atomic<size_t> memory_usage_bytes{0};

    /** CPU usage percentage */
    std::atomic<double> cpu_usage_percent{0.0};

    /** Object pool hit rate */
    std::atomic<double> pool_hit_rate{0.0};

    /** Queue depth (current items in queue) */
    std::atomic<size_t> queue_depth{0};

    /** Thread pool active threads */
    std::atomic<size_t> active_threads{0};

    /**
     * @brief Reset all metrics to zero
     */
    void reset() noexcept {
        current_throughput.store(0.0, std::memory_order_relaxed);
        peak_throughput.store(0.0, std::memory_order_relaxed);
        avg_latency_us.store(0, std::memory_order_relaxed);
        p50_latency_us.store(0, std::memory_order_relaxed);
        p95_latency_us.store(0, std::memory_order_relaxed);
        p99_latency_us.store(0, std::memory_order_relaxed);
        total_messages.store(0, std::memory_order_relaxed);
        active_connections.store(0, std::memory_order_relaxed);
        memory_usage_bytes.store(0, std::memory_order_relaxed);
        cpu_usage_percent.store(0.0, std::memory_order_relaxed);
        pool_hit_rate.store(0.0, std::memory_order_relaxed);
        queue_depth.store(0, std::memory_order_relaxed);
        active_threads.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Check if performance meets targets
     */
    [[nodiscard]] bool meets_targets() const noexcept {
        return current_throughput.load(std::memory_order_relaxed) >=
                   performance_targets::MIN_THROUGHPUT_MSG_PER_SEC &&
               p95_latency_us.load(std::memory_order_relaxed) <=
                   static_cast<uint64_t>(
                       performance_targets::MAX_P95_LATENCY.count() * 1000) &&
               memory_usage_bytes.load(std::memory_order_relaxed) <=
                   performance_targets::MAX_MEMORY_BASELINE_MB * 1024 * 1024;
    }
};

// =============================================================================
// Benchmark Configuration
// =============================================================================

/**
 * @brief Benchmark type enumeration
 */
enum class benchmark_type {
    /** Message parsing benchmark */
    parsing,

    /** MLLP throughput benchmark */
    throughput,

    /** End-to-end latency benchmark */
    latency,

    /** Memory usage benchmark */
    memory,

    /** Concurrent connection benchmark */
    concurrent,

    /** Object pool efficiency benchmark */
    pool_efficiency,

    /** Thread pool scalability benchmark */
    thread_scaling
};

/**
 * @brief Benchmark configuration
 */
struct benchmark_config {
    /** Benchmark type to run */
    benchmark_type type = benchmark_type::throughput;

    /** Test duration */
    std::chrono::seconds duration{60};

    /** Warm-up duration */
    std::chrono::seconds warmup{5};

    /** Target messages per second (0 = max) */
    size_t target_rate = 0;

    /** Number of iterations for averaging */
    size_t iterations = 3;

    /** Number of concurrent connections/threads */
    size_t concurrency = 4;

    /** Message size in bytes */
    size_t message_size = 1024;

    /** Output file for results (empty = stdout) */
    std::string output_file;

    /** Enable detailed per-operation timing */
    bool detailed_timing = false;

    /** Compare against baseline results */
    bool compare_baseline = false;
};

/**
 * @brief Benchmark result summary
 */
struct benchmark_result {
    /** Benchmark type */
    benchmark_type type;

    /** Achieved throughput (messages/second) */
    double throughput = 0.0;

    /** Average latency in microseconds */
    double avg_latency_us = 0.0;

    /** P50 latency in microseconds */
    double p50_latency_us = 0.0;

    /** P95 latency in microseconds */
    double p95_latency_us = 0.0;

    /** P99 latency in microseconds */
    double p99_latency_us = 0.0;

    /** Minimum latency in microseconds */
    double min_latency_us = 0.0;

    /** Maximum latency in microseconds */
    double max_latency_us = 0.0;

    /** Total messages processed */
    uint64_t total_messages = 0;

    /** Total errors */
    uint64_t total_errors = 0;

    /** Peak memory usage in bytes */
    size_t peak_memory_bytes = 0;

    /** Test duration */
    std::chrono::milliseconds actual_duration{0};

    /** Whether targets were met */
    bool targets_met = false;

    /**
     * @brief Check if benchmark passed
     */
    [[nodiscard]] bool passed() const noexcept {
        return total_errors == 0 && targets_met;
    }
};

}  // namespace pacs::bridge::performance

#endif  // PACS_BRIDGE_PERFORMANCE_PERFORMANCE_TYPES_H
