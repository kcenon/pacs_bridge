#ifndef PACS_BRIDGE_PROTOCOL_HL7_HL7_TYPES_H
#define PACS_BRIDGE_PROTOCOL_HL7_HL7_TYPES_H

/**
 * @file hl7_types.h
 * @brief HL7 v2.x protocol type definitions and constants
 *
 * Defines the fundamental types, constants, and error codes for HL7 v2.x
 * message parsing, building, and validation. Supports HL7 versions 2.3
 * through 2.5.1 with extensibility for future versions.
 *
 * HL7 v2.x Message Structure:
 *   - Segments: Lines separated by <CR> (0x0D)
 *   - Fields: Components within segments separated by |
 *   - Components: Sub-fields separated by ^
 *   - Subcomponents: Separated by &
 *   - Repetitions: Separated by ~
 *
 * @see https://www.hl7.org/implement/standards/product_brief.cfm?product_id=185
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Result<T> pattern - use stub for standalone builds
#ifdef PACS_BRIDGE_STANDALONE_BUILD
#include <pacs/bridge/internal/result_stub.h>
#else
#include <kcenon/common/patterns/result.h>
#endif

namespace pacs::bridge::hl7 {

// =============================================================================
// Result Type Aliases
// =============================================================================

/**
 * @brief Result type alias for HL7 operations
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
// HL7 Protocol Constants
// =============================================================================

/** Default field separator character */
constexpr char HL7_FIELD_SEPARATOR = '|';

/** Default component separator character */
constexpr char HL7_COMPONENT_SEPARATOR = '^';

/** Default repetition separator character */
constexpr char HL7_REPETITION_SEPARATOR = '~';

/** Default escape character */
constexpr char HL7_ESCAPE_CHARACTER = '\\';

/** Default subcomponent separator character */
constexpr char HL7_SUBCOMPONENT_SEPARATOR = '&';

/** Segment terminator (Carriage Return) */
constexpr char HL7_SEGMENT_TERMINATOR = '\x0D';

/** Line feed character (often follows CR in some systems) */
constexpr char HL7_LINE_FEED = '\x0A';

/** Maximum segment length (recommended) */
constexpr size_t HL7_MAX_SEGMENT_LENGTH = 65536;

/** Maximum message size (10MB) */
constexpr size_t HL7_MAX_MESSAGE_SIZE = 10 * 1024 * 1024;

// =============================================================================
// HL7 Encoding Characters
// =============================================================================

/**
 * @brief HL7 encoding characters configuration
 *
 * Stored in MSH-2 field, these characters define the delimiters used
 * throughout the message. Standard encoding is: ^~\&
 */
struct hl7_encoding_characters {
    char field_separator = HL7_FIELD_SEPARATOR;
    char component_separator = HL7_COMPONENT_SEPARATOR;
    char repetition_separator = HL7_REPETITION_SEPARATOR;
    char escape_character = HL7_ESCAPE_CHARACTER;
    char subcomponent_separator = HL7_SUBCOMPONENT_SEPARATOR;

    /**
     * @brief Create encoding characters from MSH-2 value
     * @param msh2 The encoding characters string (typically "^~\\&")
     * @return Parsed encoding characters or default if invalid
     */
    [[nodiscard]] static hl7_encoding_characters from_msh2(
        std::string_view msh2) noexcept {
        hl7_encoding_characters enc;
        if (msh2.size() >= 1) enc.component_separator = msh2[0];
        if (msh2.size() >= 2) enc.repetition_separator = msh2[1];
        if (msh2.size() >= 3) enc.escape_character = msh2[2];
        if (msh2.size() >= 4) enc.subcomponent_separator = msh2[3];
        return enc;
    }

    /**
     * @brief Convert to MSH-2 string representation
     */
    [[nodiscard]] std::string to_msh2() const {
        std::string result;
        result += component_separator;
        result += repetition_separator;
        result += escape_character;
        result += subcomponent_separator;
        return result;
    }

    /**
     * @brief Check if encoding uses default characters
     */
    [[nodiscard]] bool is_default() const noexcept {
        return field_separator == HL7_FIELD_SEPARATOR &&
               component_separator == HL7_COMPONENT_SEPARATOR &&
               repetition_separator == HL7_REPETITION_SEPARATOR &&
               escape_character == HL7_ESCAPE_CHARACTER &&
               subcomponent_separator == HL7_SUBCOMPONENT_SEPARATOR;
    }
};

// =============================================================================
// Error Codes (-950 to -969)
// =============================================================================

/**
 * @brief HL7 specific error codes
 *
 * Allocated range: -950 to -969
 */
enum class hl7_error : int {
    /** Message is empty or null */
    empty_message = -950,

    /** Missing required MSH segment */
    missing_msh = -951,

    /** Invalid MSH segment structure */
    invalid_msh = -952,

    /** Invalid segment structure */
    invalid_segment = -953,

    /** Required field is missing */
    missing_required_field = -954,

    /** Field value is invalid */
    invalid_field_value = -955,

    /** Unknown or unsupported message type */
    unknown_message_type = -956,

    /** Unknown or unsupported trigger event */
    unknown_trigger_event = -957,

    /** Invalid escape sequence */
    invalid_escape_sequence = -958,

    /** Message exceeds maximum size */
    message_too_large = -959,

    /** Segment exceeds maximum length */
    segment_too_long = -960,

    /** Invalid encoding characters */
    invalid_encoding = -961,

    /** Version not supported */
    unsupported_version = -962,

    /** Duplicate segment where only one allowed */
    duplicate_segment = -963,

    /** Segment order violation */
    invalid_segment_order = -964,

    /** Validation failed */
    validation_failed = -965,

    /** Parse error */
    parse_error = -966,

    /** Build error */
    build_error = -967
};

/**
 * @brief Convert hl7_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(hl7_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of HL7 error
 */
[[nodiscard]] constexpr const char* to_string(hl7_error error) noexcept {
    switch (error) {
        case hl7_error::empty_message:
            return "Message is empty or null";
        case hl7_error::missing_msh:
            return "Missing required MSH segment";
        case hl7_error::invalid_msh:
            return "Invalid MSH segment structure";
        case hl7_error::invalid_segment:
            return "Invalid segment structure";
        case hl7_error::missing_required_field:
            return "Required field is missing";
        case hl7_error::invalid_field_value:
            return "Field value is invalid";
        case hl7_error::unknown_message_type:
            return "Unknown or unsupported message type";
        case hl7_error::unknown_trigger_event:
            return "Unknown or unsupported trigger event";
        case hl7_error::invalid_escape_sequence:
            return "Invalid escape sequence";
        case hl7_error::message_too_large:
            return "Message exceeds maximum size";
        case hl7_error::segment_too_long:
            return "Segment exceeds maximum length";
        case hl7_error::invalid_encoding:
            return "Invalid encoding characters";
        case hl7_error::unsupported_version:
            return "HL7 version not supported";
        case hl7_error::duplicate_segment:
            return "Duplicate segment where only one allowed";
        case hl7_error::invalid_segment_order:
            return "Segment order violation";
        case hl7_error::validation_failed:
            return "Message validation failed";
        case hl7_error::parse_error:
            return "Failed to parse HL7 message";
        case hl7_error::build_error:
            return "Failed to build HL7 message";
        default:
            return "Unknown HL7 error";
    }
}

/**
 * @brief Convert hl7_error to error_info for Result<T>
 *
 * @param error HL7 error code
 * @param details Optional additional details
 * @return error_info for use with Result<T>
 */
[[nodiscard]] inline error_info to_error_info(
    hl7_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "hl7",
        details
    };
}

// =============================================================================
// HL7 Message Types
// =============================================================================

/**
 * @brief Common HL7 message types
 */
enum class message_type {
    /** Admission, Discharge, Transfer */
    ADT,

    /** Order Message */
    ORM,

    /** Observation Result */
    ORU,

    /** Query */
    QRY,

    /** General Acknowledgment */
    ACK,

    /** Application Reject */
    ARD,

    /** Scheduling Information */
    SIU,

    /** Master Files */
    MFN,

    /** Detail Financial Transaction */
    DFT,

    /** Bar Information */
    BAR,

    /** Unsolicited Display */
    UDM,

    /** Unknown/Other */
    UNKNOWN
};

/**
 * @brief Convert message type to string
 */
[[nodiscard]] constexpr const char* to_string(message_type type) noexcept {
    switch (type) {
        case message_type::ADT: return "ADT";
        case message_type::ORM: return "ORM";
        case message_type::ORU: return "ORU";
        case message_type::QRY: return "QRY";
        case message_type::ACK: return "ACK";
        case message_type::ARD: return "ARD";
        case message_type::SIU: return "SIU";
        case message_type::MFN: return "MFN";
        case message_type::DFT: return "DFT";
        case message_type::BAR: return "BAR";
        case message_type::UDM: return "UDM";
        case message_type::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Parse message type from string
 */
[[nodiscard]] inline message_type parse_message_type(
    std::string_view type_str) noexcept {
    if (type_str == "ADT") return message_type::ADT;
    if (type_str == "ORM") return message_type::ORM;
    if (type_str == "ORU") return message_type::ORU;
    if (type_str == "QRY") return message_type::QRY;
    if (type_str == "ACK") return message_type::ACK;
    if (type_str == "ARD") return message_type::ARD;
    if (type_str == "SIU") return message_type::SIU;
    if (type_str == "MFN") return message_type::MFN;
    if (type_str == "DFT") return message_type::DFT;
    if (type_str == "BAR") return message_type::BAR;
    if (type_str == "UDM") return message_type::UDM;
    return message_type::UNKNOWN;
}

// =============================================================================
// HL7 Acknowledgment Codes
// =============================================================================

/**
 * @brief HL7 acknowledgment codes (MSA-1)
 */
enum class ack_code {
    /** Application Accept - message processed successfully */
    AA,

    /** Application Error - message had errors */
    AE,

    /** Application Reject - message rejected */
    AR,

    /** Commit Accept (enhanced acknowledgment) */
    CA,

    /** Commit Error (enhanced acknowledgment) */
    CE,

    /** Commit Reject (enhanced acknowledgment) */
    CR
};

/**
 * @brief Convert acknowledgment code to string
 */
[[nodiscard]] constexpr const char* to_string(ack_code code) noexcept {
    switch (code) {
        case ack_code::AA: return "AA";
        case ack_code::AE: return "AE";
        case ack_code::AR: return "AR";
        case ack_code::CA: return "CA";
        case ack_code::CE: return "CE";
        case ack_code::CR: return "CR";
        default: return "AA";
    }
}

/**
 * @brief Parse acknowledgment code from string
 */
[[nodiscard]] inline ack_code parse_ack_code(std::string_view code_str) noexcept {
    if (code_str == "AA") return ack_code::AA;
    if (code_str == "AE") return ack_code::AE;
    if (code_str == "AR") return ack_code::AR;
    if (code_str == "CA") return ack_code::CA;
    if (code_str == "CE") return ack_code::CE;
    if (code_str == "CR") return ack_code::CR;
    return ack_code::AA;
}

/**
 * @brief Check if acknowledgment code indicates success
 */
[[nodiscard]] constexpr bool is_ack_success(ack_code code) noexcept {
    return code == ack_code::AA || code == ack_code::CA;
}

// =============================================================================
// HL7 Date/Time Types
// =============================================================================

/**
 * @brief HL7 timestamp (TS type)
 *
 * HL7 timestamp format: YYYYMMDDHHMMSS.FFFF[+/-ZZZZ]
 * - YYYY: Year (4 digits)
 * - MM: Month (2 digits, 01-12)
 * - DD: Day (2 digits, 01-31)
 * - HH: Hour (2 digits, 00-23)
 * - MM: Minute (2 digits, 00-59)
 * - SS: Second (2 digits, 00-59)
 * - FFFF: Fractional seconds (1-4 digits, optional)
 * - ZZZZ: Timezone offset (optional)
 */
struct hl7_timestamp {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    std::optional<int> timezone_offset_minutes;

    /**
     * @brief Convert to system_clock time_point
     */
    [[nodiscard]] std::chrono::system_clock::time_point to_time_point() const;

    /**
     * @brief Create from system_clock time_point
     */
    [[nodiscard]] static hl7_timestamp from_time_point(
        std::chrono::system_clock::time_point tp);

    /**
     * @brief Parse from HL7 timestamp string
     */
    [[nodiscard]] static std::optional<hl7_timestamp> parse(
        std::string_view ts_string);

    /**
     * @brief Convert to HL7 timestamp string
     * @param precision Number of timestamp components (4=date only, 8=date+time,
     * 14=full)
     */
    [[nodiscard]] std::string to_string(int precision = 14) const;

    /**
     * @brief Get current timestamp
     */
    [[nodiscard]] static hl7_timestamp now();

    /**
     * @brief Check if timestamp is valid
     */
    [[nodiscard]] bool is_valid() const noexcept;
};

// =============================================================================
// HL7 Patient Identifier
// =============================================================================

/**
 * @brief HL7 Patient Identifier (CX type)
 *
 * Composite type for patient identification including:
 * - ID number
 * - Assigning authority
 * - Identifier type code
 */
struct hl7_patient_id {
    /** ID number (CX.1) */
    std::string id;

    /** Assigning authority (CX.4) */
    std::string assigning_authority;

    /** Identifier type code (CX.5) */
    std::string id_type;

    /** Assigning facility (CX.6) */
    std::string assigning_facility;

    /**
     * @brief Check if identifier is empty
     */
    [[nodiscard]] bool empty() const noexcept { return id.empty(); }

    /**
     * @brief Equality comparison
     */
    [[nodiscard]] bool operator==(const hl7_patient_id& other) const noexcept {
        return id == other.id && assigning_authority == other.assigning_authority;
    }
};

// =============================================================================
// HL7 Person Name
// =============================================================================

/**
 * @brief HL7 Extended Person Name (XPN type)
 *
 * HL7 name format: FamilyName^GivenName^MiddleName^Suffix^Prefix^Degree
 */
struct hl7_person_name {
    /** Family name (last name) */
    std::string family_name;

    /** Given name (first name) */
    std::string given_name;

    /** Middle name or initial */
    std::string middle_name;

    /** Suffix (Jr., Sr., III, etc.) */
    std::string suffix;

    /** Prefix (Mr., Mrs., Dr., etc.) */
    std::string prefix;

    /** Academic degree (MD, PhD, etc.) */
    std::string degree;

    /** Name type code */
    std::string name_type_code;

    /**
     * @brief Check if name is empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return family_name.empty() && given_name.empty();
    }

    /**
     * @brief Get display name (Given Family)
     */
    [[nodiscard]] std::string display_name() const;

    /**
     * @brief Get formatted name (Family, Given Middle)
     */
    [[nodiscard]] std::string formatted_name() const;

    /**
     * @brief Convert to DICOM PN format (Family^Given^Middle^Prefix^Suffix)
     */
    [[nodiscard]] std::string to_dicom_pn() const;

    /**
     * @brief Parse from DICOM PN format
     */
    [[nodiscard]] static hl7_person_name from_dicom_pn(std::string_view pn);
};

// =============================================================================
// HL7 Address
// =============================================================================

/**
 * @brief HL7 Extended Address (XAD type)
 */
struct hl7_address {
    /** Street address line 1 */
    std::string street1;

    /** Street address line 2 */
    std::string street2;

    /** City */
    std::string city;

    /** State or province */
    std::string state;

    /** Postal/ZIP code */
    std::string postal_code;

    /** Country */
    std::string country;

    /** Address type (H=Home, W=Work, M=Mailing, etc.) */
    std::string address_type;

    /**
     * @brief Check if address is empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return street1.empty() && city.empty();
    }

    /**
     * @brief Get single-line formatted address
     */
    [[nodiscard]] std::string formatted() const;
};

// =============================================================================
// HL7 Message Header Info
// =============================================================================

/**
 * @brief HL7 Message Header (MSH) information
 *
 * Contains parsed information from the MSH segment which is required
 * in all HL7 v2.x messages.
 */
struct hl7_message_header {
    /** Encoding characters configuration */
    hl7_encoding_characters encoding;

    /** Sending application (MSH-3) */
    std::string sending_application;

    /** Sending facility (MSH-4) */
    std::string sending_facility;

    /** Receiving application (MSH-5) */
    std::string receiving_application;

    /** Receiving facility (MSH-6) */
    std::string receiving_facility;

    /** Message timestamp (MSH-7) */
    hl7_timestamp timestamp;

    /** Security field (MSH-8) */
    std::string security;

    /** Message type (MSH-9.1) */
    message_type type = message_type::UNKNOWN;

    /** Message type string (MSH-9.1 raw) */
    std::string type_string;

    /** Trigger event (MSH-9.2) */
    std::string trigger_event;

    /** Message structure (MSH-9.3) */
    std::string message_structure;

    /** Message control ID (MSH-10) */
    std::string message_control_id;

    /** Processing ID (MSH-11) - P=Production, D=Debug, T=Training */
    std::string processing_id;

    /** HL7 version (MSH-12) */
    std::string version_id;

    /** Sequence number (MSH-13) */
    std::optional<int64_t> sequence_number;

    /** Accept acknowledgment type (MSH-15) */
    std::string accept_ack_type;

    /** Application acknowledgment type (MSH-16) */
    std::string app_ack_type;

    /** Country code (MSH-17) */
    std::string country_code;

    /** Character set (MSH-18) */
    std::string character_set;

    /**
     * @brief Check if this is an acknowledgment message
     */
    [[nodiscard]] bool is_ack() const noexcept {
        return type == message_type::ACK;
    }

    /**
     * @brief Get full message type string (e.g., "ADT^A01")
     */
    [[nodiscard]] std::string full_message_type() const {
        if (trigger_event.empty()) return type_string;
        return type_string + "^" + trigger_event;
    }
};

// =============================================================================
// Validation Result
// =============================================================================

/**
 * @brief Validation issue severity
 */
enum class validation_severity {
    /** Error - message cannot be processed */
    error,

    /** Warning - message can be processed but has issues */
    warning,

    /** Info - informational note */
    info
};

/**
 * @brief Single validation issue
 */
struct validation_issue {
    /** Severity level */
    validation_severity severity = validation_severity::error;

    /** Error code */
    hl7_error code = hl7_error::validation_failed;

    /** Location in message (e.g., "MSH.9.1") */
    std::string location;

    /** Description of the issue */
    std::string message;
};

/**
 * @brief Result of message validation
 */
struct validation_result {
    /** Whether validation passed (no errors) */
    bool valid = true;

    /** List of validation issues */
    std::vector<validation_issue> issues;

    /**
     * @brief Add an issue
     */
    void add_issue(validation_severity severity, hl7_error code,
                   std::string_view location, std::string_view msg) {
        issues.push_back({severity, code, std::string(location), std::string(msg)});
        if (severity == validation_severity::error) {
            valid = false;
        }
    }

    /**
     * @brief Add an error
     */
    void add_error(hl7_error code, std::string_view location,
                   std::string_view msg) {
        add_issue(validation_severity::error, code, location, msg);
    }

    /**
     * @brief Add a warning
     */
    void add_warning(hl7_error code, std::string_view location,
                     std::string_view msg) {
        add_issue(validation_severity::warning, code, location, msg);
    }

    /**
     * @brief Check if there are any errors
     */
    [[nodiscard]] bool has_errors() const noexcept { return !valid; }

    /**
     * @brief Count errors
     */
    [[nodiscard]] size_t error_count() const noexcept {
        size_t count = 0;
        for (const auto& issue : issues) {
            if (issue.severity == validation_severity::error) ++count;
        }
        return count;
    }

    /**
     * @brief Count warnings
     */
    [[nodiscard]] size_t warning_count() const noexcept {
        size_t count = 0;
        for (const auto& issue : issues) {
            if (issue.severity == validation_severity::warning) ++count;
        }
        return count;
    }
};

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_HL7_TYPES_H
