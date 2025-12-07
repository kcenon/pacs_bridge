#ifndef PACS_BRIDGE_SECURITY_LOG_SANITIZER_H
#define PACS_BRIDGE_SECURITY_LOG_SANITIZER_H

/**
 * @file log_sanitizer.h
 * @brief Healthcare-specific log sanitization extending logger_system
 *
 * Provides healthcare-specific extensions to the base log_sanitizer from
 * logger_system. Adds PHI (Protected Health Information) detection and
 * masking capabilities for HIPAA compliance.
 *
 * This module wraps kcenon::logger::security::log_sanitizer and adds:
 *   - HL7 segment-aware PHI detection
 *   - Patient identifier masking (MRN, DOB, etc.)
 *   - Healthcare-specific field recognition
 *   - Configurable masking for different PHI types
 *
 * @see kcenon::logger::security::log_sanitizer (base implementation)
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Forward declare base sanitizer to avoid including full header
namespace kcenon::logger::security {
class log_sanitizer;
}

namespace pacs::bridge::security {

// =============================================================================
// PHI Field Types (Healthcare-Specific)
// =============================================================================

/**
 * @brief Types of PHI fields that can be detected and masked
 *
 * Extends the base sensitive_data_type with healthcare-specific fields.
 */
enum class phi_field_type {
    /** Patient name (PID-5) */
    patient_name,

    /** Patient ID / MRN (PID-3) */
    patient_id,

    /** Date of birth (PID-7) */
    date_of_birth,

    /** Social Security Number (PID-19) */
    ssn,

    /** Phone number (PID-13, PID-14) */
    phone_number,

    /** Address (PID-11) */
    address,

    /** Email address */
    email,

    /** Account number (PID-18) */
    account_number,

    /** Insurance ID */
    insurance_id,

    /** Generic date (non-year portion) */
    date,

    /** IP address */
    ip_address,

    /** Credit card number */
    credit_card,

    /** Custom PHI field */
    custom
};

/**
 * @brief Convert phi_field_type to string
 */
[[nodiscard]] constexpr const char* to_string(phi_field_type type) noexcept {
    switch (type) {
        case phi_field_type::patient_name:
            return "PATIENT_NAME";
        case phi_field_type::patient_id:
            return "PATIENT_ID";
        case phi_field_type::date_of_birth:
            return "DOB";
        case phi_field_type::ssn:
            return "SSN";
        case phi_field_type::phone_number:
            return "PHONE";
        case phi_field_type::address:
            return "ADDRESS";
        case phi_field_type::email:
            return "EMAIL";
        case phi_field_type::account_number:
            return "ACCOUNT";
        case phi_field_type::insurance_id:
            return "INSURANCE_ID";
        case phi_field_type::date:
            return "DATE";
        case phi_field_type::ip_address:
            return "IP_ADDRESS";
        case phi_field_type::credit_card:
            return "CREDIT_CARD";
        case phi_field_type::custom:
            return "CUSTOM";
        default:
            return "UNKNOWN";
    }
}

// =============================================================================
// Masking Configuration
// =============================================================================

/**
 * @brief Masking style for PHI redaction
 */
enum class masking_style {
    /** Replace with asterisks: "John" -> "****" */
    asterisks,

    /** Replace with field type label: "John" -> "[PATIENT_NAME]" */
    type_label,

    /** Replace with X characters: "John" -> "XXXX" */
    x_characters,

    /** Partial mask (show first/last): "1234567890" -> "123****890" */
    partial,

    /** Complete removal: "John" -> "" */
    remove
};

/**
 * @brief Healthcare log sanitization configuration
 */
struct healthcare_sanitization_config {
    /** Enable sanitization (default: true) */
    bool enabled = true;

    /** Masking style for detected PHI */
    masking_style style = masking_style::type_label;

    /** Fields to sanitize */
    std::unordered_set<phi_field_type> fields_to_sanitize = {
        phi_field_type::patient_name,
        phi_field_type::patient_id,
        phi_field_type::date_of_birth,
        phi_field_type::ssn,
        phi_field_type::phone_number,
        phi_field_type::address,
        phi_field_type::email,
        phi_field_type::account_number,
        phi_field_type::insurance_id
    };

    /** HL7 segments containing PHI (default: PID, NK1, GT1, IN1, IN2) */
    std::unordered_set<std::string> phi_segments = {
        "PID", "NK1", "GT1", "IN1", "IN2", "PD1", "ARV"
    };

    /** Also sanitize message control ID (MSH-10) */
    bool sanitize_control_id = false;

    /** Sanitize IP addresses in connection info */
    bool sanitize_ip_addresses = false;

    /** Characters for partial masking */
    size_t partial_show_prefix = 3;
    size_t partial_show_suffix = 3;
};

// =============================================================================
// Detected PHI Information
// =============================================================================

/**
 * @brief Information about detected PHI in content
 */
struct phi_detection {
    /** Type of PHI detected */
    phi_field_type type;

    /** Position in original string */
    size_t position;

    /** Length of detected PHI */
    size_t length;

    /** HL7 segment where PHI was found (if applicable) */
    std::optional<std::string> segment;

    /** HL7 field number (if applicable) */
    std::optional<int> field_number;

    /** Brief context (for debugging, already sanitized) */
    std::string context;
};

// =============================================================================
// Healthcare Log Sanitizer
// =============================================================================

/**
 * @brief Healthcare-specific log sanitizer for PHI protection
 *
 * Extends the base log_sanitizer from logger_system with healthcare-specific
 * patterns for PHI detection and HL7 message awareness.
 *
 * @example Basic Usage
 * ```cpp
 * healthcare_log_sanitizer sanitizer;
 *
 * std::string log_line = "Patient John Doe (MRN: 12345) admitted";
 * std::string safe_log = sanitizer.sanitize(log_line);
 * // Result: "Patient [PATIENT_NAME] (MRN: [PATIENT_ID]) admitted"
 * ```
 *
 * @example HL7 Message Sanitization
 * ```cpp
 * healthcare_log_sanitizer sanitizer;
 *
 * std::string hl7_msg = "MSH|...\rPID|1||123456^^^MRN||DOE^JOHN||19800101|M";
 * std::string safe_msg = sanitizer.sanitize_hl7(hl7_msg);
 * // PID segment fields are masked
 * ```
 */
class healthcare_log_sanitizer {
public:
    /**
     * @brief Constructor with configuration
     * @param config Sanitization configuration
     */
    explicit healthcare_log_sanitizer(
        const healthcare_sanitization_config& config = {});

    /**
     * @brief Destructor
     */
    ~healthcare_log_sanitizer();

    // Non-copyable, movable
    healthcare_log_sanitizer(const healthcare_log_sanitizer&) = delete;
    healthcare_log_sanitizer& operator=(const healthcare_log_sanitizer&) = delete;
    healthcare_log_sanitizer(healthcare_log_sanitizer&&) noexcept;
    healthcare_log_sanitizer& operator=(healthcare_log_sanitizer&&) noexcept;

    // =========================================================================
    // Sanitization Methods
    // =========================================================================

    /**
     * @brief Sanitize free-text content
     *
     * Uses both base sanitizer patterns and healthcare-specific patterns.
     *
     * @param content Content to sanitize
     * @return Sanitized content
     */
    [[nodiscard]] std::string sanitize(std::string_view content) const;

    /**
     * @brief Sanitize HL7 message content
     *
     * Uses HL7 structure awareness to mask PHI in specific segments
     * and fields defined in the configuration.
     *
     * @param hl7_message HL7 message content
     * @return Sanitized HL7 message
     */
    [[nodiscard]] std::string sanitize_hl7(std::string_view hl7_message) const;

    /**
     * @brief Sanitize and detect PHI
     *
     * Sanitizes content and returns information about detected PHI.
     *
     * @param content Content to sanitize
     * @return Pair of sanitized content and detection list
     */
    [[nodiscard]] std::pair<std::string, std::vector<phi_detection>>
    sanitize_with_detections(std::string_view content) const;

    // =========================================================================
    // Detection Methods
    // =========================================================================

    /**
     * @brief Check if content contains PHI
     * @param content Content to check
     * @return true if PHI patterns detected
     */
    [[nodiscard]] bool contains_phi(std::string_view content) const;

    /**
     * @brief Detect PHI in content without sanitization
     * @param content Content to analyze
     * @return List of detected PHI
     */
    [[nodiscard]] std::vector<phi_detection>
    detect_phi(std::string_view content) const;

    // =========================================================================
    // Masking Methods
    // =========================================================================

    /**
     * @brief Mask a specific value
     * @param value Value to mask
     * @param type PHI type for context
     * @return Masked value
     */
    [[nodiscard]] std::string mask(std::string_view value,
                                   phi_field_type type = phi_field_type::custom) const;

    /**
     * @brief Create type label replacement
     * @param type PHI field type
     * @return Label like "[PATIENT_NAME]"
     */
    [[nodiscard]] static std::string make_type_label(phi_field_type type);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Update sanitization configuration
     */
    void set_config(const healthcare_sanitization_config& config);

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const healthcare_sanitization_config& config() const noexcept;

    /**
     * @brief Enable/disable sanitization
     */
    void set_enabled(bool enabled);

    /**
     * @brief Check if sanitization is enabled
     */
    [[nodiscard]] bool is_enabled() const noexcept;

    /**
     * @brief Add custom pattern for detection
     * @param pattern Regex pattern
     * @param replacement Replacement text
     */
    void add_custom_pattern(std::string_view pattern,
                            std::string_view replacement = "[REDACTED]");

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Create a log-safe version of HL7 message summary
 *
 * Creates a summary string for logging that includes message type
 * and control ID but no PHI.
 *
 * @param hl7_message HL7 message
 * @return Safe summary string
 */
[[nodiscard]] std::string make_safe_hl7_summary(std::string_view hl7_message);

/**
 * @brief Create a log-safe session description
 *
 * @param remote_address Remote address
 * @param remote_port Remote port
 * @param session_id Session identifier
 * @param mask_ip Whether to mask IP address
 * @return Safe session description
 */
[[nodiscard]] std::string make_safe_session_desc(std::string_view remote_address,
                                                  uint16_t remote_port,
                                                  uint64_t session_id,
                                                  bool mask_ip = false);

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_LOG_SANITIZER_H
