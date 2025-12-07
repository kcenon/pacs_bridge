#ifndef PACS_BRIDGE_TESTING_LOAD_RUNNER_H
#define PACS_BRIDGE_TESTING_LOAD_RUNNER_H

/**
 * @file load_runner.h
 * @brief Load test executor and orchestrator
 *
 * Executes load tests against MLLP endpoints with configurable rates,
 * durations, and concurrent connections. Collects real-time metrics
 * and generates comprehensive test reports.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/45
 */

#include "load_generator.h"
#include "load_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace pacs::bridge::testing {

/**
 * @brief Load test executor
 *
 * Orchestrates load testing including connection management, message
 * generation, rate limiting, and metrics collection. Supports multiple
 * test scenarios and provides real-time progress updates.
 *
 * @code
 * // Create and configure runner
 * load_runner runner;
 *
 * auto config = load_config::sustained("localhost", 2575,
 *     std::chrono::hours(1), 500);
 *
 * // Set up progress callback
 * runner.on_progress([](const progress_info& info) {
 *     std::cout << "Progress: " << info.progress_percent << "%\n";
 * });
 *
 * // Run test
 * auto result = runner.run(config);
 * if (result) {
 *     std::cout << result->summary() << "\n";
 * }
 * @endcode
 */
class load_runner {
public:
    /**
     * @brief Default constructor
     */
    load_runner();

    /**
     * @brief Destructor
     */
    ~load_runner();

    // Non-copyable
    load_runner(const load_runner&) = delete;
    load_runner& operator=(const load_runner&) = delete;

    // Movable
    load_runner(load_runner&&) noexcept;
    load_runner& operator=(load_runner&&) noexcept;

    /**
     * @brief Run load test with configuration
     * @param config Test configuration
     * @return Test result or error
     */
    [[nodiscard]] std::expected<test_result, load_error>
    run(const load_config& config);

    /**
     * @brief Run sustained load test
     * @param host Target hostname
     * @param port Target port
     * @param duration Test duration
     * @param rate Messages per second
     * @return Test result or error
     */
    [[nodiscard]] std::expected<test_result, load_error>
    run_sustained(
        std::string_view host,
        uint16_t port,
        std::chrono::seconds duration,
        uint32_t rate);

    /**
     * @brief Run peak load test
     * @param host Target hostname
     * @param port Target port
     * @param max_rate Maximum messages per second to attempt
     * @return Test result or error
     */
    [[nodiscard]] std::expected<test_result, load_error>
    run_peak(
        std::string_view host,
        uint16_t port,
        uint32_t max_rate);

    /**
     * @brief Run endurance test
     * @param host Target hostname
     * @param port Target port
     * @param duration Test duration (default 24 hours)
     * @return Test result or error
     */
    [[nodiscard]] std::expected<test_result, load_error>
    run_endurance(
        std::string_view host,
        uint16_t port,
        std::chrono::seconds duration = std::chrono::hours(24));

    /**
     * @brief Run concurrent connection test
     * @param host Target hostname
     * @param port Target port
     * @param connections Number of concurrent connections
     * @param messages_per_connection Messages per connection
     * @return Test result or error
     */
    [[nodiscard]] std::expected<test_result, load_error>
    run_concurrent(
        std::string_view host,
        uint16_t port,
        size_t connections,
        size_t messages_per_connection);

    /**
     * @brief Run queue stress test
     * @param host Target hostname
     * @param port Target port
     * @param accumulation_time Time to accumulate messages with no response
     * @return Test result or error
     */
    [[nodiscard]] std::expected<test_result, load_error>
    run_queue_stress(
        std::string_view host,
        uint16_t port,
        std::chrono::minutes accumulation_time);

    /**
     * @brief Cancel running test
     *
     * Thread-safe cancellation that will stop the test gracefully
     * and return partial results.
     */
    void cancel();

    /**
     * @brief Check if test is currently running
     * @return true if test is in progress
     */
    [[nodiscard]] bool is_running() const noexcept;

    /**
     * @brief Get current test state
     * @return Current state
     */
    [[nodiscard]] test_state state() const noexcept;

    /**
     * @brief Get current metrics (during running test)
     * @return Current metrics or nullopt if not running
     */
    [[nodiscard]] std::optional<test_metrics> current_metrics() const;

    /**
     * @brief Set progress callback
     * @param callback Function to call with progress updates
     */
    void on_progress(progress_callback callback);

    /**
     * @brief Set progress update interval
     * @param interval Time between progress updates
     */
    void set_progress_interval(std::chrono::milliseconds interval);

    /**
     * @brief Set custom message generator
     * @param generator Custom generator instance
     */
    void set_generator(std::shared_ptr<load_generator> generator);

    /**
     * @brief Get the last test result
     * @return Last result or nullopt if no test has completed
     */
    [[nodiscard]] std::optional<test_result> last_result() const;

    /**
     * @brief Validate target connectivity before running test
     * @param host Target hostname
     * @param port Target port
     * @param timeout Connection timeout
     * @return true if target is reachable
     */
    [[nodiscard]] bool validate_target(
        std::string_view host,
        uint16_t port,
        std::chrono::milliseconds timeout = std::chrono::seconds(5));

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Run multiple test configurations in sequence
 *
 * Utility function to run a series of tests and collect all results.
 *
 * @param runner Test runner instance
 * @param configs Test configurations to run
 * @return Vector of test results
 */
[[nodiscard]] std::vector<test_result> run_test_suite(
    load_runner& runner,
    std::span<const load_config> configs);

}  // namespace pacs::bridge::testing

#endif  // PACS_BRIDGE_TESTING_LOAD_RUNNER_H
