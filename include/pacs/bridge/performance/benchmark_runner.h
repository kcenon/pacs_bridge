#ifndef PACS_BRIDGE_PERFORMANCE_BENCHMARK_RUNNER_H
#define PACS_BRIDGE_PERFORMANCE_BENCHMARK_RUNNER_H

/**
 * @file benchmark_runner.h
 * @brief Performance benchmark suite for PACS Bridge
 *
 * Provides comprehensive benchmarking tools to validate system performance
 * against SRS targets. Integrates with the load testing framework and
 * produces detailed performance reports.
 *
 * Benchmark Types:
 *   - Parser benchmarks (zero-copy vs. traditional)
 *   - Throughput benchmarks (messages per second)
 *   - Latency benchmarks (P50, P95, P99)
 *   - Memory benchmarks (allocation patterns, pool efficiency)
 *   - Concurrency benchmarks (thread scaling)
 *
 * @see docs/SRS.md SRS-PERF-001 to SRS-PERF-006
 */

#include "pacs/bridge/performance/performance_types.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// Benchmark Suite
// =============================================================================

/**
 * @brief Collection of benchmark results
 */
struct benchmark_suite_result {
    /** Suite name */
    std::string name;

    /** Individual benchmark results */
    std::vector<benchmark_result> benchmarks;

    /** Total suite duration */
    std::chrono::milliseconds total_duration{0};

    /** Overall pass/fail */
    bool passed = false;

    /** Summary statistics */
    struct {
        size_t total_benchmarks = 0;
        size_t passed_benchmarks = 0;
        size_t failed_benchmarks = 0;
        double avg_throughput = 0.0;
        double avg_latency_us = 0.0;
    } summary;

    /**
     * @brief Generate text report
     */
    [[nodiscard]] std::string to_text() const;

    /**
     * @brief Generate JSON report
     */
    [[nodiscard]] std::string to_json() const;

    /**
     * @brief Generate markdown report
     */
    [[nodiscard]] std::string to_markdown() const;
};

// =============================================================================
// Benchmark Callbacks
// =============================================================================

/**
 * @brief Progress callback for benchmark execution
 */
struct benchmark_progress {
    /** Current benchmark name */
    std::string benchmark_name;

    /** Current iteration */
    size_t current_iteration = 0;

    /** Total iterations */
    size_t total_iterations = 0;

    /** Progress percentage (0-100) */
    double progress_percent = 0.0;

    /** Elapsed time */
    std::chrono::milliseconds elapsed{0};

    /** Estimated remaining time */
    std::chrono::milliseconds remaining{0};

    /** Current throughput */
    double current_throughput = 0.0;
};

using progress_callback = std::function<void(const benchmark_progress&)>;

// =============================================================================
// Benchmark Runner
// =============================================================================

/**
 * @brief Benchmark runner for performance validation
 *
 * Executes performance benchmarks and validates results against SRS targets.
 *
 * Example usage:
 * @code
 *     benchmark_runner runner;
 *
 *     // Configure benchmarks
 *     runner.set_warmup_duration(std::chrono::seconds{5});
 *     runner.set_iterations(3);
 *
 *     // Run all benchmarks
 *     auto results = runner.run_all();
 *     if (results) {
 *         std::cout << results->to_markdown();
 *     }
 *
 *     // Or run specific benchmark
 *     auto result = runner.run_benchmark(benchmark_type::throughput);
 * @endcode
 */
class benchmark_runner {
public:
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /**
     * @brief Construct benchmark runner
     * @param config Default benchmark configuration
     */
    explicit benchmark_runner(const benchmark_config& config = {});

    /** Destructor */
    ~benchmark_runner();

    // Non-copyable, movable
    benchmark_runner(const benchmark_runner&) = delete;
    benchmark_runner& operator=(const benchmark_runner&) = delete;
    benchmark_runner(benchmark_runner&&) noexcept;
    benchmark_runner& operator=(benchmark_runner&&) noexcept;

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /**
     * @brief Set benchmark duration
     */
    void set_duration(std::chrono::seconds duration);

    /**
     * @brief Set warmup duration
     */
    void set_warmup_duration(std::chrono::seconds warmup);

    /**
     * @brief Set number of iterations
     */
    void set_iterations(size_t iterations);

    /**
     * @brief Set concurrency level
     */
    void set_concurrency(size_t threads);

    /**
     * @brief Set message size for throughput tests
     */
    void set_message_size(size_t bytes);

    /**
     * @brief Set target rate (0 = max)
     */
    void set_target_rate(size_t messages_per_second);

    /**
     * @brief Enable detailed timing
     */
    void set_detailed_timing(bool enabled);

    /**
     * @brief Set progress callback
     */
    void set_progress_callback(progress_callback callback);

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const benchmark_config& config() const noexcept;

    // -------------------------------------------------------------------------
    // Benchmark Execution
    // -------------------------------------------------------------------------

    /**
     * @brief Run all benchmarks
     *
     * @return Suite results or error
     */
    [[nodiscard]] std::expected<benchmark_suite_result, performance_error> run_all();

    /**
     * @brief Run specific benchmark type
     *
     * @param type Benchmark type to run
     * @return Benchmark result or error
     */
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_benchmark(benchmark_type type);

    /**
     * @brief Run parsing benchmark
     *
     * Tests zero-copy parser performance with various message sizes.
     */
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_parsing_benchmark();

    /**
     * @brief Run throughput benchmark
     *
     * Tests maximum sustainable message throughput.
     */
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_throughput_benchmark();

    /**
     * @brief Run latency benchmark
     *
     * Measures end-to-end latency distribution.
     */
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_latency_benchmark();

    /**
     * @brief Run memory benchmark
     *
     * Tests memory usage patterns and pool efficiency.
     */
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_memory_benchmark();

    /**
     * @brief Run concurrent connections benchmark
     *
     * Tests handling of multiple concurrent connections.
     */
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_concurrent_benchmark();

    /**
     * @brief Run object pool efficiency benchmark
     *
     * Tests object pool hit rate and allocation savings.
     */
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_pool_efficiency_benchmark();

    /**
     * @brief Run thread scaling benchmark
     *
     * Tests throughput scaling with thread count.
     */
    [[nodiscard]] std::expected<benchmark_result, performance_error>
    run_thread_scaling_benchmark();

    // -------------------------------------------------------------------------
    // Custom Benchmarks
    // -------------------------------------------------------------------------

    /**
     * @brief Register custom benchmark
     *
     * @param name Benchmark name
     * @param benchmark Benchmark function (returns throughput, latency)
     */
    using custom_benchmark_fn = std::function<benchmark_result()>;
    void register_benchmark(std::string name, custom_benchmark_fn benchmark);

    /**
     * @brief Run registered custom benchmarks
     */
    [[nodiscard]] std::expected<std::vector<benchmark_result>, performance_error>
    run_custom_benchmarks();

    // -------------------------------------------------------------------------
    // Results
    // -------------------------------------------------------------------------

    /**
     * @brief Get last benchmark result
     */
    [[nodiscard]] std::optional<benchmark_result> last_result() const;

    /**
     * @brief Get all results from last run_all()
     */
    [[nodiscard]] std::optional<benchmark_suite_result> last_suite_result() const;

    /**
     * @brief Compare results against baseline
     *
     * @param baseline Baseline results to compare against
     * @return Comparison report
     */
    [[nodiscard]] std::string compare_baseline(
        const benchmark_suite_result& baseline) const;

    /**
     * @brief Save results to file
     *
     * @param path Output file path
     * @param format Output format ("json", "markdown", "text")
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, performance_error>
    save_results(const std::string& path, const std::string& format = "json");

    /**
     * @brief Load baseline results from file
     *
     * @param path Baseline file path
     * @return Loaded results or error
     */
    [[nodiscard]] std::expected<benchmark_suite_result, performance_error>
    load_baseline(const std::string& path);

    // -------------------------------------------------------------------------
    // Cancellation
    // -------------------------------------------------------------------------

    /**
     * @brief Cancel running benchmark
     */
    void cancel();

    /**
     * @brief Check if benchmark was cancelled
     */
    [[nodiscard]] bool is_cancelled() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// Quick Benchmark Functions
// =============================================================================

/**
 * @brief Quick benchmark for a single operation
 *
 * @tparam F Callable type
 * @param operation Operation to benchmark
 * @param iterations Number of iterations
 * @return Average duration per operation
 */
template <typename F>
std::chrono::nanoseconds benchmark_operation(F&& operation, size_t iterations = 1000);

/**
 * @brief Benchmark with warmup
 *
 * @tparam F Callable type
 * @param operation Operation to benchmark
 * @param warmup_iterations Warmup iterations (not measured)
 * @param measure_iterations Measured iterations
 * @return Average duration per operation
 */
template <typename F>
std::chrono::nanoseconds benchmark_with_warmup(F&& operation,
                                                size_t warmup_iterations = 100,
                                                size_t measure_iterations = 1000);

// =============================================================================
// Template Implementations
// =============================================================================

template <typename F>
std::chrono::nanoseconds benchmark_operation(F&& operation, size_t iterations) {
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        operation();
    }
    auto end = std::chrono::steady_clock::now();
    auto total = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    return total / iterations;
}

template <typename F>
std::chrono::nanoseconds benchmark_with_warmup(F&& operation,
                                                size_t warmup_iterations,
                                                size_t measure_iterations) {
    // Warmup
    for (size_t i = 0; i < warmup_iterations; ++i) {
        operation();
    }

    // Measure
    return benchmark_operation(std::forward<F>(operation), measure_iterations);
}

}  // namespace pacs::bridge::performance

#endif  // PACS_BRIDGE_PERFORMANCE_BENCHMARK_RUNNER_H
