#ifndef PACS_BRIDGE_EMR_EMR_ADAPTER_H
#define PACS_BRIDGE_EMR_EMR_ADAPTER_H

/**
 * @file emr_adapter.h
 * @brief Abstract EMR adapter interface for vendor-specific EMR integration
 *
 * Defines a common interface for EMR adapters that can be implemented
 * for different EMR vendors (Epic, Cerner, generic FHIR R4, etc.).
 * This abstraction allows PACS Bridge to work with multiple EMR systems
 * through a unified API.
 *
 * Features:
 *   - Vendor-agnostic interface for patient lookup
 *   - Result posting to EMR
 *   - Encounter context retrieval
 *   - Health check and connection management
 *   - Factory pattern for adapter creation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/107
 * @see https://github.com/kcenon/pacs_bridge/issues/121
 */

#include "emr_types.h"
#include "encounter_context.h"
#include "patient_lookup.h"
#include "patient_record.h"
#include "result_poster.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::emr {

// =============================================================================
// EMR Adapter Error Codes (-1100 to -1119)
// =============================================================================

/**
 * @brief EMR adapter specific error codes
 *
 * Allocated range: -1100 to -1119
 */
enum class adapter_error : int {
    /** Adapter not initialized */
    not_initialized = -1100,

    /** Connection to EMR failed */
    connection_failed = -1101,

    /** Authentication failed */
    authentication_failed = -1102,

    /** Operation not supported by this adapter */
    not_supported = -1103,

    /** Invalid adapter configuration */
    invalid_configuration = -1104,

    /** Adapter operation timed out */
    timeout = -1105,

    /** Rate limited by EMR */
    rate_limited = -1106,

    /** Invalid vendor type */
    invalid_vendor = -1107,

    /** Health check failed */
    health_check_failed = -1108,

    /** Feature not available */
    feature_unavailable = -1109
};

/**
 * @brief Convert adapter_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(adapter_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of adapter error
 */
[[nodiscard]] constexpr const char* to_string(adapter_error error) noexcept {
    switch (error) {
        case adapter_error::not_initialized:
            return "EMR adapter not initialized";
        case adapter_error::connection_failed:
            return "Connection to EMR failed";
        case adapter_error::authentication_failed:
            return "EMR authentication failed";
        case adapter_error::not_supported:
            return "Operation not supported by this adapter";
        case adapter_error::invalid_configuration:
            return "Invalid adapter configuration";
        case adapter_error::timeout:
            return "EMR operation timed out";
        case adapter_error::rate_limited:
            return "Rate limited by EMR system";
        case adapter_error::invalid_vendor:
            return "Invalid EMR vendor type";
        case adapter_error::health_check_failed:
            return "EMR health check failed";
        case adapter_error::feature_unavailable:
            return "Feature not available in this adapter";
        default:
            return "Unknown adapter error";
    }
}

/**
 * @brief Convert adapter_error to error_info for Result<T>
 *
 * @param error Adapter error code
 * @param details Optional additional details
 * @return error_info for use with Result<T>
 */
[[nodiscard]] inline error_info to_error_info(
    adapter_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "emr.adapter",
        details
    };
}

// =============================================================================
// EMR Vendor Types
// =============================================================================

/**
 * @brief Supported EMR vendor types
 */
enum class emr_vendor {
    /** Generic FHIR R4 compliant EMR (default) */
    generic_fhir,

    /** Epic EMR (Epic FHIR R4 with extensions) */
    epic,

    /** Cerner/Oracle Health */
    cerner,

    /** MEDITECH Expanse */
    meditech,

    /** Allscripts */
    allscripts,

    /** Unknown/custom vendor */
    unknown
};

/**
 * @brief Convert emr_vendor to string
 */
[[nodiscard]] constexpr std::string_view to_string(emr_vendor vendor) noexcept {
    switch (vendor) {
        case emr_vendor::generic_fhir:
            return "generic";
        case emr_vendor::epic:
            return "epic";
        case emr_vendor::cerner:
            return "cerner";
        case emr_vendor::meditech:
            return "meditech";
        case emr_vendor::allscripts:
            return "allscripts";
        case emr_vendor::unknown:
        default:
            return "unknown";
    }
}

/**
 * @brief Parse emr_vendor from string
 */
[[nodiscard]] emr_vendor parse_emr_vendor(std::string_view vendor_str) noexcept;

// =============================================================================
// Adapter Feature Flags
// =============================================================================

/**
 * @brief Features that an adapter may support
 */
struct adapter_features {
    /** Supports patient lookup by MRN */
    bool patient_lookup{true};

    /** Supports patient search (name, DOB, etc.) */
    bool patient_search{true};

    /** Supports posting DiagnosticReport */
    bool result_posting{true};

    /** Supports result status updates */
    bool result_updates{true};

    /** Supports encounter context retrieval */
    bool encounter_context{true};

    /** Supports ImagingStudy resource */
    bool imaging_study{true};

    /** Supports ServiceRequest resource */
    bool service_request{true};

    /** Supports bulk data export */
    bool bulk_export{false};

    /** Supports SMART on FHIR */
    bool smart_on_fhir{true};

    /** Supports OAuth2 client credentials */
    bool oauth2_client_credentials{true};

    /** Supports basic authentication */
    bool basic_auth{true};
};

// =============================================================================
// Adapter Configuration
// =============================================================================

/**
 * @brief Configuration for EMR adapter
 */
struct emr_adapter_config {
    /** EMR vendor type */
    emr_vendor vendor{emr_vendor::generic_fhir};

    /** FHIR server base URL */
    std::string base_url;

    /** Authentication type (oauth2, basic, smart) */
    std::string auth_type{"oauth2"};

    /** OAuth2 client ID (if applicable) */
    std::optional<std::string> client_id;

    /** OAuth2 client secret (if applicable) */
    std::optional<std::string> client_secret;

    /** OAuth2 token URL (if applicable) */
    std::optional<std::string> token_url;

    /** OAuth2 scopes (if applicable) */
    std::vector<std::string> scopes;

    /** Basic auth username (if applicable) */
    std::optional<std::string> username;

    /** Basic auth password (if applicable) */
    std::optional<std::string> password;

    /** Connection timeout */
    std::chrono::seconds timeout{30};

    /** Default identifier system for MRN */
    std::optional<std::string> mrn_system;

    /** Organization identifier */
    std::optional<std::string> organization_id;

    /** Enable strict FHIR validation */
    bool strict_mode{false};

    /** Retry policy */
    retry_policy retry;

    // Vendor-specific configuration

    /** Epic: non-production environment flag */
    bool epic_non_production{false};

    /** Cerner: tenant ID */
    std::optional<std::string> cerner_tenant_id;

    /**
     * @brief Validate configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (base_url.empty()) {
            return false;
        }
        if (auth_type == "oauth2") {
            if (!client_id.has_value() || !token_url.has_value()) {
                return false;
            }
        } else if (auth_type == "basic") {
            if (!username.has_value()) {
                return false;
            }
        }
        return true;
    }
};

// =============================================================================
// Adapter Health Status
// =============================================================================

/**
 * @brief Health status of an EMR adapter
 */
struct adapter_health_status {
    /** Whether the adapter is healthy */
    bool healthy{false};

    /** Connection to EMR server established */
    bool connected{false};

    /** Authentication is valid */
    bool authenticated{false};

    /** Last successful health check time */
    std::optional<std::chrono::system_clock::time_point> last_check;

    /** Error message if unhealthy */
    std::optional<std::string> error_message;

    /** Response time of last health check */
    std::chrono::milliseconds response_time{0};

    /** FHIR server version (if available) */
    std::optional<std::string> server_version;

    /** Supported FHIR resources (from CapabilityStatement) */
    std::vector<std::string> supported_resources;
};

// =============================================================================
// EMR Adapter Interface
// =============================================================================

/**
 * @brief Abstract interface for EMR adapters
 *
 * Provides a vendor-agnostic interface for EMR operations.
 * Concrete implementations handle vendor-specific details.
 *
 * Thread-safe: All operations are thread-safe for concurrent use.
 *
 * @example Basic Usage
 * ```cpp
 * // Create adapter using factory
 * emr_adapter_config config;
 * config.vendor = emr_vendor::generic_fhir;
 * config.base_url = "https://emr.hospital.local/fhir";
 * config.auth_type = "oauth2";
 * config.client_id = "pacs_bridge";
 * config.token_url = "https://emr.hospital.local/oauth/token";
 *
 * auto adapter = create_emr_adapter(config);
 *
 * // Query patient
 * auto query = patient_query::by_mrn("MRN12345");
 * auto result = adapter->query_patient(query);
 * if (result) {
 *     std::cout << "Patient: " << result->family_name() << "\n";
 * }
 *
 * // Post result
 * study_result sr;
 * sr.study_instance_uid = "1.2.3.4.5";
 * sr.patient_id = "MRN12345";
 * sr.modality = "CT";
 * sr.study_datetime = "2025-01-15T10:30:00Z";
 *
 * auto post = adapter->post_result(sr);
 * ```
 */
class emr_adapter {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~emr_adapter() = default;

    // =========================================================================
    // Identification
    // =========================================================================

    /**
     * @brief Get the vendor type of this adapter
     */
    [[nodiscard]] virtual emr_vendor vendor() const noexcept = 0;

    /**
     * @brief Get the vendor name as string
     */
    [[nodiscard]] virtual std::string_view vendor_name() const noexcept = 0;

    /**
     * @brief Get the adapter version
     */
    [[nodiscard]] virtual std::string_view version() const noexcept = 0;

    /**
     * @brief Get supported features
     */
    [[nodiscard]] virtual adapter_features features() const noexcept = 0;

    // =========================================================================
    // Connection Management
    // =========================================================================

    /**
     * @brief Initialize the adapter
     *
     * Must be called before using other operations.
     *
     * @return Success or error
     */
    [[nodiscard]] virtual VoidResult initialize() = 0;

    /**
     * @brief Shutdown the adapter
     *
     * Releases resources and closes connections.
     */
    virtual void shutdown() noexcept = 0;

    /**
     * @brief Check if the adapter is initialized
     */
    [[nodiscard]] virtual bool is_initialized() const noexcept = 0;

    /**
     * @brief Check if the adapter is connected
     */
    [[nodiscard]] virtual bool is_connected() const noexcept = 0;

    // =========================================================================
    // Health Check
    // =========================================================================

    /**
     * @brief Perform health check
     *
     * @return Health status or error
     */
    [[nodiscard]] virtual Result<adapter_health_status>
    health_check() = 0;

    /**
     * @brief Get current health status (cached)
     */
    [[nodiscard]] virtual adapter_health_status
    get_health_status() const noexcept = 0;

    // =========================================================================
    // Patient Operations
    // =========================================================================

    /**
     * @brief Query patient by various criteria
     *
     * @param query Patient query parameters
     * @return Patient record or error
     */
    [[nodiscard]] virtual Result<patient_record>
    query_patient(const patient_query& query) = 0;

    /**
     * @brief Search for patients matching criteria
     *
     * @param query Search query
     * @return List of matching patients or error
     */
    [[nodiscard]] virtual Result<std::vector<patient_match>>
    search_patients(const patient_query& query) = 0;

    // =========================================================================
    // Result Operations
    // =========================================================================

    /**
     * @brief Post imaging result to EMR
     *
     * Creates a DiagnosticReport resource in the EMR.
     *
     * @param result Study result data
     * @return Posted result reference or error
     */
    [[nodiscard]] virtual Result<posted_result>
    post_result(const study_result& result) = 0;

    /**
     * @brief Update existing result in EMR
     *
     * @param report_id DiagnosticReport resource ID
     * @param result Updated result data
     * @return Success or error
     */
    [[nodiscard]] virtual VoidResult
    update_result(std::string_view report_id,
                  const study_result& result) = 0;

    // =========================================================================
    // Encounter Operations
    // =========================================================================

    /**
     * @brief Get encounter by ID
     *
     * @param encounter_id Encounter resource ID
     * @return Encounter info or error
     */
    [[nodiscard]] virtual Result<encounter_info>
    get_encounter(std::string_view encounter_id) = 0;

    /**
     * @brief Find active encounter for patient
     *
     * @param patient_id Patient ID or reference
     * @return Active encounter (if exists) or error
     */
    [[nodiscard]] virtual Result<std::optional<encounter_info>>
    find_active_encounter(std::string_view patient_id) = 0;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] virtual const emr_adapter_config& config() const noexcept = 0;

    /**
     * @brief Update configuration
     *
     * May require re-initialization.
     *
     * @param config New configuration
     * @return Success or error
     */
    [[nodiscard]] virtual VoidResult
    set_config(const emr_adapter_config& config) = 0;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Adapter statistics
     */
    struct statistics {
        size_t total_requests{0};
        size_t successful_requests{0};
        size_t failed_requests{0};
        size_t patient_queries{0};
        size_t result_posts{0};
        size_t encounter_queries{0};
        std::chrono::milliseconds total_request_time{0};
        std::chrono::milliseconds avg_response_time{0};
    };

    /**
     * @brief Get adapter statistics
     */
    [[nodiscard]] virtual statistics get_statistics() const noexcept = 0;

    /**
     * @brief Reset statistics
     */
    virtual void reset_statistics() noexcept = 0;

protected:
    // Prevent direct instantiation
    emr_adapter() = default;
    emr_adapter(const emr_adapter&) = default;
    emr_adapter& operator=(const emr_adapter&) = default;
    emr_adapter(emr_adapter&&) = default;
    emr_adapter& operator=(emr_adapter&&) = default;
};

// =============================================================================
// Factory Function
// =============================================================================

/**
 * @brief Create an EMR adapter based on configuration
 *
 * Factory function that creates the appropriate adapter implementation
 * based on the vendor type specified in the configuration.
 *
 * @param config Adapter configuration
 * @return Unique pointer to adapter or error
 *
 * @example
 * ```cpp
 * emr_adapter_config config;
 * config.vendor = emr_vendor::generic_fhir;
 * config.base_url = "https://fhir.example.com";
 *
 * auto result = create_emr_adapter(config);
 * if (result) {
 *     auto& adapter = *result;
 *     adapter->initialize();
 * }
 * ```
 */
[[nodiscard]] Result<std::unique_ptr<emr_adapter>>
create_emr_adapter(const emr_adapter_config& config);

/**
 * @brief Create an EMR adapter with specific vendor type
 *
 * @param vendor Vendor type
 * @param base_url FHIR server base URL
 * @return Unique pointer to adapter or error
 */
[[nodiscard]] Result<std::unique_ptr<emr_adapter>>
create_emr_adapter(emr_vendor vendor, std::string_view base_url);

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_EMR_ADAPTER_H
