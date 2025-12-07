#ifndef PACS_BRIDGE_SECURITY_AUDIT_LOGGER_H
#define PACS_BRIDGE_SECURITY_AUDIT_LOGGER_H

/**
 * @file audit_logger.h
 * @brief Healthcare-specific audit logging extending logger_system
 *
 * Provides HIPAA-compliant audit logging by extending the base audit_logger
 * from logger_system with healthcare-specific event types, transaction
 * tracking, and PHI access monitoring.
 *
 * This module wraps kcenon::logger::security::audit_logger and adds:
 *   - HL7 transaction audit events
 *   - PHI access tracking (minimal details)
 *   - DICOM/FHIR event categories
 *   - Connection and authentication events
 *   - Configurable retention for HIPAA compliance (7 years)
 *
 * HIPAA Audit Requirements (45 CFR 164.312):
 *   - Access attempts (successful and failed)
 *   - PHI access (read, write, delete)
 *   - Security incidents
 *   - User activity
 *   - System events
 *
 * @see kcenon::logger::security::audit_logger (base implementation)
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declare base audit_logger
namespace kcenon::logger::security {
class audit_logger;
}

namespace pacs::bridge::security {

// =============================================================================
// Healthcare Audit Event Types
// =============================================================================

/**
 * @brief Categories of healthcare audit events
 */
enum class healthcare_audit_category {
    /** System startup, shutdown, configuration */
    system,

    /** Authentication and authorization */
    security,

    /** HL7 message processing */
    hl7_transaction,

    /** FHIR API operations */
    fhir_transaction,

    /** DICOM operations */
    dicom_transaction,

    /** PHI access events */
    phi_access,

    /** Error and exception events */
    error,

    /** Network connectivity events */
    network,

    /** Configuration changes */
    configuration
};

/**
 * @brief Specific healthcare audit event types
 */
enum class healthcare_audit_event {
    // System events
    system_start,
    system_stop,
    config_load,
    config_change,

    // Security events
    auth_attempt,
    auth_success,
    auth_failure,
    access_denied,
    certificate_validation,
    tls_handshake,
    rate_limit_exceeded,
    ip_blocked,
    ip_whitelisted,

    // HL7 transaction events
    hl7_message_received,
    hl7_message_sent,
    hl7_message_processed,
    hl7_message_rejected,
    hl7_ack_sent,
    hl7_nak_sent,
    hl7_validation_failed,

    // FHIR transaction events
    fhir_request_received,
    fhir_response_sent,
    fhir_resource_created,
    fhir_resource_updated,
    fhir_resource_deleted,
    fhir_search_executed,

    // PHI access events
    phi_accessed,
    phi_created,
    phi_modified,
    phi_deleted,
    phi_exported,
    phi_query,

    // Network events
    connection_opened,
    connection_closed,
    connection_rejected,

    // Error events
    validation_error,
    processing_error,
    connection_error,
    timeout_error
};

/**
 * @brief Audit event severity levels
 */
enum class audit_severity {
    /** Informational event */
    info,

    /** Warning - potential issue */
    warning,

    /** Error - failure occurred */
    error,

    /** Critical - security incident */
    critical,

    /** Emergency - immediate action required */
    emergency
};

/**
 * @brief Convert audit types to strings
 */
[[nodiscard]] const char* to_string(healthcare_audit_category category) noexcept;
[[nodiscard]] const char* to_string(healthcare_audit_event event) noexcept;
[[nodiscard]] const char* to_string(audit_severity severity) noexcept;

// =============================================================================
// Healthcare Audit Event
// =============================================================================

/**
 * @brief Healthcare audit log event record
 *
 * Contains all information for a single audit log entry with
 * healthcare-specific context.
 */
struct healthcare_audit_event_record {
    /** Event timestamp (UTC) */
    std::chrono::system_clock::time_point timestamp;

    /** Unique event identifier */
    std::string event_id;

    /** Event category */
    healthcare_audit_category category = healthcare_audit_category::system;

    /** Specific event type */
    healthcare_audit_event type = healthcare_audit_event::system_start;

    /** Event severity */
    audit_severity severity = audit_severity::info;

    /** Human-readable event description */
    std::string description;

    /** Source component generating the event */
    std::string source_component;

    // ----- Session/Connection Context -----

    /** Session identifier (if applicable) */
    std::optional<uint64_t> session_id;

    /** Remote address (may be masked for privacy) */
    std::optional<std::string> remote_address;

    /** Remote port */
    std::optional<uint16_t> remote_port;

    /** TLS enabled for connection */
    std::optional<bool> tls_enabled;

    /** Client certificate subject (if mTLS) */
    std::optional<std::string> client_cert_subject;

    // ----- HL7 Message Context -----

    /** HL7 message control ID (MSH-10) */
    std::optional<std::string> message_control_id;

    /** HL7 message type (MSH-9) */
    std::optional<std::string> message_type;

    /** Sending application (MSH-3) */
    std::optional<std::string> sending_application;

    /** Sending facility (MSH-4) */
    std::optional<std::string> sending_facility;

    /** Message size in bytes */
    std::optional<size_t> message_size;

    // ----- Outcome -----

    /** Operation outcome: success, failure, unknown */
    std::string outcome = "unknown";

    /** Error code if failed */
    std::optional<int> error_code;

    /** Error message if failed */
    std::optional<std::string> error_message;

    /** Processing duration in milliseconds */
    std::optional<double> processing_time_ms;

    // ----- Additional Context -----

    /** Additional key-value properties */
    std::unordered_map<std::string, std::string> properties;

    /**
     * @brief Serialize event to JSON string
     */
    [[nodiscard]] std::string to_json() const;
};

// =============================================================================
// Healthcare Audit Configuration
// =============================================================================

/**
 * @brief Healthcare audit logging configuration
 */
struct healthcare_audit_config {
    /** Enable audit logging */
    bool enabled = true;

    /** Audit log file path */
    std::filesystem::path log_path = "audit/healthcare_audit.log";

    /** Minimum severity level to log */
    audit_severity min_severity = audit_severity::info;

    /** Categories to include (empty = all) */
    std::unordered_set<healthcare_audit_category> categories;

    /** Maximum log file size before rotation (bytes) */
    size_t max_file_size = 100 * 1024 * 1024;  // 100MB

    /** Number of rotated files to keep */
    size_t max_rotated_files = 10;

    /** Retention period for audit logs (HIPAA: 6-7 years) */
    std::chrono::hours retention_period{24 * 365 * 7};  // 7 years

    /** Enable HMAC integrity verification */
    bool integrity_verification = true;

    /** HMAC key path (optional, auto-generated if not provided) */
    std::optional<std::filesystem::path> hmac_key_path;

    /** Mask IP addresses in logs */
    bool mask_ip_addresses = false;

    /** Include processing time metrics */
    bool include_timing = true;

    /** Log HL7 message types (but not content) */
    bool log_message_types = true;
};

// =============================================================================
// Healthcare Audit Logger
// =============================================================================

/**
 * @brief HIPAA-compliant healthcare audit logger
 *
 * Extends the base audit_logger with healthcare-specific functionality
 * for HIPAA compliance and HL7/FHIR/DICOM transaction auditing.
 *
 * @example Basic Usage
 * ```cpp
 * healthcare_audit_config config;
 * config.log_path = "/var/log/pacs_bridge/audit.log";
 * config.integrity_verification = true;
 *
 * healthcare_audit_logger logger(config);
 * logger.start();
 *
 * // Log HL7 transaction
 * logger.log_hl7_received("ADT^A01", "MSG001", "PACS", 1024, session_id);
 * logger.log_hl7_processed("MSG001", true, 15.5);
 * ```
 *
 * @example Builder Pattern
 * ```cpp
 * logger.log_event(healthcare_audit_category::security,
 *                  healthcare_audit_event::auth_failure)
 *       .severity(audit_severity::warning)
 *       .description("TLS handshake failed")
 *       .session(session_id)
 *       .remote_address("192.168.1.100")
 *       .error(-995, "Certificate verification failed")
 *       .commit();
 * ```
 */
class healthcare_audit_logger {
public:
    /**
     * @brief Event builder for fluent API
     */
    class event_builder {
    public:
        event_builder(healthcare_audit_logger& logger,
                      healthcare_audit_category category,
                      healthcare_audit_event type);

        event_builder& severity(audit_severity sev);
        event_builder& description(std::string_view desc);
        event_builder& source(std::string_view component);
        event_builder& session(uint64_t id);
        event_builder& remote_address(std::string_view addr, uint16_t port = 0);
        event_builder& tls(bool enabled, std::string_view cert_subject = "");
        event_builder& message(std::string_view control_id,
                               std::string_view msg_type = "",
                               std::string_view sending_app = "",
                               std::string_view sending_facility = "");
        event_builder& message_size(size_t bytes);
        event_builder& outcome(std::string_view result);
        event_builder& error(int code, std::string_view message);
        event_builder& processing_time(double ms);
        event_builder& property(std::string_view key, std::string_view value);

        void commit();

    private:
        healthcare_audit_logger& logger_;
        healthcare_audit_event_record event_;
    };

    /**
     * @brief Constructor with configuration
     * @param config Audit configuration
     */
    explicit healthcare_audit_logger(const healthcare_audit_config& config = {});

    /**
     * @brief Destructor - flushes pending logs
     */
    ~healthcare_audit_logger();

    // Non-copyable, movable
    healthcare_audit_logger(const healthcare_audit_logger&) = delete;
    healthcare_audit_logger& operator=(const healthcare_audit_logger&) = delete;
    healthcare_audit_logger(healthcare_audit_logger&&) noexcept;
    healthcare_audit_logger& operator=(healthcare_audit_logger&&) noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Start the audit logger
     * @return true if started successfully
     */
    [[nodiscard]] bool start();

    /**
     * @brief Stop the audit logger
     */
    void stop();

    /**
     * @brief Check if logger is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    /**
     * @brief Flush pending log entries
     */
    void flush();

    // =========================================================================
    // General Logging
    // =========================================================================

    /**
     * @brief Log an audit event
     */
    void log(const healthcare_audit_event_record& event);

    /**
     * @brief Begin building an event with fluent API
     */
    [[nodiscard]] event_builder log_event(healthcare_audit_category category,
                                          healthcare_audit_event type);

    // =========================================================================
    // HL7 Transaction Logging
    // =========================================================================

    /**
     * @brief Log HL7 message received
     */
    void log_hl7_received(std::string_view message_type,
                          std::string_view control_id,
                          std::string_view sending_app,
                          size_t message_size,
                          uint64_t session_id = 0);

    /**
     * @brief Log HL7 message processed
     */
    void log_hl7_processed(std::string_view control_id,
                           bool success,
                           double processing_time_ms,
                           std::optional<int> error_code = std::nullopt);

    /**
     * @brief Log HL7 ACK/NAK sent
     */
    void log_hl7_response(std::string_view control_id,
                          bool ack,
                          std::string_view ack_code = "AA");

    /**
     * @brief Log HL7 validation failure
     */
    void log_hl7_validation_failed(std::string_view control_id,
                                   std::string_view reason,
                                   std::string_view field = "");

    // =========================================================================
    // Security Event Logging
    // =========================================================================

    /**
     * @brief Log authentication attempt
     */
    void log_auth_attempt(std::string_view remote_address,
                          bool success,
                          std::string_view method = "TLS",
                          std::string_view details = "");

    /**
     * @brief Log access denied
     */
    void log_access_denied(std::string_view remote_address,
                           std::string_view reason,
                           uint64_t session_id = 0);

    /**
     * @brief Log rate limit exceeded
     */
    void log_rate_limited(std::string_view remote_address,
                          std::string_view limit_type,
                          uint64_t session_id = 0);

    /**
     * @brief Log security violation
     */
    void log_security_violation(audit_severity severity,
                                std::string_view description,
                                std::string_view remote_address = "",
                                uint64_t session_id = 0);

    // =========================================================================
    // System Event Logging
    // =========================================================================

    /**
     * @brief Log system startup
     */
    void log_system_start(std::string_view version = "",
                          std::string_view config_path = "");

    /**
     * @brief Log system shutdown
     */
    void log_system_stop(std::string_view reason = "normal");

    /**
     * @brief Log configuration change
     */
    void log_config_change(std::string_view component,
                           std::string_view setting,
                           std::string_view old_value,
                           std::string_view new_value);

    // =========================================================================
    // Network Event Logging
    // =========================================================================

    /**
     * @brief Log connection opened
     */
    void log_connection_opened(std::string_view remote_address,
                               uint16_t remote_port,
                               uint64_t session_id,
                               bool tls_enabled = false);

    /**
     * @brief Log connection closed
     */
    void log_connection_closed(uint64_t session_id,
                               std::string_view reason = "normal");

    /**
     * @brief Log connection rejected
     */
    void log_connection_rejected(std::string_view remote_address,
                                 std::string_view reason);

    // =========================================================================
    // Integrity Verification
    // =========================================================================

    /**
     * @brief Verify integrity of audit log file
     * @param log_file Path to log file (default: current log)
     * @return true if integrity verified
     */
    [[nodiscard]] bool verify_integrity(
        const std::filesystem::path& log_file = "") const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Audit logging statistics
     */
    struct statistics {
        size_t events_logged = 0;
        size_t hl7_transactions = 0;
        size_t security_events = 0;
        size_t error_events = 0;
        size_t bytes_written = 0;
        std::chrono::system_clock::time_point started_at;
        std::chrono::system_clock::time_point last_event_at;
    };

    /**
     * @brief Get logging statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const healthcare_audit_config& config() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Global Healthcare Audit Logger
// =============================================================================

/**
 * @brief Get the global healthcare audit logger instance
 */
healthcare_audit_logger& global_healthcare_audit_logger();

/**
 * @brief Initialize the global healthcare audit logger
 */
void init_global_healthcare_audit_logger(const healthcare_audit_config& config);

/**
 * @brief Shutdown the global healthcare audit logger
 */
void shutdown_global_healthcare_audit_logger();

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_AUDIT_LOGGER_H
