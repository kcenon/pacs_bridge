#ifndef PACS_BRIDGE_PROTOCOL_HL7_ORU_GENERATOR_H
#define PACS_BRIDGE_PROTOCOL_HL7_ORU_GENERATOR_H

/**
 * @file oru_generator.h
 * @brief ORU^R01 message generator for radiology report notifications
 *
 * Implements ORU^R01 (Observation Result Unsolicited) message generation
 * for radiology report status notifications. Supports preliminary, final,
 * corrected, and cancelled report statuses.
 *
 * Features:
 *   - Generate ORU^R01 messages from study metadata
 *   - Support for all standard report statuses (P, F, C, X)
 *   - Multi-line report text handling with proper encoding
 *   - LOINC codes for radiology reports
 *   - Configurable message options
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/25
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include "hl7_builder.h"
#include "hl7_message.h"
#include "hl7_types.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::hl7 {

// =============================================================================
// Report Status Codes
// =============================================================================

/**
 * @brief Report status codes (OBR-25, OBX-11)
 *
 * Standard HL7 result status codes for observation reports.
 */
enum class report_status : char {
    /** Preliminary - Draft report available */
    preliminary = 'P',

    /** Final - Final report complete */
    final_report = 'F',

    /** Corrected - Report corrected/amended */
    corrected = 'C',

    /** Cancelled - Report cancelled */
    cancelled = 'X'
};

/**
 * @brief Convert report status to string
 */
[[nodiscard]] constexpr const char* to_string(report_status status) noexcept {
    switch (status) {
        case report_status::preliminary: return "P";
        case report_status::final_report: return "F";
        case report_status::corrected: return "C";
        case report_status::cancelled: return "X";
        default: return "F";
    }
}

/**
 * @brief Get human-readable description of report status
 */
[[nodiscard]] constexpr const char* to_description(report_status status) noexcept {
    switch (status) {
        case report_status::preliminary: return "Preliminary";
        case report_status::final_report: return "Final";
        case report_status::corrected: return "Corrected";
        case report_status::cancelled: return "Cancelled";
        default: return "Unknown";
    }
}

/**
 * @brief Parse report status from character
 */
[[nodiscard]] inline std::optional<report_status> parse_report_status(
    char c) noexcept {
    switch (c) {
        case 'P': return report_status::preliminary;
        case 'F': return report_status::final_report;
        case 'C': return report_status::corrected;
        case 'X': return report_status::cancelled;
        default: return std::nullopt;
    }
}

// =============================================================================
// Study Information
// =============================================================================

/**
 * @brief Study information for ORU message generation
 *
 * Contains the minimal required information to generate an ORU^R01
 * message for a radiology report notification.
 */
struct oru_study_info {
    /** Patient ID (PID-3) */
    std::string patient_id;

    /** Patient ID assigning authority */
    std::string patient_id_authority;

    /** Patient family name */
    std::string patient_family_name;

    /** Patient given name */
    std::string patient_given_name;

    /** Patient birth date (YYYYMMDD) */
    std::string patient_birth_date;

    /** Patient sex (M, F, O, U) */
    std::string patient_sex;

    /** Placer order number (ORC-2, OBR-2) */
    std::string placer_order_number;

    /** Filler order number / Accession number (ORC-3, OBR-3) */
    std::string accession_number;

    /** Procedure code (OBR-4.1) */
    std::string procedure_code;

    /** Procedure description (OBR-4.2) */
    std::string procedure_description;

    /** Procedure coding system (OBR-4.3) */
    std::string procedure_coding_system;

    /** Observation date/time (OBR-7) */
    std::optional<hl7_timestamp> observation_datetime;

    /** Referring physician ID */
    std::string referring_physician_id;

    /** Referring physician family name */
    std::string referring_physician_family_name;

    /** Referring physician given name */
    std::string referring_physician_given_name;

    /** Radiologist/interpreting physician ID */
    std::string radiologist_id;

    /** Radiologist family name */
    std::string radiologist_family_name;

    /** Radiologist given name */
    std::string radiologist_given_name;

    /** Study Instance UID (optional) */
    std::optional<std::string> study_instance_uid;

    /**
     * @brief Check if required fields are present
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !patient_id.empty() && !accession_number.empty();
    }
};

// =============================================================================
// ORU Generator Configuration
// =============================================================================

/**
 * @brief Configuration options for ORU message generation
 */
struct oru_generator_config {
    /** Sending application (MSH-3) */
    std::string sending_application = "PACS";

    /** Sending facility (MSH-4) */
    std::string sending_facility = "RADIOLOGY";

    /** Receiving application (MSH-5) */
    std::string receiving_application = "RIS";

    /** Receiving facility (MSH-6) */
    std::string receiving_facility = "HOSPITAL";

    /** HL7 version (MSH-12) */
    std::string version = "2.5.1";

    /** Processing ID (P=Production, T=Training, D=Debug) */
    std::string processing_id = "P";

    /** Use LOINC codes for report observation identifier */
    bool use_loinc_codes = true;

    /** LOINC code for radiology report (default: 18782-3 Radiology Study observation) */
    std::string loinc_report_code = "18782-3";

    /** LOINC code description */
    std::string loinc_report_description = "Radiology Study observation";

    /** LOINC coding system designator */
    std::string loinc_coding_system = "LN";

    /** Maximum line length for report text (0 = no wrapping) */
    size_t max_line_length = 0;
};

// =============================================================================
// ORU Generator
// =============================================================================

/**
 * @brief ORU^R01 message generator for radiology reports
 *
 * Generates ORU^R01 (Observation Result Unsolicited) messages for
 * reporting radiology study results and report status changes.
 *
 * @example Basic Usage
 * ```cpp
 * oru_study_info study;
 * study.patient_id = "12345";
 * study.patient_family_name = "DOE";
 * study.patient_given_name = "JOHN";
 * study.accession_number = "ACC001";
 * study.procedure_code = "71020";
 * study.procedure_description = "CHEST XRAY PA AND LAT";
 *
 * oru_generator generator;
 * auto result = generator.generate_final(study, "Normal chest radiograph.");
 * if (result) {
 *     std::string msg = result->serialize();
 * }
 * ```
 *
 * @example With Configuration
 * ```cpp
 * oru_generator_config config;
 * config.sending_application = "PACS_BRIDGE";
 * config.use_loinc_codes = true;
 *
 * oru_generator generator(config);
 * auto msg = generator.generate(study, "Report text...", report_status::final_report);
 * ```
 */
class oru_generator {
public:
    /**
     * @brief Default constructor with default configuration
     */
    oru_generator();

    /**
     * @brief Constructor with custom configuration
     */
    explicit oru_generator(const oru_generator_config& config);

    /**
     * @brief Destructor
     */
    ~oru_generator();

    // Non-copyable
    oru_generator(const oru_generator&) = delete;
    oru_generator& operator=(const oru_generator&) = delete;

    // Movable
    oru_generator(oru_generator&&) noexcept;
    oru_generator& operator=(oru_generator&&) noexcept;

    // =========================================================================
    // Message Generation
    // =========================================================================

    /**
     * @brief Generate ORU^R01 message with specified status
     *
     * @param study Study information
     * @param report_text Report text content
     * @param status Report status
     * @return Generated HL7 message or error
     */
    [[nodiscard]] Result<hl7_message> generate(
        const oru_study_info& study,
        std::string_view report_text,
        report_status status) const;

    /**
     * @brief Generate preliminary report ORU^R01 message
     *
     * @param study Study information
     * @param report_text Report text content
     * @return Generated HL7 message or error
     */
    [[nodiscard]] Result<hl7_message> generate_preliminary(
        const oru_study_info& study,
        std::string_view report_text) const;

    /**
     * @brief Generate final report ORU^R01 message
     *
     * @param study Study information
     * @param report_text Report text content
     * @return Generated HL7 message or error
     */
    [[nodiscard]] Result<hl7_message> generate_final(
        const oru_study_info& study,
        std::string_view report_text) const;

    /**
     * @brief Generate corrected report ORU^R01 message
     *
     * @param study Study information
     * @param report_text Report text content
     * @return Generated HL7 message or error
     */
    [[nodiscard]] Result<hl7_message> generate_corrected(
        const oru_study_info& study,
        std::string_view report_text) const;

    /**
     * @brief Generate cancelled report ORU^R01 message
     *
     * @param study Study information
     * @param cancellation_reason Reason for cancellation (optional)
     * @return Generated HL7 message or error
     */
    [[nodiscard]] Result<hl7_message> generate_cancelled(
        const oru_study_info& study,
        std::string_view cancellation_reason = "") const;

    // =========================================================================
    // Convenience Methods (Static)
    // =========================================================================

    /**
     * @brief Generate ORU^R01 message string with default configuration
     *
     * @param study Study information
     * @param report_text Report text content
     * @param status Report status
     * @return Serialized HL7 message string or error
     */
    [[nodiscard]] static Result<std::string> generate_string(
        const oru_study_info& study,
        std::string_view report_text,
        report_status status);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const oru_generator_config& config() const noexcept;

    /**
     * @brief Set configuration
     */
    void set_config(const oru_generator_config& config);

    // =========================================================================
    // Text Encoding Utilities
    // =========================================================================

    /**
     * @brief Encode text for HL7 OBX segment
     *
     * Handles special characters and multi-line text encoding according
     * to HL7 escape rules:
     *   - \F\ for field separator (|)
     *   - \S\ for component separator (^)
     *   - \R\ for repetition separator (~)
     *   - \E\ for escape character (\)
     *   - \T\ for subcomponent separator (&)
     *   - \.br\ for line breaks
     *
     * @param text Raw text to encode
     * @param encoding Encoding characters
     * @return Encoded text safe for HL7
     */
    [[nodiscard]] static std::string encode_report_text(
        std::string_view text,
        const hl7_encoding_characters& encoding = {});

    /**
     * @brief Decode HL7-encoded text
     *
     * @param encoded_text Encoded text from HL7 message
     * @param encoding Encoding characters
     * @return Decoded plain text
     */
    [[nodiscard]] static std::string decode_report_text(
        std::string_view encoded_text,
        const hl7_encoding_characters& encoding = {});

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_ORU_GENERATOR_H
