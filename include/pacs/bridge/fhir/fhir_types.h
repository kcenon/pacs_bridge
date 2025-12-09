#ifndef PACS_BRIDGE_FHIR_FHIR_TYPES_H
#define PACS_BRIDGE_FHIR_FHIR_TYPES_H

/**
 * @file fhir_types.h
 * @brief FHIR Gateway Module - Type definitions
 *
 * Defines types and enumerations for FHIR R4 resource handling.
 *
 * @see docs/SDS_COMPONENTS.md - Section 3: FHIR Gateway Module
 * @see https://github.com/kcenon/pacs_bridge/issues/31
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::fhir {

// =============================================================================
// Error Codes (-800 to -849)
// =============================================================================

/**
 * @brief FHIR module specific error codes
 *
 * Allocated range: -800 to -849
 */
enum class fhir_error : int {
    /** Invalid FHIR resource */
    invalid_resource = -800,

    /** Resource not found */
    resource_not_found = -801,

    /** Resource validation failed */
    validation_failed = -802,

    /** Unsupported resource type */
    unsupported_resource_type = -803,

    /** Server error */
    server_error = -804,

    /** Subscription error */
    subscription_error = -805,

    /** JSON parsing error */
    json_parse_error = -806,

    /** Missing required field */
    missing_required_field = -807
};

/**
 * @brief Convert fhir_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(fhir_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief FHIR resource types supported by the gateway
 */
enum class resource_type {
    patient,
    service_request,
    imaging_study,
    diagnostic_report,
    practitioner,
    organization,
    endpoint,
    unknown
};

/**
 * @brief FHIR interaction types
 */
enum class interaction_type {
    read,
    vread,
    update,
    patch,
    delete_resource,
    history_instance,
    history_type,
    create,
    search,
    capabilities
};

/**
 * @brief FHIR resource identifier
 */
struct resource_id {
    resource_type type = resource_type::unknown;
    std::string id;
    std::optional<std::string> version_id;
};

/**
 * @brief FHIR server configuration
 */
struct fhir_server_config {
    std::string host = "0.0.0.0";
    uint16_t port = 8080;
    std::string base_path = "/fhir";
    bool enable_tls = false;
    std::chrono::seconds request_timeout{30};
    size_t max_bundle_size = 100;
};

} // namespace pacs::bridge::fhir

#endif // PACS_BRIDGE_FHIR_FHIR_TYPES_H
