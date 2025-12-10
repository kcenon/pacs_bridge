#ifndef PACS_BRIDGE_EMR_PATIENT_MATCHER_H
#define PACS_BRIDGE_EMR_PATIENT_MATCHER_H

/**
 * @file patient_matcher.h
 * @brief Patient matching and disambiguation logic
 *
 * Provides algorithms for matching and disambiguating patient records
 * when multiple candidates are returned from EMR queries. Implements
 * various matching strategies based on demographic data.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/104
 */

#include "patient_record.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::emr {

// =============================================================================
// Match Result
// =============================================================================

/**
 * @brief Result of patient matching operation
 */
struct match_result {
    /** Index of best matching patient (-1 if no match) */
    int best_match_index{-1};

    /** Confidence score of best match (0.0 to 1.0) */
    double best_match_score{0.0};

    /** All candidates with their scores */
    std::vector<patient_match> candidates;

    /** Whether a single definitive match was found */
    bool is_definitive{false};

    /** Whether disambiguation is needed */
    bool needs_disambiguation{false};

    /** Reason for ambiguity (if any) */
    std::string ambiguity_reason;

    /**
     * @brief Get best matching patient if definitive
     */
    [[nodiscard]] const patient_record* best_patient() const noexcept {
        if (best_match_index >= 0 &&
            static_cast<size_t>(best_match_index) < candidates.size()) {
            return &candidates[static_cast<size_t>(best_match_index)].patient;
        }
        return nullptr;
    }
};

// =============================================================================
// Matching Criteria
// =============================================================================

/**
 * @brief Criteria for patient matching
 */
struct match_criteria {
    /** Expected MRN (if known) */
    std::optional<std::string> mrn;

    /** Expected family name */
    std::optional<std::string> family_name;

    /** Expected given name */
    std::optional<std::string> given_name;

    /** Expected birth date (YYYY-MM-DD) */
    std::optional<std::string> birth_date;

    /** Expected sex */
    std::optional<std::string> sex;

    /** Identifier system to match */
    std::optional<std::string> identifier_system;

    /** Identifier value to match */
    std::optional<std::string> identifier_value;
};

// =============================================================================
// Patient Matcher Configuration
// =============================================================================

/**
 * @brief Configuration for patient matching
 */
struct matcher_config {
    /** Weight for MRN match (0.0 to 1.0) */
    double mrn_weight{1.0};

    /** Weight for family name match */
    double family_name_weight{0.3};

    /** Weight for given name match */
    double given_name_weight{0.2};

    /** Weight for birth date match */
    double birth_date_weight{0.3};

    /** Weight for sex match */
    double sex_weight{0.1};

    /** Weight for identifier match */
    double identifier_weight{0.9};

    /** Minimum overall score to consider a match */
    double min_match_score{0.5};

    /** Score threshold for definitive match */
    double definitive_threshold{0.95};

    /** Enable fuzzy name matching */
    bool fuzzy_name_matching{true};

    /** Maximum edit distance for fuzzy matching */
    size_t max_edit_distance{2};

    /** Normalize names before comparison (lowercase, remove accents) */
    bool normalize_names{true};

    /** Treat missing birth date as partial match */
    bool allow_missing_birthdate{true};
};

// =============================================================================
// Patient Matcher
// =============================================================================

/**
 * @brief Patient matching and disambiguation service
 *
 * Provides algorithms for matching patient records based on demographic
 * data. Used to disambiguate when multiple candidates are returned
 * from EMR queries.
 *
 * Thread-safe: All operations are thread-safe.
 *
 * @example Basic Usage
 * ```cpp
 * patient_matcher matcher;
 *
 * match_criteria criteria;
 * criteria.family_name = "Smith";
 * criteria.given_name = "John";
 * criteria.birth_date = "1980-01-01";
 *
 * std::vector<patient_record> candidates = ...; // From EMR search
 *
 * auto result = matcher.find_best_match(candidates, criteria);
 * if (result.is_definitive) {
 *     auto* patient = result.best_patient();
 *     // Use the matched patient
 * } else if (result.needs_disambiguation) {
 *     // Present candidates to user for selection
 * }
 * ```
 *
 * @example Custom Matching Weights
 * ```cpp
 * matcher_config config;
 * config.birth_date_weight = 0.5;  // Increase birth date importance
 * config.definitive_threshold = 0.9;  // Lower threshold for definitive match
 *
 * patient_matcher matcher(config);
 * ```
 */
class patient_matcher {
public:
    /**
     * @brief Construct with default configuration
     */
    patient_matcher();

    /**
     * @brief Construct with custom configuration
     */
    explicit patient_matcher(const matcher_config& config);

    /**
     * @brief Destructor
     */
    ~patient_matcher();

    // Non-copyable, movable
    patient_matcher(const patient_matcher&) = delete;
    patient_matcher& operator=(const patient_matcher&) = delete;
    patient_matcher(patient_matcher&&) noexcept;
    patient_matcher& operator=(patient_matcher&&) noexcept;

    // =========================================================================
    // Matching Operations
    // =========================================================================

    /**
     * @brief Find best matching patient from candidates
     *
     * @param candidates List of candidate patients
     * @param criteria Matching criteria
     * @return Match result with scores
     */
    [[nodiscard]] match_result find_best_match(
        const std::vector<patient_record>& candidates,
        const match_criteria& criteria) const;

    /**
     * @brief Calculate match score for a single patient
     *
     * @param patient Patient to score
     * @param criteria Matching criteria
     * @return Match score (0.0 to 1.0)
     */
    [[nodiscard]] double calculate_score(
        const patient_record& patient,
        const match_criteria& criteria) const;

    /**
     * @brief Score all candidates
     *
     * @param candidates List of candidate patients
     * @param criteria Matching criteria
     * @return Sorted list of matches (highest score first)
     */
    [[nodiscard]] std::vector<patient_match> score_candidates(
        const std::vector<patient_record>& candidates,
        const match_criteria& criteria) const;

    /**
     * @brief Check if two patients are likely the same person
     *
     * @param patient1 First patient
     * @param patient2 Second patient
     * @return Match score (0.0 to 1.0)
     */
    [[nodiscard]] double compare_patients(
        const patient_record& patient1,
        const patient_record& patient2) const;

    // =========================================================================
    // String Matching Utilities
    // =========================================================================

    /**
     * @brief Calculate string similarity (0.0 to 1.0)
     *
     * Uses Jaro-Winkler similarity for name comparison.
     *
     * @param str1 First string
     * @param str2 Second string
     * @return Similarity score
     */
    [[nodiscard]] static double string_similarity(
        std::string_view str1,
        std::string_view str2);

    /**
     * @brief Calculate Levenshtein edit distance
     *
     * @param str1 First string
     * @param str2 Second string
     * @return Edit distance
     */
    [[nodiscard]] static size_t edit_distance(
        std::string_view str1,
        std::string_view str2);

    /**
     * @brief Normalize name for comparison
     *
     * Converts to lowercase, removes accents, trims whitespace.
     *
     * @param name Name to normalize
     * @return Normalized name
     */
    [[nodiscard]] static std::string normalize_name(std::string_view name);

    /**
     * @brief Compare dates with partial matching
     *
     * @param date1 First date (YYYY-MM-DD)
     * @param date2 Second date (YYYY-MM-DD)
     * @return Similarity score (0.0 to 1.0)
     */
    [[nodiscard]] static double compare_dates(
        std::string_view date1,
        std::string_view date2);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const matcher_config& config() const noexcept;

    /**
     * @brief Update configuration
     */
    void set_config(const matcher_config& config);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// Disambiguation Strategies
// =============================================================================

/**
 * @brief Strategy for automatic disambiguation
 */
enum class disambiguation_strategy {
    /** Select highest scoring match above threshold */
    highest_score,

    /** Require exact MRN match */
    exact_mrn,

    /** Require exact identifier match */
    exact_identifier,

    /** Require all criteria to match */
    all_criteria,

    /** Never auto-disambiguate, always require user input */
    manual_only
};

/**
 * @brief Apply disambiguation strategy to match result
 *
 * @param result Match result to process
 * @param strategy Disambiguation strategy
 * @param threshold Score threshold for auto-disambiguation
 * @return Updated match result
 */
[[nodiscard]] match_result apply_disambiguation_strategy(
    const match_result& result,
    disambiguation_strategy strategy,
    double threshold = 0.95);

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_PATIENT_MATCHER_H
