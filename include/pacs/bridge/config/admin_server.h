#ifndef PACS_BRIDGE_CONFIG_ADMIN_SERVER_H
#define PACS_BRIDGE_CONFIG_ADMIN_SERVER_H

/**
 * @file admin_server.h
 * @brief Administrative HTTP server for configuration management
 *
 * Provides HTTP endpoints for administrative operations including
 * configuration hot-reload and runtime management.
 *
 * Endpoints:
 *   POST /admin/reload     - Trigger configuration reload
 *   GET  /admin/config     - Get current configuration (sanitized)
 *   GET  /admin/status     - Get reload status and statistics
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/39
 */

#include "config_manager.h"
#include "pacs/bridge/monitoring/health_server.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pacs::bridge::config {

// =============================================================================
// Admin HTTP Response
// =============================================================================

/**
 * @brief HTTP response structure for admin endpoints
 */
struct admin_response {
    /** HTTP status code */
    int status_code = 200;

    /** Content-Type header value */
    std::string content_type = "application/json";

    /** Response body */
    std::string body;

    /**
     * @brief Create a 200 OK response with JSON body
     */
    [[nodiscard]] static admin_response ok(std::string json_body) {
        return {200, "application/json", std::move(json_body)};
    }

    /**
     * @brief Create a 400 Bad Request response
     */
    [[nodiscard]] static admin_response bad_request(std::string message) {
        return {400, "application/json",
                R"({"success": false, "error": ")" + message + R"("})"};
    }

    /**
     * @brief Create a 500 Internal Server Error response
     */
    [[nodiscard]] static admin_response internal_error(std::string message) {
        return {500, "application/json",
                R"({"success": false, "error": ")" + message + R"("})"};
    }

    /**
     * @brief Create a 404 Not Found response
     */
    [[nodiscard]] static admin_response not_found() {
        return {404, "application/json",
                R"({"success": false, "error": "Not found"})"};
    }

    /**
     * @brief Create a 405 Method Not Allowed response
     */
    [[nodiscard]] static admin_response method_not_allowed() {
        return {405, "application/json",
                R"({"success": false, "error": "Method not allowed"})"};
    }
};

// =============================================================================
// Admin Server
// =============================================================================

/**
 * @brief Administrative HTTP server for runtime management
 *
 * Provides endpoints for configuration management and administrative operations.
 * Should be bound to localhost or protected by authentication in production.
 *
 * @example
 * ```cpp
 * config_manager manager("/etc/pacs/config.yaml");
 *
 * admin_server::config admin_config;
 * admin_config.port = 8082;
 * admin_config.bind_address = "127.0.0.1";  // Localhost only
 *
 * admin_server server(manager, admin_config);
 * server.start();
 *
 * // Handle POST /admin/reload via handle_request
 * auto response = server.handle_request("POST", "/admin/reload");
 * ```
 */
class admin_server {
public:
    /**
     * @brief Server configuration
     */
    struct config {
        /** HTTP port to listen on */
        uint16_t port = 8082;

        /** Base path for admin endpoints */
        std::string base_path = "/admin";

        /** Bind address (default: localhost only for security) */
        std::string bind_address = "127.0.0.1";

        /** Connection timeout in seconds */
        int connection_timeout_seconds = 30;

        /** Maximum concurrent connections */
        size_t max_connections = 10;

        /** Enable configuration viewing (may expose sensitive data) */
        bool enable_config_view = false;

        /** List of allowed client addresses (empty = all allowed) */
        std::vector<std::string> allowed_addresses;
    };

    /**
     * @brief Constructor
     * @param manager Reference to configuration manager (must outlive server)
     */
    explicit admin_server(config_manager& manager);

    /**
     * @brief Constructor with configuration
     * @param manager Reference to configuration manager (must outlive server)
     * @param cfg Server configuration
     */
    admin_server(config_manager& manager, const config& cfg);

    /**
     * @brief Destructor - stops server if running
     */
    ~admin_server();

    // Non-copyable
    admin_server(const admin_server&) = delete;
    admin_server& operator=(const admin_server&) = delete;

    // Movable
    admin_server(admin_server&&) noexcept;
    admin_server& operator=(admin_server&&) noexcept;

    // =========================================================================
    // Server Lifecycle
    // =========================================================================

    /**
     * @brief Start the HTTP server
     *
     * @return true on success, false on failure
     */
    [[nodiscard]] bool start();

    /**
     * @brief Stop the HTTP server
     *
     * @param wait_for_connections If true, wait for active connections
     */
    void stop(bool wait_for_connections = true);

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Server Information
    // =========================================================================

    /**
     * @brief Get the port the server is listening on
     */
    [[nodiscard]] uint16_t port() const noexcept;

    /**
     * @brief Get the base path for admin endpoints
     */
    [[nodiscard]] std::string base_path() const;

    /**
     * @brief Get full URL for reload endpoint
     */
    [[nodiscard]] std::string reload_url() const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Server statistics
     */
    struct statistics {
        /** Total requests received */
        size_t total_requests = 0;

        /** Reload requests */
        size_t reload_requests = 0;

        /** Successful reloads */
        size_t successful_reloads = 0;

        /** Failed reloads */
        size_t failed_reloads = 0;

        /** Current active connections */
        size_t active_connections = 0;
    };

    /**
     * @brief Get server statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    // =========================================================================
    // Request Handling
    // =========================================================================

    /**
     * @brief Handle an admin request directly
     *
     * Useful for testing or when integrating with existing HTTP infrastructure.
     *
     * @param method HTTP method (GET, POST, etc.)
     * @param path Request path (e.g., "/admin/reload")
     * @return HTTP response
     */
    [[nodiscard]] admin_response handle_request(std::string_view method,
                                                 std::string_view path) const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::config

#endif  // PACS_BRIDGE_CONFIG_ADMIN_SERVER_H
