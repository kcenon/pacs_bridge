#ifndef PACS_BRIDGE_BRIDGE_SERVER_H
#define PACS_BRIDGE_BRIDGE_SERVER_H

/**
 * @file bridge_server.h
 * @brief Main orchestration server for PACS Bridge system
 *
 * Provides the central entrypoint that coordinates all gateway components:
 *   - MPPS ingestion from pacs_system (#172)
 *   - MPPS to HL7 workflow processing (#173)
 *   - Reliable outbound delivery with queue persistence (#174)
 *   - Health monitoring and metrics
 *
 * Features:
 *   - Single entrypoint to start/stop the entire Phase 2 workflow
 *   - Configuration via bridge_config or YAML/JSON file
 *   - Graceful shutdown with pending operation completion
 *   - Health endpoint for component status monitoring
 *   - Statistics aggregation across all components
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/175
 * @see docs/SDS_INTERFACES.md (INT-API-001)
 */

#include "pacs/bridge/config/bridge_config.h"
#include "pacs/bridge/monitoring/health_types.h"

#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge {

// =============================================================================
// Error Codes (-800 to -809)
// =============================================================================

/**
 * @brief Bridge server specific error codes
 *
 * Allocated range: -800 to -809
 */
enum class bridge_server_error : int {
    /** Server is already running */
    already_running = -800,

    /** Server is not running */
    not_running = -801,

    /** Configuration is invalid */
    invalid_configuration = -802,

    /** Failed to load configuration file */
    config_load_failed = -803,

    /** MPPS handler initialization failed */
    mpps_init_failed = -804,

    /** Outbound sender initialization failed */
    outbound_init_failed = -805,

    /** Workflow initialization failed */
    workflow_init_failed = -806,

    /** Health checker initialization failed */
    health_init_failed = -807,

    /** Shutdown timeout exceeded */
    shutdown_timeout = -808,

    /** Internal error */
    internal_error = -809
};

/**
 * @brief Convert bridge_server_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(bridge_server_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of error
 */
[[nodiscard]] constexpr const char* to_string(bridge_server_error error) noexcept {
    switch (error) {
        case bridge_server_error::already_running:
            return "Bridge server is already running";
        case bridge_server_error::not_running:
            return "Bridge server is not running";
        case bridge_server_error::invalid_configuration:
            return "Bridge configuration is invalid";
        case bridge_server_error::config_load_failed:
            return "Failed to load configuration file";
        case bridge_server_error::mpps_init_failed:
            return "Failed to initialize MPPS handler";
        case bridge_server_error::outbound_init_failed:
            return "Failed to initialize outbound sender";
        case bridge_server_error::workflow_init_failed:
            return "Failed to initialize MPPS-HL7 workflow";
        case bridge_server_error::health_init_failed:
            return "Failed to initialize health checker";
        case bridge_server_error::shutdown_timeout:
            return "Shutdown timeout exceeded";
        case bridge_server_error::internal_error:
            return "Internal server error";
        default:
            return "Unknown bridge server error";
    }
}

// =============================================================================
// Statistics
// =============================================================================

/**
 * @brief Aggregated server statistics
 */
struct bridge_statistics {
    // =========================================================================
    // MLLP Statistics
    // =========================================================================

    /** Number of active MLLP connections */
    size_t mllp_active_connections = 0;

    /** Total MLLP messages received */
    size_t mllp_messages_received = 0;

    /** Total MLLP messages sent */
    size_t mllp_messages_sent = 0;

    /** MLLP error count */
    size_t mllp_errors = 0;

    // =========================================================================
    // MPPS Statistics
    // =========================================================================

    /** Total MPPS events received */
    size_t mpps_events_received = 0;

    /** MPPS IN PROGRESS events */
    size_t mpps_in_progress_count = 0;

    /** MPPS COMPLETED events */
    size_t mpps_completed_count = 0;

    /** MPPS DISCONTINUED events */
    size_t mpps_discontinued_count = 0;

    // =========================================================================
    // Workflow Statistics
    // =========================================================================

    /** Workflow executions */
    size_t workflow_executions = 0;

    /** Successful workflow completions */
    size_t workflow_successes = 0;

    /** Failed workflow executions */
    size_t workflow_failures = 0;

    // =========================================================================
    // Queue Statistics
    // =========================================================================

    /** Current queue depth */
    size_t queue_depth = 0;

    /** Messages in dead letter queue */
    size_t queue_dead_letters = 0;

    /** Total messages enqueued */
    size_t queue_total_enqueued = 0;

    /** Total messages delivered */
    size_t queue_total_delivered = 0;

    // =========================================================================
    // Cache Statistics
    // =========================================================================

    /** Patient cache size */
    size_t cache_size = 0;

    /** Cache hit rate (0.0 to 1.0) */
    double cache_hit_rate = 0.0;

    // =========================================================================
    // Timing
    // =========================================================================

    /** Server uptime */
    std::chrono::seconds uptime{0};

    /** Last activity timestamp */
    std::chrono::system_clock::time_point last_activity;
};

// =============================================================================
// Health Status
// =============================================================================

/**
 * @brief Component health status for bridge server
 */
struct bridge_health_status {
    /** Overall health status */
    bool healthy = false;

    /** MPPS handler is healthy */
    bool mpps_handler_healthy = false;

    /** Outbound sender is healthy */
    bool outbound_sender_healthy = false;

    /** Workflow processor is healthy */
    bool workflow_healthy = false;

    /** Message queue is healthy */
    bool queue_healthy = false;

    /** Patient cache is healthy */
    bool cache_healthy = true;

    /** MLLP server is healthy (if enabled) */
    bool mllp_server_healthy = true;

    /** FHIR server is healthy (if enabled) */
    bool fhir_server_healthy = true;

    /** Detailed status message */
    std::string details;

    /** Individual component health reports */
    std::vector<monitoring::component_health> component_reports;
};

// =============================================================================
// Bridge Server
// =============================================================================

/**
 * @brief Main PACS Bridge orchestration server
 *
 * Coordinates all gateway components for the Phase 2 workflow:
 * MPPS events → HL7 mapping → Reliable outbound delivery
 *
 * @example Basic Usage
 * ```cpp
 * // Load configuration from file
 * bridge_server server("/etc/pacs_bridge/config.yaml");
 *
 * // Start all services
 * auto result = server.start();
 * if (!result) {
 *     std::cerr << "Failed to start: " << to_string(result.error()) << std::endl;
 *     return 1;
 * }
 *
 * std::cout << "PACS Bridge started successfully" << std::endl;
 *
 * // Block until shutdown signal (SIGINT/SIGTERM)
 * server.wait_for_shutdown();
 *
 * // Graceful shutdown
 * server.stop();
 * ```
 *
 * @example With Configuration Object
 * ```cpp
 * config::bridge_config config;
 * config.name = "PACS_BRIDGE_01";
 * config.pacs.host = "pacs.hospital.local";
 * config.pacs.port = 11113;
 * config.queue.database_path = "/var/lib/pacs_bridge/queue.db";
 *
 * bridge_server server(config);
 * server.start();
 * ```
 *
 * @example Health Monitoring
 * ```cpp
 * bridge_server server(config);
 * server.start();
 *
 * // Periodic health check
 * while (server.is_running()) {
 *     auto health = server.get_health_status();
 *     if (!health.healthy) {
 *         log_warning("Health degraded: {}", health.details);
 *     }
 *
 *     auto stats = server.get_statistics();
 *     log_info("Queue depth: {}, Delivered: {}",
 *              stats.queue_depth, stats.queue_total_delivered);
 *
 *     std::this_thread::sleep_for(std::chrono::seconds{30});
 * }
 * ```
 */
class bridge_server {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    /**
     * @brief Construct bridge server with configuration object
     *
     * @param config Bridge configuration
     * @throws std::invalid_argument if config is invalid
     */
    explicit bridge_server(const config::bridge_config& config);

    /**
     * @brief Construct bridge server from configuration file
     *
     * Supports YAML and JSON configuration formats.
     *
     * @param config_path Path to configuration file
     * @throws std::runtime_error if file cannot be loaded or parsed
     */
    explicit bridge_server(const std::filesystem::path& config_path);

    /**
     * @brief Destructor - stops server if running
     */
    ~bridge_server();

    // Non-copyable
    bridge_server(const bridge_server&) = delete;
    bridge_server& operator=(const bridge_server&) = delete;

    // Movable
    bridge_server(bridge_server&&) noexcept;
    bridge_server& operator=(bridge_server&&) noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Start all services
     *
     * Initializes and starts all Phase 2 components in order:
     * 1. Health checker
     * 2. Queue manager (with recovery)
     * 3. Outbound router
     * 4. Reliable outbound sender
     * 5. MPPS-HL7 workflow
     * 6. MPPS handler
     * 7. (Optional) MLLP server for inbound HL7
     *
     * @return Success or error with details
     */
    [[nodiscard]] std::expected<void, bridge_server_error> start();

    /**
     * @brief Stop all services gracefully
     *
     * Stops components in reverse order, allowing pending operations
     * to complete within the timeout period.
     *
     * @param timeout Maximum wait time for pending operations
     */
    void stop(std::chrono::seconds timeout = std::chrono::seconds{30});

    /**
     * @brief Block until shutdown signal is received
     *
     * Blocks the calling thread until:
     * - SIGINT or SIGTERM is received
     * - stop() is called from another thread
     * - A critical component failure occurs
     */
    void wait_for_shutdown();

    /**
     * @brief Check if server is running
     *
     * @return true if server is started and operational
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Runtime Configuration
    // =========================================================================

    /**
     * @brief Reload configuration from file (hot-reload)
     *
     * Reloads configuration and applies changes that can be updated
     * at runtime without full restart.
     *
     * Hot-reloadable settings:
     * - Routing rules
     * - Logging level
     * - Health check thresholds
     *
     * Non-hot-reloadable (require restart):
     * - MLLP server port
     * - PACS connection settings
     * - Queue database path
     *
     * @param config_path Path to new configuration file
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, bridge_server_error>
    reload_config(const std::filesystem::path& config_path);

    /**
     * @brief Add outbound destination dynamically
     *
     * @param destination Destination configuration
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, bridge_server_error>
    add_destination(const config::outbound_destination& destination);

    /**
     * @brief Remove outbound destination
     *
     * @param name Destination name
     */
    void remove_destination(std::string_view name);

    /**
     * @brief Get list of configured destinations
     */
    [[nodiscard]] std::vector<std::string> destinations() const;

    // =========================================================================
    // Monitoring
    // =========================================================================

    /**
     * @brief Get aggregated server statistics
     *
     * Returns combined statistics from all components.
     */
    [[nodiscard]] bridge_statistics get_statistics() const;

    /**
     * @brief Reset statistics counters
     */
    void reset_statistics();

    /**
     * @brief Simple health check
     *
     * @return true if all critical components are healthy
     */
    [[nodiscard]] bool is_healthy() const;

    /**
     * @brief Get detailed health status
     *
     * Returns health status for each component with details.
     */
    [[nodiscard]] bridge_health_status get_health_status() const;

    // =========================================================================
    // Configuration Access
    // =========================================================================

    /**
     * @brief Get server name
     */
    [[nodiscard]] std::string_view name() const noexcept;

    /**
     * @brief Get current configuration (read-only)
     */
    [[nodiscard]] const config::bridge_config& config() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge

#endif  // PACS_BRIDGE_BRIDGE_SERVER_H
