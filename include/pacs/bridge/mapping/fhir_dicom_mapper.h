#ifndef PACS_BRIDGE_MAPPING_FHIR_DICOM_MAPPER_H
#define PACS_BRIDGE_MAPPING_FHIR_DICOM_MAPPER_H

/**
 * @file fhir_dicom_mapper.h
 * @brief FHIR to DICOM and DICOM to FHIR translation layer
 *
 * Provides bidirectional mapping between FHIR R4 resources and DICOM
 * datasets for MWL (Modality Worklist) and study queries.
 *
 * Supported mappings:
 *   - FHIR ServiceRequest -> DICOM MWL Scheduled Procedure Step
 *   - DICOM Study -> FHIR ImagingStudy
 *   - FHIR Patient <-> DICOM Patient (bidirectional)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/35
 * @see docs/reference_materials/05_mwl_mapping.md
 */

#include "hl7_dicom_mapper.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declarations
namespace pacs::bridge::fhir {
class patient_resource;
struct fhir_human_name;
struct fhir_identifier;
enum class administrative_gender;
}  // namespace pacs::bridge::fhir

namespace pacs::bridge::mapping {

// =============================================================================
// FHIR ServiceRequest Data Structure
// =============================================================================

/**
 * @brief FHIR Coding data type
 * @see https://hl7.org/fhir/R4/datatypes.html#Coding
 */
struct fhir_coding {
    /** The code system URI */
    std::string system;

    /** Version of the code system (optional) */
    std::optional<std::string> version;

    /** Symbol in syntax defined by the system */
    std::string code;

    /** Representation defined by the system */
    std::string display;
};

/**
 * @brief FHIR CodeableConcept data type
 * @see https://hl7.org/fhir/R4/datatypes.html#CodeableConcept
 */
struct fhir_codeable_concept {
    /** Code defined by a terminology system */
    std::vector<fhir_coding> coding;

    /** Plain text representation of the concept */
    std::optional<std::string> text;
};

/**
 * @brief FHIR Reference data type
 * @see https://hl7.org/fhir/R4/references.html
 */
struct fhir_reference {
    /** Literal reference (relative, absolute, or URN) */
    std::optional<std::string> reference;

    /** Type the reference refers to (e.g., "Patient") */
    std::optional<std::string> type;

    /** Logical identifier */
    std::optional<std::string> identifier;

    /** Text alternative for the resource */
    std::optional<std::string> display;
};

/**
 * @brief FHIR ServiceRequest resource data
 * @see https://hl7.org/fhir/R4/servicerequest.html
 */
struct fhir_service_request {
    /** Resource ID */
    std::string id;

    /** Identifiers assigned to this order */
    std::vector<std::pair<std::string, std::string>> identifiers;  // system, value

    /** Status: draft | active | completed | cancelled */
    std::string status = "active";

    /** Intent: proposal | plan | order */
    std::string intent = "order";

    /** Classification of service */
    std::optional<fhir_codeable_concept> category;

    /** What is being requested/ordered */
    fhir_codeable_concept code;

    /** Individual or Entity the service is ordered for */
    fhir_reference subject;

    /** Who/what is requesting service */
    std::optional<fhir_reference> requester;

    /** Requested performer */
    std::vector<fhir_reference> performer;

    /** When service should occur (dateTime) */
    std::optional<std::string> occurrence_date_time;

    /** Explanation/Justification for procedure */
    std::optional<std::string> reason_code;

    /** Additional clinical information */
    std::optional<std::string> note;

    /** Routine | urgent | asap | stat */
    std::string priority = "routine";
};

// =============================================================================
// FHIR ImagingStudy Data Structure
// =============================================================================

/**
 * @brief FHIR ImagingStudy.series element
 */
struct fhir_imaging_series {
    /** DICOM Series Instance UID */
    std::string uid;

    /** Numeric identifier of this series */
    std::optional<uint32_t> number;

    /** The modality of the instances in the series */
    fhir_coding modality;

    /** A short human readable summary of the series */
    std::optional<std::string> description;

    /** Number of Series Related Instances */
    std::optional<uint32_t> number_of_instances;

    /** Body part examined */
    std::optional<fhir_coding> body_site;

    /** DICOM SOP Instance UIDs in this series */
    std::vector<std::string> instance_uids;
};

/**
 * @brief FHIR ImagingStudy resource data
 * @see https://hl7.org/fhir/R4/imagingstudy.html
 */
struct fhir_imaging_study {
    /** Resource ID */
    std::string id;

    /** Identifiers for the whole study */
    std::vector<std::pair<std::string, std::string>> identifiers;  // system, value

    /** Status: registered | available | cancelled */
    std::string status = "available";

    /** Who or what is the subject of the imaging study */
    fhir_reference subject;

    /** When the study was started */
    std::optional<std::string> started;

    /** Request fulfilled */
    std::optional<fhir_reference> based_on;

    /** Referring physician */
    std::optional<fhir_reference> referrer;

    /** Study Instance UID */
    std::string study_instance_uid;

    /** Number of Study Related Series */
    std::optional<uint32_t> number_of_series;

    /** Number of Study Related Instances */
    std::optional<uint32_t> number_of_instances;

    /** Institution-generated description */
    std::optional<std::string> description;

    /** Each study has one or more series of images */
    std::vector<fhir_imaging_series> series;
};

// =============================================================================
// DICOM Study Data Structure
// =============================================================================

/**
 * @brief DICOM Series information
 */
struct dicom_series {
    /** Series Instance UID (0020,000E) */
    std::string series_instance_uid;

    /** Series Number (0020,0011) */
    std::optional<uint32_t> series_number;

    /** Modality (0008,0060) */
    std::string modality;

    /** Series Description (0008,103E) */
    std::string series_description;

    /** Number of Series Related Instances (0020,1209) */
    std::optional<uint32_t> number_of_instances;

    /** Body Part Examined (0018,0015) */
    std::string body_part_examined;

    /** Instance UIDs in this series */
    std::vector<std::string> instance_uids;
};

/**
 * @brief DICOM Study attributes
 */
struct dicom_study {
    /** Study Instance UID (0020,000D) */
    std::string study_instance_uid;

    /** Study Date (0008,0020) - YYYYMMDD */
    std::string study_date;

    /** Study Time (0008,0030) - HHMMSS */
    std::string study_time;

    /** Accession Number (0008,0050) */
    std::string accession_number;

    /** Study Description (0008,1030) */
    std::string study_description;

    /** Patient ID (0010,0020) */
    std::string patient_id;

    /** Patient Name (0010,0010) */
    std::string patient_name;

    /** Referring Physician's Name (0008,0090) */
    std::string referring_physician_name;

    /** Number of Study Related Series (0020,1206) */
    std::optional<uint32_t> number_of_series;

    /** Number of Study Related Instances (0020,1208) */
    std::optional<uint32_t> number_of_instances;

    /** Series in this study */
    std::vector<dicom_series> series;

    /** Study status */
    std::string status;
};

// =============================================================================
// Error Codes (-950 to -959)
// =============================================================================

/**
 * @brief FHIR-DICOM mapping specific error codes
 *
 * Allocated range: -950 to -959
 */
enum class fhir_dicom_error : int {
    /** Unsupported resource type for mapping */
    unsupported_resource_type = -950,

    /** Missing required field in FHIR resource */
    missing_required_field = -951,

    /** Invalid field value */
    invalid_field_value = -952,

    /** Patient reference could not be resolved */
    patient_not_found = -953,

    /** Code system translation failed */
    code_translation_failed = -954,

    /** Date/time format conversion failed */
    datetime_conversion_failed = -955,

    /** UID generation failed */
    uid_generation_failed = -956,

    /** Validation failed */
    validation_failed = -957,

    /** Internal mapping error */
    internal_error = -958
};

/**
 * @brief Convert fhir_dicom_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(fhir_dicom_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of error
 */
[[nodiscard]] constexpr const char* to_string(fhir_dicom_error error) noexcept {
    switch (error) {
        case fhir_dicom_error::unsupported_resource_type:
            return "Unsupported resource type for FHIR-DICOM mapping";
        case fhir_dicom_error::missing_required_field:
            return "Missing required field in FHIR resource";
        case fhir_dicom_error::invalid_field_value:
            return "Invalid field value for DICOM mapping";
        case fhir_dicom_error::patient_not_found:
            return "Patient reference could not be resolved";
        case fhir_dicom_error::code_translation_failed:
            return "Code system translation failed";
        case fhir_dicom_error::datetime_conversion_failed:
            return "Date/time format conversion failed";
        case fhir_dicom_error::uid_generation_failed:
            return "UID generation failed";
        case fhir_dicom_error::validation_failed:
            return "Mapped data validation failed";
        case fhir_dicom_error::internal_error:
            return "Internal mapping error";
        default:
            return "Unknown FHIR-DICOM mapping error";
    }
}

// =============================================================================
// Mapper Configuration
// =============================================================================

/**
 * @brief Configuration for FHIR-DICOM mapper
 */
struct fhir_dicom_mapper_config {
    /** UID root for generating new UIDs */
    std::string uid_root = "1.2.840.10008.5.1.4.1.2.2";

    /** Default character set */
    std::string specific_character_set = "ISO_IR 192";  // UTF-8

    /** Auto-generate Study Instance UID if not provided */
    bool auto_generate_study_uid = true;

    /** Auto-generate SPS ID if not provided */
    bool auto_generate_sps_id = true;

    /** Default modality if not specified */
    std::string default_modality = "OT";

    /** Default scheduled station AE title */
    std::string default_station_ae_title;

    /** Validate output data */
    bool validate_output = true;

    /** LOINC to DICOM code mapping enabled */
    bool enable_loinc_mapping = true;

    /** SNOMED to DICOM code mapping enabled */
    bool enable_snomed_mapping = true;
};

// =============================================================================
// FHIR-DICOM Mapper
// =============================================================================

/**
 * @brief FHIR to DICOM and DICOM to FHIR mapper
 *
 * Provides bidirectional conversion between FHIR R4 resources and
 * DICOM data structures for:
 *   - ServiceRequest to MWL (Modality Worklist)
 *   - DICOM Study to ImagingStudy
 *   - Patient demographics
 *
 * @example ServiceRequest to MWL
 * ```cpp
 * fhir_dicom_mapper mapper;
 *
 * fhir_service_request request;
 * request.code.coding.push_back({"http://loinc.org", {}, "24558-9", "CT Chest"});
 * request.subject.reference = "Patient/patient-123";
 * request.occurrence_date_time = "2024-01-15T10:00:00Z";
 *
 * auto result = mapper.service_request_to_mwl(request, patient);
 * if (result) {
 *     // Use MWL item
 *     std::cout << "Accession: " << result->imaging_service_request.accession_number;
 * }
 * ```
 *
 * @example DICOM Study to ImagingStudy
 * ```cpp
 * dicom_study study;
 * study.study_instance_uid = "1.2.3.4.5.6.7.8.9";
 * study.study_date = "20240115";
 * study.study_time = "103000";
 *
 * auto imaging_study = mapper.study_to_imaging_study(study);
 * ```
 *
 * Thread-safe: All operations are thread-safe.
 */
class fhir_dicom_mapper {
public:
    /**
     * @brief Patient lookup function type
     *
     * Given a patient reference (e.g., "Patient/123"), returns the
     * patient data from the cache.
     */
    using patient_lookup_function =
        std::function<Result<dicom_patient>(const std::string&)>;

    /**
     * @brief Default constructor with default configuration
     */
    fhir_dicom_mapper();

    /**
     * @brief Constructor with custom configuration
     */
    explicit fhir_dicom_mapper(const fhir_dicom_mapper_config& config);

    /**
     * @brief Destructor
     */
    ~fhir_dicom_mapper();

    // Non-copyable, movable
    fhir_dicom_mapper(const fhir_dicom_mapper&) = delete;
    fhir_dicom_mapper& operator=(const fhir_dicom_mapper&) = delete;
    fhir_dicom_mapper(fhir_dicom_mapper&&) noexcept;
    fhir_dicom_mapper& operator=(fhir_dicom_mapper&&) noexcept;

    // =========================================================================
    // ServiceRequest to MWL Mapping
    // =========================================================================

    /**
     * @brief Convert FHIR ServiceRequest to DICOM MWL item
     *
     * Maps FHIR ServiceRequest resource to DICOM Modality Worklist
     * Scheduled Procedure Step.
     *
     * Mapping:
     *   - subject.reference -> Patient lookup -> PatientID
     *   - code.coding[0] -> Scheduled Protocol Code Sequence
     *   - occurrenceDateTime -> Scheduled Start Date/Time
     *   - performer[0] -> Scheduled Station AE Title
     *   - requester -> Referring Physician Name
     *
     * @param request FHIR ServiceRequest resource
     * @param patient DICOM patient data (pre-resolved)
     * @return MWL item or error
     */
    [[nodiscard]] Result<mwl_item>
    service_request_to_mwl(const fhir_service_request& request,
                           const dicom_patient& patient) const;

    /**
     * @brief Convert FHIR ServiceRequest to MWL with patient lookup
     *
     * Same as service_request_to_mwl but resolves patient reference
     * using the configured patient lookup function.
     *
     * @param request FHIR ServiceRequest resource
     * @return MWL item or error
     */
    [[nodiscard]] Result<mwl_item>
    service_request_to_mwl(const fhir_service_request& request) const;

    // =========================================================================
    // DICOM Study to ImagingStudy Mapping
    // =========================================================================

    /**
     * @brief Convert DICOM Study to FHIR ImagingStudy
     *
     * Maps DICOM Study data to FHIR R4 ImagingStudy resource.
     *
     * Mapping:
     *   - StudyInstanceUID -> identifier[0]
     *   - StudyDate/Time -> started
     *   - AccessionNumber -> identifier[1]
     *   - StudyDescription -> description
     *   - NumberOfSeries -> numberOfSeries
     *   - NumberOfInstances -> numberOfInstances
     *
     * @param study DICOM study data
     * @param patient_reference Optional patient reference (e.g., "Patient/123")
     * @return ImagingStudy data or error
     */
    [[nodiscard]] Result<fhir_imaging_study>
    study_to_imaging_study(
        const dicom_study& study,
        const std::optional<std::string>& patient_reference = std::nullopt) const;

    // =========================================================================
    // Patient Mapping
    // =========================================================================

    /**
     * @brief Convert DICOM patient to FHIR Patient resource data
     *
     * @param dicom_patient DICOM patient attributes
     * @return FHIR patient data (can be used to create patient_resource)
     */
    [[nodiscard]] Result<std::unique_ptr<fhir::patient_resource>>
    dicom_to_fhir_patient(const dicom_patient& dicom_patient) const;

    /**
     * @brief Convert FHIR Patient resource to DICOM patient attributes
     *
     * @param patient FHIR Patient resource
     * @return DICOM patient data
     */
    [[nodiscard]] Result<dicom_patient>
    fhir_to_dicom_patient(const fhir::patient_resource& patient) const;

    // =========================================================================
    // Code System Translation
    // =========================================================================

    /**
     * @brief Translate LOINC code to DICOM procedure code
     *
     * @param loinc_code LOINC code
     * @return DICOM procedure code or empty if not found
     */
    [[nodiscard]] std::optional<fhir_coding> loinc_to_dicom(
        const std::string& loinc_code) const;

    /**
     * @brief Translate SNOMED code to DICOM body site code
     *
     * @param snomed_code SNOMED CT code
     * @return DICOM body site code or empty if not found
     */
    [[nodiscard]] std::optional<fhir_coding> snomed_to_dicom(
        const std::string& snomed_code) const;

    // =========================================================================
    // Utility Functions
    // =========================================================================

    /**
     * @brief Generate a new DICOM UID
     *
     * @param suffix Optional suffix to append
     * @return Generated UID
     */
    [[nodiscard]] std::string generate_uid(
        const std::string& suffix = "") const;

    /**
     * @brief Convert FHIR dateTime to DICOM date/time
     *
     * FHIR: YYYY-MM-DDTHH:MM:SS[.SSS][Z|+HH:MM]
     * DICOM Date: YYYYMMDD
     * DICOM Time: HHMMSS[.FFFFFF]
     *
     * @param fhir_datetime FHIR datetime string
     * @return Pair of (date, time) strings or error
     */
    [[nodiscard]] static Result<std::pair<std::string, std::string>>
    fhir_datetime_to_dicom(std::string_view fhir_datetime);

    /**
     * @brief Convert DICOM date/time to FHIR dateTime
     *
     * @param dicom_date DICOM date (YYYYMMDD)
     * @param dicom_time DICOM time (HHMMSS[.FFFFFF])
     * @return FHIR dateTime string
     */
    [[nodiscard]] static Result<std::string>
    dicom_datetime_to_fhir(std::string_view dicom_date,
                           std::string_view dicom_time);

    /**
     * @brief Convert FHIR priority to DICOM priority
     *
     * FHIR: routine | urgent | asap | stat
     * DICOM: LOW | MEDIUM | HIGH | STAT
     *
     * @param fhir_priority FHIR priority code
     * @return DICOM priority code
     */
    [[nodiscard]] static std::string fhir_priority_to_dicom(
        std::string_view fhir_priority);

    /**
     * @brief Convert DICOM priority to FHIR priority
     *
     * @param dicom_priority DICOM priority code
     * @return FHIR priority code
     */
    [[nodiscard]] static std::string dicom_priority_to_fhir(
        std::string_view dicom_priority);

    /**
     * @brief Parse patient reference to extract patient ID
     *
     * @param reference Reference string (e.g., "Patient/123")
     * @return Patient ID or empty if invalid
     */
    [[nodiscard]] static std::optional<std::string>
    parse_patient_reference(const std::string& reference);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const fhir_dicom_mapper_config& config() const noexcept;

    /**
     * @brief Set configuration
     */
    void set_config(const fhir_dicom_mapper_config& config);

    /**
     * @brief Set patient lookup function
     *
     * @param lookup Function to resolve patient references
     */
    void set_patient_lookup(patient_lookup_function lookup);

    // =========================================================================
    // Validation
    // =========================================================================

    /**
     * @brief Validate FHIR ServiceRequest for MWL mapping
     *
     * @param request ServiceRequest to validate
     * @return List of validation errors (empty if valid)
     */
    [[nodiscard]] std::vector<std::string> validate_service_request(
        const fhir_service_request& request) const;

    /**
     * @brief Validate MWL item
     *
     * @param item MWL item to validate
     * @return List of validation errors (empty if valid)
     */
    [[nodiscard]] std::vector<std::string> validate_mwl(
        const mwl_item& item) const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// JSON Serialization Helpers
// =============================================================================

/**
 * @brief Serialize fhir_imaging_study to JSON
 */
[[nodiscard]] std::string imaging_study_to_json(const fhir_imaging_study& study);

/**
 * @brief Parse fhir_service_request from JSON
 *
 * @param json JSON string
 * @return Parsed ServiceRequest or error
 */
[[nodiscard]] Result<fhir_service_request>
service_request_from_json(const std::string& json);

/**
 * @brief Serialize fhir_service_request to JSON
 */
[[nodiscard]] std::string service_request_to_json(const fhir_service_request& request);

}  // namespace pacs::bridge::mapping

#endif  // PACS_BRIDGE_MAPPING_FHIR_DICOM_MAPPER_H
