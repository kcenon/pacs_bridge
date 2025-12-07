#ifndef PACS_BRIDGE_MONITORING_HEALTH_TYPES_H
#define PACS_BRIDGE_MONITORING_HEALTH_TYPES_H

/**
 * @file health_types.h
 * @brief Health check type definitions for PACS Bridge
 *
 * Provides common types for health monitoring including status enums,
 * component health structures, and overall health check results.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/41
 */

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::monitoring {

// ═══════════════════════════════════════════════════════════════════════════
// Error Codes (-980 to -989)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Health check specific error codes
 *
 * Allocated range: -980 to -989
 * @see docs/SDS_COMPONENTS.md for error code allocation
 */
enum class health_error : int {
    /** Health check operation timed out */
    timeout = -980,

    /** A monitored component is unavailable */
    component_unavailable = -981,

    /** A health threshold has been exceeded */
    threshold_exceeded = -982,

    /** Health check configuration is invalid */
    invalid_configuration = -983,

    /** Health check is not initialized */
    not_initialized = -984,

    /** Failed to serialize health response */
    serialization_failed = -985
};

/**
 * @brief Convert health_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(health_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of health error
 */
[[nodiscard]] constexpr const char* to_string(health_error error) noexcept {
    switch (error) {
        case health_error::timeout:
            return "Health check operation timed out";
        case health_error::component_unavailable:
            return "Component is unavailable";
        case health_error::threshold_exceeded:
            return "Health threshold exceeded";
        case health_error::invalid_configuration:
            return "Invalid health check configuration";
        case health_error::not_initialized:
            return "Health checker not initialized";
        case health_error::serialization_failed:
            return "Failed to serialize health response";
        default:
            return "Unknown health check error";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Health Status
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Health status enumeration
 *
 * Represents the overall health state of a component or system.
 */
enum class health_status {
    /** Component is fully operational */
    healthy,

    /** Component is operational but with warnings or reduced capacity */
    degraded,

    /** Component is not operational */
    unhealthy
};

/**
 * @brief Convert health_status to string representation
 */
[[nodiscard]] constexpr const char* to_string(health_status status) noexcept {
    switch (status) {
        case health_status::healthy:
            return "UP";
        case health_status::degraded:
            return "DEGRADED";
        case health_status::unhealthy:
            return "DOWN";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Parse health_status from string
 * @param str Status string ("UP", "DEGRADED", "DOWN")
 * @return Parsed status or nullopt if invalid
 */
[[nodiscard]] inline std::optional<health_status>
parse_health_status(std::string_view str) noexcept {
    if (str == "UP" || str == "healthy") {
        return health_status::healthy;
    }
    if (str == "DEGRADED" || str == "degraded") {
        return health_status::degraded;
    }
    if (str == "DOWN" || str == "unhealthy") {
        return health_status::unhealthy;
    }
    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════════════
// Component Health
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Health information for a single component
 */
struct component_health {
    /** Component name (e.g., "mllp_server", "pacs_system") */
    std::string name;

    /** Current health status */
    health_status status = health_status::unhealthy;

    /** Optional response time in milliseconds */
    std::optional<int64_t> response_time_ms;

    /** Optional additional details or error message */
    std::optional<std::string> details;

    /** Optional metrics (e.g., "active_connections": 5) */
    std::map<std::string, std::string> metrics;

    /**
     * @brief Check if component is considered healthy
     */
    [[nodiscard]] constexpr bool is_healthy() const noexcept {
        return status == health_status::healthy;
    }

    /**
     * @brief Check if component is operational (healthy or degraded)
     */
    [[nodiscard]] constexpr bool is_operational() const noexcept {
        return status != health_status::unhealthy;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Health Check Results
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Liveness check result
 *
 * Simple check to verify the service is running.
 * Used by load balancers to determine if the process should be restarted.
 */
struct liveness_result {
    /** Overall liveness status */
    health_status status = health_status::unhealthy;

    /** Timestamp when check was performed */
    std::chrono::system_clock::time_point timestamp;

    /**
     * @brief Create a healthy liveness result
     */
    [[nodiscard]] static liveness_result ok() {
        return {health_status::healthy, std::chrono::system_clock::now()};
    }

    /**
     * @brief Create an unhealthy liveness result
     */
    [[nodiscard]] static liveness_result fail() {
        return {health_status::unhealthy, std::chrono::system_clock::now()};
    }
};

/**
 * @brief Readiness check result
 *
 * Checks if the service is ready to accept traffic.
 * Includes status of critical dependencies.
 */
struct readiness_result {
    /** Overall readiness status */
    health_status status = health_status::unhealthy;

    /** Timestamp when check was performed */
    std::chrono::system_clock::time_point timestamp;

    /** Status of each checked component */
    std::map<std::string, health_status> components;

    /**
     * @brief Check if all components are healthy
     */
    [[nodiscard]] bool all_healthy() const noexcept {
        for (const auto& [name, component_status] : components) {
            if (component_status != health_status::healthy) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Check if any component is unhealthy
     */
    [[nodiscard]] bool any_unhealthy() const noexcept {
        for (const auto& [name, component_status] : components) {
            if (component_status == health_status::unhealthy) {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief Deep health check result
 *
 * Comprehensive health check including detailed component information,
 * response times, and operational metrics.
 */
struct deep_health_result {
    /** Overall system status */
    health_status status = health_status::unhealthy;

    /** Timestamp when check was performed */
    std::chrono::system_clock::time_point timestamp;

    /** Detailed health information for each component */
    std::vector<component_health> components;

    /** Optional overall message or summary */
    std::optional<std::string> message;

    /**
     * @brief Get component health by name
     * @param name Component name
     * @return Pointer to component health or nullptr if not found
     */
    [[nodiscard]] const component_health*
    find_component(std::string_view name) const noexcept {
        for (const auto& comp : components) {
            if (comp.name == name) {
                return &comp;
            }
        }
        return nullptr;
    }

    /**
     * @brief Calculate overall status from component statuses
     *
     * - All healthy: healthy
     * - Any degraded (none unhealthy): degraded
     * - Any unhealthy: unhealthy
     */
    void calculate_overall_status() noexcept {
        if (components.empty()) {
            status = health_status::unhealthy;
            return;
        }

        bool has_degraded = false;
        for (const auto& comp : components) {
            if (comp.status == health_status::unhealthy) {
                status = health_status::unhealthy;
                return;
            }
            if (comp.status == health_status::degraded) {
                has_degraded = true;
            }
        }

        status = has_degraded ? health_status::degraded : health_status::healthy;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Health Check Configuration
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Health check thresholds configuration
 */
struct health_thresholds {
    /** Maximum acceptable memory usage in MB */
    size_t memory_mb = 500;

    /** Maximum acceptable queue depth */
    size_t queue_depth = 50000;

    /** Maximum acceptable dead letter count */
    size_t queue_dead_letters = 100;

    /** Maximum acceptable error rate percentage */
    double error_rate_percent = 5.0;

    /** Maximum response time for component checks (ms) */
    int64_t component_timeout_ms = 5000;
};

/**
 * @brief Health check server configuration
 */
struct health_config {
    /** Enable health check endpoints */
    bool enabled = true;

    /** HTTP port for health endpoints */
    uint16_t port = 8081;

    /** Base path for health endpoints */
    std::string base_path = "/health";

    /** Health check thresholds */
    health_thresholds thresholds;

    /** Include detailed metrics in responses */
    bool include_metrics = true;

    /** CORS allowed origins (empty = no CORS) */
    std::vector<std::string> cors_origins;
};

}  // namespace pacs::bridge::monitoring

#endif  // PACS_BRIDGE_MONITORING_HEALTH_TYPES_H
