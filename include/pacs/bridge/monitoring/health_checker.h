#ifndef PACS_BRIDGE_MONITORING_HEALTH_CHECKER_H
#define PACS_BRIDGE_MONITORING_HEALTH_CHECKER_H

/**
 * @file health_checker.h
 * @brief Health checker interface and implementation for PACS Bridge
 *
 * Provides comprehensive health checking capabilities for all PACS Bridge
 * components including MLLP server, FHIR gateway, pacs_system connection,
 * message queue, and system resources.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/41
 */

#include "health_types.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pacs::bridge::monitoring {

// ═══════════════════════════════════════════════════════════════════════════
// Forward Declarations
// ═══════════════════════════════════════════════════════════════════════════

// Forward declarations for component types
// These will be replaced with actual types when components are implemented
namespace detail {
struct component_check_context;
}

// ═══════════════════════════════════════════════════════════════════════════
// Component Check Interface
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Interface for component health checks
 *
 * Implement this interface to add custom health checks for components.
 */
class component_check {
public:
    virtual ~component_check() = default;

    /**
     * @brief Get the component name
     */
    [[nodiscard]] virtual std::string name() const = 0;

    /**
     * @brief Check component health
     * @param timeout Maximum time allowed for the check
     * @return Component health result
     */
    [[nodiscard]] virtual component_health check(
        std::chrono::milliseconds timeout) = 0;

    /**
     * @brief Check if this component is critical for readiness
     *
     * Critical components being unhealthy will cause readiness check to fail.
     */
    [[nodiscard]] virtual bool is_critical() const noexcept { return true; }
};

// ═══════════════════════════════════════════════════════════════════════════
// Built-in Component Checks
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief MLLP server health check
 *
 * Checks if the MLLP server is listening and accepting connections.
 */
class mllp_server_check : public component_check {
public:
    /**
     * @brief Constructor
     * @param get_status Function to get MLLP server status
     */
    using status_provider = std::function<bool()>;
    using stats_provider =
        std::function<std::tuple<size_t, size_t, size_t>()>;  // active, total,
                                                               // errors

    explicit mllp_server_check(status_provider is_running,
                               stats_provider get_stats = nullptr);

    [[nodiscard]] std::string name() const override { return "mllp_server"; }

    [[nodiscard]] component_health check(
        std::chrono::milliseconds timeout) override;

private:
    status_provider is_running_;
    stats_provider get_stats_;
};

/**
 * @brief PACS system connection health check
 *
 * Verifies connectivity to the pacs_system (DICOM SCP).
 */
class pacs_connection_check : public component_check {
public:
    /**
     * @brief Constructor
     * @param echo_fn Function to perform DICOM C-ECHO
     */
    using echo_provider = std::function<bool(std::chrono::milliseconds)>;

    explicit pacs_connection_check(echo_provider echo_fn);

    [[nodiscard]] std::string name() const override { return "pacs_system"; }

    [[nodiscard]] component_health check(
        std::chrono::milliseconds timeout) override;

private:
    echo_provider echo_fn_;
};

/**
 * @brief Message queue health check
 *
 * Checks queue database connectivity and queue metrics.
 */
class queue_health_check : public component_check {
public:
    /**
     * @brief Queue metrics structure
     */
    struct queue_metrics {
        size_t pending_messages = 0;
        size_t dead_letters = 0;
        bool database_connected = false;
    };

    using metrics_provider = std::function<queue_metrics()>;

    explicit queue_health_check(metrics_provider get_metrics,
                                const health_thresholds& thresholds);

    [[nodiscard]] std::string name() const override { return "message_queue"; }

    [[nodiscard]] component_health check(
        std::chrono::milliseconds timeout) override;

private:
    metrics_provider get_metrics_;
    health_thresholds thresholds_;
};

/**
 * @brief FHIR server health check
 *
 * Checks if the FHIR REST server is running and responding.
 */
class fhir_server_check : public component_check {
public:
    using status_provider = std::function<bool()>;
    using stats_provider = std::function<std::tuple<size_t, size_t>()>;  // active_requests, total_requests

    explicit fhir_server_check(status_provider is_running,
                               stats_provider get_stats = nullptr);

    [[nodiscard]] std::string name() const override { return "fhir_server"; }

    [[nodiscard]] component_health check(
        std::chrono::milliseconds timeout) override;

    /**
     * @brief FHIR server is optional, not critical for readiness
     */
    [[nodiscard]] bool is_critical() const noexcept override { return false; }

private:
    status_provider is_running_;
    stats_provider get_stats_;
};

/**
 * @brief System memory health check
 *
 * Monitors process memory usage against configured thresholds.
 */
class memory_health_check : public component_check {
public:
    explicit memory_health_check(const health_thresholds& thresholds);

    [[nodiscard]] std::string name() const override { return "memory"; }

    [[nodiscard]] component_health check(
        std::chrono::milliseconds timeout) override;

    /**
     * @brief Memory is not critical for readiness
     */
    [[nodiscard]] bool is_critical() const noexcept override { return false; }

private:
    health_thresholds thresholds_;

    /**
     * @brief Get current process memory usage in bytes
     */
    [[nodiscard]] static size_t get_process_memory();
};

// ═══════════════════════════════════════════════════════════════════════════
// Health Checker
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Central health checker for PACS Bridge
 *
 * Coordinates health checks across all registered components and provides
 * liveness, readiness, and deep health check capabilities.
 *
 * Thread-safe: All public methods are thread-safe.
 *
 * @example
 * ```cpp
 * health_checker checker(config);
 *
 * // Register component checks
 * checker.register_check(
 *     std::make_unique<mllp_server_check>([&]{ return server.is_running(); }));
 *
 * // Perform health checks
 * auto liveness = checker.check_liveness();
 * auto readiness = checker.check_readiness();
 * auto deep = checker.check_deep();
 * ```
 */
class health_checker {
public:
    /**
     * @brief Constructor
     * @param config Health check configuration
     */
    explicit health_checker(const health_config& config);

    /**
     * @brief Destructor
     */
    ~health_checker();

    // Non-copyable
    health_checker(const health_checker&) = delete;
    health_checker& operator=(const health_checker&) = delete;

    // Movable
    health_checker(health_checker&&) noexcept;
    health_checker& operator=(health_checker&&) noexcept;

    // ═══════════════════════════════════════════════════════════════════════
    // Component Registration
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Register a component health check
     * @param check Component check instance
     */
    void register_check(std::unique_ptr<component_check> check);

    /**
     * @brief Register a simple component check with a lambda
     * @param name Component name
     * @param check_fn Function that returns component health
     * @param critical Whether this component is critical for readiness
     */
    void register_check(
        std::string name,
        std::function<component_health(std::chrono::milliseconds)> check_fn,
        bool critical = true);

    /**
     * @brief Unregister a component check by name
     * @param name Component name
     * @return true if check was found and removed
     */
    bool unregister_check(std::string_view name);

    /**
     * @brief Get list of registered component names
     */
    [[nodiscard]] std::vector<std::string> registered_components() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Health Check Operations
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Perform liveness check
     *
     * Simple check to verify the service process is alive.
     * Should return quickly (< 100ms).
     *
     * @return Liveness result
     */
    [[nodiscard]] liveness_result check_liveness() const;

    /**
     * @brief Perform readiness check
     *
     * Checks if all critical components are operational and the service
     * is ready to accept traffic.
     *
     * @return Readiness result with component statuses
     */
    [[nodiscard]] readiness_result check_readiness() const;

    /**
     * @brief Perform deep health check
     *
     * Comprehensive check of all components with detailed metrics
     * and response times.
     *
     * @return Deep health result with full component details
     */
    [[nodiscard]] deep_health_result check_deep() const;

    /**
     * @brief Check a specific component
     * @param name Component name
     * @return Component health or nullopt if not found
     */
    [[nodiscard]] std::optional<component_health> check_component(
        std::string_view name) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Configuration
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const health_config& config() const noexcept;

    /**
     * @brief Update health thresholds
     */
    void update_thresholds(const health_thresholds& thresholds);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Serialize liveness result to JSON
 */
[[nodiscard]] std::string to_json(const liveness_result& result);

/**
 * @brief Serialize readiness result to JSON
 */
[[nodiscard]] std::string to_json(const readiness_result& result);

/**
 * @brief Serialize deep health result to JSON
 */
[[nodiscard]] std::string to_json(const deep_health_result& result);

/**
 * @brief Format timestamp as ISO 8601 string
 */
[[nodiscard]] std::string format_timestamp(
    std::chrono::system_clock::time_point tp);

}  // namespace pacs::bridge::monitoring

#endif  // PACS_BRIDGE_MONITORING_HEALTH_CHECKER_H
