/**
 * @file encounter_context.cpp
 * @brief Implementation of encounter context retrieval
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/106
 */

#include "pacs/bridge/emr/encounter_context.h"

#include "pacs/bridge/emr/fhir_client.h"
#include "pacs/bridge/emr/search_params.h"

#include <algorithm>
#include <iomanip>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <variant>

// JSON parsing
#ifdef PACS_BRIDGE_HAS_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif

namespace pacs::bridge::emr {

// =============================================================================
// Status/Class Parsing
// =============================================================================

encounter_status parse_encounter_status(std::string_view status) noexcept {
    if (status == "planned") {
        return encounter_status::planned;
    }
    if (status == "arrived") {
        return encounter_status::arrived;
    }
    if (status == "triaged") {
        return encounter_status::triaged;
    }
    if (status == "in-progress") {
        return encounter_status::in_progress;
    }
    if (status == "onleave") {
        return encounter_status::on_leave;
    }
    if (status == "finished") {
        return encounter_status::finished;
    }
    if (status == "cancelled") {
        return encounter_status::cancelled;
    }
    if (status == "entered-in-error") {
        return encounter_status::entered_in_error;
    }
    return encounter_status::unknown;
}

encounter_class parse_encounter_class(std::string_view code) noexcept {
    if (code == "IMP" || code == "ACUTE" || code == "NONAC") {
        return encounter_class::inpatient;
    }
    if (code == "AMB") {
        return encounter_class::outpatient;
    }
    if (code == "EMER") {
        return encounter_class::emergency;
    }
    if (code == "HH") {
        return encounter_class::home_health;
    }
    if (code == "VR") {
        return encounter_class::virtual_visit;
    }
    if (code == "PRENC") {
        return encounter_class::preadmission;
    }
    if (code == "SS") {
        return encounter_class::short_stay;
    }
    return encounter_class::unknown;
}

// =============================================================================
// encounter_info Implementation
// =============================================================================

std::string encounter_info::to_reference() const {
    return "Encounter/" + id;
}

bool encounter_info::is_active() const noexcept {
    return pacs::bridge::emr::is_active(status);
}

std::optional<location_info> encounter_info::current_location() const {
    if (locations.empty()) {
        return std::nullopt;
    }

    // Find location without end time (current) or the most recent one
    for (auto it = locations.rbegin(); it != locations.rend(); ++it) {
        if (!it->end_time.has_value()) {
            return *it;
        }
    }

    // Return the last location if all have end times
    return locations.back();
}

std::optional<practitioner_info> encounter_info::attending_physician() const {
    for (const auto& p : participants) {
        if (p.type == "ATND" || p.type == "attending" ||
            p.type == "Attending Physician") {
            return p;
        }
    }
    return std::nullopt;
}

std::optional<practitioner_info> encounter_info::performing_physician() const {
    for (const auto& p : participants) {
        if (p.type == "PPRF" || p.type == "performing" ||
            p.type == "Primary Performer") {
            return p;
        }
    }
    return std::nullopt;
}

// =============================================================================
// Cache Entry
// =============================================================================

struct cache_entry {
    encounter_info encounter;
    std::chrono::steady_clock::time_point expiry;
};

// =============================================================================
// encounter_context_provider Implementation
// =============================================================================

class encounter_context_provider::impl {
public:
    using result_info = std::variant<encounter_info, encounter_error>;
    using result_opt_info = std::variant<std::optional<encounter_info>, encounter_error>;
    using result_vec_info = std::variant<std::vector<encounter_info>, encounter_error>;

    explicit impl(encounter_context_config config)
        : config_(std::move(config)) {}

    result_info get_encounter(std::string_view encounter_id) {
        // Check cache first
        {
            std::shared_lock lock(cache_mutex_);
            auto it = cache_by_id_.find(std::string(encounter_id));
            if (it != cache_by_id_.end()) {
                if (std::chrono::steady_clock::now() < it->second.expiry) {
                    ++cache_hits_;
                    return it->second.encounter;
                }
            }
        }
        ++cache_misses_;

        if (!config_.client) {
            return encounter_error::query_failed;
        }

        // Query FHIR server
        auto response = config_.client->read("Encounter", encounter_id);
        if (!response.has_value()) {
            return encounter_error::not_found;
        }

        // Parse the resource JSON from the result
        auto parse_result = parse_encounter_json(response.value().value.json);
        if (std::holds_alternative<encounter_error>(parse_result)) {
            return std::get<encounter_error>(parse_result);
        }

        auto encounter = std::get<encounter_info>(parse_result);

        // Cache the result
        cache_encounter(encounter);

        return encounter;
    }

    result_info find_by_visit_number(
        std::string_view visit_number,
        std::optional<std::string_view> system) {
        // Check cache
        {
            std::shared_lock lock(cache_mutex_);
            auto it = cache_by_visit_.find(std::string(visit_number));
            if (it != cache_by_visit_.end()) {
                auto id_it = cache_by_id_.find(it->second);
                if (id_it != cache_by_id_.end() &&
                    std::chrono::steady_clock::now() < id_it->second.expiry) {
                    ++cache_hits_;
                    return id_it->second.encounter;
                }
            }
        }
        ++cache_misses_;

        if (!config_.client) {
            return encounter_error::query_failed;
        }

        // Build search params
        search_params params;
        std::string identifier_value;
        if (system.has_value()) {
            identifier_value = std::string(system.value()) + "|" + std::string(visit_number);
        } else {
            identifier_value = std::string(visit_number);
        }
        params.add("identifier", identifier_value);

        auto response = config_.client->search("Encounter", params);
        if (!response.has_value()) {
            return encounter_error::query_failed;
        }

        // Parse bundle response
        auto encounters = parse_bundle_entries(response.value().value);
        if (encounters.empty()) {
            return encounter_error::visit_not_found;
        }

        if (encounters.size() > 1) {
            // Return the most recent active one
            for (auto& enc : encounters) {
                if (enc.is_active()) {
                    cache_encounter(enc);
                    return enc;
                }
            }
        }

        cache_encounter(encounters.front());
        return encounters.front();
    }

    result_opt_info find_active_encounter(
        std::string_view patient_id) {
        if (!config_.client) {
            return encounter_error::query_failed;
        }

        std::string patient_ref = std::string(patient_id);
        if (patient_ref.find("Patient/") == std::string::npos) {
            patient_ref = "Patient/" + patient_ref;
        }

        search_params params;
        params.add("patient", patient_ref)
              .add("status", "in-progress,arrived,triaged");

        auto response = config_.client->search("Encounter", params);
        if (!response.has_value()) {
            return encounter_error::query_failed;
        }

        auto encounters = parse_bundle_entries(response.value().value);
        if (encounters.empty()) {
            return std::optional<encounter_info>{std::nullopt};
        }

        if (encounters.size() > 1) {
            // Log warning about multiple active encounters
            // Return the most recent one (first in list, assuming sorted by date)
        }

        cache_encounter(encounters.front());
        return std::optional<encounter_info>{encounters.front()};
    }

    result_vec_info find_encounters(
        std::string_view patient_id,
        std::optional<encounter_status> status_filter,
        size_t max_results) {
        if (!config_.client) {
            return encounter_error::query_failed;
        }

        std::string patient_ref = std::string(patient_id);
        if (patient_ref.find("Patient/") == std::string::npos) {
            patient_ref = "Patient/" + patient_ref;
        }

        search_params params;
        params.add("patient", patient_ref)
              .count(static_cast<int>(max_results));

        if (status_filter.has_value()) {
            params.add("status", std::string(to_string(status_filter.value())));
        }

        auto response = config_.client->search("Encounter", params);
        if (!response.has_value()) {
            return encounter_error::query_failed;
        }

        return parse_bundle_entries(response.value().value);
    }

    void clear_cache() {
        std::unique_lock lock(cache_mutex_);
        cache_by_id_.clear();
        cache_by_visit_.clear();
    }

    encounter_context_provider::cache_stats get_cache_stats() const {
        std::shared_lock lock(cache_mutex_);
        return {cache_by_id_.size(), cache_hits_, cache_misses_};
    }

private:
    void cache_encounter(const encounter_info& encounter) {
        std::unique_lock lock(cache_mutex_);

        // Evict old entries if cache is full
        if (cache_by_id_.size() >= config_.max_cache_size) {
            evict_oldest_entry();
        }

        auto expiry =
            std::chrono::steady_clock::now() + config_.cache_ttl;

        cache_by_id_[encounter.id] = {encounter, expiry};
        if (!encounter.visit_number.empty()) {
            cache_by_visit_[encounter.visit_number] = encounter.id;
        }
    }

    void evict_oldest_entry() {
        // Simple eviction: remove first entry
        if (!cache_by_id_.empty()) {
            auto it = cache_by_id_.begin();
            // Remove from visit cache if present
            for (auto vit = cache_by_visit_.begin();
                 vit != cache_by_visit_.end();) {
                if (vit->second == it->first) {
                    vit = cache_by_visit_.erase(vit);
                } else {
                    ++vit;
                }
            }
            cache_by_id_.erase(it);
        }
    }

    std::vector<encounter_info> parse_bundle_entries(
        const fhir_bundle& bundle) {
        std::vector<encounter_info> encounters;

        for (const auto& entry : bundle.entries) {
            if (!entry.resource.empty()) {
                auto result = parse_encounter_json(entry.resource);
                if (std::holds_alternative<encounter_info>(result)) {
                    encounters.push_back(std::get<encounter_info>(result));
                }
            }
        }

        return encounters;
    }

    encounter_context_config config_;
    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, cache_entry> cache_by_id_;
    std::unordered_map<std::string, std::string> cache_by_visit_;
    mutable size_t cache_hits_ = 0;
    mutable size_t cache_misses_ = 0;
};

// =============================================================================
// encounter_context_provider Public Methods
// =============================================================================

encounter_context_provider::encounter_context_provider(
    encounter_context_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {}

encounter_context_provider::~encounter_context_provider() = default;

encounter_context_provider::encounter_context_provider(
    encounter_context_provider&&) noexcept = default;

encounter_context_provider& encounter_context_provider::operator=(
    encounter_context_provider&&) noexcept = default;

encounter_context_provider::result<encounter_info>
encounter_context_provider::get_encounter(std::string_view encounter_id) {
    return impl_->get_encounter(encounter_id);
}

encounter_context_provider::result<encounter_info>
encounter_context_provider::find_by_visit_number(
    std::string_view visit_number,
    std::optional<std::string_view> system) {
    return impl_->find_by_visit_number(visit_number, system);
}

encounter_context_provider::result<std::optional<encounter_info>>
encounter_context_provider::find_active_encounter(std::string_view patient_id) {
    return impl_->find_active_encounter(patient_id);
}

encounter_context_provider::result<std::vector<encounter_info>>
encounter_context_provider::find_encounters(
    std::string_view patient_id,
    std::optional<encounter_status> status_filter,
    size_t max_results) {
    return impl_->find_encounters(patient_id, status_filter, max_results);
}

void encounter_context_provider::clear_cache() {
    impl_->clear_cache();
}

encounter_context_provider::cache_stats
encounter_context_provider::get_cache_stats() const {
    return impl_->get_cache_stats();
}

// =============================================================================
// JSON Parsing Helpers
// =============================================================================

namespace {

#ifdef PACS_BRIDGE_HAS_NLOHMANN_JSON
std::chrono::system_clock::time_point parse_datetime(const std::string& dt) {
    // Simple ISO8601 parsing - production should use proper date library
    std::tm tm = {};
    std::istringstream ss(dt);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}
#endif

}  // namespace

// =============================================================================
// JSON Parsing
// =============================================================================

std::variant<encounter_info, encounter_error> parse_encounter_json(
    std::string_view json) {
#ifdef PACS_BRIDGE_HAS_NLOHMANN_JSON
    try {
        auto json_obj = nlohmann::json::parse(json);
        return parse_encounter_from_json(json_obj);
    } catch (...) {
        return encounter_error::parse_failed;
    }
#else
    (void)json;
    return encounter_error::parse_failed;
#endif
}

#ifdef PACS_BRIDGE_HAS_NLOHMANN_JSON

template <>
std::variant<encounter_info, encounter_error> parse_encounter_from_json(
    const nlohmann::json& json) {
    try {
        encounter_info info;

        // Resource type validation
        if (json.value("resourceType", "") != "Encounter") {
            return encounter_error::invalid_data;
        }

        // ID
        info.id = json.value("id", "");
        if (info.id.empty()) {
            return encounter_error::invalid_data;
        }

        // Status
        info.status = parse_encounter_status(json.value("status", "unknown"));

        // Class
        if (json.contains("class") && json["class"].is_object()) {
            const auto& cls = json["class"];
            std::string code = cls.value("code", "");
            info.enc_class = parse_encounter_class(code);
            info.class_display = cls.value("display", "");
        }

        // Identifier (visit number)
        if (json.contains("identifier") && json["identifier"].is_array()) {
            for (const auto& id : json["identifier"]) {
                std::string type_code;
                if (id.contains("type") && id["type"].contains("coding")) {
                    for (const auto& coding : id["type"]["coding"]) {
                        type_code = coding.value("code", "");
                        if (type_code == "VN" || type_code == "VISIT") {
                            info.visit_number = id.value("value", "");
                            break;
                        }
                    }
                }
                // Fallback: use first identifier with value
                if (info.visit_number.empty() && id.contains("value")) {
                    info.visit_number = id.value("value", "");
                }
            }
        }

        // Type
        if (json.contains("type") && json["type"].is_array() &&
            !json["type"].empty()) {
            const auto& type = json["type"][0];
            if (type.contains("coding") && type["coding"].is_array()) {
                for (const auto& coding : type["coding"]) {
                    info.type_codes.push_back(coding.value("code", ""));
                }
            }
            info.type_display = type.value("text", "");
            if (info.type_display.empty() && type.contains("coding") &&
                !type["coding"].empty()) {
                info.type_display = type["coding"][0].value("display", "");
            }
        }

        // Subject (patient)
        if (json.contains("subject") && json["subject"].is_object()) {
            info.patient_reference = json["subject"].value("reference", "");
        }

        // Period
        if (json.contains("period") && json["period"].is_object()) {
            const auto& period = json["period"];
            if (period.contains("start")) {
                info.start_time = parse_datetime(period.value("start", ""));
            }
            if (period.contains("end")) {
                info.end_time = parse_datetime(period.value("end", ""));
            }
        }

        // Location
        if (json.contains("location") && json["location"].is_array()) {
            for (const auto& loc : json["location"]) {
                location_info location;
                if (loc.contains("location")) {
                    location.id = loc["location"].value("reference", "");
                    location.display = loc["location"].value("display", "");
                }
                location.status = loc.value("status", "");
                if (loc.contains("physicalType") &&
                    loc["physicalType"].contains("coding") &&
                    !loc["physicalType"]["coding"].empty()) {
                    location.physical_type =
                        loc["physicalType"]["coding"][0].value("code", "");
                }
                if (loc.contains("period")) {
                    if (loc["period"].contains("start")) {
                        location.start_time =
                            parse_datetime(loc["period"].value("start", ""));
                    }
                    if (loc["period"].contains("end")) {
                        location.end_time =
                            parse_datetime(loc["period"].value("end", ""));
                    }
                }
                info.locations.push_back(std::move(location));
            }
        }

        // Participant (practitioners)
        if (json.contains("participant") && json["participant"].is_array()) {
            for (const auto& part : json["participant"]) {
                practitioner_info practitioner;
                if (part.contains("individual")) {
                    practitioner.id =
                        part["individual"].value("reference", "");
                    practitioner.display =
                        part["individual"].value("display", "");
                }
                if (part.contains("type") && part["type"].is_array() &&
                    !part["type"].empty()) {
                    const auto& type = part["type"][0];
                    if (type.contains("coding") && !type["coding"].empty()) {
                        practitioner.type =
                            type["coding"][0].value("code", "");
                    }
                }
                if (part.contains("period")) {
                    if (part["period"].contains("start")) {
                        practitioner.start_time =
                            parse_datetime(part["period"].value("start", ""));
                    }
                    if (part["period"].contains("end")) {
                        practitioner.end_time =
                            parse_datetime(part["period"].value("end", ""));
                    }
                }
                info.participants.push_back(std::move(practitioner));
            }
        }

        // Service provider
        if (json.contains("serviceProvider") &&
            json["serviceProvider"].is_object()) {
            info.service_provider =
                json["serviceProvider"].value("reference", "");
            info.service_provider_display =
                json["serviceProvider"].value("display", "");
        }

        // Reason (text)
        if (json.contains("reasonCode") && json["reasonCode"].is_array() &&
            !json["reasonCode"].empty()) {
            info.reason_text = json["reasonCode"][0].value("text", "");
        }

        // Diagnosis references
        if (json.contains("diagnosis") && json["diagnosis"].is_array()) {
            for (const auto& diag : json["diagnosis"]) {
                if (diag.contains("condition") &&
                    diag["condition"].contains("reference")) {
                    info.diagnosis_references.push_back(
                        diag["condition"].value("reference", ""));
                }
            }
        }

        return info;

    } catch (const std::exception&) {
        return encounter_error::parse_failed;
    }
}

#endif  // PACS_BRIDGE_HAS_NLOHMANN_JSON

}  // namespace pacs::bridge::emr
