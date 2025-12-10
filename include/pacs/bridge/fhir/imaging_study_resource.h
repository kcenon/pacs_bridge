#ifndef PACS_BRIDGE_FHIR_IMAGING_STUDY_RESOURCE_H
#define PACS_BRIDGE_FHIR_IMAGING_STUDY_RESOURCE_H

/**
 * @file imaging_study_resource.h
 * @brief FHIR ImagingStudy resource implementation
 *
 * Implements the FHIR R4 ImagingStudy resource for representing imaging
 * study information. Provides mapping from DICOM study queries to FHIR format.
 *
 * @see https://hl7.org/fhir/R4/imagingstudy.html
 * @see https://github.com/kcenon/pacs_bridge/issues/34
 */

#include "fhir_resource.h"
#include "fhir_types.h"
#include "resource_handler.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declarations
namespace pacs::bridge::mapping {
class fhir_dicom_mapper;
struct fhir_imaging_study;
struct fhir_imaging_series;
struct fhir_coding;
struct fhir_reference;
struct dicom_study;
}  // namespace pacs::bridge::mapping

namespace pacs::bridge::fhir {

// =============================================================================
// FHIR ImagingStudy Status Codes
// =============================================================================

/**
 * @brief FHIR ImagingStudy status codes
 *
 * @see https://hl7.org/fhir/R4/valueset-imagingstudy-status.html
 */
enum class imaging_study_status {
    registered,
    available,
    cancelled,
    entered_in_error,
    unknown
};

/**
 * @brief Convert imaging_study_status to FHIR code string
 */
[[nodiscard]] constexpr std::string_view to_string(
    imaging_study_status status) noexcept {
    switch (status) {
        case imaging_study_status::registered: return "registered";
        case imaging_study_status::available: return "available";
        case imaging_study_status::cancelled: return "cancelled";
        case imaging_study_status::entered_in_error: return "entered-in-error";
        case imaging_study_status::unknown: return "unknown";
    }
    return "unknown";
}

/**
 * @brief Parse imaging_study_status from string
 */
[[nodiscard]] std::optional<imaging_study_status> parse_imaging_study_status(
    std::string_view status_str) noexcept;

// =============================================================================
// FHIR ImagingStudy Data Types
// =============================================================================

/**
 * @brief FHIR Coding data type for ImagingStudy
 *
 * @see https://hl7.org/fhir/R4/datatypes.html#Coding
 */
struct imaging_study_coding {
    std::string system;
    std::optional<std::string> version;
    std::string code;
    std::string display;
};

/**
 * @brief FHIR Reference data type for ImagingStudy
 *
 * @see https://hl7.org/fhir/R4/references.html
 */
struct imaging_study_reference {
    std::optional<std::string> reference;
    std::optional<std::string> type;
    std::optional<std::string> identifier;
    std::optional<std::string> display;
};

/**
 * @brief FHIR Identifier data type for ImagingStudy
 */
struct imaging_study_identifier {
    std::optional<std::string> use;
    std::optional<std::string> system;
    std::string value;
    std::optional<std::string> type_text;
};

/**
 * @brief FHIR ImagingStudy.series element
 *
 * Represents a series within an imaging study.
 */
struct imaging_study_series {
    /** DICOM Series Instance UID */
    std::string uid;

    /** Numeric identifier of this series */
    std::optional<uint32_t> number;

    /** The modality of the instances in the series */
    imaging_study_coding modality;

    /** A short human readable summary of the series */
    std::optional<std::string> description;

    /** Number of Series Related Instances */
    std::optional<uint32_t> number_of_instances;

    /** Body part examined */
    std::optional<imaging_study_coding> body_site;

    /** Laterality (left, right, bilateral) */
    std::optional<imaging_study_coding> laterality;

    /** When the series started */
    std::optional<std::string> started;

    /** Who performed the series */
    std::vector<imaging_study_reference> performer;
};

// =============================================================================
// FHIR ImagingStudy Resource
// =============================================================================

/**
 * @brief FHIR R4 ImagingStudy resource
 *
 * Represents imaging study information per FHIR R4 specification.
 * Maps from DICOM study queries to FHIR ImagingStudy resource format.
 *
 * @example Creating an ImagingStudy Resource
 * ```cpp
 * imaging_study_resource study;
 * study.set_id("study-123");
 * study.set_status(imaging_study_status::available);
 *
 * // Set Study Instance UID
 * imaging_study_identifier uid_ident;
 * uid_ident.system = "urn:dicom:uid";
 * uid_ident.value = "1.2.3.4.5.6.7.8.9";
 * study.add_identifier(uid_ident);
 *
 * // Set subject
 * imaging_study_reference subject;
 * subject.reference = "Patient/patient-123";
 * study.set_subject(subject);
 *
 * // Set started time
 * study.set_started("2024-01-15T10:30:00Z");
 *
 * // Add series
 * imaging_study_series series;
 * series.uid = "1.2.3.4.5.6.7.8.9.1";
 * series.modality.system = "http://dicom.nema.org/resources/ontology/DCM";
 * series.modality.code = "CT";
 * series.modality.display = "Computed Tomography";
 * series.number_of_instances = 50;
 * study.add_series(series);
 *
 * // Serialize to JSON
 * std::string json = study.to_json();
 * ```
 *
 * @see https://hl7.org/fhir/R4/imagingstudy.html
 */
class imaging_study_resource : public fhir_resource {
public:
    /**
     * @brief Default constructor
     */
    imaging_study_resource();

    /**
     * @brief Destructor
     */
    ~imaging_study_resource() override;

    // Non-copyable, movable
    imaging_study_resource(const imaging_study_resource&) = delete;
    imaging_study_resource& operator=(const imaging_study_resource&) = delete;
    imaging_study_resource(imaging_study_resource&&) noexcept;
    imaging_study_resource& operator=(imaging_study_resource&&) noexcept;

    // =========================================================================
    // fhir_resource Interface
    // =========================================================================

    /**
     * @brief Get resource type
     */
    [[nodiscard]] resource_type type() const noexcept override;

    /**
     * @brief Get resource type name
     */
    [[nodiscard]] std::string type_name() const override;

    /**
     * @brief Serialize to JSON
     */
    [[nodiscard]] std::string to_json() const override;

    /**
     * @brief Validate the resource
     */
    [[nodiscard]] bool validate() const override;

    // =========================================================================
    // Identifiers
    // =========================================================================

    /**
     * @brief Add an identifier to the imaging study
     */
    void add_identifier(const imaging_study_identifier& identifier);

    /**
     * @brief Get all identifiers
     */
    [[nodiscard]] const std::vector<imaging_study_identifier>& identifiers() const noexcept;

    /**
     * @brief Clear all identifiers
     */
    void clear_identifiers();

    // =========================================================================
    // Status
    // =========================================================================

    /**
     * @brief Set status (required)
     */
    void set_status(imaging_study_status status);

    /**
     * @brief Get status
     */
    [[nodiscard]] imaging_study_status status() const noexcept;

    // =========================================================================
    // Subject (Patient Reference)
    // =========================================================================

    /**
     * @brief Set subject (patient reference, required)
     */
    void set_subject(const imaging_study_reference& subject);

    /**
     * @brief Get subject
     */
    [[nodiscard]] const std::optional<imaging_study_reference>& subject() const noexcept;

    // =========================================================================
    // Study Information
    // =========================================================================

    /**
     * @brief Set started date/time (ISO 8601 format)
     */
    void set_started(std::string datetime);

    /**
     * @brief Get started date/time
     */
    [[nodiscard]] const std::optional<std::string>& started() const noexcept;

    /**
     * @brief Set based-on reference (request that was fulfilled)
     */
    void set_based_on(const imaging_study_reference& based_on);

    /**
     * @brief Get based-on reference
     */
    [[nodiscard]] const std::optional<imaging_study_reference>& based_on() const noexcept;

    /**
     * @brief Set referrer (referring physician)
     */
    void set_referrer(const imaging_study_reference& referrer);

    /**
     * @brief Get referrer
     */
    [[nodiscard]] const std::optional<imaging_study_reference>& referrer() const noexcept;

    /**
     * @brief Set number of series
     */
    void set_number_of_series(uint32_t count);

    /**
     * @brief Get number of series
     */
    [[nodiscard]] std::optional<uint32_t> number_of_series() const noexcept;

    /**
     * @brief Set number of instances
     */
    void set_number_of_instances(uint32_t count);

    /**
     * @brief Get number of instances
     */
    [[nodiscard]] std::optional<uint32_t> number_of_instances() const noexcept;

    /**
     * @brief Set description
     */
    void set_description(std::string description);

    /**
     * @brief Get description
     */
    [[nodiscard]] const std::optional<std::string>& description() const noexcept;

    // =========================================================================
    // Series
    // =========================================================================

    /**
     * @brief Add a series to the imaging study
     */
    void add_series(const imaging_study_series& series);

    /**
     * @brief Get all series
     */
    [[nodiscard]] const std::vector<imaging_study_series>& series() const noexcept;

    /**
     * @brief Clear all series
     */
    void clear_series();

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Create ImagingStudy resource from JSON
     * @param json JSON string
     * @return ImagingStudy resource or nullptr on parse error
     */
    [[nodiscard]] static std::unique_ptr<imaging_study_resource> from_json(
        const std::string& json);

    /**
     * @brief Create from mapping::fhir_imaging_study
     * @param study fhir_imaging_study structure from mapper
     * @return ImagingStudy resource
     */
    [[nodiscard]] static std::unique_ptr<imaging_study_resource> from_mapping_struct(
        const mapping::fhir_imaging_study& study);

    /**
     * @brief Convert to mapping::fhir_imaging_study
     * @return fhir_imaging_study structure for mapping
     */
    [[nodiscard]] mapping::fhir_imaging_study to_mapping_struct() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// ImagingStudy Storage Interface
// =============================================================================

/**
 * @brief Study storage interface for ImagingStudy handler
 *
 * Abstracts the study storage to allow different backend implementations
 * (in-memory cache, database, PACS query, etc.).
 */
class study_storage {
public:
    virtual ~study_storage() = default;

    /**
     * @brief Get study by ID
     * @param id Study ID (resource ID, not Study Instance UID)
     * @return DICOM study data or nullopt if not found
     */
    [[nodiscard]] virtual std::optional<mapping::dicom_study> get(
        const std::string& id) const = 0;

    /**
     * @brief Get study by Study Instance UID
     * @param uid Study Instance UID
     * @return DICOM study data or nullopt if not found
     */
    [[nodiscard]] virtual std::optional<mapping::dicom_study> get_by_uid(
        const std::string& uid) const = 0;

    /**
     * @brief Search studies
     * @param patient_id Optional patient ID filter
     * @param accession_number Optional accession number filter
     * @param status Optional status filter
     * @param modality Optional modality filter
     * @return List of matching studies
     */
    [[nodiscard]] virtual std::vector<mapping::dicom_study> search(
        const std::optional<std::string>& patient_id = std::nullopt,
        const std::optional<std::string>& accession_number = std::nullopt,
        const std::optional<std::string>& status = std::nullopt,
        const std::optional<std::string>& modality = std::nullopt) const = 0;

    /**
     * @brief Get all study IDs
     * @return List of study IDs
     */
    [[nodiscard]] virtual std::vector<std::string> keys() const = 0;
};

/**
 * @brief In-memory study storage implementation
 */
class in_memory_study_storage : public study_storage {
public:
    in_memory_study_storage();
    ~in_memory_study_storage() override;

    /**
     * @brief Store a study
     * @param id Study ID
     * @param study DICOM study data
     * @return true on success
     */
    bool store(const std::string& id, const mapping::dicom_study& study);

    [[nodiscard]] std::optional<mapping::dicom_study> get(
        const std::string& id) const override;
    [[nodiscard]] std::optional<mapping::dicom_study> get_by_uid(
        const std::string& uid) const override;
    [[nodiscard]] std::vector<mapping::dicom_study> search(
        const std::optional<std::string>& patient_id,
        const std::optional<std::string>& accession_number,
        const std::optional<std::string>& status,
        const std::optional<std::string>& modality) const override;
    [[nodiscard]] std::vector<std::string> keys() const override;

    /**
     * @brief Remove a study
     * @param id Study ID
     * @return true if removed
     */
    bool remove(const std::string& id);

    /**
     * @brief Clear all studies
     */
    void clear();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// ImagingStudy Resource Handler
// =============================================================================

/**
 * @brief Handler for FHIR ImagingStudy resource operations
 *
 * Implements read and search operations for ImagingStudy resources by
 * querying study storage and mapping DICOM attributes to FHIR format.
 *
 * Supported operations:
 * - read: GET /ImagingStudy/{id}
 * - search: GET /ImagingStudy?patient=xxx
 * - search: GET /ImagingStudy?identifier=xxx (accession number)
 * - search: GET /ImagingStudy?status=xxx
 *
 * @example Basic Usage
 * ```cpp
 * auto mapper = std::make_shared<fhir_dicom_mapper>();
 * auto storage = std::make_shared<in_memory_study_storage>();
 * // ... populate storage with studies ...
 *
 * auto handler = std::make_shared<imaging_study_handler>(mapper, storage);
 *
 * // Read by ID
 * auto result = handler->read("study-123");
 * if (is_success(result)) {
 *     auto& study = get_resource(result);
 *     std::cout << study->to_json() << std::endl;
 * }
 *
 * // Search by patient
 * std::map<std::string, std::string> params = {{"patient", "Patient/patient-123"}};
 * auto search_result = handler->search(params, {});
 * ```
 *
 * Thread-safety: All operations are thread-safe.
 */
class imaging_study_handler : public resource_handler {
public:
    /**
     * @brief Constructor
     * @param mapper FHIR-DICOM mapper for converting studies
     * @param storage Study storage backend
     */
    imaging_study_handler(
        std::shared_ptr<mapping::fhir_dicom_mapper> mapper,
        std::shared_ptr<study_storage> storage);

    /**
     * @brief Destructor
     */
    ~imaging_study_handler() override;

    // Non-copyable, movable
    imaging_study_handler(const imaging_study_handler&) = delete;
    imaging_study_handler& operator=(const imaging_study_handler&) = delete;
    imaging_study_handler(imaging_study_handler&&) noexcept;
    imaging_study_handler& operator=(imaging_study_handler&&) noexcept;

    // =========================================================================
    // resource_handler Interface
    // =========================================================================

    /**
     * @brief Get handled resource type
     */
    [[nodiscard]] resource_type handled_type() const noexcept override;

    /**
     * @brief Get resource type name
     */
    [[nodiscard]] std::string_view type_name() const noexcept override;

    /**
     * @brief Read ImagingStudy by ID
     */
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> read(
        const std::string& id) override;

    /**
     * @brief Search for ImagingStudies
     *
     * Supported search parameters:
     * - _id: Resource ID
     * - patient: Patient reference (e.g., "Patient/123")
     * - identifier: Study Instance UID or accession number
     * - status: Study status (registered, available, cancelled)
     * - started: Study start date (YYYY-MM-DD)
     */
    [[nodiscard]] resource_result<search_result> search(
        const std::map<std::string, std::string>& params,
        const pagination_params& pagination) override;

    /**
     * @brief Get supported search parameters
     */
    [[nodiscard]] std::map<std::string, std::string>
    supported_search_params() const override;

    /**
     * @brief Check if interaction is supported
     */
    [[nodiscard]] bool supports_interaction(
        interaction_type type) const noexcept override;

    /**
     * @brief Get supported interactions
     */
    [[nodiscard]] std::vector<interaction_type>
    supported_interactions() const override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Convert DICOM study to FHIR ImagingStudy resource
 *
 * @param dicom_study Source DICOM study data
 * @param patient_reference Optional patient reference (e.g., "Patient/123")
 * @return FHIR ImagingStudy resource
 */
[[nodiscard]] std::unique_ptr<imaging_study_resource> dicom_to_fhir_imaging_study(
    const mapping::dicom_study& dicom_study,
    const std::optional<std::string>& patient_reference = std::nullopt);

/**
 * @brief Generate a resource ID from Study Instance UID
 *
 * @param study_instance_uid DICOM Study Instance UID
 * @return URL-safe resource ID
 */
[[nodiscard]] std::string study_uid_to_resource_id(std::string_view study_instance_uid);

/**
 * @brief Extract Study Instance UID from resource ID
 *
 * @param resource_id FHIR resource ID
 * @return Study Instance UID or empty if not a valid study resource ID
 */
[[nodiscard]] std::string resource_id_to_study_uid(std::string_view resource_id);

}  // namespace pacs::bridge::fhir

#endif  // PACS_BRIDGE_FHIR_IMAGING_STUDY_RESOURCE_H
