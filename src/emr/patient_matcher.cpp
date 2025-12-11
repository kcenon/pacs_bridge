/**
 * @file patient_matcher.cpp
 * @brief Patient matching and disambiguation implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/104
 */

#include "pacs/bridge/emr/patient_matcher.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <numeric>

namespace pacs::bridge::emr {

// =============================================================================
// String Matching Algorithms
// =============================================================================

namespace {

/**
 * @brief Calculate Jaro similarity between two strings
 */
double jaro_similarity(std::string_view s1, std::string_view s2) {
    if (s1.empty() && s2.empty()) return 1.0;
    if (s1.empty() || s2.empty()) return 0.0;
    if (s1 == s2) return 1.0;

    size_t len1 = s1.length();
    size_t len2 = s2.length();

    // Maximum distance for matching
    size_t match_distance = static_cast<size_t>(
        std::max(len1, len2) / 2 - 1);
    if (match_distance < 1) match_distance = 1;

    std::vector<bool> s1_matches(len1, false);
    std::vector<bool> s2_matches(len2, false);

    size_t matches = 0;
    size_t transpositions = 0;

    // Find matches
    for (size_t i = 0; i < len1; ++i) {
        size_t start = (i > match_distance) ? i - match_distance : 0;
        size_t end = std::min(i + match_distance + 1, len2);

        for (size_t j = start; j < end; ++j) {
            if (s2_matches[j] || s1[i] != s2[j]) continue;
            s1_matches[i] = true;
            s2_matches[j] = true;
            ++matches;
            break;
        }
    }

    if (matches == 0) return 0.0;

    // Count transpositions
    size_t k = 0;
    for (size_t i = 0; i < len1; ++i) {
        if (!s1_matches[i]) continue;
        while (!s2_matches[k]) ++k;
        if (s1[i] != s2[k]) ++transpositions;
        ++k;
    }

    double jaro = (
        static_cast<double>(matches) / static_cast<double>(len1) +
        static_cast<double>(matches) / static_cast<double>(len2) +
        static_cast<double>(matches - transpositions / 2) /
            static_cast<double>(matches)
    ) / 3.0;

    return jaro;
}

/**
 * @brief Calculate Jaro-Winkler similarity
 */
double jaro_winkler_similarity(std::string_view s1, std::string_view s2) {
    double jaro = jaro_similarity(s1, s2);

    // Find common prefix (up to 4 characters)
    size_t prefix_len = 0;
    size_t max_prefix = std::min({s1.length(), s2.length(), size_t(4)});

    while (prefix_len < max_prefix && s1[prefix_len] == s2[prefix_len]) {
        ++prefix_len;
    }

    // Winkler modification: boost for common prefix
    constexpr double scaling_factor = 0.1;
    return jaro + static_cast<double>(prefix_len) * scaling_factor * (1.0 - jaro);
}

/**
 * @brief Normalize string for comparison
 */
std::string normalize_for_comparison(std::string_view str) {
    std::string result;
    result.reserve(str.size());

    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        }
    }

    return result;
}

}  // namespace

// =============================================================================
// Implementation
// =============================================================================

class patient_matcher::impl {
public:
    explicit impl(const matcher_config& config)
        : config_(config) {}

    match_result find_best_match(
        const std::vector<patient_record>& candidates,
        const match_criteria& criteria) const {

        match_result result;

        if (candidates.empty()) {
            return result;
        }

        // Score all candidates
        result.candidates = score_candidates_impl(candidates, criteria);

        if (result.candidates.empty()) {
            return result;
        }

        // Find best match
        result.best_match_index = 0;
        result.best_match_score = result.candidates[0].score;

        // Check for definitive match
        if (result.best_match_score >= config_.definitive_threshold) {
            // Check if there's a second candidate with similar score
            if (result.candidates.size() > 1) {
                double second_score = result.candidates[1].score;
                double gap = result.best_match_score - second_score;

                if (gap < 0.1) {
                    // Too close, needs disambiguation
                    result.is_definitive = false;
                    result.needs_disambiguation = true;
                    result.ambiguity_reason =
                        "Multiple high-scoring matches found";
                } else {
                    result.is_definitive = true;
                }
            } else {
                result.is_definitive = true;
            }
        } else if (result.best_match_score >= config_.min_match_score) {
            result.is_definitive = false;
            result.needs_disambiguation = true;
            result.ambiguity_reason =
                "Best match score below definitive threshold";
        } else {
            result.best_match_index = -1;
            result.ambiguity_reason = "No matches above minimum threshold";
        }

        return result;
    }

    double calculate_score(
        const patient_record& patient,
        const match_criteria& criteria) const {

        double total_weight = 0.0;
        double weighted_score = 0.0;

        // MRN match
        if (criteria.mrn.has_value()) {
            total_weight += config_.mrn_weight;
            if (patient.mrn == *criteria.mrn) {
                weighted_score += config_.mrn_weight;
            } else {
                // Check other identifiers
                for (const auto& id : patient.identifiers) {
                    if (id.value == *criteria.mrn) {
                        weighted_score += config_.mrn_weight * 0.9;
                        break;
                    }
                }
            }
        }

        // Identifier with system
        if (criteria.identifier_system.has_value() &&
            criteria.identifier_value.has_value()) {
            total_weight += config_.identifier_weight;
            auto found = patient.identifier_by_system(*criteria.identifier_system);
            if (found.has_value() && *found == *criteria.identifier_value) {
                weighted_score += config_.identifier_weight;
            }
        }

        // Family name
        if (criteria.family_name.has_value()) {
            total_weight += config_.family_name_weight;
            double name_score = compare_family_name(patient, *criteria.family_name);
            weighted_score += config_.family_name_weight * name_score;
        }

        // Given name
        if (criteria.given_name.has_value()) {
            total_weight += config_.given_name_weight;
            double name_score = compare_given_name(patient, *criteria.given_name);
            weighted_score += config_.given_name_weight * name_score;
        }

        // Birth date
        if (criteria.birth_date.has_value()) {
            total_weight += config_.birth_date_weight;
            if (patient.birth_date.has_value()) {
                double date_score = compare_dates_impl(
                    *patient.birth_date, *criteria.birth_date);
                weighted_score += config_.birth_date_weight * date_score;
            } else if (config_.allow_missing_birthdate) {
                // Partial credit for missing data
                weighted_score += config_.birth_date_weight * 0.5;
            }
        }

        // Sex
        if (criteria.sex.has_value()) {
            total_weight += config_.sex_weight;
            if (patient.sex.has_value() &&
                *patient.sex == *criteria.sex) {
                weighted_score += config_.sex_weight;
            }
        }

        if (total_weight == 0.0) {
            return 0.0;
        }

        return weighted_score / total_weight;
    }

    std::vector<patient_match> score_candidates_impl(
        const std::vector<patient_record>& candidates,
        const match_criteria& criteria) const {

        std::vector<patient_match> matches;
        matches.reserve(candidates.size());

        for (const auto& patient : candidates) {
            patient_match match;
            match.patient = patient;
            match.score = calculate_score(patient, criteria);
            match.match_method = "demographic";
            matches.push_back(std::move(match));
        }

        // Sort by score descending
        std::sort(matches.begin(), matches.end(),
                  [](const patient_match& a, const patient_match& b) {
                      return a.score > b.score;
                  });

        return matches;
    }

    double compare_patients(
        const patient_record& patient1,
        const patient_record& patient2) const {

        double total_weight = 0.0;
        double weighted_score = 0.0;

        // Compare MRN
        if (!patient1.mrn.empty() && !patient2.mrn.empty()) {
            total_weight += config_.mrn_weight;
            if (patient1.mrn == patient2.mrn) {
                weighted_score += config_.mrn_weight;
            }
        }

        // Compare family name
        std::string fam1 = patient1.family_name();
        std::string fam2 = patient2.family_name();
        if (!fam1.empty() && !fam2.empty()) {
            total_weight += config_.family_name_weight;
            double sim = string_similarity_impl(fam1, fam2);
            weighted_score += config_.family_name_weight * sim;
        }

        // Compare given name
        std::string given1 = patient1.given_name();
        std::string given2 = patient2.given_name();
        if (!given1.empty() && !given2.empty()) {
            total_weight += config_.given_name_weight;
            double sim = string_similarity_impl(given1, given2);
            weighted_score += config_.given_name_weight * sim;
        }

        // Compare birth date
        if (patient1.birth_date.has_value() && patient2.birth_date.has_value()) {
            total_weight += config_.birth_date_weight;
            double sim = compare_dates_impl(*patient1.birth_date,
                                            *patient2.birth_date);
            weighted_score += config_.birth_date_weight * sim;
        }

        // Compare sex
        if (patient1.sex.has_value() && patient2.sex.has_value()) {
            total_weight += config_.sex_weight;
            if (*patient1.sex == *patient2.sex) {
                weighted_score += config_.sex_weight;
            }
        }

        if (total_weight == 0.0) {
            return 0.0;
        }

        return weighted_score / total_weight;
    }

    double string_similarity_impl(std::string_view s1, std::string_view s2) const {
        if (config_.normalize_names) {
            std::string n1 = normalize_for_comparison(s1);
            std::string n2 = normalize_for_comparison(s2);
            return jaro_winkler_similarity(n1, n2);
        }
        return jaro_winkler_similarity(s1, s2);
    }

    static double compare_dates_impl(std::string_view d1, std::string_view d2) {
        if (d1 == d2) return 1.0;
        if (d1.empty() || d2.empty()) return 0.0;

        // Parse dates (YYYY-MM-DD format)
        auto parse_date = [](std::string_view date)
            -> std::tuple<int, int, int> {
            int year = 0, month = 0, day = 0;

            // Remove dashes
            std::string clean;
            for (char c : date) {
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    clean += c;
                }
            }

            if (clean.length() >= 4) {
                year = std::stoi(clean.substr(0, 4));
            }
            if (clean.length() >= 6) {
                month = std::stoi(clean.substr(4, 2));
            }
            if (clean.length() >= 8) {
                day = std::stoi(clean.substr(6, 2));
            }

            return {year, month, day};
        };

        auto [y1, m1, d1_val] = parse_date(d1);
        auto [y2, m2, d2_val] = parse_date(d2);

        // Exact match
        if (y1 == y2 && m1 == m2 && d1_val == d2_val) {
            return 1.0;
        }

        // Year and month match (partial)
        if (y1 == y2 && m1 == m2) {
            return 0.8;
        }

        // Year match only
        if (y1 == y2) {
            return 0.5;
        }

        // No match
        return 0.0;
    }

    const matcher_config& config() const noexcept {
        return config_;
    }

    void set_config(const matcher_config& config) {
        config_ = config;
    }

private:
    double compare_family_name(const patient_record& patient,
                               std::string_view name) const {
        std::string patient_family = patient.family_name();
        if (patient_family.empty()) {
            return 0.0;
        }
        return string_similarity_impl(patient_family, name);
    }

    double compare_given_name(const patient_record& patient,
                              std::string_view name) const {
        std::string patient_given = patient.given_name();
        if (patient_given.empty()) {
            return 0.0;
        }
        return string_similarity_impl(patient_given, name);
    }

    matcher_config config_;
};

// =============================================================================
// Public Interface
// =============================================================================

patient_matcher::patient_matcher()
    : impl_(std::make_unique<impl>(matcher_config{})) {}

patient_matcher::patient_matcher(const matcher_config& config)
    : impl_(std::make_unique<impl>(config)) {}

patient_matcher::~patient_matcher() = default;

patient_matcher::patient_matcher(patient_matcher&&) noexcept = default;
patient_matcher& patient_matcher::operator=(patient_matcher&&) noexcept = default;

match_result patient_matcher::find_best_match(
    const std::vector<patient_record>& candidates,
    const match_criteria& criteria) const {
    return impl_->find_best_match(candidates, criteria);
}

double patient_matcher::calculate_score(
    const patient_record& patient,
    const match_criteria& criteria) const {
    return impl_->calculate_score(patient, criteria);
}

std::vector<patient_match> patient_matcher::score_candidates(
    const std::vector<patient_record>& candidates,
    const match_criteria& criteria) const {
    return impl_->score_candidates_impl(candidates, criteria);
}

double patient_matcher::compare_patients(
    const patient_record& patient1,
    const patient_record& patient2) const {
    return impl_->compare_patients(patient1, patient2);
}

double patient_matcher::string_similarity(
    std::string_view str1,
    std::string_view str2) {
    return jaro_winkler_similarity(str1, str2);
}

size_t patient_matcher::edit_distance(
    std::string_view str1,
    std::string_view str2) {
    size_t len1 = str1.length();
    size_t len2 = str2.length();

    std::vector<std::vector<size_t>> dp(len1 + 1, std::vector<size_t>(len2 + 1));

    for (size_t i = 0; i <= len1; ++i) dp[i][0] = i;
    for (size_t j = 0; j <= len2; ++j) dp[0][j] = j;

    for (size_t i = 1; i <= len1; ++i) {
        for (size_t j = 1; j <= len2; ++j) {
            size_t cost = (str1[i - 1] == str2[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,      // deletion
                dp[i][j - 1] + 1,      // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }

    return dp[len1][len2];
}

std::string patient_matcher::normalize_name(std::string_view name) {
    return normalize_for_comparison(name);
}

double patient_matcher::compare_dates(
    std::string_view date1,
    std::string_view date2) {
    return impl::compare_dates_impl(date1, date2);
}

const matcher_config& patient_matcher::config() const noexcept {
    return impl_->config();
}

void patient_matcher::set_config(const matcher_config& config) {
    impl_->set_config(config);
}

// =============================================================================
// Disambiguation Strategy
// =============================================================================

match_result apply_disambiguation_strategy(
    const match_result& result,
    disambiguation_strategy strategy,
    double threshold) {

    match_result new_result = result;

    switch (strategy) {
        case disambiguation_strategy::highest_score:
            if (new_result.best_match_score >= threshold) {
                new_result.is_definitive = true;
                new_result.needs_disambiguation = false;
            }
            break;

        case disambiguation_strategy::exact_mrn:
            // Look for exact MRN match
            for (size_t i = 0; i < new_result.candidates.size(); ++i) {
                // This would require the criteria to be passed in
                // For now, just use the top match if score is perfect
                if (new_result.candidates[i].score >= 0.999) {
                    new_result.best_match_index = static_cast<int>(i);
                    new_result.best_match_score = new_result.candidates[i].score;
                    new_result.is_definitive = true;
                    new_result.needs_disambiguation = false;
                    break;
                }
            }
            break;

        case disambiguation_strategy::exact_identifier:
            // Similar to exact_mrn but for any identifier
            if (new_result.best_match_score >= 0.999) {
                new_result.is_definitive = true;
                new_result.needs_disambiguation = false;
            }
            break;

        case disambiguation_strategy::all_criteria:
            // Only accept perfect matches
            if (new_result.best_match_score >= 0.999) {
                new_result.is_definitive = true;
                new_result.needs_disambiguation = false;
            }
            break;

        case disambiguation_strategy::manual_only:
            // Never auto-disambiguate
            new_result.is_definitive = false;
            if (new_result.candidates.size() > 1) {
                new_result.needs_disambiguation = true;
            }
            break;
    }

    return new_result;
}

}  // namespace pacs::bridge::emr
