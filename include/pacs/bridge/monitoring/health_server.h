#ifndef PACS_BRIDGE_MONITORING_HEALTH_SERVER_H
#define PACS_BRIDGE_MONITORING_HEALTH_SERVER_H

/**
 * @file health_server.h
 * @brief HTTP server for health check endpoints
 *
 * Provides a lightweight HTTP server exposing health check endpoints
 * for load balancer integration and operational monitoring.
 *
 * Endpoints:
 *   GET /health/live  - Liveness check (K8s livenessProbe)
 *   GET /health/ready - Readiness check (K8s readinessProbe)
 *   GET /health/deep  - Deep health check with component details
 *   GET /metrics      - Prometheus metrics (optional, requires metrics_provider)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/41
 */

#include "health_checker.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pacs::bridge::monitoring {

// ═══════════════════════════════════════════════════════════════════════════
// HTTP Response
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief HTTP response structure for health endpoints
 */
struct http_response {
    /** HTTP status code */
    int status_code = 200;

    /** Content-Type header value */
    std::string content_type = "application/json";

    /** Response body */
    std::string body;

    /**
     * @brief Create a 200 OK response with JSON body
     */
    [[nodiscard]] static http_response ok(std::string json_body) {
        return {200, "application/json", std::move(json_body)};
    }

    /**
     * @brief Create a 503 Service Unavailable response with JSON body
     */
    [[nodiscard]] static http_response service_unavailable(
        std::string json_body) {
        return {503, "application/json", std::move(json_body)};
    }

    /**
     * @brief Create a 404 Not Found response
     */
    [[nodiscard]] static http_response not_found() {
        return {404, "application/json", R"({"error": "Not found"})"};
    }

    /**
     * @brief Create a 500 Internal Server Error response
     */
    [[nodiscard]] static http_response internal_error(std::string message) {
        return {500, "application/json",
                R"({"error": ")" + std::move(message) + R"("})"};
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Health Server
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief HTTP server for health check endpoints
 *
 * Lightweight HTTP server that exposes health check endpoints for
 * integration with Kubernetes, Docker, and load balancers.
 *
 * The server supports the following endpoints:
 *
 * | Endpoint | Method | Description | K8s Probe |
 * |----------|--------|-------------|-----------|
 * | /health/live | GET | Liveness check | livenessProbe |
 * | /health/ready | GET | Readiness check | readinessProbe |
 * | /health/deep | GET | Deep health check | - |
 * | /metrics | GET | Prometheus metrics | - |
 *
 * Response Codes:
 *   - 200 OK: Healthy/Ready
 *   - 503 Service Unavailable: Unhealthy/Not Ready
 *
 * @example
 * ```cpp
 * // Create health checker with registered components
 * health_checker checker(config);
 * checker.register_check(std::make_unique<mllp_server_check>(...));
 *
 * // Create and start health server
 * health_server server(checker, server_config);
 * auto result = server.start();
 * if (!result) {
 *     // Handle error
 * }
 *
 * // Server runs until stopped
 * server.stop();
 * ```
 */
class health_server {
public:
    /**
     * @brief Server configuration
     */
    struct config {
        /** HTTP port to listen on */
        uint16_t port = 8081;

        /** Base path for health endpoints */
        std::string base_path = "/health";

        /** Bind address (default: all interfaces) */
        std::string bind_address = "0.0.0.0";

        /** Connection timeout in seconds */
        int connection_timeout_seconds = 30;

        /** Maximum concurrent connections */
        size_t max_connections = 100;

        /** Enable CORS headers */
        bool enable_cors = false;

        /** CORS allowed origins (if enable_cors is true) */
        std::vector<std::string> cors_origins;

        /** Enable /metrics endpoint for Prometheus */
        bool enable_metrics_endpoint = true;

        /** Path for metrics endpoint */
        std::string metrics_path = "/metrics";
    };

    /**
     * @brief Type for metrics provider function
     *
     * A function that returns Prometheus-formatted metrics string
     */
    using metrics_provider = std::function<std::string()>;

    /**
     * @brief Constructor with default configuration
     * @param checker Reference to health checker (must outlive server)
     */
    explicit health_server(health_checker& checker);

    /**
     * @brief Constructor
     * @param checker Reference to health checker (must outlive server)
     * @param cfg Server configuration
     */
    health_server(health_checker& checker, const config& cfg);

    /**
     * @brief Destructor - stops server if running
     */
    ~health_server();

    // Non-copyable
    health_server(const health_server&) = delete;
    health_server& operator=(const health_server&) = delete;

    // Movable
    health_server(health_server&&) noexcept;
    health_server& operator=(health_server&&) noexcept;

    // ═══════════════════════════════════════════════════════════════════════
    // Server Lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Start the HTTP server
     *
     * Starts listening for HTTP requests on the configured port.
     * This method returns immediately; the server runs in background threads.
     *
     * @return true on success, false on failure
     */
    [[nodiscard]] bool start();

    /**
     * @brief Stop the HTTP server
     *
     * Gracefully stops the server and waits for active connections to complete.
     *
     * @param wait_for_connections If true, wait for active connections
     */
    void stop(bool wait_for_connections = true);

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // ═══════════════════════════════════════════════════════════════════════
    // Server Information
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the port the server is listening on
     */
    [[nodiscard]] uint16_t port() const noexcept;

    /**
     * @brief Get the base path for health endpoints
     */
    [[nodiscard]] std::string base_path() const;

    /**
     * @brief Get full URL for liveness endpoint
     */
    [[nodiscard]] std::string liveness_url() const;

    /**
     * @brief Get full URL for readiness endpoint
     */
    [[nodiscard]] std::string readiness_url() const;

    /**
     * @brief Get full URL for deep health endpoint
     */
    [[nodiscard]] std::string deep_health_url() const;

    /**
     * @brief Get full URL for metrics endpoint
     */
    [[nodiscard]] std::string metrics_url() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Metrics Integration
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Set the metrics provider function
     *
     * The provider function will be called when /metrics endpoint is requested.
     * It should return a Prometheus text format string.
     *
     * @param provider Function that returns Prometheus metrics string
     *
     * @example
     * ```cpp
     * server.set_metrics_provider([]() {
     *     return bridge_metrics_collector::instance().get_prometheus_metrics();
     * });
     * ```
     */
    void set_metrics_provider(metrics_provider provider);

    // ═══════════════════════════════════════════════════════════════════════
    // Statistics
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Server statistics
     */
    struct statistics {
        /** Total requests received */
        size_t total_requests = 0;

        /** Requests to /health/live */
        size_t liveness_requests = 0;

        /** Requests to /health/ready */
        size_t readiness_requests = 0;

        /** Requests to /health/deep */
        size_t deep_health_requests = 0;

        /** Requests to /metrics */
        size_t metrics_requests = 0;

        /** Current active connections */
        size_t active_connections = 0;

        /** Total errors (4xx and 5xx responses) */
        size_t errors = 0;
    };

    /**
     * @brief Get server statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Request Handling (for testing/custom integration)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Handle a health check request directly
     *
     * Useful for testing or when integrating with existing HTTP infrastructure.
     *
     * @param path Request path (e.g., "/health/live")
     * @return HTTP response
     */
    [[nodiscard]] http_response handle_request(std::string_view path) const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Kubernetes Probe Configuration Helper
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Generate Kubernetes probe configuration YAML
 *
 * Generates YAML snippets for Kubernetes deployment configuration.
 *
 * @param port Health server port
 * @param base_path Base path for health endpoints
 * @return YAML configuration string
 */
[[nodiscard]] std::string generate_k8s_probe_config(
    uint16_t port, std::string_view base_path = "/health");

/**
 * @brief Generate Docker HEALTHCHECK instruction
 *
 * @param port Health server port
 * @param base_path Base path for health endpoints
 * @return HEALTHCHECK instruction string
 */
[[nodiscard]] std::string generate_docker_healthcheck(
    uint16_t port, std::string_view base_path = "/health");

}  // namespace pacs::bridge::monitoring

#endif  // PACS_BRIDGE_MONITORING_HEALTH_SERVER_H
