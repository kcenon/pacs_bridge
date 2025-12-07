#ifndef PACS_BRIDGE_MAPPING_HL7_DICOM_MAPPER_H
#define PACS_BRIDGE_MAPPING_HL7_DICOM_MAPPER_H

/**
 * @file hl7_dicom_mapper.h
 * @brief HL7 to DICOM Modality Worklist (MWL) mapper
 *
 * Provides bidirectional mapping between HL7 v2.x messages and DICOM
 * Modality Worklist (MWL) data structures. Supports conversion of:
 *   - ORM^O01 orders to MWL Scheduled Procedure Steps
 *   - ADT messages for patient demographic updates
 *   - MPPS results back to HL7 ORU messages
 *
 * The mapper handles:
 *   - Patient name format conversion (HL7 XPN <-> DICOM PN)
 *   - Date/time format conversion (HL7 TS <-> DICOM DT)
 *   - Character set conversions
 *   - Configurable field mapping rules
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/16
 * @see docs/reference_materials/05_mwl_mapping.md
 */

#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::mapping {

// =============================================================================
// DICOM MWL Data Structures
// =============================================================================

/**
 * @brief DICOM Patient Module attributes
 */
struct dicom_patient {
    /** Patient ID (0010,0020) */
    std::string patient_id;

    /** Issuer of Patient ID (0010,0021) */
    std::string issuer_of_patient_id;

    /** Patient Name (0010,0010) - DICOM PN format */
    std::string patient_name;

    /** Patient Birth Date (0010,0030) - YYYYMMDD */
    std::string patient_birth_date;

    /** Patient Sex (0010,0040) - M, F, O */
    std::string patient_sex;

    /** Patient Weight (0010,1030) - kg */
    std::optional<double> patient_weight;

    /** Patient Size (0010,1020) - meters */
    std::optional<double> patient_size;

    /** Other Patient IDs (0010,1000) */
    std::vector<std::string> other_patient_ids;

    /** Patient Comments (0010,4000) */
    std::string patient_comments;
};

/**
 * @brief DICOM Requested Procedure attributes
 */
struct dicom_requested_procedure {
    /** Requested Procedure ID (0040,1001) */
    std::string requested_procedure_id;

    /** Requested Procedure Description (0032,1060) */
    std::string requested_procedure_description;

    /** Requested Procedure Code Sequence (0032,1064) */
    std::string procedure_code_value;
    std::string procedure_code_meaning;
    std::string procedure_coding_scheme;

    /** Study Instance UID (0020,000D) */
    std::string study_instance_uid;

    /** Reason for the Requested Procedure (0040,1002) */
    std::string reason_for_procedure;

    /** Requested Procedure Priority (0040,1003) */
    std::string requested_procedure_priority;

    /** Referring Physician's Name (0008,0090) */
    std::string referring_physician_name;

    /** Referring Physician's ID (0008,0080) */
    std::string referring_physician_id;
};

/**
 * @brief DICOM Scheduled Procedure Step attributes
 */
struct dicom_scheduled_procedure_step {
    /** Scheduled Station AE Title (0040,0001) */
    std::string scheduled_station_ae_title;

    /** Scheduled Procedure Step Start Date (0040,0002) */
    std::string scheduled_start_date;

    /** Scheduled Procedure Step Start Time (0040,0003) */
    std::string scheduled_start_time;

    /** Modality (0008,0060) */
    std::string modality;

    /** Scheduled Performing Physician's Name (0040,0006) */
    std::string scheduled_performing_physician;

    /** Scheduled Procedure Step Description (0040,0007) */
    std::string scheduled_step_description;

    /** Scheduled Procedure Step ID (0040,0009) */
    std::string scheduled_step_id;

    /** Scheduled Protocol Code Sequence (0040,0008) */
    std::string protocol_code_value;
    std::string protocol_code_meaning;
    std::string protocol_coding_scheme;

    /** Scheduled Procedure Step Location (0040,0011) */
    std::string scheduled_step_location;

    /** Pre-Medication (0040,0012) */
    std::string pre_medication;

    /** Scheduled Procedure Step Status (0040,0020) */
    std::string scheduled_step_status;

    /** Comments on the Scheduled Procedure Step (0040,0400) */
    std::string comments;
};

/**
 * @brief DICOM Imaging Service Request attributes
 */
struct dicom_imaging_service_request {
    /** Accession Number (0008,0050) */
    std::string accession_number;

    /** Requesting Physician (0032,1032) */
    std::string requesting_physician;

    /** Requesting Service (0032,1033) */
    std::string requesting_service;

    /** Placer Order Number (0040,2016) */
    std::string placer_order_number;

    /** Filler Order Number (0040,2017) */
    std::string filler_order_number;

    /** Order Entry Date/Time (0040,2004) */
    std::string order_entry_datetime;

    /** Order Placer Identifier Sequence */
    std::string order_placer_id;
};

/**
 * @brief Complete MWL item combining all modules
 */
struct mwl_item {
    /** Patient information */
    dicom_patient patient;

    /** Imaging service request */
    dicom_imaging_service_request imaging_service_request;

    /** Requested procedure */
    dicom_requested_procedure requested_procedure;

    /** Scheduled procedure steps (may have multiple) */
    std::vector<dicom_scheduled_procedure_step> scheduled_steps;

    /** Original HL7 message control ID for tracking */
    std::string hl7_message_control_id;

    /** Specific Character Set (0008,0005) */
    std::string specific_character_set;
};

// =============================================================================
// Error Codes (-940 to -949)
// =============================================================================

/**
 * @brief Mapping specific error codes
 *
 * Allocated range: -940 to -949
 */
enum class mapping_error : int {
    /** Message type not supported for mapping */
    unsupported_message_type = -940,

    /** Required field missing in source message */
    missing_required_field = -941,

    /** Field value format is invalid */
    invalid_field_format = -942,

    /** Character set conversion failed */
    charset_conversion_failed = -943,

    /** Date/time parsing failed */
    datetime_parse_failed = -944,

    /** Name format conversion failed */
    name_conversion_failed = -945,

    /** Mapping rule not found */
    no_mapping_rule = -946,

    /** Mapping validation failed */
    validation_failed = -947,

    /** Custom mapping function error */
    custom_mapper_error = -948
};

/**
 * @brief Convert mapping_error to error code
 */
[[nodiscard]] constexpr int to_error_code(mapping_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(mapping_error error) noexcept {
    switch (error) {
        case mapping_error::unsupported_message_type:
            return "Message type not supported for mapping";
        case mapping_error::missing_required_field:
            return "Required field missing in source message";
        case mapping_error::invalid_field_format:
            return "Field value format is invalid";
        case mapping_error::charset_conversion_failed:
            return "Character set conversion failed";
        case mapping_error::datetime_parse_failed:
            return "Date/time parsing failed";
        case mapping_error::name_conversion_failed:
            return "Name format conversion failed";
        case mapping_error::no_mapping_rule:
            return "No mapping rule found for field";
        case mapping_error::validation_failed:
            return "Mapped data validation failed";
        case mapping_error::custom_mapper_error:
            return "Custom mapping function returned error";
        default:
            return "Unknown mapping error";
    }
}

// =============================================================================
// Mapping Configuration
// =============================================================================

/**
 * @brief Field mapping rule
 */
struct field_mapping_rule {
    /** Source HL7 path (e.g., "PID.5.1") */
    std::string hl7_path;

    /** Target DICOM tag or field name */
    std::string dicom_field;

    /** Transform function name (optional) */
    std::string transform;

    /** Default value if source is empty */
    std::string default_value;

    /** Is this field required */
    bool required = false;
};

/**
 * @brief Mapper configuration
 */
struct mapper_config {
    /** DICOM Specific Character Set to use */
    std::string specific_character_set = "ISO_IR 192";  // UTF-8

    /** Generate Study Instance UID if not provided */
    bool auto_generate_study_uid = true;

    /** Generate SPS ID if not provided */
    bool auto_generate_sps_id = true;

    /** Default modality if not specified */
    std::string default_modality = "OT";

    /** Default scheduled station AE title */
    std::string default_station_ae_title;

    /** Custom field mapping rules (override defaults) */
    std::vector<field_mapping_rule> custom_rules;

    /** Validate mapped output */
    bool validate_output = true;

    /** Allow partial mappings (continue on non-critical errors) */
    bool allow_partial_mapping = true;
};

// =============================================================================
// HL7 to DICOM Mapper
// =============================================================================

/**
 * @brief HL7 to DICOM MWL mapper
 *
 * Converts HL7 v2.x messages to DICOM Modality Worklist items.
 * Supports ORM^O01 orders and ADT patient demographics.
 *
 * @example Basic Usage
 * ```cpp
 * hl7_dicom_mapper mapper;
 *
 * auto hl7_msg = hl7_message::parse(raw_message);
 * if (hl7_msg) {
 *     auto mwl = mapper.to_mwl(*hl7_msg);
 *     if (mwl) {
 *         // Use MWL item
 *         std::cout << "Patient: " << mwl->patient.patient_name << std::endl;
 *     }
 * }
 * ```
 *
 * @example With Custom Configuration
 * ```cpp
 * mapper_config config;
 * config.default_modality = "CT";
 * config.default_station_ae_title = "CT_SCANNER_1";
 *
 * hl7_dicom_mapper mapper(config);
 * ```
 *
 * @example Custom Field Mapping
 * ```cpp
 * mapper_config config;
 * config.custom_rules.push_back({
 *     .hl7_path = "ZDS.1",  // Custom Z-segment
 *     .dicom_field = "study_instance_uid",
 *     .required = true
 * });
 *
 * hl7_dicom_mapper mapper(config);
 * ```
 */
class hl7_dicom_mapper {
public:
    /**
     * @brief Custom transform function type
     *
     * Takes source value, returns transformed value or error.
     */
    using transform_function =
        std::function<std::expected<std::string, mapping_error>(std::string_view)>;

    /**
     * @brief Default constructor with default configuration
     */
    hl7_dicom_mapper();

    /**
     * @brief Constructor with custom configuration
     */
    explicit hl7_dicom_mapper(const mapper_config& config);

    /**
     * @brief Destructor
     */
    ~hl7_dicom_mapper();

    // Non-copyable, movable
    hl7_dicom_mapper(const hl7_dicom_mapper&) = delete;
    hl7_dicom_mapper& operator=(const hl7_dicom_mapper&) = delete;
    hl7_dicom_mapper(hl7_dicom_mapper&&) noexcept;
    hl7_dicom_mapper& operator=(hl7_dicom_mapper&&) noexcept;

    // =========================================================================
    // HL7 to MWL Mapping
    // =========================================================================

    /**
     * @brief Convert ORM^O01 message to MWL item
     *
     * @param message HL7 ORM order message
     * @return MWL item or error
     */
    [[nodiscard]] std::expected<mwl_item, mapping_error> to_mwl(
        const hl7::hl7_message& message) const;

    /**
     * @brief Extract patient demographics from ADT message
     *
     * @param message HL7 ADT message
     * @return Patient data or error
     */
    [[nodiscard]] std::expected<dicom_patient, mapping_error> to_patient(
        const hl7::hl7_message& message) const;

    /**
     * @brief Check if message type can be mapped to MWL
     *
     * @param message HL7 message
     * @return true if mappable
     */
    [[nodiscard]] bool can_map_to_mwl(const hl7::hl7_message& message) const;

    // =========================================================================
    // MWL to HL7 Mapping (Reverse)
    // =========================================================================

    /**
     * @brief Create ORU result message from MPPS data
     *
     * @param mwl Original MWL item
     * @param status MPPS status (IN PROGRESS, COMPLETED, DISCONTINUED)
     * @return HL7 ORU message
     */
    [[nodiscard]] std::expected<hl7::hl7_message, mapping_error> to_oru(
        const mwl_item& mwl, std::string_view status) const;

    // =========================================================================
    // Utility Conversion Functions
    // =========================================================================

    /**
     * @brief Convert HL7 name (XPN) to DICOM PN format
     *
     * HL7: Family^Given^Middle^Suffix^Prefix^Degree
     * DICOM: Family^Given^Middle^Prefix^Suffix
     */
    [[nodiscard]] static std::string hl7_name_to_dicom(
        const hl7::hl7_person_name& name);

    /**
     * @brief Convert DICOM PN to HL7 XPN
     */
    [[nodiscard]] static hl7::hl7_person_name dicom_name_to_hl7(
        std::string_view dicom_pn);

    /**
     * @brief Convert HL7 timestamp to DICOM date (YYYYMMDD)
     */
    [[nodiscard]] static std::string hl7_datetime_to_dicom_date(
        const hl7::hl7_timestamp& ts);

    /**
     * @brief Convert HL7 timestamp to DICOM time (HHMMSS)
     */
    [[nodiscard]] static std::string hl7_datetime_to_dicom_time(
        const hl7::hl7_timestamp& ts);

    /**
     * @brief Convert HL7 timestamp to DICOM datetime
     */
    [[nodiscard]] static std::string hl7_datetime_to_dicom(
        const hl7::hl7_timestamp& ts);

    /**
     * @brief Parse HL7 timestamp string to DICOM format
     */
    [[nodiscard]] static std::expected<std::string, mapping_error>
    parse_hl7_datetime(std::string_view hl7_ts);

    /**
     * @brief Convert HL7 sex code to DICOM
     *
     * HL7: M, F, O, U, A, N
     * DICOM: M, F, O
     */
    [[nodiscard]] static std::string hl7_sex_to_dicom(std::string_view hl7_sex);

    /**
     * @brief Convert HL7 priority to DICOM
     *
     * HL7: S=Stat, A=ASAP, R=Routine, T=Timing critical
     * DICOM: STAT, HIGH, MEDIUM, LOW
     */
    [[nodiscard]] static std::string hl7_priority_to_dicom(
        std::string_view hl7_priority);

    /**
     * @brief Generate DICOM UID
     *
     * @param root UID root (organization identifier)
     * @return Generated UID
     */
    [[nodiscard]] static std::string generate_uid(std::string_view root = "");

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const mapper_config& config() const noexcept;

    /**
     * @brief Set configuration
     */
    void set_config(const mapper_config& config);

    /**
     * @brief Register custom transform function
     *
     * @param name Transform name to use in mapping rules
     * @param func Transform function
     */
    void register_transform(std::string_view name, transform_function func);

    // =========================================================================
    // Validation
    // =========================================================================

    /**
     * @brief Validate MWL item for required fields
     *
     * @param item MWL item to validate
     * @return Validation errors (empty if valid)
     */
    [[nodiscard]] std::vector<std::string> validate_mwl(
        const mwl_item& item) const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Patient ID Mapping Helper
// =============================================================================

/**
 * @brief Helper for managing patient identifier mappings
 */
class patient_id_mapper {
public:
    /**
     * @brief Map HL7 patient identifiers to DICOM format
     *
     * @param hl7_ids List of HL7 CX identifiers from PID-3
     * @param primary_domain Domain to use for primary ID
     * @return Mapped patient IDs
     */
    [[nodiscard]] static dicom_patient map_identifiers(
        const std::vector<hl7::hl7_patient_id>& hl7_ids,
        std::string_view primary_domain = "");

    /**
     * @brief Parse PID-3 field into list of patient IDs
     */
    [[nodiscard]] static std::vector<hl7::hl7_patient_id> parse_pid3(
        const hl7::hl7_field& pid3);
};

}  // namespace pacs::bridge::mapping

#endif  // PACS_BRIDGE_MAPPING_HL7_DICOM_MAPPER_H
