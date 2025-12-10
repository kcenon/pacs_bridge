#ifndef PACS_BRIDGE_PROTOCOL_HL7_HL7_VALIDATOR_H
#define PACS_BRIDGE_PROTOCOL_HL7_HL7_VALIDATOR_H

/**
 * @file hl7_validator.h
 * @brief HL7 v2.x message-type specific validation
 *
 * Provides validation for HL7 v2.x messages based on message type,
 * checking for required segments and fields according to HL7 standards
 * and IHE profiles.
 *
 * Supported message types:
 *   - ADT (A01, A04, A08, A40)
 *   - ORM (O01)
 *   - ORU (R01)
 *   - SIU (S12-S15)
 *   - ACK
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/11
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include "hl7_message.h"
#include "hl7_types.h"

#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::hl7 {

// =============================================================================
// Validation Issue Types
// =============================================================================

/**
 * @brief Type of validation issue
 */
enum class validation_issue_type {
    /** Required segment is missing */
    missing_segment,

    /** Required field is missing or empty */
    missing_field,

    /** Field value is invalid */
    invalid_field_value,

    /** Segment order is incorrect */
    invalid_segment_order,

    /** Unexpected segment present */
    unexpected_segment,

    /** Conditional validation failed */
    conditional_failed
};

/**
 * @brief Detailed validation issue information
 */
struct validator_issue {
    /** Issue severity */
    validation_severity severity = validation_severity::error;

    /** Type of issue */
    validation_issue_type type = validation_issue_type::missing_field;

    /** Location in message (e.g., "MSH.9" or "PID segment") */
    std::string location;

    /** Human-readable description */
    std::string message;

    /** HL7 error code (if applicable) */
    hl7_error code = hl7_error::validation_failed;
};

/**
 * @brief Result of message validation
 */
struct validator_result {
    /** Whether validation passed (no errors) */
    bool valid = true;

    /** Message type that was validated */
    message_type type = message_type::UNKNOWN;

    /** Trigger event (e.g., "A01") */
    std::string trigger_event;

    /** List of validation issues */
    std::vector<validator_issue> issues;

    /**
     * @brief Add an error issue
     */
    void add_error(validation_issue_type type, std::string_view location,
                   std::string_view msg);

    /**
     * @brief Add a warning issue
     */
    void add_warning(validation_issue_type type, std::string_view location,
                     std::string_view msg);

    /**
     * @brief Check if there are any errors
     */
    [[nodiscard]] bool has_errors() const noexcept { return !valid; }

    /**
     * @brief Count errors
     */
    [[nodiscard]] size_t error_count() const noexcept;

    /**
     * @brief Count warnings
     */
    [[nodiscard]] size_t warning_count() const noexcept;

    /**
     * @brief Get formatted error summary
     */
    [[nodiscard]] std::string summary() const;
};

// =============================================================================
// HL7 Validator
// =============================================================================

/**
 * @brief HL7 message validator
 *
 * Validates HL7 messages against message-type specific rules.
 * Automatically detects message type from MSH-9 and applies
 * appropriate validation rules.
 *
 * @example Basic Usage
 * ```cpp
 * auto msg = hl7_message::parse(raw_data);
 * if (msg) {
 *     auto result = hl7_validator::validate(*msg);
 *     if (!result.valid) {
 *         for (const auto& issue : result.issues) {
 *             std::cerr << issue.location << ": " << issue.message << std::endl;
 *         }
 *     }
 * }
 * ```
 *
 * @example Type-Specific Validation
 * ```cpp
 * auto result = hl7_validator::validate_orm(msg);
 * if (result.has_errors()) {
 *     std::cerr << result.summary() << std::endl;
 * }
 * ```
 */
class hl7_validator {
public:
    // =========================================================================
    // Auto-Detect Validation
    // =========================================================================

    /**
     * @brief Validate message with auto-detected type
     *
     * Reads MSH-9 to determine message type and applies
     * appropriate validation rules.
     *
     * @param message Message to validate
     * @return Validation result
     */
    [[nodiscard]] static validator_result validate(const hl7_message& message);

    // =========================================================================
    // Type-Specific Validation
    // =========================================================================

    /**
     * @brief Validate ADT message
     *
     * ADT trigger events: A01 (Admit), A02 (Transfer), A03 (Discharge),
     * A04 (Register), A08 (Update), A40 (Merge)
     *
     * Required segments: MSH, EVN, PID
     * Required PID fields: PID-3 (Patient ID), PID-5 (Patient Name)
     *
     * @param message ADT message
     * @return Validation result
     */
    [[nodiscard]] static validator_result validate_adt(
        const hl7_message& message);

    /**
     * @brief Validate ORM message
     *
     * ORM trigger events: O01 (Order)
     *
     * Required segments: MSH, PID, ORC, OBR
     * Required ORC fields: ORC-1, ORC-2 or ORC-3
     * Required OBR fields: OBR-4
     *
     * @param message ORM message
     * @return Validation result
     */
    [[nodiscard]] static validator_result validate_orm(
        const hl7_message& message);

    /**
     * @brief Validate ORU message
     *
     * ORU trigger events: R01 (Result)
     *
     * Required segments: MSH, PID, OBR, OBX
     * Required OBR fields: OBR-25 (Result Status)
     *
     * @param message ORU message
     * @return Validation result
     */
    [[nodiscard]] static validator_result validate_oru(
        const hl7_message& message);

    /**
     * @brief Validate SIU message
     *
     * SIU trigger events: S12-S15 (Scheduling)
     *
     * Required segments: MSH, SCH, PID
     * Required SCH fields: SCH-1 (Placer Appointment ID)
     *
     * @param message SIU message
     * @return Validation result
     */
    [[nodiscard]] static validator_result validate_siu(
        const hl7_message& message);

    /**
     * @brief Validate ACK message
     *
     * Required segments: MSH, MSA
     * Required MSA fields: MSA-1 (Ack Code), MSA-2 (Message Control ID)
     *
     * @param message ACK message
     * @return Validation result
     */
    [[nodiscard]] static validator_result validate_ack(
        const hl7_message& message);

    // =========================================================================
    // Common Validation Helpers
    // =========================================================================

    /**
     * @brief Validate MSH segment (common to all messages)
     *
     * @param message Message containing MSH
     * @param result Validation result to populate
     */
    static void validate_msh(const hl7_message& message,
                             validator_result& result);

    /**
     * @brief Validate PID segment
     *
     * @param message Message containing PID
     * @param result Validation result to populate
     * @param require_patient_name Whether patient name is required
     */
    static void validate_pid(const hl7_message& message,
                             validator_result& result,
                             bool require_patient_name = true);

    /**
     * @brief Check if a segment exists
     *
     * @param message Message to check
     * @param segment_id Segment ID
     * @param result Validation result to populate
     * @param required Whether segment is required (error) or optional (warning)
     * @return true if segment exists
     */
    static bool check_segment(const hl7_message& message,
                              std::string_view segment_id,
                              validator_result& result,
                              bool required = true);

    /**
     * @brief Check if a field is present and non-empty
     *
     * @param message Message to check
     * @param path Field path (e.g., "PID.3")
     * @param result Validation result to populate
     * @param required Whether field is required
     * @return true if field is present
     */
    static bool check_field(const hl7_message& message,
                            std::string_view path,
                            validator_result& result,
                            bool required = true);
};

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_HL7_VALIDATOR_H
