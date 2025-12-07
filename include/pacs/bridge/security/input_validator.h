#ifndef PACS_BRIDGE_SECURITY_INPUT_VALIDATOR_H
#define PACS_BRIDGE_SECURITY_INPUT_VALIDATOR_H

/**
 * @file input_validator.h
 * @brief Input validation and sanitization for HL7 messages
 *
 * Provides comprehensive input validation to prevent injection attacks,
 * enforce message size limits, and validate HL7 message structure.
 * Essential for HIPAA compliance and protecting against OWASP Top 10
 * vulnerabilities.
 *
 * Security Features:
 *   - HL7 message structure validation
 *   - MSH segment field validation
 *   - Control character detection and handling
 *   - Message size enforcement
 *   - SQL/Command injection prevention
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace pacs::bridge::security {

// =============================================================================
// Error Codes (-960 to -969)
// =============================================================================

/**
 * @brief Input validation error codes
 *
 * Allocated range: -960 to -969
 */
enum class validation_error : int {
    /** Message is empty or null */
    empty_message = -960,

    /** Message exceeds maximum allowed size */
    message_too_large = -961,

    /** Invalid HL7 message structure */
    invalid_hl7_structure = -962,

    /** Missing required MSH segment */
    missing_msh_segment = -963,

    /** Invalid MSH field values */
    invalid_msh_fields = -964,

    /** Prohibited control characters detected */
    prohibited_characters = -965,

    /** Potential injection attack detected */
    injection_detected = -966,

    /** Invalid character encoding */
    invalid_encoding = -967,

    /** Message timestamp validation failed */
    invalid_timestamp = -968,

    /** Sender/receiver application validation failed */
    invalid_application_id = -969
};

/**
 * @brief Convert validation_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(validation_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of validation error
 */
[[nodiscard]] constexpr const char* to_string(validation_error error) noexcept {
    switch (error) {
        case validation_error::empty_message:
            return "Message is empty or null";
        case validation_error::message_too_large:
            return "Message exceeds maximum allowed size";
        case validation_error::invalid_hl7_structure:
            return "Invalid HL7 message structure";
        case validation_error::missing_msh_segment:
            return "Missing required MSH segment";
        case validation_error::invalid_msh_fields:
            return "Invalid MSH field values";
        case validation_error::prohibited_characters:
            return "Prohibited control characters detected";
        case validation_error::injection_detected:
            return "Potential injection attack detected";
        case validation_error::invalid_encoding:
            return "Invalid character encoding";
        case validation_error::invalid_timestamp:
            return "Message timestamp validation failed";
        case validation_error::invalid_application_id:
            return "Sender/receiver application validation failed";
        default:
            return "Unknown validation error";
    }
}

// =============================================================================
// Validation Configuration
// =============================================================================

/**
 * @brief Input validation configuration
 *
 * Configures validation rules and limits for HL7 message processing.
 */
struct validation_config {
    /** Maximum allowed message size in bytes (default: 10MB) */
    size_t max_message_size = 10 * 1024 * 1024;

    /** Maximum allowed segment count */
    size_t max_segment_count = 10000;

    /** Maximum field length */
    size_t max_field_length = 65536;

    /** Validate MSH segment structure */
    bool validate_msh = true;

    /** Validate message timestamp is within acceptable range */
    bool validate_timestamp = true;

    /** Maximum allowed timestamp skew from current time */
    std::chrono::hours max_timestamp_skew{24};

    /** Allowed sending applications (empty = allow all) */
    std::unordered_set<std::string> allowed_sending_apps;

    /** Allowed sending facilities (empty = allow all) */
    std::unordered_set<std::string> allowed_sending_facilities;

    /** Allowed receiving applications (empty = allow all) */
    std::unordered_set<std::string> allowed_receiving_apps;

    /** Allowed receiving facilities (empty = allow all) */
    std::unordered_set<std::string> allowed_receiving_facilities;

    /** Detect and reject potential SQL injection patterns */
    bool detect_sql_injection = true;

    /** Detect and reject potential command injection patterns */
    bool detect_command_injection = true;

    /** Allow binary data in OBX segments */
    bool allow_binary_data = true;

    /** Strip null bytes from message */
    bool strip_null_bytes = true;

    /** Normalize line endings to CR */
    bool normalize_line_endings = true;
};

// =============================================================================
// Validation Result
// =============================================================================

/**
 * @brief Detailed validation result
 */
struct validation_result {
    /** Validation passed */
    bool valid = false;

    /** Error code if validation failed */
    std::optional<validation_error> error;

    /** Detailed error message */
    std::string error_message;

    /** Field or segment where error occurred */
    std::string error_location;

    /** Warnings that don't fail validation */
    std::vector<std::string> warnings;

    /** Extracted MSH-3 (Sending Application) */
    std::optional<std::string> sending_app;

    /** Extracted MSH-4 (Sending Facility) */
    std::optional<std::string> sending_facility;

    /** Extracted MSH-5 (Receiving Application) */
    std::optional<std::string> receiving_app;

    /** Extracted MSH-6 (Receiving Facility) */
    std::optional<std::string> receiving_facility;

    /** Extracted MSH-9 (Message Type) */
    std::optional<std::string> message_type;

    /** Extracted MSH-10 (Message Control ID) */
    std::optional<std::string> message_control_id;

    /** Message size in bytes */
    size_t message_size = 0;

    /** Number of segments in message */
    size_t segment_count = 0;

    /** Create successful result */
    [[nodiscard]] static validation_result success() {
        validation_result result;
        result.valid = true;
        return result;
    }

    /** Create failure result */
    [[nodiscard]] static validation_result failure(validation_error err,
                                                   std::string_view message,
                                                   std::string_view location = "") {
        validation_result result;
        result.valid = false;
        result.error = err;
        result.error_message = std::string(message);
        result.error_location = std::string(location);
        return result;
    }
};

// =============================================================================
// Input Validator
// =============================================================================

/**
 * @brief HL7 message input validator
 *
 * Validates incoming HL7 messages against security and format rules.
 * Should be called before any message processing to ensure message
 * integrity and prevent attacks.
 *
 * @example Basic Usage
 * ```cpp
 * validation_config config;
 * config.max_message_size = 1024 * 1024;  // 1MB
 * config.allowed_sending_apps = {"PACS", "RIS", "HIS"};
 *
 * input_validator validator(config);
 *
 * auto result = validator.validate(hl7_message);
 * if (!result.valid) {
 *     log_error("Validation failed: {} at {}",
 *               result.error_message, result.error_location);
 *     return;
 * }
 * ```
 *
 * @example With Sanitization
 * ```cpp
 * input_validator validator(config);
 *
 * // Validate and get sanitized message
 * auto [result, sanitized] = validator.validate_and_sanitize(raw_message);
 * if (result.valid) {
 *     process_message(sanitized);
 * }
 * ```
 */
class input_validator {
public:
    /**
     * @brief Constructor with configuration
     * @param config Validation configuration
     */
    explicit input_validator(const validation_config& config = {});

    /**
     * @brief Destructor
     */
    ~input_validator();

    // Non-copyable, movable
    input_validator(const input_validator&) = delete;
    input_validator& operator=(const input_validator&) = delete;
    input_validator(input_validator&&) noexcept;
    input_validator& operator=(input_validator&&) noexcept;

    // =========================================================================
    // Validation Methods
    // =========================================================================

    /**
     * @brief Validate an HL7 message
     *
     * Performs comprehensive validation including:
     * - Size limits
     * - Structure validation
     * - MSH segment validation
     * - Application/facility whitelisting
     * - Injection attack detection
     *
     * @param message Raw HL7 message bytes
     * @return Validation result with details
     */
    [[nodiscard]] validation_result validate(std::string_view message) const;

    /**
     * @brief Validate and sanitize an HL7 message
     *
     * Validates the message and returns a sanitized version with:
     * - Null bytes removed (if configured)
     * - Line endings normalized (if configured)
     * - Control characters handled
     *
     * @param message Raw HL7 message bytes
     * @return Pair of validation result and sanitized message
     */
    [[nodiscard]] std::pair<validation_result, std::string>
    validate_and_sanitize(std::string_view message) const;

    // =========================================================================
    // Individual Validation Methods
    // =========================================================================

    /**
     * @brief Check message size against limits
     * @param message Message to check
     * @return Error if size exceeds limits
     */
    [[nodiscard]] std::optional<validation_error>
    check_size(std::string_view message) const;

    /**
     * @brief Validate HL7 message structure
     * @param message Message to validate
     * @return Validation result
     */
    [[nodiscard]] validation_result
    validate_structure(std::string_view message) const;

    /**
     * @brief Validate MSH segment
     * @param msh_segment MSH segment content
     * @return Validation result with extracted fields
     */
    [[nodiscard]] validation_result
    validate_msh(std::string_view msh_segment) const;

    /**
     * @brief Check for SQL injection patterns
     * @param content Content to check
     * @return true if potential injection detected
     */
    [[nodiscard]] bool detect_sql_injection(std::string_view content) const;

    /**
     * @brief Check for command injection patterns
     * @param content Content to check
     * @return true if potential injection detected
     */
    [[nodiscard]] bool detect_command_injection(std::string_view content) const;

    // =========================================================================
    // Sanitization Methods
    // =========================================================================

    /**
     * @brief Sanitize message content
     *
     * Removes or replaces problematic characters while preserving
     * valid HL7 structure.
     *
     * @param message Message to sanitize
     * @return Sanitized message
     */
    [[nodiscard]] std::string sanitize(std::string_view message) const;

    /**
     * @brief Remove null bytes from message
     * @param message Message to process
     * @return Message with null bytes removed
     */
    [[nodiscard]] static std::string strip_nulls(std::string_view message);

    /**
     * @brief Normalize line endings to HL7 standard (CR)
     * @param message Message to process
     * @return Message with normalized line endings
     */
    [[nodiscard]] static std::string normalize_endings(std::string_view message);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Update validation configuration
     * @param config New configuration
     */
    void set_config(const validation_config& config);

    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    [[nodiscard]] const validation_config& config() const noexcept;

    /**
     * @brief Add allowed sending application
     * @param app Application identifier (MSH-3)
     */
    void add_allowed_sending_app(std::string_view app);

    /**
     * @brief Add allowed sending facility
     * @param facility Facility identifier (MSH-4)
     */
    void add_allowed_sending_facility(std::string_view facility);

    /**
     * @brief Add allowed receiving application
     * @param app Application identifier (MSH-5)
     */
    void add_allowed_receiving_app(std::string_view app);

    /**
     * @brief Add allowed receiving facility
     * @param facility Facility identifier (MSH-6)
     */
    void add_allowed_receiving_facility(std::string_view facility);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_INPUT_VALIDATOR_H
