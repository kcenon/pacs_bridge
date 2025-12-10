#ifndef PACS_BRIDGE_FHIR_PATIENT_RESOURCE_H
#define PACS_BRIDGE_FHIR_PATIENT_RESOURCE_H

/**
 * @file patient_resource.h
 * @brief FHIR Patient resource implementation
 *
 * Implements the FHIR R4 Patient resource for representing patient
 * demographic information. Provides mapping from internal patient cache
 * to FHIR format.
 *
 * @see https://hl7.org/fhir/R4/patient.html
 * @see https://github.com/kcenon/pacs_bridge/issues/32
 */

#include "fhir_resource.h"
#include "fhir_types.h"
#include "resource_handler.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declarations in correct namespace
namespace pacs::bridge::cache {
class patient_cache;
}  // namespace pacs::bridge::cache

namespace pacs::bridge::mapping {
struct dicom_patient;
}  // namespace pacs::bridge::mapping

namespace pacs::bridge::fhir {

// =============================================================================
// FHIR Patient Data Types
// =============================================================================

/**
 * @brief FHIR Identifier data type
 *
 * @see https://hl7.org/fhir/R4/datatypes.html#Identifier
 */
struct fhir_identifier {
    /** Identifier use (usual, official, temp, secondary, old) */
    std::optional<std::string> use;

    /** Namespace for the identifier value */
    std::optional<std::string> system;

    /** The identifier value */
    std::string value;

    /** Description of identifier type */
    std::optional<std::string> type_text;
};

/**
 * @brief FHIR HumanName data type
 *
 * @see https://hl7.org/fhir/R4/datatypes.html#HumanName
 */
struct fhir_human_name {
    /** Name use (usual, official, temp, nickname, anonymous, old, maiden) */
    std::optional<std::string> use;

    /** Full text representation of the name */
    std::optional<std::string> text;

    /** Family name (surname) */
    std::optional<std::string> family;

    /** Given names (first, middle, etc.) */
    std::vector<std::string> given;

    /** Parts that come before the name */
    std::vector<std::string> prefix;

    /** Parts that come after the name */
    std::vector<std::string> suffix;
};

/**
 * @brief FHIR administrative gender codes
 *
 * @see https://hl7.org/fhir/R4/valueset-administrative-gender.html
 */
enum class administrative_gender {
    male,
    female,
    other,
    unknown
};

/**
 * @brief Convert administrative_gender to FHIR code string
 */
[[nodiscard]] constexpr std::string_view to_string(
    administrative_gender gender) noexcept {
    switch (gender) {
        case administrative_gender::male: return "male";
        case administrative_gender::female: return "female";
        case administrative_gender::other: return "other";
        case administrative_gender::unknown: return "unknown";
    }
    return "unknown";
}

/**
 * @brief Parse administrative_gender from string
 */
[[nodiscard]] std::optional<administrative_gender> parse_gender(
    std::string_view gender_str) noexcept;

// =============================================================================
// FHIR Patient Resource
// =============================================================================

/**
 * @brief FHIR R4 Patient resource
 *
 * Represents patient demographic information per FHIR R4 specification.
 * Maps from internal DICOM patient data to FHIR Patient resource format.
 *
 * @example Creating a Patient Resource
 * ```cpp
 * patient_resource patient;
 * patient.set_id("patient-123");
 *
 * // Add identifier
 * fhir_identifier mrn;
 * mrn.system = "urn:oid:1.2.3.4.5";
 * mrn.value = "MRN12345";
 * mrn.use = "usual";
 * patient.add_identifier(mrn);
 *
 * // Set name
 * fhir_human_name name;
 * name.family = "Doe";
 * name.given = {"John", "Andrew"};
 * name.use = "official";
 * patient.add_name(name);
 *
 * // Set other demographics
 * patient.set_gender(administrative_gender::male);
 * patient.set_birth_date("1980-01-01");
 *
 * // Serialize to JSON
 * std::string json = patient.to_json();
 * ```
 *
 * @see https://hl7.org/fhir/R4/patient.html
 */
class patient_resource : public fhir_resource {
public:
    /**
     * @brief Default constructor
     */
    patient_resource();

    /**
     * @brief Destructor
     */
    ~patient_resource() override;

    // Non-copyable, movable
    patient_resource(const patient_resource&) = delete;
    patient_resource& operator=(const patient_resource&) = delete;
    patient_resource(patient_resource&&) noexcept;
    patient_resource& operator=(patient_resource&&) noexcept;

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
     * @brief Add an identifier to the patient
     */
    void add_identifier(const fhir_identifier& identifier);

    /**
     * @brief Get all identifiers
     */
    [[nodiscard]] const std::vector<fhir_identifier>& identifiers() const noexcept;

    /**
     * @brief Clear all identifiers
     */
    void clear_identifiers();

    // =========================================================================
    // Names
    // =========================================================================

    /**
     * @brief Add a name to the patient
     */
    void add_name(const fhir_human_name& name);

    /**
     * @brief Get all names
     */
    [[nodiscard]] const std::vector<fhir_human_name>& names() const noexcept;

    /**
     * @brief Clear all names
     */
    void clear_names();

    // =========================================================================
    // Demographics
    // =========================================================================

    /**
     * @brief Set gender
     */
    void set_gender(administrative_gender gender);

    /**
     * @brief Get gender
     */
    [[nodiscard]] std::optional<administrative_gender> gender() const noexcept;

    /**
     * @brief Set birth date (YYYY-MM-DD format)
     */
    void set_birth_date(std::string date);

    /**
     * @brief Get birth date
     */
    [[nodiscard]] const std::optional<std::string>& birth_date() const noexcept;

    /**
     * @brief Set active status
     */
    void set_active(bool active);

    /**
     * @brief Get active status
     */
    [[nodiscard]] std::optional<bool> active() const noexcept;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Create patient resource from JSON
     * @param json JSON string
     * @return Patient resource or nullptr on parse error
     */
    [[nodiscard]] static std::unique_ptr<patient_resource> from_json(
        const std::string& json);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Patient Resource Handler
// =============================================================================

/**
 * @brief Handler for FHIR Patient resource operations
 *
 * Implements read and search operations for Patient resources by mapping
 * from the internal patient cache.
 *
 * Supported operations:
 * - read: GET /Patient/{id}
 * - search: GET /Patient?identifier=xxx
 * - search: GET /Patient?name=xxx
 * - search: GET /Patient?birthdate=xxx
 *
 * @example Basic Usage
 * ```cpp
 * auto cache = std::make_shared<patient_cache>();
 * // ... populate cache ...
 *
 * auto handler = std::make_shared<patient_resource_handler>(cache);
 *
 * // Read by ID
 * auto result = handler->read("patient-123");
 * if (is_success(result)) {
 *     auto& patient = get_resource(result);
 *     std::cout << patient->to_json() << std::endl;
 * }
 *
 * // Search by identifier
 * std::map<std::string, std::string> params = {{"identifier", "MRN12345"}};
 * auto search_result = handler->search(params, {});
 * ```
 *
 * Thread-safety: All operations are thread-safe.
 */
class patient_resource_handler : public resource_handler {
public:
    /**
     * @brief Constructor
     * @param cache Patient cache to query
     */
    explicit patient_resource_handler(
        std::shared_ptr<pacs::bridge::cache::patient_cache> cache);

    /**
     * @brief Destructor
     */
    ~patient_resource_handler() override;

    // Non-copyable, movable
    patient_resource_handler(const patient_resource_handler&) = delete;
    patient_resource_handler& operator=(const patient_resource_handler&) = delete;
    patient_resource_handler(patient_resource_handler&&) noexcept;
    patient_resource_handler& operator=(patient_resource_handler&&) noexcept;

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
     * @brief Read patient by ID
     */
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> read(
        const std::string& id) override;

    /**
     * @brief Search for patients
     *
     * Supported search parameters:
     * - identifier: Patient identifier value
     * - name: Patient name (partial match supported)
     * - birthdate: Patient birth date (YYYY-MM-DD)
     * - _id: Resource ID
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
// Mapping Utilities
// =============================================================================

/**
 * @brief Convert DICOM patient to FHIR Patient resource
 *
 * @param dicom_patient Source DICOM patient data
 * @param patient_id Optional patient ID (uses patient_id from dicom_patient if not set)
 * @return FHIR Patient resource
 */
[[nodiscard]] std::unique_ptr<patient_resource> dicom_to_fhir_patient(
    const pacs::bridge::mapping::dicom_patient& dicom_patient,
    const std::optional<std::string>& patient_id = std::nullopt);

/**
 * @brief Convert DICOM name format to FHIR HumanName
 *
 * DICOM PN format: Family^Given^Middle^Prefix^Suffix
 * FHIR HumanName has separate fields for each component
 *
 * @param dicom_name DICOM PN format name
 * @return FHIR HumanName
 */
[[nodiscard]] fhir_human_name dicom_name_to_fhir(std::string_view dicom_name);

/**
 * @brief Convert FHIR HumanName to DICOM name format
 *
 * @param name FHIR HumanName
 * @return DICOM PN format name
 */
[[nodiscard]] std::string fhir_name_to_dicom(const fhir_human_name& name);

/**
 * @brief Convert DICOM date format to FHIR date format
 *
 * DICOM: YYYYMMDD
 * FHIR: YYYY-MM-DD
 *
 * @param dicom_date DICOM date string
 * @return FHIR date string or empty if invalid
 */
[[nodiscard]] std::string dicom_date_to_fhir(std::string_view dicom_date);

/**
 * @brief Convert FHIR date format to DICOM date format
 *
 * @param fhir_date FHIR date string (YYYY-MM-DD)
 * @return DICOM date string (YYYYMMDD) or empty if invalid
 */
[[nodiscard]] std::string fhir_date_to_dicom(std::string_view fhir_date);

/**
 * @brief Convert DICOM sex code to FHIR gender
 *
 * DICOM: M, F, O
 * FHIR: male, female, other, unknown
 *
 * @param dicom_sex DICOM sex code
 * @return FHIR administrative gender
 */
[[nodiscard]] administrative_gender dicom_sex_to_fhir_gender(
    std::string_view dicom_sex);

/**
 * @brief Convert FHIR gender to DICOM sex code
 *
 * @param gender FHIR gender
 * @return DICOM sex code (M, F, O)
 */
[[nodiscard]] std::string fhir_gender_to_dicom_sex(administrative_gender gender);

}  // namespace pacs::bridge::fhir

#endif  // PACS_BRIDGE_FHIR_PATIENT_RESOURCE_H
