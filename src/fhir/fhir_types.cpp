/**
 * @file fhir_types.cpp
 * @brief FHIR type implementation
 *
 * Implements parsing functions for FHIR types.
 *
 * @see include/pacs/bridge/fhir/fhir_types.h
 */

#include "pacs/bridge/fhir/fhir_types.h"

#include <algorithm>
#include <cctype>

namespace pacs::bridge::fhir {

namespace {

/**
 * @brief Case-insensitive string comparison
 */
bool iequals(std::string_view a, std::string_view b) noexcept {
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
}

/**
 * @brief Trim whitespace from string view
 */
std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

}  // namespace

// =============================================================================
// HTTP Method Parsing
// =============================================================================

std::optional<http_method> parse_http_method(
    std::string_view method_str) noexcept {
    auto trimmed = trim(method_str);
    if (iequals(trimmed, "GET")) {
        return http_method::get;
    }
    if (iequals(trimmed, "POST")) {
        return http_method::post;
    }
    if (iequals(trimmed, "PUT")) {
        return http_method::put;
    }
    if (iequals(trimmed, "PATCH")) {
        return http_method::patch;
    }
    if (iequals(trimmed, "DELETE")) {
        return http_method::delete_method;
    }
    if (iequals(trimmed, "HEAD")) {
        return http_method::head;
    }
    if (iequals(trimmed, "OPTIONS")) {
        return http_method::options;
    }
    return std::nullopt;
}

// =============================================================================
// Content Type Parsing
// =============================================================================

content_type parse_content_type(std::string_view header) noexcept {
    auto trimmed = trim(header);

    // Handle empty header - default to FHIR JSON
    if (trimmed.empty() || trimmed == "*/*") {
        return content_type::fhir_json;
    }

    // Extract media type (before any parameters like charset)
    auto semicolon = trimmed.find(';');
    if (semicolon != std::string_view::npos) {
        trimmed = trim(trimmed.substr(0, semicolon));
    }

    // Check for FHIR-specific types first
    if (iequals(trimmed, "application/fhir+json")) {
        return content_type::fhir_json;
    }
    if (iequals(trimmed, "application/fhir+xml")) {
        return content_type::fhir_xml;
    }

    // Check for generic types (FHIR accepts these)
    if (iequals(trimmed, "application/json")) {
        return content_type::json;
    }
    if (iequals(trimmed, "application/xml") || iequals(trimmed, "text/xml")) {
        return content_type::xml;
    }

    // Handle Accept header with multiple types and quality values
    // e.g., "application/fhir+json, application/json;q=0.9"
    if (trimmed.find(',') != std::string_view::npos) {
        // Parse as comma-separated list
        std::string_view remaining = header;
        while (!remaining.empty()) {
            auto comma = remaining.find(',');
            auto part = trim(remaining.substr(
                0, comma == std::string_view::npos ? remaining.size() : comma));

            // Extract media type from this part
            auto semi = part.find(';');
            auto media = trim(part.substr(0, semi));

            // Check if this is a FHIR type
            if (iequals(media, "application/fhir+json")) {
                return content_type::fhir_json;
            }
            if (iequals(media, "application/fhir+xml")) {
                return content_type::fhir_xml;
            }
            if (iequals(media, "application/json")) {
                return content_type::json;
            }
            if (iequals(media, "application/xml") ||
                iequals(media, "text/xml")) {
                return content_type::xml;
            }

            if (comma == std::string_view::npos) {
                break;
            }
            remaining = remaining.substr(comma + 1);
        }
    }

    return content_type::unknown;
}

// =============================================================================
// HTTP Status Reason Phrases
// =============================================================================

std::string_view get_reason_phrase(http_status status) noexcept {
    switch (status) {
        case http_status::ok:
            return "OK";
        case http_status::created:
            return "Created";
        case http_status::no_content:
            return "No Content";
        case http_status::not_modified:
            return "Not Modified";
        case http_status::bad_request:
            return "Bad Request";
        case http_status::unauthorized:
            return "Unauthorized";
        case http_status::forbidden:
            return "Forbidden";
        case http_status::not_found:
            return "Not Found";
        case http_status::method_not_allowed:
            return "Method Not Allowed";
        case http_status::not_acceptable:
            return "Not Acceptable";
        case http_status::conflict:
            return "Conflict";
        case http_status::gone:
            return "Gone";
        case http_status::precondition_failed:
            return "Precondition Failed";
        case http_status::unprocessable_entity:
            return "Unprocessable Entity";
        case http_status::internal_server_error:
            return "Internal Server Error";
        case http_status::not_implemented:
            return "Not Implemented";
        case http_status::service_unavailable:
            return "Service Unavailable";
    }
    return "Unknown";
}

// =============================================================================
// Resource Type Parsing
// =============================================================================

std::optional<resource_type> parse_resource_type(
    std::string_view type_str) noexcept {
    auto trimmed = trim(type_str);

    if (iequals(trimmed, "Patient")) {
        return resource_type::patient;
    }
    if (iequals(trimmed, "ServiceRequest")) {
        return resource_type::service_request;
    }
    if (iequals(trimmed, "ImagingStudy")) {
        return resource_type::imaging_study;
    }
    if (iequals(trimmed, "DiagnosticReport")) {
        return resource_type::diagnostic_report;
    }
    if (iequals(trimmed, "Practitioner")) {
        return resource_type::practitioner;
    }
    if (iequals(trimmed, "Organization")) {
        return resource_type::organization;
    }
    if (iequals(trimmed, "Endpoint")) {
        return resource_type::endpoint;
    }
    if (iequals(trimmed, "OperationOutcome")) {
        return resource_type::operation_outcome;
    }
    if (iequals(trimmed, "Bundle")) {
        return resource_type::bundle;
    }
    if (iequals(trimmed, "CapabilityStatement")) {
        return resource_type::capability_statement;
    }

    return std::nullopt;
}

}  // namespace pacs::bridge::fhir
