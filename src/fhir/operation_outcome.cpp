/**
 * @file operation_outcome.cpp
 * @brief FHIR OperationOutcome implementation
 *
 * Implements OperationOutcome resource for error responses.
 *
 * @see include/pacs/bridge/fhir/operation_outcome.h
 */

#include "pacs/bridge/fhir/operation_outcome.h"

#include <algorithm>
#include <sstream>

namespace pacs::bridge::fhir {

// =============================================================================
// Issue Type String Conversion
// =============================================================================

std::string_view to_string(issue_type type) noexcept {
    switch (type) {
        case issue_type::invalid:
            return "invalid";
        case issue_type::structure:
            return "structure";
        case issue_type::required:
            return "required";
        case issue_type::value:
            return "value";
        case issue_type::invariant:
            return "invariant";
        case issue_type::security:
            return "security";
        case issue_type::login:
            return "login";
        case issue_type::unknown:
            return "unknown";
        case issue_type::expired:
            return "expired";
        case issue_type::forbidden:
            return "forbidden";
        case issue_type::suppressed:
            return "suppressed";
        case issue_type::processing:
            return "processing";
        case issue_type::not_supported:
            return "not-supported";
        case issue_type::duplicate:
            return "duplicate";
        case issue_type::multiple_matches:
            return "multiple-matches";
        case issue_type::not_found:
            return "not-found";
        case issue_type::deleted:
            return "deleted";
        case issue_type::too_long:
            return "too-long";
        case issue_type::code_invalid:
            return "code-invalid";
        case issue_type::extension:
            return "extension";
        case issue_type::too_costly:
            return "too-costly";
        case issue_type::business_rule:
            return "business-rule";
        case issue_type::conflict:
            return "conflict";
        case issue_type::transient:
            return "transient";
        case issue_type::lock_error:
            return "lock-error";
        case issue_type::no_store:
            return "no-store";
        case issue_type::exception:
            return "exception";
        case issue_type::timeout:
            return "timeout";
        case issue_type::incomplete:
            return "incomplete";
        case issue_type::throttled:
            return "throttled";
    }
    return "processing";
}

// =============================================================================
// Outcome Issue Factory Methods
// =============================================================================

outcome_issue outcome_issue::error(issue_type type, std::string diagnostics) {
    outcome_issue issue;
    issue.severity = issue_severity::error;
    issue.code = type;
    issue.diagnostics = std::move(diagnostics);
    return issue;
}

outcome_issue outcome_issue::warning(issue_type type, std::string diagnostics) {
    outcome_issue issue;
    issue.severity = issue_severity::warning;
    issue.code = type;
    issue.diagnostics = std::move(diagnostics);
    return issue;
}

outcome_issue outcome_issue::info(std::string message) {
    outcome_issue issue;
    issue.severity = issue_severity::information;
    issue.code = issue_type::processing;
    issue.diagnostics = std::move(message);
    return issue;
}

// =============================================================================
// Operation Outcome Implementation
// =============================================================================

operation_outcome::operation_outcome(outcome_issue issue) {
    issues_.push_back(std::move(issue));
}

operation_outcome::operation_outcome(std::vector<outcome_issue> issues)
    : issues_(std::move(issues)) {}

void operation_outcome::add_issue(outcome_issue issue) {
    issues_.push_back(std::move(issue));
}

const std::vector<outcome_issue>& operation_outcome::issues() const noexcept {
    return issues_;
}

bool operation_outcome::has_issues() const noexcept { return !issues_.empty(); }

bool operation_outcome::has_errors() const noexcept {
    return std::any_of(issues_.begin(), issues_.end(), [](const auto& issue) {
        return issue.severity == issue_severity::error ||
               issue.severity == issue_severity::fatal;
    });
}

issue_severity operation_outcome::highest_severity() const noexcept {
    if (issues_.empty()) {
        return issue_severity::information;
    }

    auto it = std::min_element(issues_.begin(), issues_.end(),
                               [](const auto& a, const auto& b) {
                                   // Lower enum value = higher severity
                                   return static_cast<int>(a.severity) <
                                          static_cast<int>(b.severity);
                               });
    return it->severity;
}

const std::string& operation_outcome::id() const noexcept { return id_; }

void operation_outcome::set_id(std::string id) { id_ = std::move(id); }

// =============================================================================
// JSON Serialization
// =============================================================================

namespace {

/**
 * @brief Escape a string for JSON
 */
std::string json_escape(std::string_view input) {
    std::string result;
    result.reserve(input.size() + 10);  // Reserve a bit extra for escapes

    for (char c : input) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control characters
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x",
                             static_cast<unsigned int>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

}  // namespace

std::string operation_outcome::to_json() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"resourceType\": \"OperationOutcome\"";

    if (!id_.empty()) {
        json << ",\n  \"id\": \"" << json_escape(id_) << "\"";
    }

    if (!issues_.empty()) {
        json << ",\n  \"issue\": [\n";

        for (size_t i = 0; i < issues_.size(); ++i) {
            const auto& issue = issues_[i];

            json << "    {\n";
            json << "      \"severity\": \"" << to_string(issue.severity)
                 << "\",\n";
            json << "      \"code\": \"" << to_string(issue.code) << "\"";

            if (issue.details_text) {
                json << ",\n      \"details\": {\n";
                json << "        \"text\": \""
                     << json_escape(*issue.details_text) << "\"\n";
                json << "      }";
            }

            if (issue.diagnostics) {
                json << ",\n      \"diagnostics\": \""
                     << json_escape(*issue.diagnostics) << "\"";
            }

            if (!issue.expression.empty()) {
                json << ",\n      \"expression\": [";
                for (size_t j = 0; j < issue.expression.size(); ++j) {
                    if (j > 0) {
                        json << ", ";
                    }
                    json << "\"" << json_escape(issue.expression[j]) << "\"";
                }
                json << "]";
            }

            if (!issue.location.empty()) {
                json << ",\n      \"location\": [";
                for (size_t j = 0; j < issue.location.size(); ++j) {
                    if (j > 0) {
                        json << ", ";
                    }
                    json << "\"" << json_escape(issue.location[j]) << "\"";
                }
                json << "]";
            }

            json << "\n    }";
            if (i < issues_.size() - 1) {
                json << ",";
            }
            json << "\n";
        }

        json << "  ]";
    }

    json << "\n}";
    return json.str();
}

std::string operation_outcome::to_xml() const {
    // XML serialization is optional for now
    // Return a placeholder that indicates XML is not yet implemented
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<OperationOutcome xmlns=\"http://hl7.org/fhir\">\n";

    if (!id_.empty()) {
        xml << "  <id value=\"" << id_ << "\"/>\n";
    }

    for (const auto& issue : issues_) {
        xml << "  <issue>\n";
        xml << "    <severity value=\"" << to_string(issue.severity)
            << "\"/>\n";
        xml << "    <code value=\"" << to_string(issue.code) << "\"/>\n";

        if (issue.diagnostics) {
            xml << "    <diagnostics value=\"" << *issue.diagnostics
                << "\"/>\n";
        }

        xml << "  </issue>\n";
    }

    xml << "</OperationOutcome>\n";
    return xml.str();
}

// =============================================================================
// Factory Methods for Common Errors
// =============================================================================

operation_outcome operation_outcome::not_found(std::string_view resource_type,
                                               std::string_view resource_id) {
    std::string message = "Resource ";
    message += resource_type;
    message += "/";
    message += resource_id;
    message += " not found";

    return operation_outcome(
        outcome_issue::error(issue_type::not_found, std::move(message)));
}

operation_outcome operation_outcome::bad_request(std::string message) {
    return operation_outcome(
        outcome_issue::error(issue_type::invalid, std::move(message)));
}

operation_outcome operation_outcome::validation_error(
    std::string message, std::vector<std::string> paths) {
    auto issue = outcome_issue::error(issue_type::invalid, std::move(message));
    issue.expression = std::move(paths);
    return operation_outcome(std::move(issue));
}

operation_outcome operation_outcome::internal_error(std::string message) {
    return operation_outcome(
        outcome_issue::error(issue_type::exception, std::move(message)));
}

operation_outcome operation_outcome::method_not_allowed(
    std::string_view method, std::string_view resource_type) {
    std::string message = "Method ";
    message += method;
    message += " not allowed for resource type ";
    message += resource_type;

    return operation_outcome(
        outcome_issue::error(issue_type::not_supported, std::move(message)));
}

operation_outcome operation_outcome::not_acceptable(
    std::string_view accept_header) {
    std::string message =
        "Cannot produce content type matching Accept header: ";
    message += accept_header;

    return operation_outcome(
        outcome_issue::error(issue_type::not_supported, std::move(message)));
}

operation_outcome operation_outcome::conflict(std::string message) {
    return operation_outcome(
        outcome_issue::error(issue_type::conflict, std::move(message)));
}

operation_outcome operation_outcome::gone(std::string_view resource_type,
                                          std::string_view resource_id) {
    std::string message = "Resource ";
    message += resource_type;
    message += "/";
    message += resource_id;
    message += " has been deleted";

    return operation_outcome(
        outcome_issue::error(issue_type::deleted, std::move(message)));
}

operation_outcome operation_outcome::information(std::string message) {
    return operation_outcome(outcome_issue::info(std::move(message)));
}

// =============================================================================
// Utility Functions
// =============================================================================

http_status outcome_to_http_status(
    const operation_outcome& outcome) noexcept {
    if (!outcome.has_issues()) {
        return http_status::ok;
    }

    // Check the first error issue to determine status code
    for (const auto& issue : outcome.issues()) {
        if (issue.severity != issue_severity::error &&
            issue.severity != issue_severity::fatal) {
            continue;
        }

        switch (issue.code) {
            case issue_type::not_found:
            case issue_type::deleted:
                return http_status::not_found;
            case issue_type::invalid:
            case issue_type::structure:
            case issue_type::required:
            case issue_type::value:
                return http_status::bad_request;
            case issue_type::security:
            case issue_type::login:
            case issue_type::unknown:
            case issue_type::expired:
                return http_status::unauthorized;
            case issue_type::forbidden:
            case issue_type::suppressed:
                return http_status::forbidden;
            case issue_type::not_supported:
                return http_status::method_not_allowed;
            case issue_type::conflict:
            case issue_type::duplicate:
                return http_status::conflict;
            case issue_type::too_costly:
            case issue_type::throttled:
                return http_status::service_unavailable;
            case issue_type::invariant:
            case issue_type::business_rule:
                return http_status::unprocessable_entity;
            default:
                return http_status::internal_server_error;
        }
    }

    return http_status::internal_server_error;
}

http_response create_outcome_response(const operation_outcome& outcome) {
    http_response response;
    response.status = outcome_to_http_status(outcome);
    response.body = outcome.to_json();
    response.content = content_type::fhir_json;
    response.headers["Content-Type"] = std::string(
        to_mime_type(content_type::fhir_json));
    return response;
}

}  // namespace pacs::bridge::fhir
