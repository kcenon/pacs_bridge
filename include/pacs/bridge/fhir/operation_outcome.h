#ifndef PACS_BRIDGE_FHIR_OPERATION_OUTCOME_H
#define PACS_BRIDGE_FHIR_OPERATION_OUTCOME_H

/**
 * @file operation_outcome.h
 * @brief FHIR OperationOutcome resource for error responses
 *
 * Provides structured error responses per FHIR R4 specification.
 * OperationOutcome is returned for all error conditions and can also
 * accompany successful responses.
 *
 * @see https://hl7.org/fhir/R4/operationoutcome.html
 * @see https://github.com/kcenon/pacs_bridge/issues/31
 */

#include "fhir_types.h"

#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::fhir {

// =============================================================================
// Issue Severity and Code
// =============================================================================

/**
 * @brief Severity of the issue (required)
 *
 * @see https://hl7.org/fhir/R4/valueset-issue-severity.html
 */
enum class issue_severity {
    fatal,       // The issue caused processing to abort
    error,       // The issue indicates a problem
    warning,     // The issue indicates potential problems
    information  // The issue is purely informational
};

/**
 * @brief Convert issue_severity to FHIR code string
 */
[[nodiscard]] constexpr std::string_view to_string(
    issue_severity severity) noexcept {
    switch (severity) {
        case issue_severity::fatal: return "fatal";
        case issue_severity::error: return "error";
        case issue_severity::warning: return "warning";
        case issue_severity::information: return "information";
    }
    return "error";
}

/**
 * @brief Type of issue detected (required)
 *
 * @see https://hl7.org/fhir/R4/valueset-issue-type.html
 */
enum class issue_type {
    // Invalid content
    invalid,           // Content invalid against spec or profile
    structure,         // Structural issue in content
    required,          // Required element missing
    value,            // Element value invalid
    invariant,         // Validation rule failed

    // Security
    security,          // Authentication/authorization error
    login,            // Login required
    unknown,          // Unknown user
    expired,          // Session expired
    forbidden,        // Access denied
    suppressed,       // Information suppressed

    // Processing
    processing,        // Processing failure
    not_supported,     // Operation not supported
    duplicate,         // Duplicate record
    multiple_matches,  // Multiple matches found
    not_found,         // Resource not found
    deleted,          // Resource deleted
    too_long,         // Content too long
    code_invalid,     // Code/system invalid
    extension,        // Unacceptable extension
    too_costly,       // Operation too costly
    business_rule,    // Business rule violated
    conflict,         // Edit conflict
    transient,        // Transient error
    lock_error,       // Resource locked
    no_store,         // No storage available
    exception,        // Exception occurred
    timeout,          // Timeout
    incomplete,       // Incomplete results
    throttled         // Request throttled
};

/**
 * @brief Convert issue_type to FHIR code string
 */
[[nodiscard]] std::string_view to_string(issue_type type) noexcept;

// =============================================================================
// Operation Outcome Issue
// =============================================================================

/**
 * @brief Single issue in an OperationOutcome
 */
struct outcome_issue {
    /** Severity of the issue (required) */
    issue_severity severity = issue_severity::error;

    /** Type of issue (required) */
    issue_type code = issue_type::processing;

    /** Additional details (optional) */
    std::optional<std::string> details_text;

    /** Human readable diagnostics (optional) */
    std::optional<std::string> diagnostics;

    /** FHIRPath expression to element (optional) */
    std::vector<std::string> expression;

    /** XPath expression to element (optional) */
    std::vector<std::string> location;

    /**
     * @brief Create an error issue
     */
    [[nodiscard]] static outcome_issue error(issue_type type,
                                             std::string diagnostics);

    /**
     * @brief Create a warning issue
     */
    [[nodiscard]] static outcome_issue warning(issue_type type,
                                               std::string diagnostics);

    /**
     * @brief Create an info issue
     */
    [[nodiscard]] static outcome_issue info(std::string message);
};

// =============================================================================
// Operation Outcome Resource
// =============================================================================

/**
 * @brief FHIR OperationOutcome resource
 *
 * Collection of error, warning, or information messages that result
 * from a system action.
 *
 * @see https://hl7.org/fhir/R4/operationoutcome.html
 */
class operation_outcome {
public:
    /**
     * @brief Default constructor
     */
    operation_outcome() = default;

    /**
     * @brief Construct with a single issue
     */
    explicit operation_outcome(outcome_issue issue);

    /**
     * @brief Construct with multiple issues
     */
    explicit operation_outcome(std::vector<outcome_issue> issues);

    /**
     * @brief Add an issue to the outcome
     */
    void add_issue(outcome_issue issue);

    /**
     * @brief Get all issues
     */
    [[nodiscard]] const std::vector<outcome_issue>& issues() const noexcept;

    /**
     * @brief Check if there are any issues
     */
    [[nodiscard]] bool has_issues() const noexcept;

    /**
     * @brief Check if any issue is an error or fatal
     */
    [[nodiscard]] bool has_errors() const noexcept;

    /**
     * @brief Get the most severe issue severity
     */
    [[nodiscard]] issue_severity highest_severity() const noexcept;

    /**
     * @brief Get resource ID (optional)
     */
    [[nodiscard]] const std::string& id() const noexcept;

    /**
     * @brief Set resource ID
     */
    void set_id(std::string id);

    /**
     * @brief Serialize to FHIR JSON format
     */
    [[nodiscard]] std::string to_json() const;

    /**
     * @brief Serialize to FHIR XML format
     */
    [[nodiscard]] std::string to_xml() const;

    // =========================================================================
    // Factory Methods for Common Errors
    // =========================================================================

    /**
     * @brief Create a "not found" outcome (HTTP 404)
     */
    [[nodiscard]] static operation_outcome not_found(
        std::string_view resource_type,
        std::string_view resource_id);

    /**
     * @brief Create a "bad request" outcome (HTTP 400)
     */
    [[nodiscard]] static operation_outcome bad_request(std::string message);

    /**
     * @brief Create a "validation error" outcome (HTTP 422)
     */
    [[nodiscard]] static operation_outcome validation_error(
        std::string message,
        std::vector<std::string> paths = {});

    /**
     * @brief Create an "internal error" outcome (HTTP 500)
     */
    [[nodiscard]] static operation_outcome internal_error(
        std::string message);

    /**
     * @brief Create a "method not allowed" outcome (HTTP 405)
     */
    [[nodiscard]] static operation_outcome method_not_allowed(
        std::string_view method,
        std::string_view resource_type);

    /**
     * @brief Create an "unsupported media type" outcome (HTTP 406)
     */
    [[nodiscard]] static operation_outcome not_acceptable(
        std::string_view accept_header);

    /**
     * @brief Create a "conflict" outcome (HTTP 409)
     */
    [[nodiscard]] static operation_outcome conflict(std::string message);

    /**
     * @brief Create a "gone" outcome (HTTP 410)
     */
    [[nodiscard]] static operation_outcome gone(
        std::string_view resource_type,
        std::string_view resource_id);

    /**
     * @brief Create an informational outcome
     */
    [[nodiscard]] static operation_outcome information(std::string message);

private:
    std::string id_;
    std::vector<outcome_issue> issues_;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Get appropriate HTTP status for an OperationOutcome
 */
[[nodiscard]] http_status outcome_to_http_status(
    const operation_outcome& outcome) noexcept;

/**
 * @brief Create HTTP response from OperationOutcome
 */
[[nodiscard]] http_response create_outcome_response(
    const operation_outcome& outcome);

}  // namespace pacs::bridge::fhir

#endif  // PACS_BRIDGE_FHIR_OPERATION_OUTCOME_H
