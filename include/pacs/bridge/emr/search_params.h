#ifndef PACS_BRIDGE_EMR_SEARCH_PARAMS_H
#define PACS_BRIDGE_EMR_SEARCH_PARAMS_H

/**
 * @file search_params.h
 * @brief FHIR search parameter builder
 *
 * Provides a fluent interface for building FHIR search queries
 * with proper URL encoding and parameter formatting.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 * @see https://www.hl7.org/fhir/search.html
 */

#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pacs::bridge::emr {

/**
 * @brief FHIR search modifier prefixes
 *
 * Used with date, number, and quantity search parameters.
 */
enum class search_prefix {
    /** Equal (default) */
    eq,
    /** Not equal */
    ne,
    /** Greater than */
    gt,
    /** Less than */
    lt,
    /** Greater than or equal */
    ge,
    /** Less than or equal */
    le,
    /** Starts after */
    sa,
    /** Ends before */
    eb,
    /** Approximately */
    ap
};

/**
 * @brief Convert search_prefix to string
 */
[[nodiscard]] constexpr std::string_view to_string(search_prefix prefix) noexcept {
    switch (prefix) {
        case search_prefix::eq:
            return "eq";
        case search_prefix::ne:
            return "ne";
        case search_prefix::gt:
            return "gt";
        case search_prefix::lt:
            return "lt";
        case search_prefix::ge:
            return "ge";
        case search_prefix::le:
            return "le";
        case search_prefix::sa:
            return "sa";
        case search_prefix::eb:
            return "eb";
        case search_prefix::ap:
            return "ap";
        default:
            return "eq";
    }
}

/**
 * @brief FHIR search parameter builder
 *
 * Provides a fluent interface for building FHIR search queries.
 * Supports all standard FHIR search parameter types and modifiers.
 *
 * @example Basic Search
 * ```cpp
 * search_params params;
 * params.add("name", "Smith")
 *       .add("birthdate", "1990-01-01")
 *       .add("_count", "10");
 *
 * auto query = params.to_query_string();
 * // Returns: "name=Smith&birthdate=1990-01-01&_count=10"
 * ```
 *
 * @example Patient Search with Modifiers
 * ```cpp
 * auto params = search_params::for_patient()
 *     .identifier("http://hospital.org/mrn|123456")
 *     .name_contains("john")
 *     .birthdate_before("2000-01-01")
 *     .active(true)
 *     .count(20);
 *
 * auto query = params.to_query_string();
 * ```
 *
 * @example Date Range Search
 * ```cpp
 * search_params params;
 * params.add_date("date", search_prefix::ge, "2024-01-01")
 *       .add_date("date", search_prefix::lt, "2024-12-31");
 * ```
 */
class search_params {
public:
    /**
     * @brief Default constructor
     */
    search_params() = default;

    // =========================================================================
    // Generic Parameter Methods
    // =========================================================================

    /**
     * @brief Add a simple string parameter
     * @param name Parameter name
     * @param value Parameter value
     * @return Reference to this for chaining
     */
    search_params& add(std::string_view name, std::string_view value) {
        params_.emplace_back(std::string(name), std::string(value));
        return *this;
    }

    /**
     * @brief Add a parameter with modifier (e.g., name:exact=John)
     * @param name Parameter name
     * @param modifier Modifier (exact, contains, etc.)
     * @param value Parameter value
     * @return Reference to this for chaining
     */
    search_params& add_with_modifier(std::string_view name,
                                     std::string_view modifier,
                                     std::string_view value) {
        std::string param_name(name);
        param_name.push_back(':');
        param_name.append(modifier);
        params_.emplace_back(std::move(param_name), std::string(value));
        return *this;
    }

    /**
     * @brief Add a date parameter with comparison prefix
     * @param name Parameter name
     * @param prefix Comparison prefix (eq, gt, lt, etc.)
     * @param value Date value (YYYY-MM-DD or ISO 8601)
     * @return Reference to this for chaining
     */
    search_params& add_date(std::string_view name, search_prefix prefix,
                            std::string_view value) {
        std::string val;
        if (prefix != search_prefix::eq) {
            val.append(to_string(prefix));
        }
        val.append(value);
        params_.emplace_back(std::string(name), std::move(val));
        return *this;
    }

    /**
     * @brief Add a number parameter with comparison prefix
     * @param name Parameter name
     * @param prefix Comparison prefix
     * @param value Numeric value
     * @return Reference to this for chaining
     */
    search_params& add_number(std::string_view name, search_prefix prefix,
                              int64_t value) {
        std::string val;
        if (prefix != search_prefix::eq) {
            val.append(to_string(prefix));
        }
        val.append(std::to_string(value));
        params_.emplace_back(std::string(name), std::move(val));
        return *this;
    }

    /**
     * @brief Add a token parameter (system|code)
     * @param name Parameter name
     * @param system Token system URI (optional)
     * @param code Token code
     * @return Reference to this for chaining
     */
    search_params& add_token(std::string_view name,
                             std::optional<std::string_view> system,
                             std::string_view code) {
        std::string val;
        if (system.has_value() && !system->empty()) {
            val.append(*system);
        }
        val.push_back('|');
        val.append(code);
        params_.emplace_back(std::string(name), std::move(val));
        return *this;
    }

    /**
     * @brief Add a reference parameter
     * @param name Parameter name
     * @param resource_type Referenced resource type
     * @param id Resource ID
     * @return Reference to this for chaining
     */
    search_params& add_reference(std::string_view name,
                                 std::string_view resource_type,
                                 std::string_view id) {
        std::string val(resource_type);
        val.push_back('/');
        val.append(id);
        params_.emplace_back(std::string(name), std::move(val));
        return *this;
    }

    // =========================================================================
    // Common Search Parameters
    // =========================================================================

    /**
     * @brief Set _id parameter
     */
    search_params& id(std::string_view value) {
        return add("_id", value);
    }

    /**
     * @brief Set _count parameter (page size)
     */
    search_params& count(size_t value) {
        return add("_count", std::to_string(value));
    }

    /**
     * @brief Set _offset parameter (for pagination)
     */
    search_params& offset(size_t value) {
        return add("_offset", std::to_string(value));
    }

    /**
     * @brief Set _sort parameter
     * @param field Field to sort by
     * @param descending If true, sort descending
     */
    search_params& sort(std::string_view field, bool descending = false) {
        std::string val;
        if (descending) {
            val.push_back('-');
        }
        val.append(field);
        return add("_sort", val);
    }

    /**
     * @brief Set _include parameter for referenced resources
     */
    search_params& include(std::string_view resource_type,
                           std::string_view search_param) {
        std::string val(resource_type);
        val.push_back(':');
        val.append(search_param);
        return add("_include", val);
    }

    /**
     * @brief Set _revinclude parameter for reverse references
     */
    search_params& rev_include(std::string_view resource_type,
                               std::string_view search_param) {
        std::string val(resource_type);
        val.push_back(':');
        val.append(search_param);
        return add("_revinclude", val);
    }

    /**
     * @brief Set _summary parameter
     * @param value Summary type (true, false, text, count, data)
     */
    search_params& summary(std::string_view value) {
        return add("_summary", value);
    }

    /**
     * @brief Set _elements parameter to limit returned elements
     */
    search_params& elements(const std::vector<std::string_view>& fields) {
        if (fields.empty()) {
            return *this;
        }
        std::string val;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) {
                val.push_back(',');
            }
            val.append(fields[i]);
        }
        return add("_elements", val);
    }

    // =========================================================================
    // Patient-Specific Parameters
    // =========================================================================

    /**
     * @brief Create search params for Patient resource
     */
    [[nodiscard]] static search_params for_patient() {
        return search_params{};
    }

    /**
     * @brief Add identifier parameter
     */
    search_params& identifier(std::string_view value) {
        return add("identifier", value);
    }

    /**
     * @brief Add identifier with system
     */
    search_params& identifier(std::string_view system, std::string_view value) {
        return add_token("identifier", system, value);
    }

    /**
     * @brief Add name parameter (contains search)
     */
    search_params& name(std::string_view value) {
        return add("name", value);
    }

    /**
     * @brief Add name with exact match modifier
     */
    search_params& name_exact(std::string_view value) {
        return add_with_modifier("name", "exact", value);
    }

    /**
     * @brief Add name with contains modifier
     */
    search_params& name_contains(std::string_view value) {
        return add_with_modifier("name", "contains", value);
    }

    /**
     * @brief Add family name parameter
     */
    search_params& family(std::string_view value) {
        return add("family", value);
    }

    /**
     * @brief Add given name parameter
     */
    search_params& given(std::string_view value) {
        return add("given", value);
    }

    /**
     * @brief Add birthdate parameter
     */
    search_params& birthdate(std::string_view value) {
        return add("birthdate", value);
    }

    /**
     * @brief Add birthdate before a date
     */
    search_params& birthdate_before(std::string_view value) {
        return add_date("birthdate", search_prefix::lt, value);
    }

    /**
     * @brief Add birthdate after a date
     */
    search_params& birthdate_after(std::string_view value) {
        return add_date("birthdate", search_prefix::gt, value);
    }

    /**
     * @brief Add gender parameter
     */
    search_params& gender(std::string_view value) {
        return add("gender", value);
    }

    /**
     * @brief Add active parameter
     */
    search_params& active(bool value) {
        return add("active", value ? "true" : "false");
    }

    // =========================================================================
    // ServiceRequest-Specific Parameters
    // =========================================================================

    /**
     * @brief Create search params for ServiceRequest resource
     */
    [[nodiscard]] static search_params for_service_request() {
        return search_params{};
    }

    /**
     * @brief Add patient reference parameter
     */
    search_params& patient(std::string_view patient_id) {
        return add_reference("patient", "Patient", patient_id);
    }

    /**
     * @brief Add status parameter
     */
    search_params& status(std::string_view value) {
        return add("status", value);
    }

    /**
     * @brief Add category parameter
     */
    search_params& category(std::string_view system, std::string_view code) {
        return add_token("category", system, code);
    }

    /**
     * @brief Add code parameter
     */
    search_params& code(std::string_view system, std::string_view code_value) {
        return add_token("code", system, code_value);
    }

    /**
     * @brief Add authored date parameter
     */
    search_params& authored(std::string_view value) {
        return add("authored", value);
    }

    // =========================================================================
    // ImagingStudy-Specific Parameters
    // =========================================================================

    /**
     * @brief Create search params for ImagingStudy resource
     */
    [[nodiscard]] static search_params for_imaging_study() {
        return search_params{};
    }

    /**
     * @brief Add study instance UID parameter
     */
    search_params& study_uid(std::string_view uid) {
        return add_token("identifier",
                         "urn:dicom:uid",
                         uid);
    }

    /**
     * @brief Add accession number parameter
     */
    search_params& accession(std::string_view value) {
        return add("identifier", value);
    }

    /**
     * @brief Add modality parameter
     */
    search_params& modality(std::string_view code_value) {
        return add_token("modality", "http://dicom.nema.org/resources/ontology/DCM", code_value);
    }

    /**
     * @brief Add started date parameter
     */
    search_params& started(std::string_view value) {
        return add("started", value);
    }

    // =========================================================================
    // DiagnosticReport-Specific Parameters
    // =========================================================================

    /**
     * @brief Create search params for DiagnosticReport resource
     */
    [[nodiscard]] static search_params for_diagnostic_report() {
        return search_params{};
    }

    /**
     * @brief Add based-on reference (ServiceRequest)
     */
    search_params& based_on(std::string_view service_request_id) {
        return add_reference("based-on", "ServiceRequest", service_request_id);
    }

    /**
     * @brief Add issued date parameter
     */
    search_params& issued(std::string_view value) {
        return add("issued", value);
    }

    // =========================================================================
    // Query String Generation
    // =========================================================================

    /**
     * @brief Build the query string
     * @return URL-encoded query string (without leading '?')
     */
    [[nodiscard]] std::string to_query_string() const {
        if (params_.empty()) {
            return {};
        }

        std::ostringstream oss;
        bool first = true;
        for (const auto& [name, value] : params_) {
            if (!first) {
                oss << '&';
            }
            first = false;
            oss << url_encode(name) << '=' << url_encode(value);
        }
        return oss.str();
    }

    /**
     * @brief Check if parameters are empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return params_.empty();
    }

    /**
     * @brief Get number of parameters
     */
    [[nodiscard]] size_t size() const noexcept {
        return params_.size();
    }

    /**
     * @brief Clear all parameters
     */
    void clear() noexcept {
        params_.clear();
    }

    /**
     * @brief Get raw parameters
     */
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>&
    parameters() const noexcept {
        return params_;
    }

private:
    std::vector<std::pair<std::string, std::string>> params_;

    /**
     * @brief URL-encode a string
     */
    [[nodiscard]] static std::string url_encode(std::string_view str) {
        std::ostringstream oss;
        for (char ch : str) {
            auto c = static_cast<unsigned char>(ch);
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' ||
                c == '~') {
                oss << ch;
            } else if (c == ' ') {
                oss << '+';
            } else {
                oss << '%' << std::uppercase << std::hex
                    << static_cast<int>(c >> 4)
                    << static_cast<int>(c & 0x0F);
            }
        }
        return oss.str();
    }
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_SEARCH_PARAMS_H
