#ifndef PACS_BRIDGE_FHIR_FHIR_SERVER_H
#define PACS_BRIDGE_FHIR_FHIR_SERVER_H

/**
 * @file fhir_server.h
 * @brief FHIR Gateway Module - REST server
 *
 * Provides FHIR R4 REST API server for EHR integration.
 *
 * @see docs/SDS_COMPONENTS.md - Section 3: FHIR Gateway Module (DES-FHIR-001)
 * @see https://github.com/kcenon/pacs_bridge/issues/31
 */

#include "fhir_types.h"
#include "fhir_resource.h"

#include <functional>
#include <memory>

namespace pacs::bridge::fhir {

/**
 * @brief Request handler callback type
 */
using request_handler = std::function<std::unique_ptr<fhir_resource>(
    interaction_type, const resource_id&, const std::string&)>;

/**
 * @brief FHIR REST server
 *
 * Provides FHIR R4 compliant REST API for:
 *   - Patient resource queries
 *   - ServiceRequest (order) management
 *   - ImagingStudy queries
 *   - DiagnosticReport creation
 *
 * Thread-safe: All operations are thread-safe.
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

    /**
     * @brief Start the FHIR server
     * @return true if started successfully
     */
    [[nodiscard]] bool start();

    /**
     * @brief Stop the FHIR server
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    /**
     * @brief Register a resource handler
     * @param type Resource type to handle
     * @param handler Handler function
     */
    void register_handler(resource_type type, request_handler handler);

    /**
     * @brief Get server configuration
     */
    [[nodiscard]] const fhir_server_config& config() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

} // namespace pacs::bridge::fhir

#endif // PACS_BRIDGE_FHIR_FHIR_SERVER_H
