#ifndef PACS_BRIDGE_CONFIG_EMR_CONFIG_H
#define PACS_BRIDGE_CONFIG_EMR_CONFIG_H

/**
 * @file emr_config.h
 * @brief EMR integration configuration for PACS Bridge
 *
 * Defines configuration structures for EMR/FHIR client integration including
 * connection settings, authentication, feature flags, and caching.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/109
 * @see https://github.com/kcenon/pacs_bridge/issues/101
 */

#include "pacs/bridge/security/oauth2_types.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::config {

// =============================================================================
// EMR Error Codes (-1100 to -1119)
// =============================================================================

/**
 * @brief EMR configuration specific error codes
 *
 * Allocated range: -1100 to -1119
 */
enum class emr_config_error : int {
    /** General configuration invalid */
    config_invalid = -1100,

    /** Missing required base URL */
    missing_url = -1101,

    /** Invalid authentication configuration */
    invalid_auth = -1102,

    /** Missing required credentials */
    missing_credentials = -1103,

    /** Invalid timeout value */
    invalid_timeout = -1104,

    /** Invalid vendor type */
    invalid_vendor = -1105,

    /** Invalid retry configuration */
    invalid_retry = -1106,

    /** Invalid cache configuration */
    invalid_cache = -1107,

    /** Environment variable not found */
    env_var_not_found = -1108
};

/**
 * @brief Convert emr_config_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(emr_config_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of EMR config error
 */
[[nodiscard]] constexpr const char* to_string(emr_config_error error) noexcept {
    switch (error) {
        case emr_config_error::config_invalid:
            return "EMR configuration is invalid";
        case emr_config_error::missing_url:
            return "Missing required EMR base URL";
        case emr_config_error::invalid_auth:
            return "Invalid authentication configuration";
        case emr_config_error::missing_credentials:
            return "Missing required authentication credentials";
        case emr_config_error::invalid_timeout:
            return "Invalid timeout value";
        case emr_config_error::invalid_vendor:
            return "Invalid EMR vendor type";
        case emr_config_error::invalid_retry:
            return "Invalid retry configuration";
        case emr_config_error::invalid_cache:
            return "Invalid cache configuration";
        case emr_config_error::env_var_not_found:
            return "Required environment variable not found";
        default:
            return "Unknown EMR configuration error";
    }
}

// =============================================================================
// EMR Vendor Types
// =============================================================================

/**
 * @brief Supported EMR vendor types
 *
 * Determines which vendor-specific adapter to use.
 */
enum class emr_vendor {
    /** Generic FHIR R4 (default) */
    generic,

    /** Epic Systems */
    epic,

    /** Cerner/Oracle Health */
    cerner
};

/**
 * @brief Convert emr_vendor to string
 */
[[nodiscard]] constexpr const char* to_string(emr_vendor vendor) noexcept {
    switch (vendor) {
        case emr_vendor::generic:
            return "generic";
        case emr_vendor::epic:
            return "epic";
        case emr_vendor::cerner:
            return "cerner";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse emr_vendor from string
 * @param str Vendor string
 * @return Parsed vendor or nullopt if invalid
 */
[[nodiscard]] inline std::optional<emr_vendor>
parse_emr_vendor(std::string_view str) noexcept {
    if (str == "generic" || str == "Generic" || str == "GENERIC") {
        return emr_vendor::generic;
    }
    if (str == "epic" || str == "Epic" || str == "EPIC") {
        return emr_vendor::epic;
    }
    if (str == "cerner" || str == "Cerner" || str == "CERNER") {
        return emr_vendor::cerner;
    }
    return std::nullopt;
}

// =============================================================================
// Connection Configuration
// =============================================================================

/**
 * @brief EMR connection settings
 */
struct emr_connection_config {
    /** FHIR server base URL (e.g., "https://emr.hospital.local/fhir/r4") */
    std::string base_url;

    /** Request timeout */
    std::chrono::seconds timeout{30};

    /** Maximum concurrent connections */
    size_t max_connections{10};

    /** Verify SSL certificate */
    bool verify_ssl{true};

    /** Keep-alive timeout for connections */
    std::chrono::seconds keepalive_timeout{60};

    /**
     * @brief Validate connection configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (base_url.empty()) return false;
        if (timeout.count() <= 0) return false;
        if (max_connections == 0) return false;
        return true;
    }
};

// =============================================================================
// Authentication Configuration
// =============================================================================

/**
 * @brief OAuth2 authentication settings for EMR
 */
struct emr_oauth2_config {
    /** Token endpoint URL */
    std::string token_url;

    /** Client identifier */
    std::string client_id;

    /** Client secret */
    std::string client_secret;

    /** Requested scopes */
    std::vector<std::string> scopes;

    /** Time margin before token expiration to trigger refresh */
    std::chrono::seconds token_refresh_margin{60};

    /**
     * @brief Validate OAuth2 configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (token_url.empty()) return false;
        if (client_id.empty()) return false;
        if (client_secret.empty()) return false;
        return true;
    }

    /**
     * @brief Convert to security::oauth2_config
     */
    [[nodiscard]] security::oauth2_config to_oauth2_config() const {
        security::oauth2_config config;
        config.token_url = token_url;
        config.client_id = client_id;
        config.client_secret = client_secret;
        config.scopes = scopes;
        config.token_refresh_margin = token_refresh_margin;
        return config;
    }
};

/**
 * @brief Basic authentication settings for EMR
 */
struct emr_basic_auth_config {
    /** Username */
    std::string username;

    /** Password */
    std::string password;

    /**
     * @brief Validate basic auth configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !username.empty() && !password.empty();
    }
};

/**
 * @brief API key authentication settings for EMR
 */
struct emr_api_key_config {
    /** Header name for API key */
    std::string header_name{"X-API-Key"};

    /** API key value */
    std::string key;

    /**
     * @brief Validate API key configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !header_name.empty() && !key.empty();
    }
};

/**
 * @brief Combined authentication configuration
 */
struct emr_auth_config {
    /** Authentication type */
    security::auth_type type{security::auth_type::oauth2};

    /** OAuth2 settings (when type == oauth2) */
    emr_oauth2_config oauth2;

    /** Basic auth settings (when type == basic) */
    emr_basic_auth_config basic;

    /** API key settings (when type == api_key) */
    emr_api_key_config api_key;

    /**
     * @brief Validate authentication configuration based on type
     */
    [[nodiscard]] bool is_valid() const noexcept {
        switch (type) {
            case security::auth_type::none:
                return true;
            case security::auth_type::basic:
                return basic.is_valid();
            case security::auth_type::oauth2:
                return oauth2.is_valid();
            case security::auth_type::api_key:
                return api_key.is_valid();
            default:
                return false;
        }
    }
};

// =============================================================================
// Feature Flags
// =============================================================================

/**
 * @brief EMR feature flags
 *
 * Enable/disable specific EMR integration features.
 */
struct emr_features_config {
    /** Enable patient demographics lookup from EMR */
    bool patient_lookup{true};

    /** Enable posting DiagnosticReport results to EMR */
    bool result_posting{true};

    /** Enable encounter context retrieval */
    bool encounter_context{true};

    /** Enable automatic retry on transient failures */
    bool auto_retry{true};

    /** Enable caching of patient/encounter data */
    bool caching{true};
};

// =============================================================================
// Retry Configuration
// =============================================================================

/**
 * @brief Retry settings for EMR requests
 */
struct emr_retry_config {
    /** Maximum number of retry attempts */
    size_t max_attempts{3};

    /** Initial backoff duration */
    std::chrono::milliseconds initial_backoff{1000};

    /** Maximum backoff duration */
    std::chrono::milliseconds max_backoff{30000};

    /** Backoff multiplier for exponential backoff */
    double backoff_multiplier{2.0};

    /**
     * @brief Validate retry configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (max_attempts == 0) return false;
        if (initial_backoff.count() <= 0) return false;
        if (max_backoff.count() < initial_backoff.count()) return false;
        if (backoff_multiplier <= 0.0) return false;
        return true;
    }

    /**
     * @brief Calculate backoff for given attempt number
     * @param attempt Attempt number (0-indexed)
     * @return Backoff duration
     */
    [[nodiscard]] std::chrono::milliseconds
    calculate_backoff(size_t attempt) const noexcept {
        if (attempt == 0) return initial_backoff;

        double multiplier = 1.0;
        for (size_t i = 0; i < attempt; ++i) {
            multiplier *= backoff_multiplier;
        }

        auto backoff = std::chrono::milliseconds(
            static_cast<int64_t>(initial_backoff.count() * multiplier));

        return std::min(backoff, max_backoff);
    }
};

// =============================================================================
// Cache Configuration
// =============================================================================

/**
 * @brief Cache settings for EMR data
 */
struct emr_cache_config {
    /** Patient data TTL */
    std::chrono::seconds patient_ttl{300};

    /** Encounter data TTL */
    std::chrono::seconds encounter_ttl{60};

    /** Maximum cache entries */
    size_t max_entries{10000};

    /** Enable LRU eviction when cache is full */
    bool evict_on_full{true};

    /**
     * @brief Validate cache configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (patient_ttl.count() < 0) return false;
        if (encounter_ttl.count() < 0) return false;
        if (max_entries == 0) return false;
        return true;
    }
};

// =============================================================================
// Mapping Configuration
// =============================================================================

/**
 * @brief EMR mapping and identifier configuration
 */
struct emr_mapping_config {
    /** Patient ID system URI (e.g., "urn:oid:2.16.840.1.113883.4.1") */
    std::string patient_id_system;

    /** Default performer reference for results */
    std::string default_performer_id{"Practitioner/default"};

    /** Accession number system URI */
    std::string accession_number_system;

    /** Organization reference for result author */
    std::string organization_id;
};

// =============================================================================
// Complete EMR Configuration
// =============================================================================

/**
 * @brief Complete EMR integration configuration
 *
 * @example YAML Configuration
 * ```yaml
 * emr:
 *   enabled: true
 *   vendor: "generic"
 *
 *   connection:
 *     base_url: "https://emr.hospital.local/fhir/r4"
 *     timeout_seconds: 30
 *     max_connections: 10
 *     verify_ssl: true
 *
 *   auth:
 *     type: "oauth2"
 *     oauth2:
 *       token_url: "https://emr.hospital.local/oauth/token"
 *       client_id: "${EMR_CLIENT_ID}"
 *       client_secret: "${EMR_CLIENT_SECRET}"
 *       scopes:
 *         - "patient/*.read"
 *         - "patient/DiagnosticReport.write"
 *       token_refresh_margin_seconds: 60
 *
 *   features:
 *     patient_lookup: true
 *     result_posting: true
 *     encounter_context: true
 *     auto_retry: true
 *
 *   retry:
 *     max_attempts: 3
 *     initial_backoff_ms: 1000
 *     max_backoff_ms: 30000
 *     backoff_multiplier: 2.0
 *
 *   cache:
 *     patient_ttl_seconds: 300
 *     encounter_ttl_seconds: 60
 *     max_entries: 10000
 *
 *   mapping:
 *     patient_id_system: "urn:oid:2.16.840.1.113883.4.1"
 *     default_performer_id: "Practitioner/default"
 * ```
 */
struct emr_config {
    /** Enable/disable EMR integration */
    bool enabled{false};

    /** EMR vendor type */
    emr_vendor vendor{emr_vendor::generic};

    /** Connection settings */
    emr_connection_config connection;

    /** Authentication settings */
    emr_auth_config auth;

    /** Feature flags */
    emr_features_config features;

    /** Retry settings */
    emr_retry_config retry;

    /** Cache settings */
    emr_cache_config cache;

    /** Mapping settings */
    emr_mapping_config mapping;

    /**
     * @brief Validate the complete EMR configuration
     * @return true if configuration is valid
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (!enabled) return true;  // Disabled config is always valid

        if (!connection.is_valid()) return false;
        if (!auth.is_valid()) return false;
        if (!retry.is_valid()) return false;
        if (!cache.is_valid()) return false;

        return true;
    }

    /**
     * @brief Get list of validation errors
     * @return List of error messages (empty if valid)
     */
    [[nodiscard]] std::vector<std::string> validate() const;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Create default EMR configuration
 * @return Default configuration with sensible values
 */
[[nodiscard]] inline emr_config default_emr_config() noexcept {
    emr_config config;
    config.enabled = false;
    config.vendor = emr_vendor::generic;
    return config;
}

/**
 * @brief Substitute environment variables in string
 *
 * Replaces ${VAR_NAME} patterns with environment variable values.
 *
 * @param str String containing environment variable references
 * @return String with substituted values
 */
[[nodiscard]] std::string substitute_env_vars(std::string_view str);

/**
 * @brief Apply environment variable substitution to EMR config
 *
 * Processes all string fields that may contain ${VAR_NAME} patterns.
 *
 * @param config Configuration to process
 * @return Configuration with substituted values
 */
[[nodiscard]] emr_config apply_env_substitution(const emr_config& config);

}  // namespace pacs::bridge::config

#endif  // PACS_BRIDGE_CONFIG_EMR_CONFIG_H
