#ifndef PACS_BRIDGE_EMR_PATIENT_LOOKUP_H
#define PACS_BRIDGE_EMR_PATIENT_LOOKUP_H

/**
 * @file patient_lookup.h
 * @brief Patient demographics query interface for EMR integration
 *
 * Provides a high-level interface for querying patient demographics
 * from external EMR systems via FHIR API. Supports various search
 * criteria and handles result caching.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/104
 * @see https://www.hl7.org/fhir/patient.html#search
 */

#include "emr_types.h"
#include "fhir_client.h"
#include "patient_record.h"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::emr {

// Forward declarations
class patient_matcher;

// =============================================================================
// Patient Query Parameters
// =============================================================================

/**
 * @brief Patient search query parameters
 *
 * Contains all supported search criteria for patient lookup.
 * At least one search parameter must be provided.
 */
struct patient_query {
    /** Patient ID / MRN */
    std::optional<std::string> patient_id;

    /** Identifier system (assigning authority) */
    std::optional<std::string> identifier_system;

    /** Family name */
    std::optional<std::string> family_name;

    /** Given name (first name) */
    std::optional<std::string> given_name;

    /** Birth date (YYYY-MM-DD format) */
    std::optional<std::string> birth_date;

    /** Gender (male, female, other, unknown) */
    std::optional<std::string> gender;

    /** Include inactive patients in search */
    bool include_inactive{false};

    /** Maximum number of results to return */
    size_t max_results{10};

    /**
     * @brief Check if query has any search criteria
     */
    [[nodiscard]] bool is_empty() const noexcept {
        return !patient_id.has_value() &&
               !family_name.has_value() &&
               !given_name.has_value() &&
               !birth_date.has_value();
    }

    /**
     * @brief Check if query is for exact MRN lookup
     */
    [[nodiscard]] bool is_mrn_lookup() const noexcept {
        return patient_id.has_value() &&
               !family_name.has_value() &&
               !given_name.has_value();
    }

    /**
     * @brief Create query for MRN lookup
     */
    [[nodiscard]] static patient_query by_mrn(std::string mrn) {
        patient_query q;
        q.patient_id = std::move(mrn);
        q.max_results = 1;
        return q;
    }

    /**
     * @brief Create query for name and DOB lookup
     */
    [[nodiscard]] static patient_query by_name_dob(
        std::string family,
        std::string given,
        std::string dob) {
        patient_query q;
        q.family_name = std::move(family);
        q.given_name = std::move(given);
        q.birth_date = std::move(dob);
        return q;
    }

    /**
     * @brief Create query with identifier system
     */
    [[nodiscard]] static patient_query by_identifier(
        std::string system,
        std::string value) {
        patient_query q;
        q.identifier_system = std::move(system);
        q.patient_id = std::move(value);
        q.max_results = 1;
        return q;
    }
};

// =============================================================================
// Patient Lookup Configuration
// =============================================================================

/**
 * @brief Configuration for patient lookup service
 */
struct patient_lookup_config {
    /** Enable caching of query results */
    bool enable_cache{true};

    /** Cache TTL for successful lookups */
    std::chrono::seconds cache_ttl{3600};  // 1 hour

    /** Cache TTL for not-found results (negative caching) */
    std::chrono::seconds negative_cache_ttl{300};  // 5 minutes

    /** Maximum cache entries */
    size_t max_cache_entries{10000};

    /** Enable automatic disambiguation for multiple matches */
    bool auto_disambiguate{true};

    /** Minimum match score for auto-disambiguation */
    double min_match_score{0.9};

    /** Default identifier system for MRN lookups */
    std::string default_identifier_system;

    /** Include raw JSON in patient records */
    bool include_raw_json{false};
};

// =============================================================================
// EMR Patient Lookup Service
// =============================================================================

/**
 * @brief Patient demographics lookup service
 *
 * Provides patient lookup functionality against external EMR systems
 * via FHIR API. Supports caching, automatic retry, and disambiguation
 * of multiple matches.
 *
 * Thread-safe: All operations are thread-safe for concurrent use.
 *
 * @example Basic Usage
 * ```cpp
 * // Create FHIR client
 * fhir_client_config fhir_config;
 * fhir_config.base_url = "https://emr.hospital.local/fhir";
 *
 * auto client = std::make_shared<fhir_client>(fhir_config);
 *
 * // Create lookup service
 * patient_lookup_config lookup_config;
 * lookup_config.enable_cache = true;
 *
 * emr_patient_lookup lookup(client, lookup_config);
 *
 * // Query by MRN
 * auto result = lookup.get_by_mrn("MRN12345");
 * if (result) {
 *     std::cout << "Patient: " << result->family_name()
 *               << ", " << result->given_name() << "\n";
 * } else {
 *     std::cerr << "Error: " << to_string(result.error()) << "\n";
 * }
 * ```
 *
 * @example Search with Disambiguation
 * ```cpp
 * auto query = patient_query::by_name_dob("Smith", "John", "1980-01-01");
 * query.max_results = 10;
 *
 * auto result = lookup.search_patients(query);
 * if (result) {
 *     std::cout << "Found " << result->size() << " patients\n";
 *     for (const auto& match : *result) {
 *         std::cout << "  - " << match.patient.mrn
 *                   << " (score: " << match.score << ")\n";
 *     }
 * }
 * ```
 */
class emr_patient_lookup {
public:
    /**
     * @brief Construct with FHIR client
     *
     * @param client FHIR client for EMR communication
     * @param config Lookup configuration
     */
    explicit emr_patient_lookup(
        std::shared_ptr<fhir_client> client,
        const patient_lookup_config& config = {});

    /**
     * @brief Destructor
     */
    ~emr_patient_lookup();

    // Non-copyable, movable
    emr_patient_lookup(const emr_patient_lookup&) = delete;
    emr_patient_lookup& operator=(const emr_patient_lookup&) = delete;
    emr_patient_lookup(emr_patient_lookup&&) noexcept;
    emr_patient_lookup& operator=(emr_patient_lookup&&) noexcept;

    // =========================================================================
    // Single Patient Lookup
    // =========================================================================

    /**
     * @brief Get patient by MRN
     *
     * Performs exact match lookup by medical record number.
     * Results are cached if caching is enabled.
     *
     * @param mrn Medical record number
     * @return Patient record or error
     */
    [[nodiscard]] auto get_by_mrn(std::string_view mrn)
        -> std::expected<patient_record, patient_error>;

    /**
     * @brief Get patient by identifier with system
     *
     * @param system Identifier system URI
     * @param value Identifier value
     * @return Patient record or error
     */
    [[nodiscard]] auto get_by_identifier(std::string_view system,
                                         std::string_view value)
        -> std::expected<patient_record, patient_error>;

    /**
     * @brief Get patient by FHIR resource ID
     *
     * @param id FHIR Patient resource ID
     * @return Patient record or error
     */
    [[nodiscard]] auto get_by_id(std::string_view id)
        -> std::expected<patient_record, patient_error>;

    /**
     * @brief Find single patient matching query
     *
     * Returns a single patient if exactly one match is found.
     * Returns an error if no matches or multiple matches are found
     * (unless auto-disambiguation is enabled and successful).
     *
     * @param query Search query
     * @return Patient record or error
     */
    [[nodiscard]] auto find_patient(const patient_query& query)
        -> std::expected<patient_record, patient_error>;

    // =========================================================================
    // Multiple Patient Search
    // =========================================================================

    /**
     * @brief Search for patients matching query
     *
     * Returns all patients matching the query criteria,
     * up to max_results limit.
     *
     * @param query Search query
     * @return List of matching patients with scores or error
     */
    [[nodiscard]] auto search_patients(const patient_query& query)
        -> std::expected<std::vector<patient_match>, patient_error>;

    /**
     * @brief Search patients with raw FHIR search params
     *
     * Allows direct FHIR search parameter passthrough for
     * advanced queries.
     *
     * @param params FHIR search parameters
     * @return List of matching patients or error
     */
    [[nodiscard]] auto search_with_params(const search_params& params)
        -> std::expected<std::vector<patient_record>, patient_error>;

    // =========================================================================
    // Cache Management
    // =========================================================================

    /**
     * @brief Clear patient cache
     */
    void clear_cache();

    /**
     * @brief Remove specific patient from cache
     *
     * @param mrn Medical record number
     */
    void invalidate_cache(std::string_view mrn);

    /**
     * @brief Prefetch patients into cache
     *
     * @param mrns List of MRNs to prefetch
     * @return Number of successfully prefetched patients
     */
    size_t prefetch(const std::vector<std::string>& mrns);

    /**
     * @brief Get cache statistics
     */
    struct cache_stats {
        size_t hits{0};
        size_t misses{0};
        size_t entries{0};
        double hit_rate{0.0};
    };

    [[nodiscard]] cache_stats get_cache_stats() const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const patient_lookup_config& config() const noexcept;

    /**
     * @brief Update configuration
     *
     * @param config New configuration
     */
    void set_config(const patient_lookup_config& config);

    /**
     * @brief Set custom patient matcher
     *
     * @param matcher Patient matcher for disambiguation
     */
    void set_matcher(std::shared_ptr<patient_matcher> matcher);

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Lookup statistics
     */
    struct statistics {
        size_t total_queries{0};
        size_t successful_queries{0};
        size_t failed_queries{0};
        size_t multiple_matches{0};
        size_t cache_hits{0};
        size_t cache_misses{0};
        std::chrono::milliseconds total_query_time{0};
    };

    /**
     * @brief Get lookup statistics
     */
    [[nodiscard]] statistics get_statistics() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset_statistics() noexcept;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// FHIR Patient Parser
// =============================================================================

/**
 * @brief Parse FHIR Patient resource to patient_record
 *
 * @param json FHIR Patient JSON
 * @return Parsed patient record or error
 */
[[nodiscard]] std::expected<patient_record, patient_error> parse_fhir_patient(
    std::string_view json);

/**
 * @brief Parse FHIR Bundle of Patient resources
 *
 * @param bundle FHIR Bundle
 * @return List of parsed patient records
 */
[[nodiscard]] std::vector<patient_record> parse_patient_bundle(
    const fhir_bundle& bundle);

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_PATIENT_LOOKUP_H
