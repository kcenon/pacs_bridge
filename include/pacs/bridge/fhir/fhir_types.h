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
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::fhir {

// =============================================================================
// HTTP Types
// =============================================================================

/**
 * @brief HTTP methods supported by FHIR REST API
 */
enum class http_method {
    get,
    post,
    put,
    patch,
    delete_method,
    head,
    options
};

/**
 * @brief Convert HTTP method to string
 */
[[nodiscard]] constexpr std::string_view to_string(http_method method) noexcept {
    switch (method) {
        case http_method::get: return "GET";
        case http_method::post: return "POST";
        case http_method::put: return "PUT";
        case http_method::patch: return "PATCH";
        case http_method::delete_method: return "DELETE";
        case http_method::head: return "HEAD";
        case http_method::options: return "OPTIONS";
    }
    return "UNKNOWN";
}

/**
 * @brief Parse HTTP method from string
 */
[[nodiscard]] std::optional<http_method> parse_http_method(
    std::string_view method_str) noexcept;

/**
 * @brief FHIR content types for content negotiation
 */
enum class content_type {
    fhir_json,      // application/fhir+json
    fhir_xml,       // application/fhir+xml
    json,           // application/json
    xml,            // application/xml
    unknown
};

/**
 * @brief Convert content_type to MIME type string
 */
[[nodiscard]] constexpr std::string_view to_mime_type(
    content_type type) noexcept {
    switch (type) {
        case content_type::fhir_json: return "application/fhir+json";
        case content_type::fhir_xml: return "application/fhir+xml";
        case content_type::json: return "application/json";
        case content_type::xml: return "application/xml";
        case content_type::unknown: return "application/octet-stream";
    }
    return "application/octet-stream";
}

/**
 * @brief Parse content type from Accept header or Content-Type header
 */
[[nodiscard]] content_type parse_content_type(std::string_view header) noexcept;

/**
 * @brief HTTP status codes used by FHIR REST API
 */
enum class http_status : int {
    // 2xx Success
    ok = 200,
    created = 201,
    no_content = 204,

    // 3xx Redirection
    not_modified = 304,

    // 4xx Client Error
    bad_request = 400,
    unauthorized = 401,
    forbidden = 403,
    not_found = 404,
    method_not_allowed = 405,
    not_acceptable = 406,
    conflict = 409,
    gone = 410,
    precondition_failed = 412,
    unprocessable_entity = 422,

    // 5xx Server Error
    internal_server_error = 500,
    not_implemented = 501,
    service_unavailable = 503
};

/**
 * @brief Convert http_status to integer
 */
[[nodiscard]] constexpr int to_int(http_status status) noexcept {
    return static_cast<int>(status);
}

/**
 * @brief Get reason phrase for HTTP status
 */
[[nodiscard]] std::string_view get_reason_phrase(http_status status) noexcept;

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
 * @brief Get human-readable description of FHIR error
 */
[[nodiscard]] constexpr const char* to_string(fhir_error error) noexcept {
    switch (error) {
        case fhir_error::invalid_resource:
            return "Invalid FHIR resource";
        case fhir_error::resource_not_found:
            return "Resource not found";
        case fhir_error::validation_failed:
            return "Resource validation failed";
        case fhir_error::unsupported_resource_type:
            return "Unsupported resource type";
        case fhir_error::server_error:
            return "Server error";
        case fhir_error::subscription_error:
            return "Subscription error";
        case fhir_error::json_parse_error:
            return "JSON parsing error";
        case fhir_error::missing_required_field:
            return "Missing required field";
        default:
            return "Unknown FHIR error";
    }
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
    subscription,
    operation_outcome,
    bundle,
    capability_statement,
    unknown
};

/**
 * @brief Convert resource_type to FHIR resource type name
 */
[[nodiscard]] constexpr std::string_view to_string(
    resource_type type) noexcept {
    switch (type) {
        case resource_type::patient: return "Patient";
        case resource_type::service_request: return "ServiceRequest";
        case resource_type::imaging_study: return "ImagingStudy";
        case resource_type::diagnostic_report: return "DiagnosticReport";
        case resource_type::practitioner: return "Practitioner";
        case resource_type::organization: return "Organization";
        case resource_type::endpoint: return "Endpoint";
        case resource_type::subscription: return "Subscription";
        case resource_type::operation_outcome: return "OperationOutcome";
        case resource_type::bundle: return "Bundle";
        case resource_type::capability_statement: return "CapabilityStatement";
        case resource_type::unknown: return "Unknown";
    }
    return "Unknown";
}

/**
 * @brief Parse resource type from string
 */
[[nodiscard]] std::optional<resource_type> parse_resource_type(
    std::string_view type_str) noexcept;

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
    std::string fhir_version = "4.0.1";  // FHIR R4
    bool enable_tls = false;
    std::chrono::seconds request_timeout{30};
    size_t max_bundle_size = 100;
    size_t default_page_size = 20;
    size_t max_page_size = 100;
    size_t max_connections = 100;
    bool enable_cors = false;
    std::vector<std::string> cors_origins;
};

// =============================================================================
// HTTP Request/Response Types
// =============================================================================

/**
 * @brief HTTP request structure for FHIR endpoints
 */
struct http_request {
    http_method method = http_method::get;
    std::string path;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
    std::string body;
    content_type accept = content_type::fhir_json;
    content_type content = content_type::fhir_json;
};

/**
 * @brief HTTP response structure for FHIR endpoints
 */
struct http_response {
    http_status status = http_status::ok;
    std::map<std::string, std::string> headers;
    std::string body;
    content_type content = content_type::fhir_json;

    /**
     * @brief Create a 200 OK response with JSON body
     */
    [[nodiscard]] static http_response ok(std::string json_body) {
        http_response resp;
        resp.status = http_status::ok;
        resp.body = std::move(json_body);
        resp.content = content_type::fhir_json;
        return resp;
    }

    /**
     * @brief Create a 201 Created response with Location header
     */
    [[nodiscard]] static http_response created(std::string json_body,
                                               std::string location) {
        http_response resp;
        resp.status = http_status::created;
        resp.body = std::move(json_body);
        resp.content = content_type::fhir_json;
        resp.headers["Location"] = std::move(location);
        return resp;
    }

    /**
     * @brief Create a 204 No Content response
     */
    [[nodiscard]] static http_response no_content() {
        http_response resp;
        resp.status = http_status::no_content;
        return resp;
    }

    /**
     * @brief Create an error response with OperationOutcome
     */
    [[nodiscard]] static http_response error(http_status code,
                                             std::string outcome_json) {
        http_response resp;
        resp.status = code;
        resp.body = std::move(outcome_json);
        resp.content = content_type::fhir_json;
        return resp;
    }
};

// =============================================================================
// Pagination Types
// =============================================================================

/**
 * @brief Pagination parameters for search results
 */
struct pagination_params {
    size_t offset = 0;
    size_t count = 20;
    std::optional<std::string> cursor;
};

/**
 * @brief Bundle link for pagination
 */
struct bundle_link {
    std::string relation;  // "self", "first", "next", "previous", "last"
    std::string url;
};

} // namespace pacs::bridge::fhir

#endif // PACS_BRIDGE_FHIR_FHIR_TYPES_H
