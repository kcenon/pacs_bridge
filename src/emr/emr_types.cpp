/**
 * @file emr_types.cpp
 * @brief Implementation of EMR type functions
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 */

#include "pacs/bridge/emr/emr_types.h"

#include <algorithm>
#include <cctype>

namespace pacs::bridge::emr {

std::optional<fhir_resource_type> parse_resource_type(
    std::string_view type_str) noexcept {
    // Case-insensitive comparison helper
    auto iequals = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    };

    if (iequals(type_str, "Patient")) {
        return fhir_resource_type::patient;
    }
    if (iequals(type_str, "ServiceRequest")) {
        return fhir_resource_type::service_request;
    }
    if (iequals(type_str, "ImagingStudy")) {
        return fhir_resource_type::imaging_study;
    }
    if (iequals(type_str, "DiagnosticReport")) {
        return fhir_resource_type::diagnostic_report;
    }
    if (iequals(type_str, "Practitioner")) {
        return fhir_resource_type::practitioner;
    }
    if (iequals(type_str, "Organization")) {
        return fhir_resource_type::organization;
    }
    if (iequals(type_str, "Encounter")) {
        return fhir_resource_type::encounter;
    }
    if (iequals(type_str, "Observation")) {
        return fhir_resource_type::observation;
    }
    if (iequals(type_str, "Condition")) {
        return fhir_resource_type::condition;
    }
    if (iequals(type_str, "Procedure")) {
        return fhir_resource_type::procedure;
    }
    if (iequals(type_str, "MedicationRequest")) {
        return fhir_resource_type::medication_request;
    }
    if (iequals(type_str, "Bundle")) {
        return fhir_resource_type::bundle;
    }
    if (iequals(type_str, "OperationOutcome")) {
        return fhir_resource_type::operation_outcome;
    }
    if (iequals(type_str, "CapabilityStatement")) {
        return fhir_resource_type::capability_statement;
    }

    return std::nullopt;
}

std::optional<std::string_view> http_response::get_header(
    std::string_view name) const noexcept {
    // Case-insensitive header lookup
    for (const auto& [key, value] : headers) {
        bool match = true;
        if (key.size() != name.size()) {
            continue;
        }
        for (size_t i = 0; i < key.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(key[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace pacs::bridge::emr
