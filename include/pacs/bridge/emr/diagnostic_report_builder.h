#ifndef PACS_BRIDGE_EMR_DIAGNOSTIC_REPORT_BUILDER_H
#define PACS_BRIDGE_EMR_DIAGNOSTIC_REPORT_BUILDER_H

/**
 * @file diagnostic_report_builder.h
 * @brief FHIR DiagnosticReport resource builder
 *
 * Provides a fluent builder interface for constructing FHIR R4
 * DiagnosticReport resources for posting to EMR systems.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/105
 * @see https://www.hl7.org/fhir/diagnosticreport.html
 */

#include "result_poster.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::emr {

// =============================================================================
// FHIR Coding Structure
// =============================================================================

/**
 * @brief FHIR Coding data type
 *
 * @see https://www.hl7.org/fhir/datatypes.html#Coding
 */
struct fhir_coding {
    std::string system;
    std::optional<std::string> version;
    std::string code;
    std::optional<std::string> display;

    /**
     * @brief Create LOINC coding
     */
    [[nodiscard]] static fhir_coding loinc(std::string code,
                                           std::string display) {
        return {"http://loinc.org", std::nullopt, std::move(code),
                std::move(display)};
    }

    /**
     * @brief Create SNOMED CT coding
     */
    [[nodiscard]] static fhir_coding snomed(std::string code,
                                            std::string display) {
        return {"http://snomed.info/sct", std::nullopt, std::move(code),
                std::move(display)};
    }

    /**
     * @brief Create HL7 v2 coding
     */
    [[nodiscard]] static fhir_coding hl7v2(std::string table,
                                           std::string code,
                                           std::string display) {
        return {"http://terminology.hl7.org/CodeSystem/v2-" + table,
                std::nullopt, std::move(code), std::move(display)};
    }

    /**
     * @brief Create DICOM coding
     */
    [[nodiscard]] static fhir_coding dicom(std::string code,
                                           std::string display) {
        return {"http://dicom.nema.org/resources/ontology/DCM", std::nullopt,
                std::move(code), std::move(display)};
    }
};

/**
 * @brief FHIR CodeableConcept data type
 *
 * @see https://www.hl7.org/fhir/datatypes.html#CodeableConcept
 */
struct fhir_codeable_concept {
    std::vector<fhir_coding> coding;
    std::optional<std::string> text;

    /**
     * @brief Add a coding to this concept
     */
    void add_coding(fhir_coding c) {
        coding.push_back(std::move(c));
    }
};

// =============================================================================
// FHIR Reference Structure
// =============================================================================

/**
 * @brief FHIR Reference data type
 *
 * @see https://www.hl7.org/fhir/references.html
 */
struct fhir_reference {
    std::optional<std::string> reference;
    std::optional<std::string> type;
    std::optional<std::string> display;

    /**
     * @brief Create reference from resource type and ID
     */
    [[nodiscard]] static fhir_reference from_id(std::string_view resource_type,
                                                 std::string_view id) {
        fhir_reference ref;
        ref.reference = std::string(resource_type) + "/" + std::string(id);
        ref.type = std::string(resource_type);
        return ref;
    }

    /**
     * @brief Create reference from full reference string
     */
    [[nodiscard]] static fhir_reference from_string(std::string ref_str) {
        fhir_reference ref;
        ref.reference = std::move(ref_str);
        return ref;
    }
};

// =============================================================================
// FHIR Identifier Structure
// =============================================================================

/**
 * @brief FHIR Identifier data type
 *
 * @see https://www.hl7.org/fhir/datatypes.html#Identifier
 */
struct fhir_identifier {
    std::optional<std::string> use;  // usual | official | temp | secondary | old
    std::optional<std::string> system;
    std::string value;
    std::optional<fhir_codeable_concept> type;
};

// =============================================================================
// Diagnostic Report Builder
// =============================================================================

/**
 * @brief Fluent builder for FHIR DiagnosticReport resources
 *
 * Provides a convenient interface for constructing DiagnosticReport
 * JSON for posting to EMR FHIR endpoints.
 *
 * @example Basic Usage
 * ```cpp
 * auto json = diagnostic_report_builder()
 *     .status(result_status::final_report)
 *     .category_radiology()
 *     .code_imaging_study()
 *     .subject("Patient/123")
 *     .effective_datetime("2025-01-15T10:30:00Z")
 *     .issued("2025-01-15T10:35:00Z")
 *     .performer("Practitioner/456")
 *     .imaging_study("ImagingStudy/789")
 *     .conclusion("No acute findings.")
 *     .build();
 *
 * if (json) {
 *     std::cout << *json << "\n";
 * }
 * ```
 *
 * @example From study_result
 * ```cpp
 * study_result result;
 * result.study_instance_uid = "1.2.3.4.5.6.7.8.9";
 * result.patient_id = "MRN12345";
 * result.patient_reference = "Patient/123";
 * result.modality = "CT";
 * result.study_datetime = "2025-01-15T10:30:00Z";
 * result.status = result_status::final_report;
 *
 * auto json = diagnostic_report_builder::from_study_result(result)
 *     .conclusion("No acute findings.")
 *     .build();
 * ```
 */
class diagnostic_report_builder {
public:
    /**
     * @brief Default constructor
     */
    diagnostic_report_builder();

    /**
     * @brief Destructor
     */
    ~diagnostic_report_builder();

    // Non-copyable, movable
    diagnostic_report_builder(const diagnostic_report_builder&) = delete;
    diagnostic_report_builder& operator=(const diagnostic_report_builder&) = delete;
    diagnostic_report_builder(diagnostic_report_builder&&) noexcept;
    diagnostic_report_builder& operator=(diagnostic_report_builder&&) noexcept;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /**
     * @brief Create builder from study_result
     *
     * @param result Study result data
     * @return Pre-configured builder
     */
    [[nodiscard]] static diagnostic_report_builder from_study_result(
        const study_result& result);

    // =========================================================================
    // Required Fields
    // =========================================================================

    /**
     * @brief Set report status (required)
     */
    diagnostic_report_builder& status(result_status value);

    /**
     * @brief Set report code (required)
     *
     * @param codeable_concept CodeableConcept for the report type
     */
    diagnostic_report_builder& code(fhir_codeable_concept codeable_concept);

    /**
     * @brief Set code to standard imaging study LOINC code
     *
     * Sets code to LOINC 18748-4 "Diagnostic imaging study"
     */
    diagnostic_report_builder& code_imaging_study();

    /**
     * @brief Set code with custom LOINC
     */
    diagnostic_report_builder& code_loinc(std::string_view loinc_code,
                                          std::string_view display);

    /**
     * @brief Set subject reference (required)
     *
     * @param reference Patient reference (e.g., "Patient/123")
     */
    diagnostic_report_builder& subject(std::string_view reference);

    /**
     * @brief Set subject with display name
     */
    diagnostic_report_builder& subject(std::string_view reference,
                                       std::string_view display);

    // =========================================================================
    // Category
    // =========================================================================

    /**
     * @brief Add category
     */
    diagnostic_report_builder& category(fhir_codeable_concept codeable_concept);

    /**
     * @brief Set category to Radiology (RAD)
     *
     * Uses HL7 v2 diagnostic service section code
     */
    diagnostic_report_builder& category_radiology();

    // =========================================================================
    // Timing
    // =========================================================================

    /**
     * @brief Set effective date/time (when study was performed)
     *
     * @param datetime ISO 8601 formatted datetime
     */
    diagnostic_report_builder& effective_datetime(std::string_view datetime);

    /**
     * @brief Set effective period
     *
     * @param start Period start (ISO 8601)
     * @param end Period end (ISO 8601)
     */
    diagnostic_report_builder& effective_period(std::string_view start,
                                                std::string_view end);

    /**
     * @brief Set issued date/time (when report was released)
     *
     * @param datetime ISO 8601 formatted instant
     */
    diagnostic_report_builder& issued(std::string_view datetime);

    // =========================================================================
    // Performers
    // =========================================================================

    /**
     * @brief Add performer reference
     *
     * @param reference Practitioner reference (e.g., "Practitioner/123")
     */
    diagnostic_report_builder& performer(std::string_view reference);

    /**
     * @brief Add performer with display name
     */
    diagnostic_report_builder& performer(std::string_view reference,
                                         std::string_view display);

    /**
     * @brief Add results interpreter
     */
    diagnostic_report_builder& results_interpreter(std::string_view reference);

    // =========================================================================
    // Related Resources
    // =========================================================================

    /**
     * @brief Set based-on reference (the request/order)
     *
     * @param reference ServiceRequest reference
     */
    diagnostic_report_builder& based_on(std::string_view reference);

    /**
     * @brief Set encounter reference
     */
    diagnostic_report_builder& encounter(std::string_view reference);

    /**
     * @brief Add imaging study reference
     */
    diagnostic_report_builder& imaging_study(std::string_view reference);

    // =========================================================================
    // Identifiers
    // =========================================================================

    /**
     * @brief Add identifier
     */
    diagnostic_report_builder& identifier(const fhir_identifier& ident);

    /**
     * @brief Add accession number identifier
     */
    diagnostic_report_builder& accession_number(std::string_view value,
                                                std::string_view system = "");

    /**
     * @brief Add Study Instance UID identifier
     */
    diagnostic_report_builder& study_instance_uid(std::string_view uid);

    // =========================================================================
    // Results
    // =========================================================================

    /**
     * @brief Set clinical conclusion
     */
    diagnostic_report_builder& conclusion(std::string_view text);

    /**
     * @brief Add conclusion code
     */
    diagnostic_report_builder& conclusion_code(fhir_coding coding);

    /**
     * @brief Add conclusion code with SNOMED CT
     */
    diagnostic_report_builder& conclusion_code_snomed(std::string_view code,
                                                      std::string_view display);

    /**
     * @brief Add result observation reference
     */
    diagnostic_report_builder& result(std::string_view reference);

    // =========================================================================
    // Build
    // =========================================================================

    /**
     * @brief Build the DiagnosticReport JSON
     *
     * @return JSON string or nullopt if validation fails
     */
    [[nodiscard]] std::optional<std::string> build() const;

    /**
     * @brief Build with validation errors
     *
     * @return JSON string or error message
     */
    [[nodiscard]] std::expected<std::string, std::string> build_validated() const;

    /**
     * @brief Validate the current builder state
     *
     * @return true if all required fields are set
     */
    [[nodiscard]] bool is_valid() const;

    /**
     * @brief Get validation errors
     *
     * @return List of validation error messages
     */
    [[nodiscard]] std::vector<std::string> validation_errors() const;

    /**
     * @brief Reset builder to initial state
     */
    void reset();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_DIAGNOSTIC_REPORT_BUILDER_H
