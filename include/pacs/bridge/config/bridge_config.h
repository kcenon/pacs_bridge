#ifndef PACS_BRIDGE_CONFIG_BRIDGE_CONFIG_H
#define PACS_BRIDGE_CONFIG_BRIDGE_CONFIG_H

/**
 * @file bridge_config.h
 * @brief Complete configuration structures for the PACS Bridge system
 *
 * Defines all configuration structures for the PACS Bridge including
 * HL7/MLLP settings, FHIR gateway configuration, pacs_system integration,
 * message routing rules, and queue management.
 *
 * Configuration Hierarchy:
 *   bridge_config (root)
 *   ├── hl7_config (listener + outbound destinations)
 *   ├── fhir_config (REST server settings)
 *   ├── pacs_config (pacs_system connection)
 *   ├── mapping_config (code translations)
 *   ├── routing_rules (message routing)
 *   ├── queue_config (message queue)
 *   ├── patient_cache_config (patient demographics cache)
 *   └── logging_config (logging settings)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/20
 * @see docs/PRD.md - FR-5.1.1 to FR-5.1.4
 */

#include "pacs/bridge/mllp/mllp_types.h"
#include "pacs/bridge/security/tls_types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::config {

// =============================================================================
// Error Codes (-750 to -759)
// =============================================================================

/**
 * @brief Configuration specific error codes
 *
 * Allocated range: -750 to -759
 * @note Relocated from -900 to -909 to resolve collision with workflow_error.
 *       See https://github.com/kcenon/pacs_bridge/issues/344
 */
enum class config_error : int {
    /** Configuration file not found */
    file_not_found = -750,

    /** Failed to parse configuration file */
    parse_error = -751,

    /** Configuration validation failed */
    validation_error = -752,

    /** Required field is missing */
    missing_required_field = -753,

    /** Invalid value for configuration field */
    invalid_value = -754,

    /** Environment variable not found */
    env_var_not_found = -755,

    /** Invalid file format (not YAML or JSON) */
    invalid_format = -756,

    /** Configuration file is empty */
    empty_config = -757,

    /** Circular include detected */
    circular_include = -758,

    /** IO error reading file */
    io_error = -759
};

/**
 * @brief Convert config_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(config_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of config error
 */
[[nodiscard]] constexpr const char* to_string(config_error error) noexcept {
    switch (error) {
        case config_error::file_not_found:
            return "Configuration file not found";
        case config_error::parse_error:
            return "Failed to parse configuration file";
        case config_error::validation_error:
            return "Configuration validation failed";
        case config_error::missing_required_field:
            return "Required configuration field is missing";
        case config_error::invalid_value:
            return "Invalid value for configuration field";
        case config_error::env_var_not_found:
            return "Environment variable not found";
        case config_error::invalid_format:
            return "Invalid configuration file format";
        case config_error::empty_config:
            return "Configuration file is empty";
        case config_error::circular_include:
            return "Circular include detected in configuration";
        case config_error::io_error:
            return "IO error reading configuration file";
        default:
            return "Unknown configuration error";
    }
}

// =============================================================================
// Validation Error Details
// =============================================================================

/**
 * @brief Detailed validation error information
 */
struct validation_error_info {
    /** Path to the configuration field (e.g., "hl7.listener.port") */
    std::string field_path;

    /** Error message describing the validation failure */
    std::string message;

    /** Actual value that failed validation (if applicable) */
    std::optional<std::string> actual_value;

    /** Expected value or constraint description */
    std::optional<std::string> expected;
};

// =============================================================================
// FHIR Server Configuration
// =============================================================================

/**
 * @brief FHIR R4 REST server configuration
 */
struct fhir_server_config {
    /** Port for FHIR REST API */
    uint16_t port = 8080;

    /** Base path for FHIR endpoints (e.g., "/fhir/r4") */
    std::string base_path = "/fhir/r4";

    /** Bind address (empty = all interfaces) */
    std::string bind_address;

    /** Maximum concurrent requests */
    size_t max_connections = 100;

    /** Request timeout */
    std::chrono::seconds request_timeout{60};

    /** Pagination page size for search results */
    size_t page_size = 100;

    /** TLS configuration */
    security::tls_config tls;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (port == 0) return false;
        if (base_path.empty()) return false;
        if (max_connections == 0) return false;
        if (page_size == 0) return false;
        if (tls.enabled && !tls.is_valid_for_server()) return false;
        return true;
    }
};

/**
 * @brief Complete FHIR gateway configuration
 */
struct fhir_config {
    /** Enable FHIR gateway */
    bool enabled = false;

    /** FHIR server settings */
    fhir_server_config server;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (!enabled) return true;  // Disabled config is always valid
        return server.is_valid();
    }
};

// =============================================================================
// pacs_system Integration Configuration
// =============================================================================

/**
 * @brief pacs_system connection configuration
 */
struct pacs_config {
    /** pacs_system hostname */
    std::string host = "localhost";

    /** pacs_system DICOM port */
    uint16_t port = 11112;

    /** Our AE title */
    std::string ae_title = "PACS_BRIDGE";

    /** Called AE title (pacs_system) */
    std::string called_ae = "PACS_SCP";

    /** Connection timeout */
    std::chrono::seconds timeout{30};

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (host.empty()) return false;
        if (port == 0) return false;
        if (ae_title.empty()) return false;
        if (called_ae.empty()) return false;
        return true;
    }
};

// =============================================================================
// HL7 Configuration (Listener + Outbound)
// =============================================================================

/**
 * @brief Outbound MLLP destination configuration
 */
struct outbound_destination {
    /** Destination name (for logging and reference) */
    std::string name;

    /** Target hostname */
    std::string host;

    /** Target port */
    uint16_t port = mllp::MLLP_DEFAULT_PORT;

    /** Message types routed to this destination */
    std::vector<std::string> message_types;

    /** Priority (lower = higher priority for failover) */
    int priority = 1;

    /** Enable this destination */
    bool enabled = true;

    /** Retry count on failure */
    size_t retry_count = 3;

    /** Retry delay */
    std::chrono::milliseconds retry_delay{1000};

    /** TLS configuration */
    security::tls_config tls;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (name.empty()) return false;
        if (host.empty()) return false;
        if (port == 0) return false;
        if (tls.enabled && !tls.is_valid_for_client()) return false;
        return true;
    }
};

/**
 * @brief Complete HL7/MLLP configuration
 */
struct hl7_config {
    /** MLLP listener configuration */
    mllp::mllp_server_config listener;

    /** Outbound destinations */
    std::vector<outbound_destination> outbound_destinations;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (!listener.is_valid()) return false;
        for (const auto& dest : outbound_destinations) {
            if (!dest.is_valid()) return false;
        }
        return true;
    }
};

// =============================================================================
// Mapping Configuration
// =============================================================================

/**
 * @brief Code system mapping configuration
 */
struct mapping_config {
    /** AE titles by modality type (e.g., "CT" -> ["CT_SCANNER_1", "CT_SCANNER_2"]) */
    std::map<std::string, std::vector<std::string>> modality_ae_titles;

    /** Procedure code to modality mapping (e.g., "CT001" -> "CT") */
    std::map<std::string, std::string> procedure_to_modality;

    /** Custom field mappings (HL7 field -> DICOM tag) */
    std::map<std::string, std::string> custom_field_mappings;

    /** Default issuer of patient ID */
    std::string default_patient_id_issuer;

    /** Validate configuration (always valid - mappings are optional) */
    [[nodiscard]] constexpr bool is_valid() const noexcept { return true; }
};

// =============================================================================
// Routing Configuration
// =============================================================================

/**
 * @brief Message routing rule
 */
struct routing_rule {
    /** Rule name (for logging and reference) */
    std::string name;

    /** Message type pattern to match (e.g., "ADT^A*", "ORM^O01") */
    std::string message_type_pattern;

    /** Source application pattern (e.g., "HIS_*") */
    std::string source_pattern;

    /** Destination handler name */
    std::string destination;

    /** Rule priority (higher = more priority) */
    int priority = 0;

    /** Rule is enabled */
    bool enabled = true;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (name.empty()) return false;
        if (message_type_pattern.empty() && source_pattern.empty())
            return false;
        if (destination.empty()) return false;
        return true;
    }
};

// =============================================================================
// Queue Configuration
// =============================================================================

/**
 * @brief Message queue configuration
 */
struct queue_config {
    /** SQLite database path for queue persistence */
    std::filesystem::path database_path = "queue.db";

    /** Maximum queue size (messages) */
    size_t max_queue_size = 50000;

    /** Maximum retry count before dead-lettering */
    size_t max_retry_count = 5;

    /** Initial retry delay */
    std::chrono::seconds initial_retry_delay{5};

    /** Retry backoff multiplier */
    double retry_backoff_multiplier = 2.0;

    /** Message time-to-live */
    std::chrono::hours message_ttl{24};

    /** Number of worker threads for delivery */
    size_t worker_count = 4;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (max_queue_size == 0) return false;
        if (max_retry_count == 0) return false;
        if (worker_count == 0) return false;
        if (retry_backoff_multiplier <= 0.0) return false;
        return true;
    }
};

// =============================================================================
// Patient Cache Configuration
// =============================================================================

/**
 * @brief Patient demographics cache configuration
 */
struct patient_cache_config {
    /** Maximum cache entries */
    size_t max_entries = 10000;

    /** Cache entry time-to-live */
    std::chrono::seconds ttl{3600};

    /** Evict entries when cache is full (LRU) */
    bool evict_on_full = true;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (max_entries == 0) return false;
        return true;
    }
};

// =============================================================================
// Logging Configuration
// =============================================================================

/**
 * @brief Log level enumeration
 */
enum class log_level { trace, debug, info, warn, error, fatal };

/**
 * @brief Get string representation of log level
 */
[[nodiscard]] constexpr const char* to_string(log_level level) noexcept {
    switch (level) {
        case log_level::trace:
            return "TRACE";
        case log_level::debug:
            return "DEBUG";
        case log_level::info:
            return "INFO";
        case log_level::warn:
            return "WARN";
        case log_level::error:
            return "ERROR";
        case log_level::fatal:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Logging configuration
 */
struct logging_config {
    /** Log level */
    log_level level = log_level::info;

    /** Log format ("json" or "text") */
    std::string format = "json";

    /** Log file path (empty = stdout only) */
    std::filesystem::path file;

    /** Maximum log file size in MB (0 = unlimited) */
    size_t max_file_size_mb = 100;

    /** Number of rotated log files to keep */
    size_t max_files = 5;

    /** Include source location in logs */
    bool include_source_location = false;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (format != "json" && format != "text") return false;
        return true;
    }
};

// =============================================================================
// Complete Bridge Configuration
// =============================================================================

/**
 * @brief Complete PACS Bridge configuration
 *
 * Root configuration structure containing all settings for the bridge system.
 *
 * @example YAML Configuration
 * ```yaml
 * server:
 *   name: "PACS_BRIDGE"
 *
 * hl7:
 *   listener:
 *     port: 2575
 *     max_connections: 50
 *   outbound:
 *     - name: "RIS"
 *       host: "ris.hospital.local"
 *       port: 2576
 *       message_types: ["ORM^O01"]
 *
 * pacs:
 *   host: "localhost"
 *   port: 11112
 *   ae_title: "PACS_BRIDGE"
 *   called_ae: "PACS_SCP"
 *
 * routing_rules:
 *   - name: "ADT to Cache"
 *     message_type_pattern: "ADT^A*"
 *     destination: "patient_cache"
 *     priority: 10
 *
 * logging:
 *   level: "INFO"
 *   format: "json"
 * ```
 */
struct bridge_config {
    /** Server instance name */
    std::string name = "PACS_BRIDGE";

    /** HL7/MLLP configuration */
    hl7_config hl7;

    /** FHIR gateway configuration */
    fhir_config fhir;

    /** pacs_system integration configuration */
    pacs_config pacs;

    /** Code mapping configuration */
    mapping_config mapping;

    /** Message routing rules */
    std::vector<routing_rule> routing_rules;

    /** Message queue configuration */
    queue_config queue;

    /** Patient cache configuration */
    patient_cache_config patient_cache;

    /** Logging configuration */
    logging_config logging;

    /**
     * @brief Validate the complete configuration
     * @return List of validation errors (empty if valid)
     */
    [[nodiscard]] std::vector<validation_error_info> validate() const;

    /**
     * @brief Check if configuration is valid
     * @return true if all sub-configurations are valid
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return validate().empty();
    }
};

}  // namespace pacs::bridge::config

#endif  // PACS_BRIDGE_CONFIG_BRIDGE_CONFIG_H
