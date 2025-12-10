#ifndef PACS_BRIDGE_FHIR_FHIR_SERVER_H
#define PACS_BRIDGE_FHIR_FHIR_SERVER_H

/**
 * @file fhir_server.h
 * @brief FHIR Gateway Module - REST server
 *
 * Provides FHIR R4 REST API server for EHR integration.
 *
 * Endpoints:
 *   GET    /fhir/r4/{ResourceType}           - Search resources
 *   GET    /fhir/r4/{ResourceType}/{id}      - Read resource by ID
 *   POST   /fhir/r4/{ResourceType}           - Create resource
 *   PUT    /fhir/r4/{ResourceType}/{id}      - Update resource
 *   DELETE /fhir/r4/{ResourceType}/{id}      - Delete resource
 *   GET    /fhir/r4/metadata                 - CapabilityStatement
 *
 * @see docs/SDS_COMPONENTS.md - Section 3: FHIR Gateway Module (DES-FHIR-001)
 * @see https://github.com/kcenon/pacs_bridge/issues/31
 */

#include "fhir_resource.h"
#include "fhir_types.h"
#include "resource_handler.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::fhir {

// Forward declaration
class operation_outcome;

// =============================================================================
// URL Router
// =============================================================================

/**
 * @brief Parsed route information from a FHIR request URL
 */
struct parsed_route {
    /** Whether the URL was successfully parsed */
    bool valid = false;

    /** Interaction type determined from HTTP method and path */
    interaction_type interaction = interaction_type::read;

    /** Resource type (if applicable) */
    resource_type type = resource_type::unknown;

    /** Resource type name string */
    std::string type_name;

    /** Resource ID (for read/update/delete) */
    std::optional<std::string> resource_id;

    /** Version ID (for vread) */
    std::optional<std::string> version_id;

    /** Compartment (e.g., Patient/{id}/Condition) */
    std::optional<std::string> compartment;

    /** Operation name (for $operation) */
    std::optional<std::string> operation;
};

/**
 * @brief Parse a FHIR request URL
 *
 * @param method HTTP method
 * @param path Request path (e.g., "/fhir/r4/Patient/123")
 * @param base_path Server base path (e.g., "/fhir/r4")
 * @return Parsed route information
 */
[[nodiscard]] parsed_route parse_fhir_route(
    http_method method, std::string_view path, std::string_view base_path);

// =============================================================================
// Server Statistics
// =============================================================================

/**
 * @brief FHIR server statistics
 */
struct server_statistics {
    /** Total requests received */
    size_t total_requests = 0;

    /** Requests by interaction type */
    size_t read_requests = 0;
    size_t search_requests = 0;
    size_t create_requests = 0;
    size_t update_requests = 0;
    size_t delete_requests = 0;

    /** Error counts */
    size_t client_errors = 0;  // 4xx
    size_t server_errors = 0;  // 5xx

    /** Current active connections */
    size_t active_connections = 0;

    /** Average response time in milliseconds */
    double avg_response_time_ms = 0.0;
};

// =============================================================================
// FHIR Server
// =============================================================================

/**
 * @brief FHIR R4 REST server
 *
 * Provides FHIR R4 compliant REST API for:
 *   - Patient resource queries
 *   - ServiceRequest (order) management
 *   - ImagingStudy queries
 *   - DiagnosticReport creation
 *
 * Features:
 *   - Content negotiation (JSON/XML)
 *   - Pagination for search results
 *   - OperationOutcome for error responses
 *   - CapabilityStatement endpoint
 *
 * Thread-safe: All operations are thread-safe.
 *
 * @example
 * ```cpp
 * // Create and configure server
 * fhir_server_config config;
 * config.port = 8080;
 * config.base_path = "/fhir/r4";
 *
 * fhir_server server(config);
 *
 * // Register resource handlers
 * server.register_handler(std::make_shared<patient_handler>());
 * server.register_handler(std::make_shared<service_request_handler>());
 *
 * // Start server
 * if (!server.start()) {
 *     // Handle error
 * }
 *
 * // Server runs until stopped
 * server.stop();
 * ```
 */
class fhir_server {
public:
    /**
     * @brief Construct FHIR server with configuration
     * @param config Server configuration
     */
    explicit fhir_server(const fhir_server_config& config);

    ~fhir_server();

    // Non-copyable, movable
    fhir_server(const fhir_server&) = delete;
    fhir_server& operator=(const fhir_server&) = delete;
    fhir_server(fhir_server&&) noexcept;
    fhir_server& operator=(fhir_server&&) noexcept;

    // =========================================================================
    // Server Lifecycle
    // =========================================================================

    /**
     * @brief Start the FHIR server
     *
     * Binds to the configured port and starts accepting connections.
     * Returns immediately; server runs in background threads.
     *
     * @return true if started successfully, false on error
     */
    [[nodiscard]] bool start();

    /**
     * @brief Stop the FHIR server
     *
     * Gracefully stops the server, waiting for active requests to complete.
     *
     * @param wait_for_requests If true, wait for in-flight requests
     */
    void stop(bool wait_for_requests = true);

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Handler Registration
    // =========================================================================

    /**
     * @brief Register a resource handler
     *
     * @param handler Handler for a specific resource type
     * @return true if registered, false if type already has a handler
     */
    bool register_handler(std::shared_ptr<resource_handler> handler);

    /**
     * @brief Get the handler registry
     */
    [[nodiscard]] const handler_registry& handlers() const noexcept;

    // =========================================================================
    // Request Handling
    // =========================================================================

    /**
     * @brief Handle an HTTP request
     *
     * Routes the request to the appropriate handler and returns a response.
     * This method is primarily for testing or when integrating with an
     * existing HTTP server.
     *
     * @param request HTTP request
     * @return HTTP response
     */
    [[nodiscard]] http_response handle_request(const http_request& request);

    // =========================================================================
    // Server Information
    // =========================================================================

    /**
     * @brief Get server configuration
     */
    [[nodiscard]] const fhir_server_config& config() const noexcept;

    /**
     * @brief Get server statistics
     */
    [[nodiscard]] server_statistics get_statistics() const;

    /**
     * @brief Get the server's base URL
     *
     * @return Base URL (e.g., "http://localhost:8080/fhir/r4")
     */
    [[nodiscard]] std::string base_url() const;

    /**
     * @brief Get the CapabilityStatement for this server
     *
     * @return CapabilityStatement as JSON string
     */
    [[nodiscard]] std::string capability_statement() const;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// Content Negotiation Utilities
// =============================================================================

/**
 * @brief Parse Accept header to determine response format
 *
 * @param accept_header Accept header value
 * @return Preferred content type (defaults to fhir_json)
 */
[[nodiscard]] content_type negotiate_content_type(
    std::string_view accept_header) noexcept;

/**
 * @brief Check if a content type is acceptable for FHIR
 *
 * @param type Content type to check
 * @return true if acceptable, false otherwise
 */
[[nodiscard]] bool is_fhir_content_type(content_type type) noexcept;

/**
 * @brief Serialize a FHIR resource to the specified format
 *
 * @param resource Resource to serialize
 * @param type Target content type
 * @return Serialized resource string
 */
[[nodiscard]] std::string serialize_resource(
    const fhir_resource& resource, content_type type);

// =============================================================================
// Pagination Utilities
// =============================================================================

/**
 * @brief Parse pagination parameters from query string
 *
 * @param params Query parameters
 * @param config Server configuration (for defaults)
 * @return Parsed pagination parameters
 */
[[nodiscard]] pagination_params parse_pagination(
    const std::map<std::string, std::string>& params,
    const fhir_server_config& config);

/**
 * @brief Create Bundle JSON for search results
 *
 * @param result Search result
 * @param base_url Server base URL
 * @param resource_type Resource type being searched
 * @return Bundle JSON string
 */
[[nodiscard]] std::string create_search_bundle(
    const search_result& result, std::string_view base_url,
    std::string_view resource_type);

}  // namespace pacs::bridge::fhir

#endif  // PACS_BRIDGE_FHIR_FHIR_SERVER_H
