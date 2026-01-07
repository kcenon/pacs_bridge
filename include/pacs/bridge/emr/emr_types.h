#ifndef PACS_BRIDGE_EMR_EMR_TYPES_H
#define PACS_BRIDGE_EMR_EMR_TYPES_H

/**
 * @file emr_types.h
 * @brief EMR Client Module - Type definitions and error codes
 *
 * Defines error codes, configuration structures, and common types
 * for FHIR R4 client integration with external EMR systems.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 * @see docs/api/error-codes.md - Error code allocation
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Result<T> pattern - use stub for standalone builds
#ifdef PACS_BRIDGE_STANDALONE_BUILD
#include <pacs/bridge/internal/result_stub.h>
#else
#include <kcenon/common/patterns/result.h>
#endif

namespace pacs::bridge::emr {

// =============================================================================
// Result Type Aliases
// =============================================================================

/**
 * @brief Result type alias for EMR operations
 */
template<typename T>
using Result = kcenon::common::Result<T>;

/**
 * @brief VoidResult type alias for operations with no return value
 */
using VoidResult = kcenon::common::VoidResult;

/**
 * @brief Error info type alias
 */
using error_info = kcenon::common::error_info;

// =============================================================================
// Error Codes (-1000 to -1019)
// =============================================================================

/**
 * @brief EMR client specific error codes
 *
 * Allocated range: -1000 to -1019
 * @see docs/api/error-codes.md for error code allocation
 */
enum class emr_error : int {
    /** Connection to EMR server failed */
    connection_failed = -1000,

    /** Request timed out */
    timeout = -1001,

    /** Invalid or malformed response from EMR server */
    invalid_response = -1002,

    /** Requested resource was not found (HTTP 404) */
    resource_not_found = -1003,

    /** Authentication failed (HTTP 401) */
    unauthorized = -1004,

    /** Rate limit exceeded (HTTP 429) */
    rate_limited = -1005,

    /** Server returned an error (HTTP 5xx) */
    server_error = -1006,

    /** Invalid FHIR resource format */
    invalid_resource = -1007,

    /** Network error during request */
    network_error = -1008,

    /** TLS/SSL error */
    tls_error = -1009,

    /** Invalid configuration */
    invalid_configuration = -1010,

    /** Resource validation failed */
    validation_failed = -1011,

    /** Conflict error (HTTP 409) */
    conflict = -1012,

    /** Gone - resource has been deleted (HTTP 410) */
    gone = -1013,

    /** Forbidden (HTTP 403) */
    forbidden = -1014,

    /** Bad request (HTTP 400) */
    bad_request = -1015,

    /** Operation not supported */
    not_supported = -1016,

    /** Retry limit exceeded */
    retry_exhausted = -1017,

    /** Request was cancelled */
    cancelled = -1018,

    /** Unknown error */
    unknown = -1019
};

/**
 * @brief Convert emr_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(emr_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of EMR error
 */
[[nodiscard]] constexpr const char* to_string(emr_error error) noexcept {
    switch (error) {
        case emr_error::connection_failed:
            return "Connection to EMR server failed";
        case emr_error::timeout:
            return "Request timed out";
        case emr_error::invalid_response:
            return "Invalid response from EMR server";
        case emr_error::resource_not_found:
            return "Resource not found";
        case emr_error::unauthorized:
            return "Authentication failed";
        case emr_error::rate_limited:
            return "Rate limit exceeded";
        case emr_error::server_error:
            return "EMR server error";
        case emr_error::invalid_resource:
            return "Invalid FHIR resource format";
        case emr_error::network_error:
            return "Network error";
        case emr_error::tls_error:
            return "TLS/SSL error";
        case emr_error::invalid_configuration:
            return "Invalid configuration";
        case emr_error::validation_failed:
            return "Resource validation failed";
        case emr_error::conflict:
            return "Resource conflict";
        case emr_error::gone:
            return "Resource has been deleted";
        case emr_error::forbidden:
            return "Access forbidden";
        case emr_error::bad_request:
            return "Bad request";
        case emr_error::not_supported:
            return "Operation not supported";
        case emr_error::retry_exhausted:
            return "Retry limit exceeded";
        case emr_error::cancelled:
            return "Request cancelled";
        case emr_error::unknown:
            return "Unknown error";
        default:
            return "Unknown EMR error";
    }
}

/**
 * @brief Convert emr_error to error_info for Result<T>
 *
 * @param error EMR error code
 * @param details Optional additional details
 * @return error_info for use with Result<T>
 */
[[nodiscard]] inline error_info to_error_info(
    emr_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "emr",
        details
    };
}

// =============================================================================
// FHIR Content Types
// =============================================================================

/**
 * @brief FHIR content types for HTTP headers
 */
enum class fhir_content_type {
    /** application/fhir+json (preferred) */
    json,

    /** application/fhir+xml */
    xml
};

/**
 * @brief Convert fhir_content_type to MIME type string
 */
[[nodiscard]] constexpr std::string_view to_mime_type(
    fhir_content_type type) noexcept {
    switch (type) {
        case fhir_content_type::json:
            return "application/fhir+json";
        case fhir_content_type::xml:
            return "application/fhir+xml";
        default:
            return "application/fhir+json";
    }
}

// =============================================================================
// FHIR Resource Types
// =============================================================================

/**
 * @brief Common FHIR resource types
 */
enum class fhir_resource_type {
    patient,
    service_request,
    imaging_study,
    diagnostic_report,
    practitioner,
    organization,
    encounter,
    observation,
    condition,
    procedure,
    medication_request,
    bundle,
    operation_outcome,
    capability_statement,
    unknown
};

/**
 * @brief Convert resource type to FHIR resource type name
 */
[[nodiscard]] constexpr std::string_view to_string(
    fhir_resource_type type) noexcept {
    switch (type) {
        case fhir_resource_type::patient:
            return "Patient";
        case fhir_resource_type::service_request:
            return "ServiceRequest";
        case fhir_resource_type::imaging_study:
            return "ImagingStudy";
        case fhir_resource_type::diagnostic_report:
            return "DiagnosticReport";
        case fhir_resource_type::practitioner:
            return "Practitioner";
        case fhir_resource_type::organization:
            return "Organization";
        case fhir_resource_type::encounter:
            return "Encounter";
        case fhir_resource_type::observation:
            return "Observation";
        case fhir_resource_type::condition:
            return "Condition";
        case fhir_resource_type::procedure:
            return "Procedure";
        case fhir_resource_type::medication_request:
            return "MedicationRequest";
        case fhir_resource_type::bundle:
            return "Bundle";
        case fhir_resource_type::operation_outcome:
            return "OperationOutcome";
        case fhir_resource_type::capability_statement:
            return "CapabilityStatement";
        case fhir_resource_type::unknown:
            return "Unknown";
        default:
            return "Unknown";
    }
}

/**
 * @brief Parse FHIR resource type from string
 */
[[nodiscard]] std::optional<fhir_resource_type> parse_resource_type(
    std::string_view type_str) noexcept;

// =============================================================================
// HTTP Method
// =============================================================================

/**
 * @brief HTTP methods used by FHIR REST API
 */
enum class http_method {
    get,
    post,
    put,
    patch,
    delete_method
};

/**
 * @brief Convert HTTP method to string
 */
[[nodiscard]] constexpr std::string_view to_string(http_method method) noexcept {
    switch (method) {
        case http_method::get:
            return "GET";
        case http_method::post:
            return "POST";
        case http_method::put:
            return "PUT";
        case http_method::patch:
            return "PATCH";
        case http_method::delete_method:
            return "DELETE";
        default:
            return "GET";
    }
}

// =============================================================================
// HTTP Status Codes
// =============================================================================

/**
 * @brief HTTP status codes commonly returned by FHIR servers
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
    too_many_requests = 429,

    // 5xx Server Error
    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503,
    gateway_timeout = 504
};

/**
 * @brief Check if status code indicates success (2xx)
 */
[[nodiscard]] constexpr bool is_success(http_status status) noexcept {
    auto code = static_cast<int>(status);
    return code >= 200 && code < 300;
}

/**
 * @brief Check if status code indicates client error (4xx)
 */
[[nodiscard]] constexpr bool is_client_error(http_status status) noexcept {
    auto code = static_cast<int>(status);
    return code >= 400 && code < 500;
}

/**
 * @brief Check if status code indicates server error (5xx)
 */
[[nodiscard]] constexpr bool is_server_error(http_status status) noexcept {
    auto code = static_cast<int>(status);
    return code >= 500 && code < 600;
}

/**
 * @brief Convert HTTP status to emr_error
 */
[[nodiscard]] constexpr emr_error status_to_error(http_status status) noexcept {
    switch (status) {
        case http_status::bad_request:
            return emr_error::bad_request;
        case http_status::unauthorized:
            return emr_error::unauthorized;
        case http_status::forbidden:
            return emr_error::forbidden;
        case http_status::not_found:
            return emr_error::resource_not_found;
        case http_status::conflict:
            return emr_error::conflict;
        case http_status::gone:
            return emr_error::gone;
        case http_status::too_many_requests:
            return emr_error::rate_limited;
        case http_status::internal_server_error:
        case http_status::bad_gateway:
        case http_status::service_unavailable:
        case http_status::gateway_timeout:
            return emr_error::server_error;
        default:
            return emr_error::unknown;
    }
}

// =============================================================================
// Configuration Types
// =============================================================================

/**
 * @brief Retry policy configuration
 */
struct retry_policy {
    /** Maximum number of retry attempts */
    size_t max_retries{3};

    /** Initial backoff duration */
    std::chrono::milliseconds initial_backoff{1000};

    /** Maximum backoff duration */
    std::chrono::milliseconds max_backoff{30000};

    /** Backoff multiplier for exponential backoff */
    double backoff_multiplier{2.0};

    /**
     * @brief Calculate backoff duration for a given attempt
     * @param attempt Attempt number (0-based)
     * @return Backoff duration
     */
    [[nodiscard]] std::chrono::milliseconds backoff_for(
        size_t attempt) const noexcept {
        if (attempt >= max_retries) {
            return max_backoff;
        }
        auto ms = static_cast<double>(initial_backoff.count());
        for (size_t i = 0; i < attempt; ++i) {
            ms *= backoff_multiplier;
        }
        auto result = static_cast<int64_t>(ms);
        if (result > max_backoff.count()) {
            return max_backoff;
        }
        return std::chrono::milliseconds{result};
    }
};

/**
 * @brief FHIR client configuration
 *
 * Contains all settings needed for connecting to a FHIR R4 server.
 *
 * @example Basic Configuration
 * ```cpp
 * fhir_client_config config;
 * config.base_url = "https://emr.hospital.local/fhir";
 * config.timeout = std::chrono::seconds{30};
 * config.max_connections = 10;
 * config.content_type = fhir_content_type::json;
 *
 * if (!config.is_valid()) {
 *     // Handle invalid configuration
 * }
 * ```
 */
struct fhir_client_config {
    /** FHIR server base URL (e.g., "https://emr.hospital.local/fhir") */
    std::string base_url;

    /** Request timeout duration */
    std::chrono::seconds timeout{30};

    /** Maximum number of concurrent connections */
    size_t max_connections{10};

    /** Whether to verify SSL/TLS certificates */
    bool verify_ssl{true};

    /** Preferred content type for requests/responses */
    fhir_content_type content_type{fhir_content_type::json};

    /** Retry policy for failed requests */
    retry_policy retry;

    /** User-Agent header value */
    std::string user_agent{"PACS-Bridge/1.0"};

    /** Optional: Path to CA certificate bundle */
    std::optional<std::string> ca_cert_path;

    /** Optional: Path to client certificate */
    std::optional<std::string> client_cert_path;

    /** Optional: Path to client private key */
    std::optional<std::string> client_key_path;

    /**
     * @brief Validate the configuration
     * @return true if configuration is valid
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (base_url.empty()) {
            return false;
        }
        if (timeout.count() <= 0) {
            return false;
        }
        if (max_connections == 0) {
            return false;
        }
        return true;
    }

    /**
     * @brief Get full URL for a resource path
     * @param path Resource path (e.g., "/Patient/123")
     * @return Complete URL
     */
    [[nodiscard]] std::string url_for(std::string_view path) const {
        std::string url = base_url;
        // Ensure no double slashes
        if (!url.empty() && url.back() == '/' && !path.empty() &&
            path.front() == '/') {
            url.pop_back();
        }
        url.append(path);
        return url;
    }
};

// =============================================================================
// HTTP Request/Response Types
// =============================================================================

/**
 * @brief HTTP request for FHIR operations
 */
struct http_request {
    /** HTTP method */
    http_method method{http_method::get};

    /** Request URL */
    std::string url;

    /** Request headers */
    std::vector<std::pair<std::string, std::string>> headers;

    /** Request body (for POST/PUT/PATCH) */
    std::string body;

    /** Request timeout */
    std::chrono::seconds timeout{30};

    /**
     * @brief Add a header to the request
     */
    void add_header(std::string name, std::string value) {
        headers.emplace_back(std::move(name), std::move(value));
    }
};

/**
 * @brief HTTP response from FHIR server
 */
struct http_response {
    /** HTTP status code */
    http_status status{http_status::ok};

    /** Response headers */
    std::vector<std::pair<std::string, std::string>> headers;

    /** Response body */
    std::string body;

    /**
     * @brief Check if response was successful
     */
    [[nodiscard]] bool is_success() const noexcept {
        return pacs::bridge::emr::is_success(status);
    }

    /**
     * @brief Get header value by name (case-insensitive)
     * @param name Header name
     * @return Header value or nullopt if not found
     */
    [[nodiscard]] std::optional<std::string_view> get_header(
        std::string_view name) const noexcept;

    /**
     * @brief Get Location header (for created resources)
     */
    [[nodiscard]] std::optional<std::string_view> location() const noexcept {
        return get_header("Location");
    }

    /**
     * @brief Get ETag header (for version awareness)
     */
    [[nodiscard]] std::optional<std::string_view> etag() const noexcept {
        return get_header("ETag");
    }
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_EMR_TYPES_H
