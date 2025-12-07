#ifndef PACS_BRIDGE_MAPPING_DICOM_HL7_MAPPER_H
#define PACS_BRIDGE_MAPPING_DICOM_HL7_MAPPER_H

/**
 * @file dicom_hl7_mapper.h
 * @brief DICOM to HL7 mapper for MPPS to ORM/ORU message conversion
 *
 * Provides mapping functionality to convert DICOM MPPS (Modality Performed
 * Procedure Step) notifications into HL7 v2.x messages for status updates.
 * This enables bidirectional communication between PACS and HIS/RIS systems.
 *
 * Supported mappings:
 *   - MPPS IN PROGRESS → ORM^O01 (ORC-1=SC, ORC-5=IP) - Exam started
 *   - MPPS COMPLETED → ORM^O01 (ORC-1=SC, ORC-5=CM) - Exam completed
 *   - MPPS DISCONTINUED → ORM^O01 (ORC-1=DC, ORC-5=CA) - Exam cancelled
 *
 * Key field mappings:
 *   - AccessionNumber → ORC-3 (Filler Order Number)
 *   - PerformedProcedureStepStatus → ORC-5 (Order Status)
 *   - PerformedStationAETitle → OBR-21 (Filler Field 1)
 *   - PerformedProcedureStepStartDateTime → OBR-22 (Results Rpt/Status Chng)
 *   - PerformedProcedureStepEndDateTime → OBR-27 (Quantity/Timing)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/24
 * @see docs/reference_materials/07_dicom_hl7_mapping.md
 */

#include "pacs/bridge/pacs_adapter/mpps_handler.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::mapping {

// =============================================================================
// Error Codes (-930 to -939)
// =============================================================================

/**
 * @brief DICOM to HL7 mapper specific error codes
 *
 * Allocated range: -930 to -939
 */
enum class dicom_hl7_error : int {
    /** Missing required MPPS attribute */
    missing_required_attribute = -930,

    /** Invalid MPPS status for mapping */
    invalid_mpps_status = -931,

    /** Date/time format conversion failed */
    datetime_conversion_failed = -932,

    /** Patient name conversion failed */
    name_conversion_failed = -933,

    /** Message building failed */
    message_build_failed = -934,

    /** Invalid accession number */
    invalid_accession_number = -935,

    /** Missing study instance UID */
    missing_study_uid = -936,

    /** Custom transform function error */
    custom_transform_error = -937,

    /** Message serialization failed */
    serialization_failed = -938
};

/**
 * @brief Convert dicom_hl7_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(dicom_hl7_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of error
 */
[[nodiscard]] constexpr const char* to_string(dicom_hl7_error error) noexcept {
    switch (error) {
        case dicom_hl7_error::missing_required_attribute:
            return "Missing required MPPS attribute";
        case dicom_hl7_error::invalid_mpps_status:
            return "Invalid MPPS status for mapping";
        case dicom_hl7_error::datetime_conversion_failed:
            return "Date/time format conversion failed";
        case dicom_hl7_error::name_conversion_failed:
            return "Patient name conversion failed";
        case dicom_hl7_error::message_build_failed:
            return "Message building failed";
        case dicom_hl7_error::invalid_accession_number:
            return "Invalid accession number";
        case dicom_hl7_error::missing_study_uid:
            return "Missing study instance UID";
        case dicom_hl7_error::custom_transform_error:
            return "Custom transform function returned error";
        case dicom_hl7_error::serialization_failed:
            return "Message serialization failed";
        default:
            return "Unknown DICOM to HL7 mapping error";
    }
}

// =============================================================================
// Mapper Configuration
// =============================================================================

/**
 * @brief Configuration for DICOM to HL7 mapper
 */
struct dicom_hl7_mapper_config {
    /** Sending application for HL7 messages (MSH-3) */
    std::string sending_application = "PACS_BRIDGE";

    /** Sending facility (MSH-4) */
    std::string sending_facility;

    /** Receiving application (MSH-5) */
    std::string receiving_application = "HIS";

    /** Receiving facility (MSH-6) */
    std::string receiving_facility;

    /** HL7 version to use */
    std::string hl7_version = "2.5.1";

    /** Processing ID (P=Production, T=Training, D=Debug) */
    std::string processing_id = "P";

    /** Include TXA segment for timing details */
    bool include_timing_details = true;

    /** Include series-level information in OBX segments */
    bool include_series_info = true;

    /** Generate unique message control IDs */
    bool auto_generate_control_id = true;

    /** Include discontinuation reason when available */
    bool include_discontinuation_reason = true;

    /** Validate required fields before building */
    bool validate_before_build = true;
};

// =============================================================================
// Mapping Result
// =============================================================================

/**
 * @brief Result of MPPS to HL7 mapping operation
 */
struct mpps_mapping_result {
    /** Generated HL7 message */
    hl7::hl7_message message;

    /** Message control ID */
    std::string control_id;

    /** Original accession number */
    std::string accession_number;

    /** MPPS status mapped */
    pacs_adapter::mpps_event mpps_status;

    /** HL7 order status code (IP, CM, CA) */
    std::string order_status;

    /** HL7 order control code (SC, DC) */
    std::string order_control;

    /** Warnings generated during mapping (non-fatal issues) */
    std::vector<std::string> warnings;

    /**
     * @brief Check if mapping had any warnings
     */
    [[nodiscard]] bool has_warnings() const noexcept {
        return !warnings.empty();
    }
};

// =============================================================================
// DICOM to HL7 Mapper
// =============================================================================

/**
 * @brief DICOM to HL7 message mapper for MPPS notifications
 *
 * Converts DICOM MPPS datasets to HL7 v2.x ORM/ORU messages for
 * status update notifications to HIS/RIS systems.
 *
 * @example Basic Usage
 * ```cpp
 * dicom_hl7_mapper mapper;
 *
 * // When MPPS IN PROGRESS is received
 * auto result = mapper.mpps_to_orm(mpps_data, pacs_adapter::mpps_event::in_progress);
 * if (result) {
 *     std::string hl7_msg = result->message.serialize();
 *     // Send to HIS via MLLP
 * }
 * ```
 *
 * @example With Custom Configuration
 * ```cpp
 * dicom_hl7_mapper_config config;
 * config.sending_application = "RADIOLOGY_PACS";
 * config.sending_facility = "HOSPITAL_A";
 * config.receiving_application = "EPIC_HIS";
 *
 * dicom_hl7_mapper mapper(config);
 * ```
 *
 * @example Processing MPPS Events
 * ```cpp
 * // Register as MPPS callback
 * mpps_handler.set_callback([&mapper, &mllp_client](
 *     mpps_event event, const mpps_dataset& mpps) {
 *     auto result = mapper.mpps_to_orm(mpps, event);
 *     if (result) {
 *         mllp_client.send(result->message.serialize());
 *     }
 * });
 * ```
 */
class dicom_hl7_mapper {
public:
    /**
     * @brief Custom transform function type
     *
     * Takes source value, returns transformed value or error.
     */
    using transform_function =
        std::function<std::expected<std::string, dicom_hl7_error>(std::string_view)>;

    /**
     * @brief Default constructor with default configuration
     */
    dicom_hl7_mapper();

    /**
     * @brief Constructor with custom configuration
     */
    explicit dicom_hl7_mapper(const dicom_hl7_mapper_config& config);

    /**
     * @brief Destructor
     */
    ~dicom_hl7_mapper();

    // Non-copyable, movable
    dicom_hl7_mapper(const dicom_hl7_mapper&) = delete;
    dicom_hl7_mapper& operator=(const dicom_hl7_mapper&) = delete;
    dicom_hl7_mapper(dicom_hl7_mapper&&) noexcept;
    dicom_hl7_mapper& operator=(dicom_hl7_mapper&&) noexcept;

    // =========================================================================
    // MPPS to ORM Mapping
    // =========================================================================

    /**
     * @brief Convert MPPS dataset to ORM^O01 status update message
     *
     * Maps the MPPS status to appropriate HL7 order control and status codes:
     *   - IN PROGRESS: ORC-1=SC (Status Changed), ORC-5=IP (In Progress)
     *   - COMPLETED: ORC-1=SC (Status Changed), ORC-5=CM (Completed)
     *   - DISCONTINUED: ORC-1=DC (Discontinue Order), ORC-5=CA (Cancelled)
     *
     * @param mpps MPPS dataset from pacs_system
     * @param event MPPS event type
     * @return Mapping result with ORM message or error
     */
    [[nodiscard]] std::expected<mpps_mapping_result, dicom_hl7_error>
    mpps_to_orm(const pacs_adapter::mpps_dataset& mpps,
                pacs_adapter::mpps_event event) const;

    /**
     * @brief Convert MPPS IN PROGRESS to ORM^O01
     *
     * Convenience method for mapping IN PROGRESS status.
     * Generates ORM with ORC-1=SC, ORC-5=IP.
     *
     * @param mpps MPPS dataset
     * @return Mapping result or error
     */
    [[nodiscard]] std::expected<mpps_mapping_result, dicom_hl7_error>
    mpps_in_progress_to_orm(const pacs_adapter::mpps_dataset& mpps) const;

    /**
     * @brief Convert MPPS COMPLETED to ORM^O01
     *
     * Convenience method for mapping COMPLETED status.
     * Generates ORM with ORC-1=SC, ORC-5=CM.
     *
     * @param mpps MPPS dataset
     * @return Mapping result or error
     */
    [[nodiscard]] std::expected<mpps_mapping_result, dicom_hl7_error>
    mpps_completed_to_orm(const pacs_adapter::mpps_dataset& mpps) const;

    /**
     * @brief Convert MPPS DISCONTINUED to ORM^O01
     *
     * Convenience method for mapping DISCONTINUED status.
     * Generates ORM with ORC-1=DC, ORC-5=CA.
     *
     * @param mpps MPPS dataset
     * @return Mapping result or error
     */
    [[nodiscard]] std::expected<mpps_mapping_result, dicom_hl7_error>
    mpps_discontinued_to_orm(const pacs_adapter::mpps_dataset& mpps) const;

    // =========================================================================
    // Utility Conversion Functions
    // =========================================================================

    /**
     * @brief Convert DICOM date (YYYYMMDD) to HL7 format (YYYYMMDD)
     *
     * DICOM and HL7 use the same date format, but this validates
     * and normalizes the input.
     *
     * @param dicom_date DICOM date string
     * @return HL7 date string or error
     */
    [[nodiscard]] static std::expected<std::string, dicom_hl7_error>
    dicom_date_to_hl7(std::string_view dicom_date);

    /**
     * @brief Convert DICOM time to HL7 format
     *
     * DICOM: HHMMSS.FFFFFF
     * HL7:   HHMMSS[.S[S[S[S]]]]
     *
     * @param dicom_time DICOM time string
     * @return HL7 time string or error
     */
    [[nodiscard]] static std::expected<std::string, dicom_hl7_error>
    dicom_time_to_hl7(std::string_view dicom_time);

    /**
     * @brief Convert DICOM datetime to HL7 timestamp
     *
     * Combines DICOM date and time into HL7 timestamp format.
     *
     * @param dicom_date DICOM date (YYYYMMDD)
     * @param dicom_time DICOM time (HHMMSS[.FFFFFF])
     * @return HL7 timestamp or error
     */
    [[nodiscard]] static std::expected<hl7::hl7_timestamp, dicom_hl7_error>
    dicom_datetime_to_hl7_timestamp(std::string_view dicom_date,
                                     std::string_view dicom_time);

    /**
     * @brief Convert DICOM Patient Name (PN) to HL7 person name (XPN)
     *
     * DICOM PN: Family^Given^Middle^Prefix^Suffix
     * HL7 XPN:  Family^Given^Middle^Suffix^Prefix^Degree
     *
     * @param dicom_pn DICOM patient name
     * @return HL7 person name structure
     */
    [[nodiscard]] static hl7::hl7_person_name
    dicom_name_to_hl7(std::string_view dicom_pn);

    /**
     * @brief Map MPPS status to HL7 order status code
     *
     * @param event MPPS event type
     * @return HL7 order status code (IP, CM, CA)
     */
    [[nodiscard]] static std::string
    mpps_status_to_hl7_order_status(pacs_adapter::mpps_event event);

    /**
     * @brief Map MPPS status to HL7 order control code
     *
     * @param event MPPS event type
     * @return HL7 order control code (SC, DC)
     */
    [[nodiscard]] static std::string
    mpps_status_to_hl7_order_control(pacs_adapter::mpps_event event);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const dicom_hl7_mapper_config& config() const noexcept;

    /**
     * @brief Set configuration
     */
    void set_config(const dicom_hl7_mapper_config& config);

    /**
     * @brief Register custom transform function
     *
     * @param name Transform name
     * @param func Transform function
     */
    void register_transform(std::string_view name, transform_function func);

    // =========================================================================
    // Validation
    // =========================================================================

    /**
     * @brief Validate MPPS dataset has required fields for mapping
     *
     * @param mpps MPPS dataset to validate
     * @return List of missing or invalid fields (empty if valid)
     */
    [[nodiscard]] std::vector<std::string>
    validate_mpps(const pacs_adapter::mpps_dataset& mpps) const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::mapping

#endif  // PACS_BRIDGE_MAPPING_DICOM_HL7_MAPPER_H
