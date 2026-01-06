#ifndef PACS_BRIDGE_EMR_PATIENT_RECORD_H
#define PACS_BRIDGE_EMR_PATIENT_RECORD_H

/**
 * @file patient_record.h
 * @brief Patient data structure for EMR integration
 *
 * Defines the patient_record structure for representing patient demographic
 * information retrieved from external EMR systems. Maps to FHIR Patient
 * resource format.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/104
 * @see https://www.hl7.org/fhir/patient.html
 */

#include "emr_types.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::emr {

// =============================================================================
// Patient Query Error Codes (-1040 to -1059)
// =============================================================================

/**
 * @brief Patient query specific error codes
 *
 * Allocated range: -1040 to -1059
 */
enum class patient_error : int {
    /** Patient not found in EMR */
    not_found = -1040,

    /** Multiple patients found, disambiguation required */
    multiple_found = -1041,

    /** Patient query failed */
    query_failed = -1042,

    /** Invalid patient data in response */
    invalid_data = -1043,

    /** Patient merge detected (merged into another record) */
    merge_detected = -1044,

    /** Invalid search parameters */
    invalid_query = -1045,

    /** Patient record is inactive */
    inactive_patient = -1046,

    /** Patient data parsing failed */
    parse_failed = -1047,

    /** Cache operation failed */
    cache_failed = -1048
};

/**
 * @brief Convert patient_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(patient_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of patient error
 */
[[nodiscard]] constexpr const char* to_string(patient_error error) noexcept {
    switch (error) {
        case patient_error::not_found:
            return "Patient not found in EMR";
        case patient_error::multiple_found:
            return "Multiple patients found, disambiguation required";
        case patient_error::query_failed:
            return "Patient query failed";
        case patient_error::invalid_data:
            return "Invalid patient data in response";
        case patient_error::merge_detected:
            return "Patient has been merged into another record";
        case patient_error::invalid_query:
            return "Invalid search parameters";
        case patient_error::inactive_patient:
            return "Patient record is inactive";
        case patient_error::parse_failed:
            return "Patient data parsing failed";
        case patient_error::cache_failed:
            return "Cache operation failed";
        default:
            return "Unknown patient error";
    }
}

/**
 * @brief Convert patient_error to error_info for Result<T>
 *
 * @param error Patient error code
 * @param details Optional additional details
 * @return error_info for use with Result<T>
 */
[[nodiscard]] inline error_info to_error_info(
    patient_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "emr.patient",
        details
    };
}

// =============================================================================
// Patient Identifier
// =============================================================================

/**
 * @brief Patient identifier with system namespace
 *
 * Represents a patient identifier from a specific assigning authority.
 * Maps to FHIR Identifier data type.
 */
struct patient_identifier {
    /** Identifier value */
    std::string value;

    /** System/namespace URI (e.g., "http://hospital.org/mrn") */
    std::optional<std::string> system;

    /** Identifier use (usual, official, temp, secondary, old) */
    std::optional<std::string> use;

    /** Type code (e.g., "MR" for medical record number) */
    std::optional<std::string> type_code;

    /** Type display text */
    std::optional<std::string> type_display;

    /**
     * @brief Check if identifier matches a system
     */
    [[nodiscard]] bool matches_system(std::string_view sys) const noexcept {
        return system.has_value() && *system == sys;
    }

    /**
     * @brief Check if this is a medical record number
     */
    [[nodiscard]] bool is_mrn() const noexcept {
        return type_code.has_value() && *type_code == "MR";
    }
};

// =============================================================================
// Patient Address
// =============================================================================

/**
 * @brief Patient address
 *
 * Maps to FHIR Address data type.
 */
struct patient_address {
    /** Address use (home, work, temp, old, billing) */
    std::optional<std::string> use;

    /** Address type (postal, physical, both) */
    std::optional<std::string> type;

    /** Full text representation */
    std::optional<std::string> text;

    /** Street address lines */
    std::vector<std::string> lines;

    /** City */
    std::optional<std::string> city;

    /** District/county */
    std::optional<std::string> district;

    /** State/province */
    std::optional<std::string> state;

    /** Postal code */
    std::optional<std::string> postal_code;

    /** Country */
    std::optional<std::string> country;
};

// =============================================================================
// Patient Contact Point
// =============================================================================

/**
 * @brief Patient contact information (phone, email, etc.)
 *
 * Maps to FHIR ContactPoint data type.
 */
struct patient_contact_point {
    /** System (phone, fax, email, pager, url, sms, other) */
    std::string system;

    /** Contact value */
    std::string value;

    /** Use (home, work, temp, old, mobile) */
    std::optional<std::string> use;

    /** Rank order preference */
    std::optional<int> rank;
};

// =============================================================================
// Patient Name
// =============================================================================

/**
 * @brief Patient name components
 *
 * Maps to FHIR HumanName data type.
 */
struct patient_name {
    /** Name use (usual, official, temp, nickname, anonymous, old, maiden) */
    std::optional<std::string> use;

    /** Full text representation */
    std::optional<std::string> text;

    /** Family name (surname) */
    std::optional<std::string> family;

    /** Given names */
    std::vector<std::string> given;

    /** Name prefixes (e.g., "Dr.", "Mr.") */
    std::vector<std::string> prefix;

    /** Name suffixes (e.g., "Jr.", "PhD") */
    std::vector<std::string> suffix;

    /**
     * @brief Get first given name
     */
    [[nodiscard]] std::string_view first_given() const noexcept {
        return given.empty() ? std::string_view{} : given.front();
    }

    /**
     * @brief Get middle name(s) as a single string
     */
    [[nodiscard]] std::string middle_names() const {
        if (given.size() <= 1) {
            return {};
        }
        std::string result;
        for (size_t i = 1; i < given.size(); ++i) {
            if (!result.empty()) {
                result += ' ';
            }
            result += given[i];
        }
        return result;
    }

    /**
     * @brief Convert to DICOM PN format
     *
     * DICOM: Family^Given^Middle^Prefix^Suffix
     */
    [[nodiscard]] std::string to_dicom_pn() const {
        std::string result;
        result += family.value_or("");
        result += '^';
        result += first_given();
        result += '^';
        result += middle_names();
        result += '^';
        if (!prefix.empty()) {
            result += prefix.front();
        }
        result += '^';
        if (!suffix.empty()) {
            result += suffix.front();
        }
        // Remove trailing carets
        while (!result.empty() && result.back() == '^') {
            result.pop_back();
        }
        return result;
    }
};

// =============================================================================
// Patient Record
// =============================================================================

/**
 * @brief Complete patient record from EMR
 *
 * Represents patient demographic information retrieved from an external
 * EMR system via FHIR API. Contains all relevant patient data for
 * PACS integration.
 *
 * @example Basic Usage
 * ```cpp
 * patient_record patient;
 * patient.id = "Patient/123";
 * patient.mrn = "MRN12345";
 *
 * patient_name name;
 * name.family = "Doe";
 * name.given = {"John", "Andrew"};
 * patient.names.push_back(name);
 *
 * patient.birth_date = "1980-01-01";
 * patient.sex = "male";
 * ```
 */
struct patient_record {
    /** FHIR resource ID */
    std::string id;

    /** Medical Record Number (primary identifier) */
    std::string mrn;

    /** All patient identifiers */
    std::vector<patient_identifier> identifiers;

    /** Patient names (may have multiple) */
    std::vector<patient_name> names;

    /** Birth date (YYYY-MM-DD format) */
    std::optional<std::string> birth_date;

    /** Administrative sex (male, female, other, unknown) */
    std::optional<std::string> sex;

    /** Patient addresses */
    std::vector<patient_address> addresses;

    /** Contact information */
    std::vector<patient_contact_point> telecom;

    /** Active status */
    bool active{true};

    /** Deceased indicator */
    std::optional<bool> deceased;

    /** Deceased date/time if applicable */
    std::optional<std::string> deceased_datetime;

    /** Preferred language */
    std::optional<std::string> language;

    /** Managing organization reference */
    std::optional<std::string> managing_organization;

    /** Link to another patient (for merged records) */
    std::optional<std::string> link_reference;

    /** Link type (replaced-by, replaces, refer, seealso) */
    std::optional<std::string> link_type;

    /** Resource version ID */
    std::optional<std::string> version_id;

    /** Last updated timestamp */
    std::optional<std::string> last_updated;

    /** Raw FHIR JSON (for debugging/auditing) */
    std::optional<std::string> raw_json;

    /** Cache metadata: when this record was cached */
    std::optional<std::chrono::system_clock::time_point> cached_at;

    // =========================================================================
    // Convenience Methods
    // =========================================================================

    /**
     * @brief Get official name
     * @return Official name or first available name
     */
    [[nodiscard]] const patient_name* official_name() const noexcept {
        for (const auto& name : names) {
            if (name.use.has_value() && *name.use == "official") {
                return &name;
            }
        }
        return names.empty() ? nullptr : &names.front();
    }

    /**
     * @brief Get family name
     */
    [[nodiscard]] std::string family_name() const {
        auto name = official_name();
        return name && name->family.has_value() ? *name->family : std::string{};
    }

    /**
     * @brief Get given name (first name)
     */
    [[nodiscard]] std::string given_name() const {
        auto name = official_name();
        if (!name || name->given.empty()) {
            return {};
        }
        return name->given.front();
    }

    /**
     * @brief Get middle name
     */
    [[nodiscard]] std::string middle_name() const {
        auto name = official_name();
        return name ? name->middle_names() : std::string{};
    }

    /**
     * @brief Get DICOM formatted name
     */
    [[nodiscard]] std::string dicom_name() const {
        auto name = official_name();
        return name ? name->to_dicom_pn() : std::string{};
    }

    /**
     * @brief Get home address
     */
    [[nodiscard]] const patient_address* home_address() const noexcept {
        for (const auto& addr : addresses) {
            if (addr.use.has_value() && *addr.use == "home") {
                return &addr;
            }
        }
        return addresses.empty() ? nullptr : &addresses.front();
    }

    /**
     * @brief Get home phone
     */
    [[nodiscard]] std::string home_phone() const {
        for (const auto& contact : telecom) {
            if (contact.system == "phone" &&
                contact.use.has_value() && *contact.use == "home") {
                return contact.value;
            }
        }
        for (const auto& contact : telecom) {
            if (contact.system == "phone") {
                return contact.value;
            }
        }
        return {};
    }

    /**
     * @brief Get identifier by system
     */
    [[nodiscard]] std::optional<std::string> identifier_by_system(
        std::string_view system) const {
        for (const auto& id : identifiers) {
            if (id.matches_system(system)) {
                return id.value;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Convert birth date to DICOM format (YYYYMMDD)
     */
    [[nodiscard]] std::string dicom_birth_date() const {
        if (!birth_date.has_value()) {
            return {};
        }
        std::string result;
        for (char c : *birth_date) {
            if (c != '-') {
                result += c;
            }
        }
        return result;
    }

    /**
     * @brief Convert sex to DICOM format (M, F, O)
     */
    [[nodiscard]] std::string dicom_sex() const {
        if (!sex.has_value()) {
            return {};
        }
        if (*sex == "male") return "M";
        if (*sex == "female") return "F";
        if (*sex == "other") return "O";
        return {};
    }

    /**
     * @brief Check if record is valid (has minimum required data)
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !id.empty() && !mrn.empty();
    }

    /**
     * @brief Check if this patient has been merged
     */
    [[nodiscard]] bool is_merged() const noexcept {
        return link_type.has_value() && *link_type == "replaced-by";
    }
};

// =============================================================================
// Patient Search Match
// =============================================================================

/**
 * @brief Search match result with confidence score
 */
struct patient_match {
    /** Matched patient record */
    patient_record patient;

    /** Match confidence score (0.0 to 1.0) */
    double score{1.0};

    /** Match method used */
    std::string match_method;
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_PATIENT_RECORD_H
