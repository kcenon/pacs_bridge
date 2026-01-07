/**
 * @file imaging_study_resource.cpp
 * @brief FHIR ImagingStudy resource implementation
 *
 * Implements the ImagingStudy resource and handler for FHIR R4.
 *
 * @see include/pacs/bridge/fhir/imaging_study_resource.h
 * @see https://github.com/kcenon/pacs_bridge/issues/34
 */

#include "pacs/bridge/fhir/imaging_study_resource.h"

#include "pacs/bridge/mapping/fhir_dicom_mapper.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>

namespace pacs::bridge::fhir {

// =============================================================================
// JSON Utilities
// =============================================================================

namespace {

/**
 * @brief Escape a string for JSON
 */
std::string json_escape(std::string_view input) {
    std::string result;
    result.reserve(input.size() + 10);

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

/**
 * @brief Convert string to lowercase for case-insensitive comparison
 */
std::string to_lower(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

/**
 * @brief Check if a string contains another string (case-insensitive)
 */
bool contains_ignore_case(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    std::string lower_haystack = to_lower(haystack);
    std::string lower_needle = to_lower(needle);
    return lower_haystack.find(lower_needle) != std::string::npos;
}

/**
 * @brief Simple JSON string extractor
 */
std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        return "";
    }

    pos += search.size();

    // Skip whitespace and colon
    while (pos < json.size() &&
           (std::isspace(static_cast<unsigned char>(json[pos])) ||
            json[pos] == ':')) {
        ++pos;
    }

    if (pos >= json.size() || json[pos] != '"') {
        return "";
    }
    ++pos;

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                default:
                    result += json[pos];
                    break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }

    return result;
}

}  // namespace

// =============================================================================
// ImagingStudy Status
// =============================================================================

std::optional<imaging_study_status> parse_imaging_study_status(
    std::string_view status_str) noexcept {
    std::string lower = to_lower(status_str);

    if (lower == "registered") {
        return imaging_study_status::registered;
    }
    if (lower == "available") {
        return imaging_study_status::available;
    }
    if (lower == "cancelled") {
        return imaging_study_status::cancelled;
    }
    if (lower == "entered-in-error") {
        return imaging_study_status::entered_in_error;
    }
    if (lower == "unknown") {
        return imaging_study_status::unknown;
    }

    return std::nullopt;
}

// =============================================================================
// ImagingStudy Resource Implementation
// =============================================================================

class imaging_study_resource::impl {
public:
    std::vector<imaging_study_identifier> identifiers;
    imaging_study_status status = imaging_study_status::available;
    std::optional<imaging_study_reference> subject;
    std::optional<std::string> started;
    std::optional<imaging_study_reference> based_on;
    std::optional<imaging_study_reference> referrer;
    std::optional<uint32_t> number_of_series;
    std::optional<uint32_t> number_of_instances;
    std::optional<std::string> description;
    std::vector<imaging_study_series> series;
};

imaging_study_resource::imaging_study_resource()
    : pimpl_(std::make_unique<impl>()) {}

imaging_study_resource::~imaging_study_resource() = default;

imaging_study_resource::imaging_study_resource(imaging_study_resource&&) noexcept =
    default;

imaging_study_resource& imaging_study_resource::operator=(
    imaging_study_resource&&) noexcept = default;

resource_type imaging_study_resource::type() const noexcept {
    return resource_type::imaging_study;
}

std::string imaging_study_resource::type_name() const {
    return "ImagingStudy";
}

std::string imaging_study_resource::to_json() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"resourceType\": \"ImagingStudy\"";

    // ID
    if (!id().empty()) {
        json << ",\n  \"id\": \"" << json_escape(id()) << "\"";
    }

    // Identifiers
    if (!pimpl_->identifiers.empty()) {
        json << ",\n  \"identifier\": [\n";
        for (size_t i = 0; i < pimpl_->identifiers.size(); ++i) {
            const auto& ident = pimpl_->identifiers[i];
            json << "    {\n";

            bool first_field = true;

            if (ident.use.has_value()) {
                json << "      \"use\": \"" << json_escape(*ident.use) << "\"";
                first_field = false;
            }

            if (ident.system.has_value()) {
                if (!first_field) {
                    json << ",\n";
                }
                json << "      \"system\": \"" << json_escape(*ident.system)
                     << "\"";
                first_field = false;
            }

            if (!first_field) {
                json << ",\n";
            }
            json << "      \"value\": \"" << json_escape(ident.value) << "\"";

            json << "\n    }";
            if (i < pimpl_->identifiers.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
    }

    // Status
    json << ",\n  \"status\": \"" << to_string(pimpl_->status) << "\"";

    // Subject
    if (pimpl_->subject.has_value()) {
        json << ",\n  \"subject\": {\n";
        bool first = true;
        if (pimpl_->subject->reference.has_value()) {
            json << "    \"reference\": \""
                 << json_escape(*pimpl_->subject->reference) << "\"";
            first = false;
        }
        if (pimpl_->subject->type.has_value()) {
            if (!first) {
                json << ",\n";
            }
            json << "    \"type\": \""
                 << json_escape(*pimpl_->subject->type) << "\"";
            first = false;
        }
        if (pimpl_->subject->display.has_value()) {
            if (!first) {
                json << ",\n";
            }
            json << "    \"display\": \""
                 << json_escape(*pimpl_->subject->display) << "\"";
        }
        json << "\n  }";
    }

    // Started
    if (pimpl_->started.has_value()) {
        json << ",\n  \"started\": \"" << json_escape(*pimpl_->started) << "\"";
    }

    // BasedOn
    if (pimpl_->based_on.has_value() && pimpl_->based_on->reference.has_value()) {
        json << ",\n  \"basedOn\": [{\n";
        json << "    \"reference\": \""
             << json_escape(*pimpl_->based_on->reference) << "\"";
        if (pimpl_->based_on->display.has_value()) {
            json << ",\n    \"display\": \""
                 << json_escape(*pimpl_->based_on->display) << "\"";
        }
        json << "\n  }]";
    }

    // Referrer
    if (pimpl_->referrer.has_value()) {
        json << ",\n  \"referrer\": {\n";
        bool first = true;
        if (pimpl_->referrer->reference.has_value()) {
            json << "    \"reference\": \""
                 << json_escape(*pimpl_->referrer->reference) << "\"";
            first = false;
        }
        if (pimpl_->referrer->display.has_value()) {
            if (!first) {
                json << ",\n";
            }
            json << "    \"display\": \""
                 << json_escape(*pimpl_->referrer->display) << "\"";
        }
        json << "\n  }";
    }

    // Number of series/instances
    if (pimpl_->number_of_series.has_value()) {
        json << ",\n  \"numberOfSeries\": " << *pimpl_->number_of_series;
    }
    if (pimpl_->number_of_instances.has_value()) {
        json << ",\n  \"numberOfInstances\": " << *pimpl_->number_of_instances;
    }

    // Description
    if (pimpl_->description.has_value()) {
        json << ",\n  \"description\": \""
             << json_escape(*pimpl_->description) << "\"";
    }

    // Series
    if (!pimpl_->series.empty()) {
        json << ",\n  \"series\": [\n";
        for (size_t i = 0; i < pimpl_->series.size(); ++i) {
            const auto& series = pimpl_->series[i];
            json << "    {\n";
            json << "      \"uid\": \"" << json_escape(series.uid) << "\"";

            if (series.number.has_value()) {
                json << ",\n      \"number\": " << *series.number;
            }

            // Modality
            json << ",\n      \"modality\": {\n";
            json << "        \"system\": \"" << json_escape(series.modality.system)
                 << "\",\n";
            json << "        \"code\": \"" << json_escape(series.modality.code)
                 << "\"";
            if (!series.modality.display.empty()) {
                json << ",\n        \"display\": \""
                     << json_escape(series.modality.display) << "\"";
            }
            json << "\n      }";

            if (series.description.has_value()) {
                json << ",\n      \"description\": \""
                     << json_escape(*series.description) << "\"";
            }

            if (series.number_of_instances.has_value()) {
                json << ",\n      \"numberOfInstances\": "
                     << *series.number_of_instances;
            }

            if (series.body_site.has_value()) {
                json << ",\n      \"bodySite\": {\n";
                json << "        \"system\": \""
                     << json_escape(series.body_site->system) << "\",\n";
                json << "        \"code\": \""
                     << json_escape(series.body_site->code) << "\"";
                if (!series.body_site->display.empty()) {
                    json << ",\n        \"display\": \""
                         << json_escape(series.body_site->display) << "\"";
                }
                json << "\n      }";
            }

            if (series.started.has_value()) {
                json << ",\n      \"started\": \""
                     << json_escape(*series.started) << "\"";
            }

            json << "\n    }";
            if (i < pimpl_->series.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
    }

    json << "\n}";
    return json.str();
}

bool imaging_study_resource::validate() const {
    // Status is required
    // Subject is required for actual use cases, but validation passes for
    // construction purposes
    return true;
}

void imaging_study_resource::add_identifier(
    const imaging_study_identifier& identifier) {
    pimpl_->identifiers.push_back(identifier);
}

const std::vector<imaging_study_identifier>& imaging_study_resource::identifiers()
    const noexcept {
    return pimpl_->identifiers;
}

void imaging_study_resource::clear_identifiers() {
    pimpl_->identifiers.clear();
}

void imaging_study_resource::set_status(imaging_study_status status) {
    pimpl_->status = status;
}

imaging_study_status imaging_study_resource::status() const noexcept {
    return pimpl_->status;
}

void imaging_study_resource::set_subject(
    const imaging_study_reference& subject) {
    pimpl_->subject = subject;
}

const std::optional<imaging_study_reference>& imaging_study_resource::subject()
    const noexcept {
    return pimpl_->subject;
}

void imaging_study_resource::set_started(std::string datetime) {
    pimpl_->started = std::move(datetime);
}

const std::optional<std::string>& imaging_study_resource::started()
    const noexcept {
    return pimpl_->started;
}

void imaging_study_resource::set_based_on(
    const imaging_study_reference& based_on) {
    pimpl_->based_on = based_on;
}

const std::optional<imaging_study_reference>& imaging_study_resource::based_on()
    const noexcept {
    return pimpl_->based_on;
}

void imaging_study_resource::set_referrer(
    const imaging_study_reference& referrer) {
    pimpl_->referrer = referrer;
}

const std::optional<imaging_study_reference>& imaging_study_resource::referrer()
    const noexcept {
    return pimpl_->referrer;
}

void imaging_study_resource::set_number_of_series(uint32_t count) {
    pimpl_->number_of_series = count;
}

std::optional<uint32_t> imaging_study_resource::number_of_series()
    const noexcept {
    return pimpl_->number_of_series;
}

void imaging_study_resource::set_number_of_instances(uint32_t count) {
    pimpl_->number_of_instances = count;
}

std::optional<uint32_t> imaging_study_resource::number_of_instances()
    const noexcept {
    return pimpl_->number_of_instances;
}

void imaging_study_resource::set_description(std::string description) {
    pimpl_->description = std::move(description);
}

const std::optional<std::string>& imaging_study_resource::description()
    const noexcept {
    return pimpl_->description;
}

void imaging_study_resource::add_series(const imaging_study_series& series) {
    pimpl_->series.push_back(series);
}

const std::vector<imaging_study_series>& imaging_study_resource::series()
    const noexcept {
    return pimpl_->series;
}

void imaging_study_resource::clear_series() {
    pimpl_->series.clear();
}

std::unique_ptr<imaging_study_resource> imaging_study_resource::from_json(
    const std::string& json) {
    // Check resourceType
    std::string resource_type_str = extract_json_string(json, "resourceType");
    if (resource_type_str != "ImagingStudy") {
        return nullptr;
    }

    auto study = std::make_unique<imaging_study_resource>();

    // Extract id
    std::string id_str = extract_json_string(json, "id");
    if (!id_str.empty()) {
        study->set_id(std::move(id_str));
    }

    // Extract status
    std::string status_str = extract_json_string(json, "status");
    if (!status_str.empty()) {
        auto status = parse_imaging_study_status(status_str);
        if (status.has_value()) {
            study->set_status(*status);
        }
    }

    // Extract started
    std::string started_str = extract_json_string(json, "started");
    if (!started_str.empty()) {
        study->set_started(std::move(started_str));
    }

    // Extract description
    std::string desc_str = extract_json_string(json, "description");
    if (!desc_str.empty()) {
        study->set_description(std::move(desc_str));
    }

    // Note: Full JSON parsing for nested objects (subject, series, etc.)
    // would require a proper JSON parser. This provides basic support.

    return study;
}

std::unique_ptr<imaging_study_resource> imaging_study_resource::from_mapping_struct(
    const mapping::fhir_imaging_study& study) {
    auto resource = std::make_unique<imaging_study_resource>();

    // Set ID
    if (!study.id.empty()) {
        resource->set_id(study.id);
    }

    // Set identifiers
    for (const auto& [system, value] : study.identifiers) {
        imaging_study_identifier ident;
        ident.system = system;
        ident.value = value;
        resource->add_identifier(ident);
    }

    // Set status
    auto status = parse_imaging_study_status(study.status);
    if (status.has_value()) {
        resource->set_status(*status);
    }

    // Set subject
    if (study.subject.reference.has_value()) {
        imaging_study_reference subject;
        subject.reference = study.subject.reference;
        subject.type = study.subject.type;
        subject.display = study.subject.display;
        resource->set_subject(subject);
    }

    // Set started
    if (study.started.has_value()) {
        resource->set_started(*study.started);
    }

    // Set based-on
    if (study.based_on.has_value() && study.based_on->reference.has_value()) {
        imaging_study_reference based_on;
        based_on.reference = study.based_on->reference;
        based_on.display = study.based_on->display;
        resource->set_based_on(based_on);
    }

    // Set referrer
    if (study.referrer.has_value()) {
        imaging_study_reference referrer;
        referrer.reference = study.referrer->reference;
        referrer.display = study.referrer->display;
        resource->set_referrer(referrer);
    }

    // Set counts
    if (study.number_of_series.has_value()) {
        resource->set_number_of_series(*study.number_of_series);
    }
    if (study.number_of_instances.has_value()) {
        resource->set_number_of_instances(*study.number_of_instances);
    }

    // Set description
    if (study.description.has_value()) {
        resource->set_description(*study.description);
    }

    // Set series
    for (const auto& mapping_series : study.series) {
        imaging_study_series series;
        series.uid = mapping_series.uid;
        series.number = mapping_series.number;
        series.modality.system = mapping_series.modality.system;
        series.modality.code = mapping_series.modality.code;
        series.modality.display = mapping_series.modality.display;
        series.description = mapping_series.description;
        series.number_of_instances = mapping_series.number_of_instances;

        if (mapping_series.body_site.has_value()) {
            imaging_study_coding body_site;
            body_site.system = mapping_series.body_site->system;
            body_site.code = mapping_series.body_site->code;
            body_site.display = mapping_series.body_site->display;
            series.body_site = body_site;
        }

        resource->add_series(series);
    }

    return resource;
}

mapping::fhir_imaging_study imaging_study_resource::to_mapping_struct() const {
    mapping::fhir_imaging_study result;

    result.id = id();

    // Identifiers
    for (const auto& ident : pimpl_->identifiers) {
        std::string system = ident.system.value_or("");
        result.identifiers.push_back({system, ident.value});
    }

    // Status
    result.status = std::string(to_string(pimpl_->status));

    // Subject
    if (pimpl_->subject.has_value()) {
        result.subject.reference = pimpl_->subject->reference;
        result.subject.type = pimpl_->subject->type;
        result.subject.display = pimpl_->subject->display;
    }

    // Started
    result.started = pimpl_->started;

    // Based-on
    if (pimpl_->based_on.has_value()) {
        mapping::fhir_reference based_on;
        based_on.reference = pimpl_->based_on->reference;
        based_on.display = pimpl_->based_on->display;
        result.based_on = based_on;
    }

    // Referrer
    if (pimpl_->referrer.has_value()) {
        mapping::fhir_reference referrer;
        referrer.reference = pimpl_->referrer->reference;
        referrer.display = pimpl_->referrer->display;
        result.referrer = referrer;
    }

    // Counts
    result.number_of_series = pimpl_->number_of_series;
    result.number_of_instances = pimpl_->number_of_instances;

    // Description
    result.description = pimpl_->description;

    // Find Study Instance UID from identifiers
    for (const auto& ident : pimpl_->identifiers) {
        if (ident.system.has_value() &&
            ident.system->find("dicom:uid") != std::string::npos) {
            result.study_instance_uid = ident.value;
            break;
        }
    }

    // Series
    for (const auto& series : pimpl_->series) {
        mapping::fhir_imaging_series mapping_series;
        mapping_series.uid = series.uid;
        mapping_series.number = series.number;
        mapping_series.modality.system = series.modality.system;
        mapping_series.modality.code = series.modality.code;
        mapping_series.modality.display = series.modality.display;
        mapping_series.description = series.description;
        mapping_series.number_of_instances = series.number_of_instances;

        if (series.body_site.has_value()) {
            mapping::fhir_coding body_site;
            body_site.system = series.body_site->system;
            body_site.code = series.body_site->code;
            body_site.display = series.body_site->display;
            mapping_series.body_site = body_site;
        }

        result.series.push_back(std::move(mapping_series));
    }

    return result;
}

// =============================================================================
// In-Memory Study Storage Implementation
// =============================================================================

class in_memory_study_storage::impl {
public:
    std::unordered_map<std::string, mapping::dicom_study> studies;
    mutable std::shared_mutex mutex;
};

in_memory_study_storage::in_memory_study_storage()
    : pimpl_(std::make_unique<impl>()) {}

in_memory_study_storage::~in_memory_study_storage() = default;

bool in_memory_study_storage::store(const std::string& id,
                                    const mapping::dicom_study& study) {
    std::unique_lock lock(pimpl_->mutex);
    pimpl_->studies[id] = study;
    return true;
}

std::optional<mapping::dicom_study> in_memory_study_storage::get(
    const std::string& id) const {
    std::shared_lock lock(pimpl_->mutex);
    auto it = pimpl_->studies.find(id);
    if (it != pimpl_->studies.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<mapping::dicom_study> in_memory_study_storage::get_by_uid(
    const std::string& uid) const {
    std::shared_lock lock(pimpl_->mutex);
    for (const auto& [id, study] : pimpl_->studies) {
        if (study.study_instance_uid == uid) {
            return study;
        }
    }
    return std::nullopt;
}

std::vector<mapping::dicom_study> in_memory_study_storage::search(
    const std::optional<std::string>& patient_id,
    const std::optional<std::string>& accession_number,
    const std::optional<std::string>& status,
    const std::optional<std::string>& modality) const {
    std::shared_lock lock(pimpl_->mutex);
    std::vector<mapping::dicom_study> results;

    for (const auto& [id, study] : pimpl_->studies) {
        bool matches = true;

        // Filter by patient ID
        if (patient_id.has_value() && !patient_id->empty()) {
            // Handle Patient/xxx reference format
            std::string patient_id_value = *patient_id;
            if (patient_id_value.find("Patient/") == 0) {
                patient_id_value = patient_id_value.substr(8);
            }
            if (study.patient_id != patient_id_value) {
                matches = false;
            }
        }

        // Filter by accession number
        if (matches && accession_number.has_value() &&
            !accession_number->empty()) {
            if (study.accession_number != *accession_number) {
                matches = false;
            }
        }

        // Filter by status
        if (matches && status.has_value() && !status->empty()) {
            if (to_lower(study.status) != to_lower(*status)) {
                matches = false;
            }
        }

        // Filter by modality (check any series)
        if (matches && modality.has_value() && !modality->empty()) {
            bool modality_match = false;
            for (const auto& series : study.series) {
                if (to_lower(series.modality) == to_lower(*modality)) {
                    modality_match = true;
                    break;
                }
            }
            if (!modality_match) {
                matches = false;
            }
        }

        if (matches) {
            results.push_back(study);
        }
    }

    return results;
}

std::vector<std::string> in_memory_study_storage::keys() const {
    std::shared_lock lock(pimpl_->mutex);
    std::vector<std::string> result;
    result.reserve(pimpl_->studies.size());
    for (const auto& [id, _] : pimpl_->studies) {
        result.push_back(id);
    }
    return result;
}

bool in_memory_study_storage::remove(const std::string& id) {
    std::unique_lock lock(pimpl_->mutex);
    return pimpl_->studies.erase(id) > 0;
}

void in_memory_study_storage::clear() {
    std::unique_lock lock(pimpl_->mutex);
    pimpl_->studies.clear();
}

// =============================================================================
// ImagingStudy Handler Implementation
// =============================================================================

class imaging_study_handler::impl {
public:
    impl(std::shared_ptr<mapping::fhir_dicom_mapper> mapper,
         std::shared_ptr<study_storage> storage)
        : mapper_(std::move(mapper)), storage_(std::move(storage)) {}

    std::shared_ptr<mapping::fhir_dicom_mapper> mapper_;
    std::shared_ptr<study_storage> storage_;
    mutable std::shared_mutex mutex_;
};

imaging_study_handler::imaging_study_handler(
    std::shared_ptr<mapping::fhir_dicom_mapper> mapper,
    std::shared_ptr<study_storage> storage)
    : pimpl_(std::make_unique<impl>(std::move(mapper), std::move(storage))) {}

imaging_study_handler::~imaging_study_handler() = default;

imaging_study_handler::imaging_study_handler(
    imaging_study_handler&&) noexcept = default;

imaging_study_handler& imaging_study_handler::operator=(
    imaging_study_handler&&) noexcept = default;

resource_type imaging_study_handler::handled_type() const noexcept {
    return resource_type::imaging_study;
}

std::string_view imaging_study_handler::type_name() const noexcept {
    return "ImagingStudy";
}

resource_result<std::unique_ptr<fhir_resource>> imaging_study_handler::read(
    const std::string& id) {
    if (!pimpl_->storage_) {
        return operation_outcome::internal_error("Study storage not available");
    }

    std::shared_lock lock(pimpl_->mutex_);

    // Try to get by resource ID first
    auto result = pimpl_->storage_->get(id);
    if (!result.has_value()) {
        // Try to interpret as Study Instance UID
        std::string uid = resource_id_to_study_uid(id);
        if (!uid.empty()) {
            result = pimpl_->storage_->get_by_uid(uid);
        }
    }

    if (!result.has_value()) {
        return resource_not_found(id);
    }

    // Convert DICOM study to FHIR ImagingStudy
    if (pimpl_->mapper_) {
        auto fhir_result = pimpl_->mapper_->study_to_imaging_study(*result);
        if (fhir_result.is_ok()) {
            return imaging_study_resource::from_mapping_struct(fhir_result.unwrap());
        }
    }

    // Fallback: direct conversion without mapper
    return dicom_to_fhir_imaging_study(*result);
}

resource_result<search_result> imaging_study_handler::search(
    const std::map<std::string, std::string>& params,
    const pagination_params& pagination) {
    if (!pimpl_->storage_) {
        return operation_outcome::internal_error("Study storage not available");
    }

    std::shared_lock lock(pimpl_->mutex_);

    search_result result;

    // Extract search parameters
    std::optional<std::string> patient_filter;
    std::optional<std::string> identifier_filter;
    std::optional<std::string> status_filter;
    std::optional<std::string> id_filter;

    for (const auto& [param_name, param_value] : params) {
        if (param_name == "_id") {
            id_filter = param_value;
        } else if (param_name == "patient") {
            patient_filter = param_value;
        } else if (param_name == "identifier") {
            identifier_filter = param_value;
        } else if (param_name == "status") {
            status_filter = param_value;
        }
    }

    // If searching by _id, just do a direct read
    if (id_filter.has_value()) {
        auto study_result = pimpl_->storage_->get(*id_filter);
        if (study_result.has_value()) {
            std::unique_ptr<fhir_resource> resource;
            if (pimpl_->mapper_) {
                auto fhir_result =
                    pimpl_->mapper_->study_to_imaging_study(*study_result);
                if (fhir_result.is_ok()) {
                    resource =
                        imaging_study_resource::from_mapping_struct(fhir_result.unwrap());
                }
            } else {
                resource = dicom_to_fhir_imaging_study(*study_result);
            }
            if (resource) {
                result.entries.push_back(std::move(resource));
                result.search_modes.push_back("match");
            }
        }
        result.total = result.entries.size();
        return result;
    }

    // Perform search with filters
    auto studies = pimpl_->storage_->search(
        patient_filter,
        identifier_filter,  // Treat as accession number
        status_filter,
        std::nullopt);

    result.total = studies.size();

    // Apply pagination
    size_t start = pagination.offset;
    size_t count = pagination.count;

    if (start >= studies.size()) {
        return result;
    }

    size_t end = std::min(start + count, studies.size());

    // Convert matching studies to FHIR resources
    for (size_t i = start; i < end; ++i) {
        const auto& study = studies[i];
        std::unique_ptr<fhir_resource> resource;

        if (pimpl_->mapper_) {
            auto fhir_result = pimpl_->mapper_->study_to_imaging_study(study);
            if (fhir_result.is_ok()) {
                resource =
                    imaging_study_resource::from_mapping_struct(fhir_result.unwrap());
            }
        } else {
            resource = dicom_to_fhir_imaging_study(study);
        }

        if (resource) {
            result.entries.push_back(std::move(resource));
            result.search_modes.push_back("match");
        }
    }

    return result;
}

std::map<std::string, std::string>
imaging_study_handler::supported_search_params() const {
    return {{"_id", "Resource ID"},
            {"patient", "Patient reference (e.g., Patient/123)"},
            {"identifier", "Study Instance UID or Accession Number"},
            {"status", "Study status (registered, available, cancelled)"}};
}

bool imaging_study_handler::supports_interaction(
    interaction_type type) const noexcept {
    switch (type) {
        case interaction_type::read:
        case interaction_type::search:
            return true;
        default:
            return false;
    }
}

std::vector<interaction_type>
imaging_study_handler::supported_interactions() const {
    return {interaction_type::read, interaction_type::search};
}

// =============================================================================
// Utility Functions
// =============================================================================

std::unique_ptr<imaging_study_resource> dicom_to_fhir_imaging_study(
    const mapping::dicom_study& dicom_study,
    const std::optional<std::string>& patient_reference) {
    auto study = std::make_unique<imaging_study_resource>();

    // Generate resource ID from Study Instance UID
    if (!dicom_study.study_instance_uid.empty()) {
        study->set_id(study_uid_to_resource_id(dicom_study.study_instance_uid));
    }

    // Add Study Instance UID as identifier
    if (!dicom_study.study_instance_uid.empty()) {
        imaging_study_identifier uid_ident;
        uid_ident.system = "urn:dicom:uid";
        uid_ident.value = dicom_study.study_instance_uid;
        study->add_identifier(uid_ident);
    }

    // Add Accession Number as identifier
    if (!dicom_study.accession_number.empty()) {
        imaging_study_identifier accession_ident;
        accession_ident.system = "http://hospital.local/accession";
        accession_ident.value = dicom_study.accession_number;
        study->add_identifier(accession_ident);
    }

    // Set status
    if (!dicom_study.status.empty()) {
        auto status = parse_imaging_study_status(dicom_study.status);
        if (status.has_value()) {
            study->set_status(*status);
        }
    } else {
        study->set_status(imaging_study_status::available);
    }

    // Set subject
    imaging_study_reference subject;
    if (patient_reference.has_value()) {
        subject.reference = *patient_reference;
    } else if (!dicom_study.patient_id.empty()) {
        subject.reference = "Patient/" + dicom_study.patient_id;
    }
    if (!dicom_study.patient_name.empty()) {
        subject.display = dicom_study.patient_name;
    }
    if (subject.reference.has_value() || subject.display.has_value()) {
        study->set_subject(subject);
    }

    // Convert started date/time
    if (!dicom_study.study_date.empty()) {
        auto datetime_result = mapping::fhir_dicom_mapper::dicom_datetime_to_fhir(
            dicom_study.study_date, dicom_study.study_time);
        if (datetime_result) {
            study->set_started(*datetime_result);
        }
    }

    // Set referrer
    if (!dicom_study.referring_physician_name.empty()) {
        imaging_study_reference referrer;
        referrer.display = dicom_study.referring_physician_name;
        study->set_referrer(referrer);
    }

    // Set counts
    if (dicom_study.number_of_series.has_value()) {
        study->set_number_of_series(*dicom_study.number_of_series);
    }
    if (dicom_study.number_of_instances.has_value()) {
        study->set_number_of_instances(*dicom_study.number_of_instances);
    }

    // Set description
    if (!dicom_study.study_description.empty()) {
        study->set_description(dicom_study.study_description);
    }

    // Convert series
    for (const auto& dicom_series : dicom_study.series) {
        imaging_study_series series;
        series.uid = dicom_series.series_instance_uid;
        series.number = dicom_series.series_number;

        // Set modality
        series.modality.system = "http://dicom.nema.org/resources/ontology/DCM";
        series.modality.code = dicom_series.modality;
        series.modality.display = dicom_series.modality;

        if (!dicom_series.series_description.empty()) {
            series.description = dicom_series.series_description;
        }

        series.number_of_instances = dicom_series.number_of_instances;

        // Map body site if available
        if (!dicom_series.body_part_examined.empty()) {
            imaging_study_coding body_site;
            body_site.system = "http://snomed.info/sct";
            body_site.code = dicom_series.body_part_examined;
            body_site.display = dicom_series.body_part_examined;
            series.body_site = body_site;
        }

        study->add_series(series);
    }

    return study;
}

std::string study_uid_to_resource_id(std::string_view study_instance_uid) {
    // Create a URL-safe ID by replacing dots with dashes and adding prefix
    std::string result = "study-";
    result.reserve(result.size() + study_instance_uid.size());
    for (char c : study_instance_uid) {
        if (c == '.') {
            result += '-';
        } else {
            result += c;
        }
    }
    return result;
}

std::string resource_id_to_study_uid(std::string_view resource_id) {
    // Check for "study-" prefix
    const std::string_view prefix = "study-";
    if (resource_id.size() <= prefix.size() ||
        resource_id.substr(0, prefix.size()) != prefix) {
        return "";
    }

    // Convert back: replace dashes with dots
    std::string result;
    std::string_view uid_part = resource_id.substr(prefix.size());
    result.reserve(uid_part.size());
    for (char c : uid_part) {
        if (c == '-') {
            result += '.';
        } else {
            result += c;
        }
    }

    // Validate that result looks like a UID (contains dots)
    if (result.find('.') == std::string::npos) {
        return "";
    }

    return result;
}

}  // namespace pacs::bridge::fhir
