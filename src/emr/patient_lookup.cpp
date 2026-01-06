/**
 * @file patient_lookup.cpp
 * @brief Patient demographics lookup implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/104
 */

#include "pacs/bridge/emr/patient_lookup.h"
#include "pacs/bridge/emr/patient_matcher.h"
#include "pacs/bridge/emr/search_params.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace pacs::bridge::emr {

// =============================================================================
// Cache Entry
// =============================================================================

namespace {

struct cache_entry {
    patient_record patient;
    std::chrono::system_clock::time_point cached_at;
    std::chrono::seconds ttl;

    [[nodiscard]] bool is_expired() const noexcept {
        auto now = std::chrono::system_clock::now();
        return (now - cached_at) > ttl;
    }
};

struct negative_cache_entry {
    std::chrono::system_clock::time_point cached_at;
    std::chrono::seconds ttl;

    [[nodiscard]] bool is_expired() const noexcept {
        auto now = std::chrono::system_clock::now();
        return (now - cached_at) > ttl;
    }
};

}  // namespace

// =============================================================================
// Implementation
// =============================================================================

class emr_patient_lookup::impl {
public:
    explicit impl(std::shared_ptr<fhir_client> client,
                  const patient_lookup_config& config)
        : client_(std::move(client))
        , config_(config)
        , matcher_(std::make_shared<patient_matcher>()) {}

    auto get_by_mrn(std::string_view mrn)
        -> Result<patient_record> {
        std::string mrn_str(mrn);

        // Check cache first
        if (config_.enable_cache) {
            if (auto cached = get_from_cache(mrn_str)) {
                ++stats_.cache_hits;
                return *cached;
            }
            if (is_negative_cached(mrn_str)) {
                ++stats_.cache_hits;
                return to_error_info(patient_error::not_found);
            }
            ++stats_.cache_misses;
        }

        ++stats_.total_queries;
        auto start = std::chrono::steady_clock::now();

        // Build search parameters
        search_params params;
        if (config_.default_identifier_system.empty()) {
            params.identifier(mrn);
        } else {
            params.identifier(config_.default_identifier_system, mrn);
        }
        params.count(1);

        // Execute search
        auto result = client_->search("Patient", params);
        if (result.is_err()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::query_failed);
        }

        auto& bundle = result.value().value;

        if (bundle.empty()) {
            // Cache negative result
            if (config_.enable_cache) {
                add_negative_cache(mrn_str);
            }
            ++stats_.failed_queries;
            return to_error_info(patient_error::not_found);
        }

        // Parse the patient
        auto patients = parse_patient_bundle(bundle);
        if (patients.empty()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::parse_failed);
        }

        auto& patient = patients.front();
        patient.cached_at = std::chrono::system_clock::now();

        // Cache the result
        if (config_.enable_cache) {
            add_to_cache(mrn_str, patient);
        }

        auto end = std::chrono::steady_clock::now();
        stats_.total_query_time +=
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        ++stats_.successful_queries;

        return patient;
    }

    auto get_by_identifier(std::string_view system, std::string_view value)
        -> Result<patient_record> {
        std::string cache_key = std::string(system) + "|" + std::string(value);

        // Check cache
        if (config_.enable_cache) {
            if (auto cached = get_from_cache(cache_key)) {
                ++stats_.cache_hits;
                return *cached;
            }
            ++stats_.cache_misses;
        }

        ++stats_.total_queries;
        auto start = std::chrono::steady_clock::now();

        // Build search
        search_params params;
        params.identifier(system, value);
        params.count(1);

        auto result = client_->search("Patient", params);
        if (result.is_err()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::query_failed);
        }

        auto& bundle = result.value().value;
        if (bundle.empty()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::not_found);
        }

        auto patients = parse_patient_bundle(bundle);
        if (patients.empty()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::parse_failed);
        }

        auto& patient = patients.front();
        patient.cached_at = std::chrono::system_clock::now();

        if (config_.enable_cache) {
            add_to_cache(cache_key, patient);
            // Also cache by MRN if available
            if (!patient.mrn.empty()) {
                add_to_cache(patient.mrn, patient);
            }
        }

        auto end = std::chrono::steady_clock::now();
        stats_.total_query_time +=
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        ++stats_.successful_queries;

        return patient;
    }

    auto get_by_id(std::string_view id)
        -> Result<patient_record> {
        std::string id_str(id);

        ++stats_.total_queries;
        auto start = std::chrono::steady_clock::now();

        // Direct read by ID
        auto result = client_->read("Patient", id);
        if (result.is_err()) {
            ++stats_.failed_queries;
            if (result.error().code == static_cast<int>(emr_error::resource_not_found)) {
                return to_error_info(patient_error::not_found);
            }
            return to_error_info(patient_error::query_failed);
        }

        auto& resource = result.value().value;
        if (!resource.is_valid()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::invalid_data);
        }

        auto patient_result = parse_fhir_patient(resource.json);
        if (patient_result.is_err()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::parse_failed);
        }

        auto& patient = patient_result.value();
        patient.cached_at = std::chrono::system_clock::now();

        if (config_.enable_cache && !patient.mrn.empty()) {
            add_to_cache(patient.mrn, patient);
        }

        auto end = std::chrono::steady_clock::now();
        stats_.total_query_time +=
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        ++stats_.successful_queries;

        return patient;
    }

    auto find_patient(const patient_query& query)
        -> Result<patient_record> {
        if (query.is_empty()) {
            return to_error_info(patient_error::invalid_query);
        }

        // If it's a simple MRN lookup, use the direct method
        if (query.is_mrn_lookup()) {
            if (query.identifier_system.has_value()) {
                return get_by_identifier(*query.identifier_system,
                                         *query.patient_id);
            }
            return get_by_mrn(*query.patient_id);
        }

        // Otherwise, do a search
        auto search_result = search_patients(query);
        if (search_result.is_err()) {
            return search_result.error();
        }

        auto& matches = search_result.value();

        if (matches.empty()) {
            return to_error_info(patient_error::not_found);
        }

        if (matches.size() == 1) {
            return matches.front().patient;
        }

        // Multiple matches - try disambiguation
        ++stats_.multiple_matches;

        if (config_.auto_disambiguate) {
            match_criteria criteria;
            criteria.mrn = query.patient_id;
            criteria.family_name = query.family_name;
            criteria.given_name = query.given_name;
            criteria.birth_date = query.birth_date;
            criteria.identifier_system = query.identifier_system;
            criteria.identifier_value = query.patient_id;

            std::vector<patient_record> patients;
            patients.reserve(matches.size());
            for (auto& m : matches) {
                patients.push_back(std::move(m.patient));
            }

            auto match_result = matcher_->find_best_match(patients, criteria);
            if (match_result.is_definitive &&
                match_result.best_match_score >= config_.min_match_score) {
                return *match_result.best_patient();
            }
        }

        return to_error_info(patient_error::multiple_found);
    }

    auto search_patients(const patient_query& query)
        -> Result<std::vector<patient_match>> {
        if (query.is_empty()) {
            return to_error_info(patient_error::invalid_query);
        }

        ++stats_.total_queries;
        auto start = std::chrono::steady_clock::now();

        // Build search parameters
        search_params params;

        if (query.patient_id.has_value()) {
            if (query.identifier_system.has_value()) {
                params.identifier(*query.identifier_system, *query.patient_id);
            } else if (!config_.default_identifier_system.empty()) {
                params.identifier(config_.default_identifier_system,
                                  *query.patient_id);
            } else {
                params.identifier(*query.patient_id);
            }
        }

        if (query.family_name.has_value()) {
            params.family(*query.family_name);
        }

        if (query.given_name.has_value()) {
            params.given(*query.given_name);
        }

        if (query.birth_date.has_value()) {
            params.birthdate(*query.birth_date);
        }

        if (query.gender.has_value()) {
            params.gender(*query.gender);
        }

        if (!query.include_inactive) {
            params.active(true);
        }

        params.count(query.max_results);

        // Execute search
        auto result = client_->search("Patient", params);
        if (result.is_err()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::query_failed);
        }

        auto& bundle = result.value().value;
        auto patients = parse_patient_bundle(bundle);

        // Score the results
        std::vector<patient_match> matches;
        matches.reserve(patients.size());

        match_criteria criteria;
        criteria.mrn = query.patient_id;
        criteria.family_name = query.family_name;
        criteria.given_name = query.given_name;
        criteria.birth_date = query.birth_date;
        if (query.gender.has_value()) {
            criteria.sex = query.gender;
        }

        for (auto& patient : patients) {
            patient_match match;
            match.patient = std::move(patient);
            match.score = matcher_->calculate_score(match.patient, criteria);
            match.match_method = "demographic";
            matches.push_back(std::move(match));
        }

        // Sort by score descending
        std::sort(matches.begin(), matches.end(),
                  [](const patient_match& a, const patient_match& b) {
                      return a.score > b.score;
                  });

        auto end = std::chrono::steady_clock::now();
        stats_.total_query_time +=
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        ++stats_.successful_queries;

        return matches;
    }

    auto search_with_params(const search_params& params)
        -> Result<std::vector<patient_record>> {
        ++stats_.total_queries;

        auto result = client_->search("Patient", params);
        if (result.is_err()) {
            ++stats_.failed_queries;
            return to_error_info(patient_error::query_failed);
        }

        auto patients = parse_patient_bundle(result.value().value);
        ++stats_.successful_queries;

        return patients;
    }

    void clear_cache() {
        std::unique_lock lock(cache_mutex_);
        cache_.clear();
        negative_cache_.clear();
    }

    void invalidate_cache(std::string_view mrn) {
        std::unique_lock lock(cache_mutex_);
        cache_.erase(std::string(mrn));
        negative_cache_.erase(std::string(mrn));
    }

    size_t prefetch(const std::vector<std::string>& mrns) {
        size_t count = 0;
        for (const auto& mrn : mrns) {
            auto result = get_by_mrn(mrn);
            if (result.is_ok()) {
                ++count;
            }
        }
        return count;
    }

    cache_stats get_cache_stats() const {
        std::shared_lock lock(cache_mutex_);
        cache_stats stats;
        stats.hits = stats_.cache_hits;
        stats.misses = stats_.cache_misses;
        stats.entries = cache_.size();
        auto total = stats.hits + stats.misses;
        stats.hit_rate = total > 0
            ? static_cast<double>(stats.hits) / static_cast<double>(total)
            : 0.0;
        return stats;
    }

    const patient_lookup_config& config() const noexcept {
        return config_;
    }

    void set_config(const patient_lookup_config& config) {
        config_ = config;
    }

    void set_matcher(std::shared_ptr<patient_matcher> matcher) {
        matcher_ = std::move(matcher);
    }

    statistics get_statistics() const noexcept {
        return stats_;
    }

    void reset_statistics() noexcept {
        stats_ = statistics{};
    }

private:
    std::optional<patient_record> get_from_cache(const std::string& key) const {
        std::shared_lock lock(cache_mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end() && !it->second.is_expired()) {
            return it->second.patient;
        }
        return std::nullopt;
    }

    bool is_negative_cached(const std::string& key) const {
        std::shared_lock lock(cache_mutex_);
        auto it = negative_cache_.find(key);
        return it != negative_cache_.end() && !it->second.is_expired();
    }

    void add_to_cache(const std::string& key, const patient_record& patient) {
        std::unique_lock lock(cache_mutex_);

        // Enforce max entries
        if (cache_.size() >= config_.max_cache_entries) {
            evict_oldest();
        }

        cache_entry entry;
        entry.patient = patient;
        entry.cached_at = std::chrono::system_clock::now();
        entry.ttl = config_.cache_ttl;

        cache_[key] = std::move(entry);
    }

    void add_negative_cache(const std::string& key) {
        std::unique_lock lock(cache_mutex_);

        negative_cache_entry entry;
        entry.cached_at = std::chrono::system_clock::now();
        entry.ttl = config_.negative_cache_ttl;

        negative_cache_[key] = entry;
    }

    void evict_oldest() {
        // Simple eviction: remove first expired or oldest
        auto oldest_it = cache_.end();
        auto oldest_time = std::chrono::system_clock::time_point::max();

        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.is_expired()) {
                cache_.erase(it);
                return;
            }
            if (it->second.cached_at < oldest_time) {
                oldest_time = it->second.cached_at;
                oldest_it = it;
            }
        }

        if (oldest_it != cache_.end()) {
            cache_.erase(oldest_it);
        }
    }

    std::shared_ptr<fhir_client> client_;
    patient_lookup_config config_;
    std::shared_ptr<patient_matcher> matcher_;

    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, cache_entry> cache_;
    std::unordered_map<std::string, negative_cache_entry> negative_cache_;

    mutable statistics stats_;
};

// =============================================================================
// Public Interface
// =============================================================================

emr_patient_lookup::emr_patient_lookup(
    std::shared_ptr<fhir_client> client,
    const patient_lookup_config& config)
    : impl_(std::make_unique<impl>(std::move(client), config)) {}

emr_patient_lookup::~emr_patient_lookup() = default;

emr_patient_lookup::emr_patient_lookup(emr_patient_lookup&&) noexcept = default;
emr_patient_lookup& emr_patient_lookup::operator=(emr_patient_lookup&&) noexcept = default;

auto emr_patient_lookup::get_by_mrn(std::string_view mrn)
    -> Result<patient_record> {
    return impl_->get_by_mrn(mrn);
}

auto emr_patient_lookup::get_by_identifier(std::string_view system,
                                            std::string_view value)
    -> Result<patient_record> {
    return impl_->get_by_identifier(system, value);
}

auto emr_patient_lookup::get_by_id(std::string_view id)
    -> Result<patient_record> {
    return impl_->get_by_id(id);
}

auto emr_patient_lookup::find_patient(const patient_query& query)
    -> Result<patient_record> {
    return impl_->find_patient(query);
}

auto emr_patient_lookup::search_patients(const patient_query& query)
    -> Result<std::vector<patient_match>> {
    return impl_->search_patients(query);
}

auto emr_patient_lookup::search_with_params(const search_params& params)
    -> Result<std::vector<patient_record>> {
    return impl_->search_with_params(params);
}

void emr_patient_lookup::clear_cache() {
    impl_->clear_cache();
}

void emr_patient_lookup::invalidate_cache(std::string_view mrn) {
    impl_->invalidate_cache(mrn);
}

size_t emr_patient_lookup::prefetch(const std::vector<std::string>& mrns) {
    return impl_->prefetch(mrns);
}

emr_patient_lookup::cache_stats emr_patient_lookup::get_cache_stats() const {
    return impl_->get_cache_stats();
}

const patient_lookup_config& emr_patient_lookup::config() const noexcept {
    return impl_->config();
}

void emr_patient_lookup::set_config(const patient_lookup_config& config) {
    impl_->set_config(config);
}

void emr_patient_lookup::set_matcher(std::shared_ptr<patient_matcher> matcher) {
    impl_->set_matcher(std::move(matcher));
}

emr_patient_lookup::statistics emr_patient_lookup::get_statistics() const noexcept {
    return impl_->get_statistics();
}

void emr_patient_lookup::reset_statistics() noexcept {
    impl_->reset_statistics();
}

}  // namespace pacs::bridge::emr
