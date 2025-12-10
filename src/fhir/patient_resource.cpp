/**
 * @file patient_resource.cpp
 * @brief FHIR Patient resource implementation
 *
 * Implements the Patient resource and handler for FHIR R4.
 *
 * @see include/pacs/bridge/fhir/patient_resource.h
 * @see https://github.com/kcenon/pacs_bridge/issues/32
 */

#include "pacs/bridge/fhir/patient_resource.h"

#include "pacs/bridge/cache/patient_cache.h"
#include "pacs/bridge/mapping/hl7_dicom_mapper.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include <sstream>

namespace pacs::bridge::fhir {

// =============================================================================
// JSON Utilities (Shared with operation_outcome.cpp)
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
// Administrative Gender
// =============================================================================

std::optional<administrative_gender> parse_gender(
    std::string_view gender_str) noexcept {
    std::string lower = to_lower(gender_str);

    if (lower == "male" || lower == "m") {
        return administrative_gender::male;
    }
    if (lower == "female" || lower == "f") {
        return administrative_gender::female;
    }
    if (lower == "other" || lower == "o") {
        return administrative_gender::other;
    }
    if (lower == "unknown" || lower == "u") {
        return administrative_gender::unknown;
    }

    return std::nullopt;
}

// =============================================================================
// Patient Resource Implementation
// =============================================================================

class patient_resource::impl {
public:
    std::vector<fhir_identifier> identifiers;
    std::vector<fhir_human_name> names;
    std::optional<administrative_gender> gender;
    std::optional<std::string> birth_date;
    std::optional<bool> active;
};

patient_resource::patient_resource() : pimpl_(std::make_unique<impl>()) {}

patient_resource::~patient_resource() = default;

patient_resource::patient_resource(patient_resource&&) noexcept = default;

patient_resource& patient_resource::operator=(patient_resource&&) noexcept =
    default;

resource_type patient_resource::type() const noexcept {
    return resource_type::patient;
}

std::string patient_resource::type_name() const { return "Patient"; }

std::string patient_resource::to_json() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"resourceType\": \"Patient\"";

    // ID
    if (!id().empty()) {
        json << ",\n  \"id\": \"" << json_escape(id()) << "\"";
    }

    // Active
    if (pimpl_->active.has_value()) {
        json << ",\n  \"active\": " << (*pimpl_->active ? "true" : "false");
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

    // Names
    if (!pimpl_->names.empty()) {
        json << ",\n  \"name\": [\n";
        for (size_t i = 0; i < pimpl_->names.size(); ++i) {
            const auto& name = pimpl_->names[i];
            json << "    {\n";

            bool first_field = true;

            if (name.use.has_value()) {
                json << "      \"use\": \"" << json_escape(*name.use) << "\"";
                first_field = false;
            }

            if (name.text.has_value()) {
                if (!first_field) {
                    json << ",\n";
                }
                json << "      \"text\": \"" << json_escape(*name.text) << "\"";
                first_field = false;
            }

            if (name.family.has_value()) {
                if (!first_field) {
                    json << ",\n";
                }
                json << "      \"family\": \"" << json_escape(*name.family)
                     << "\"";
                first_field = false;
            }

            if (!name.given.empty()) {
                if (!first_field) {
                    json << ",\n";
                }
                json << "      \"given\": [";
                for (size_t j = 0; j < name.given.size(); ++j) {
                    if (j > 0) {
                        json << ", ";
                    }
                    json << "\"" << json_escape(name.given[j]) << "\"";
                }
                json << "]";
                first_field = false;
            }

            if (!name.prefix.empty()) {
                if (!first_field) {
                    json << ",\n";
                }
                json << "      \"prefix\": [";
                for (size_t j = 0; j < name.prefix.size(); ++j) {
                    if (j > 0) {
                        json << ", ";
                    }
                    json << "\"" << json_escape(name.prefix[j]) << "\"";
                }
                json << "]";
                first_field = false;
            }

            if (!name.suffix.empty()) {
                if (!first_field) {
                    json << ",\n";
                }
                json << "      \"suffix\": [";
                for (size_t j = 0; j < name.suffix.size(); ++j) {
                    if (j > 0) {
                        json << ", ";
                    }
                    json << "\"" << json_escape(name.suffix[j]) << "\"";
                }
                json << "]";
            }

            json << "\n    }";
            if (i < pimpl_->names.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
    }

    // Gender
    if (pimpl_->gender.has_value()) {
        json << ",\n  \"gender\": \"" << to_string(*pimpl_->gender) << "\"";
    }

    // Birth date
    if (pimpl_->birth_date.has_value()) {
        json << ",\n  \"birthDate\": \"" << json_escape(*pimpl_->birth_date)
             << "\"";
    }

    json << "\n}";
    return json.str();
}

bool patient_resource::validate() const {
    // FHIR Patient requires at least one identifier for most use cases
    // However, the resource itself is valid even without identifiers
    return true;
}

void patient_resource::add_identifier(const fhir_identifier& identifier) {
    pimpl_->identifiers.push_back(identifier);
}

const std::vector<fhir_identifier>& patient_resource::identifiers()
    const noexcept {
    return pimpl_->identifiers;
}

void patient_resource::clear_identifiers() { pimpl_->identifiers.clear(); }

void patient_resource::add_name(const fhir_human_name& name) {
    pimpl_->names.push_back(name);
}

const std::vector<fhir_human_name>& patient_resource::names() const noexcept {
    return pimpl_->names;
}

void patient_resource::clear_names() { pimpl_->names.clear(); }

void patient_resource::set_gender(administrative_gender gender) {
    pimpl_->gender = gender;
}

std::optional<administrative_gender> patient_resource::gender() const noexcept {
    return pimpl_->gender;
}

void patient_resource::set_birth_date(std::string date) {
    pimpl_->birth_date = std::move(date);
}

const std::optional<std::string>& patient_resource::birth_date() const noexcept {
    return pimpl_->birth_date;
}

void patient_resource::set_active(bool active) { pimpl_->active = active; }

std::optional<bool> patient_resource::active() const noexcept {
    return pimpl_->active;
}

std::unique_ptr<patient_resource> patient_resource::from_json(
    const std::string& json) {
    // Check resourceType
    std::string resource_type_str = extract_json_string(json, "resourceType");
    if (resource_type_str != "Patient") {
        return nullptr;
    }

    auto patient = std::make_unique<patient_resource>();

    // Extract id
    std::string id_str = extract_json_string(json, "id");
    if (!id_str.empty()) {
        patient->set_id(std::move(id_str));
    }

    // Extract gender
    std::string gender_str = extract_json_string(json, "gender");
    if (!gender_str.empty()) {
        auto gender = parse_gender(gender_str);
        if (gender.has_value()) {
            patient->set_gender(*gender);
        }
    }

    // Extract birthDate
    std::string birth_date_str = extract_json_string(json, "birthDate");
    if (!birth_date_str.empty()) {
        patient->set_birth_date(std::move(birth_date_str));
    }

    // Note: Full JSON parsing for arrays (identifier, name) would require
    // a proper JSON parser. For now, this provides basic support.

    return patient;
}

// =============================================================================
// Patient Resource Handler Implementation
// =============================================================================

class patient_resource_handler::impl {
public:
    explicit impl(std::shared_ptr<cache::patient_cache> cache)
        : cache_(std::move(cache)) {}

    std::shared_ptr<cache::patient_cache> cache_;
    mutable std::shared_mutex mutex_;
};

patient_resource_handler::patient_resource_handler(
    std::shared_ptr<cache::patient_cache> cache)
    : pimpl_(std::make_unique<impl>(std::move(cache))) {}

patient_resource_handler::~patient_resource_handler() = default;

patient_resource_handler::patient_resource_handler(
    patient_resource_handler&&) noexcept = default;

patient_resource_handler& patient_resource_handler::operator=(
    patient_resource_handler&&) noexcept = default;

resource_type patient_resource_handler::handled_type() const noexcept {
    return resource_type::patient;
}

std::string_view patient_resource_handler::type_name() const noexcept {
    return "Patient";
}

resource_result<std::unique_ptr<fhir_resource>> patient_resource_handler::read(
    const std::string& id) {
    if (!pimpl_->cache_) {
        return operation_outcome::internal_error("Patient cache not available");
    }

    std::shared_lock lock(pimpl_->mutex_);

    auto result = pimpl_->cache_->get(id);
    if (!result.has_value()) {
        return resource_not_found(id);
    }

    return dicom_to_fhir_patient(result.value(), id);
}

resource_result<search_result> patient_resource_handler::search(
    const std::map<std::string, std::string>& params,
    const pagination_params& pagination) {
    if (!pimpl_->cache_) {
        return operation_outcome::internal_error("Patient cache not available");
    }

    std::shared_lock lock(pimpl_->mutex_);

    search_result result;

    // Get all keys from cache
    auto keys = pimpl_->cache_->keys();

    // Filter based on search parameters
    std::vector<std::string> matching_keys;

    for (const auto& key : keys) {
        auto patient_result = pimpl_->cache_->peek(key);
        if (!patient_result.has_value()) {
            continue;
        }

        const auto& patient = patient_result.value();
        bool matches = true;

        // Check each search parameter
        for (const auto& [param_name, param_value] : params) {
            if (param_name == "_id") {
                // Match by resource ID (exact match)
                if (key != param_value) {
                    matches = false;
                    break;
                }
            } else if (param_name == "identifier") {
                // Match by patient identifier (exact or partial)
                bool identifier_match = false;
                if (patient.patient_id == param_value) {
                    identifier_match = true;
                }
                for (const auto& other_id : patient.other_patient_ids) {
                    if (other_id == param_value) {
                        identifier_match = true;
                        break;
                    }
                }
                if (!identifier_match) {
                    matches = false;
                    break;
                }
            } else if (param_name == "name") {
                // Match by patient name (partial, case-insensitive)
                if (!contains_ignore_case(patient.patient_name, param_value)) {
                    matches = false;
                    break;
                }
            } else if (param_name == "birthdate") {
                // Match by birth date
                // FHIR format: YYYY-MM-DD, DICOM format: YYYYMMDD
                std::string fhir_date = dicom_date_to_fhir(patient.patient_birth_date);
                if (fhir_date != param_value &&
                    patient.patient_birth_date != param_value) {
                    matches = false;
                    break;
                }
            }
        }

        if (matches) {
            matching_keys.push_back(key);
        }
    }

    // Set total count
    result.total = matching_keys.size();

    // Apply pagination
    size_t start = pagination.offset;
    size_t count = pagination.count;

    if (start >= matching_keys.size()) {
        // No results for this page
        return result;
    }

    size_t end = std::min(start + count, matching_keys.size());

    // Convert matching patients to FHIR resources
    for (size_t i = start; i < end; ++i) {
        const auto& key = matching_keys[i];
        auto patient_result = pimpl_->cache_->peek(key);
        if (patient_result.has_value()) {
            auto fhir_patient =
                dicom_to_fhir_patient(patient_result.value(), key);
            result.entries.push_back(std::move(fhir_patient));
            result.search_modes.push_back("match");
        }
    }

    return result;
}

std::map<std::string, std::string>
patient_resource_handler::supported_search_params() const {
    return {{"_id", "Resource ID"},
            {"identifier", "Patient identifier value"},
            {"name", "Patient name (partial match)"},
            {"birthdate", "Patient birth date (YYYY-MM-DD)"}};
}

bool patient_resource_handler::supports_interaction(
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
patient_resource_handler::supported_interactions() const {
    return {interaction_type::read, interaction_type::search};
}

// =============================================================================
// Mapping Utilities Implementation
// =============================================================================

std::unique_ptr<patient_resource> dicom_to_fhir_patient(
    const mapping::dicom_patient& dicom_patient,
    const std::optional<std::string>& patient_id) {
    auto patient = std::make_unique<patient_resource>();

    // Set ID
    if (patient_id.has_value()) {
        patient->set_id(*patient_id);
    } else if (!dicom_patient.patient_id.empty()) {
        patient->set_id(dicom_patient.patient_id);
    }

    // Set active (assume true if patient exists)
    patient->set_active(true);

    // Add primary identifier
    if (!dicom_patient.patient_id.empty()) {
        fhir_identifier primary_id;
        primary_id.use = "usual";
        if (!dicom_patient.issuer_of_patient_id.empty()) {
            primary_id.system = dicom_patient.issuer_of_patient_id;
        }
        primary_id.value = dicom_patient.patient_id;
        patient->add_identifier(primary_id);
    }

    // Add other patient IDs
    for (const auto& other_id : dicom_patient.other_patient_ids) {
        fhir_identifier ident;
        ident.use = "secondary";
        ident.value = other_id;
        patient->add_identifier(ident);
    }

    // Convert name
    if (!dicom_patient.patient_name.empty()) {
        fhir_human_name name = dicom_name_to_fhir(dicom_patient.patient_name);
        name.use = "official";
        patient->add_name(name);
    }

    // Convert gender
    if (!dicom_patient.patient_sex.empty()) {
        patient->set_gender(
            dicom_sex_to_fhir_gender(dicom_patient.patient_sex));
    }

    // Convert birth date
    if (!dicom_patient.patient_birth_date.empty()) {
        std::string fhir_date =
            dicom_date_to_fhir(dicom_patient.patient_birth_date);
        if (!fhir_date.empty()) {
            patient->set_birth_date(fhir_date);
        }
    }

    return patient;
}

fhir_human_name dicom_name_to_fhir(std::string_view dicom_name) {
    fhir_human_name name;

    // DICOM PN format: Family^Given^Middle^Prefix^Suffix
    // Components are separated by '^'
    std::vector<std::string> components;
    std::string current;

    for (char c : dicom_name) {
        if (c == '^') {
            components.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    components.push_back(current);

    // Parse components
    if (!components.empty() && !components[0].empty()) {
        name.family = components[0];
    }

    if (components.size() > 1 && !components[1].empty()) {
        // Given name might contain multiple names separated by spaces
        std::string given = components[1];
        std::istringstream iss(given);
        std::string given_part;
        while (iss >> given_part) {
            name.given.push_back(given_part);
        }
    }

    if (components.size() > 2 && !components[2].empty()) {
        // Middle name - add to given names
        name.given.push_back(components[2]);
    }

    if (components.size() > 3 && !components[3].empty()) {
        name.prefix.push_back(components[3]);
    }

    if (components.size() > 4 && !components[4].empty()) {
        name.suffix.push_back(components[4]);
    }

    // Build text representation
    std::ostringstream text;
    for (const auto& p : name.prefix) {
        text << p << " ";
    }
    for (const auto& g : name.given) {
        text << g << " ";
    }
    if (name.family.has_value()) {
        text << *name.family;
    }
    for (const auto& s : name.suffix) {
        text << " " << s;
    }
    std::string text_str = text.str();
    // Trim trailing space
    while (!text_str.empty() && text_str.back() == ' ') {
        text_str.pop_back();
    }
    if (!text_str.empty()) {
        name.text = text_str;
    }

    return name;
}

std::string fhir_name_to_dicom(const fhir_human_name& name) {
    std::ostringstream dicom;

    // Family
    if (name.family.has_value()) {
        dicom << *name.family;
    }
    dicom << "^";

    // Given (first name)
    if (!name.given.empty()) {
        dicom << name.given[0];
    }
    dicom << "^";

    // Middle name (second given name if exists)
    if (name.given.size() > 1) {
        dicom << name.given[1];
    }
    dicom << "^";

    // Prefix
    if (!name.prefix.empty()) {
        dicom << name.prefix[0];
    }
    dicom << "^";

    // Suffix
    if (!name.suffix.empty()) {
        dicom << name.suffix[0];
    }

    std::string result = dicom.str();

    // Remove trailing '^' characters
    while (!result.empty() && result.back() == '^') {
        result.pop_back();
    }

    return result;
}

std::string dicom_date_to_fhir(std::string_view dicom_date) {
    // DICOM: YYYYMMDD -> FHIR: YYYY-MM-DD
    if (dicom_date.size() != 8) {
        return "";
    }

    // Validate all characters are digits
    for (char c : dicom_date) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return "";
        }
    }

    std::string result;
    result.reserve(10);
    result += dicom_date.substr(0, 4);  // YYYY
    result += '-';
    result += dicom_date.substr(4, 2);  // MM
    result += '-';
    result += dicom_date.substr(6, 2);  // DD

    return result;
}

std::string fhir_date_to_dicom(std::string_view fhir_date) {
    // FHIR: YYYY-MM-DD -> DICOM: YYYYMMDD
    if (fhir_date.size() != 10) {
        return "";
    }

    if (fhir_date[4] != '-' || fhir_date[7] != '-') {
        return "";
    }

    std::string result;
    result.reserve(8);
    result += fhir_date.substr(0, 4);   // YYYY
    result += fhir_date.substr(5, 2);   // MM
    result += fhir_date.substr(8, 2);   // DD

    // Validate all characters are digits
    for (char c : result) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return "";
        }
    }

    return result;
}

administrative_gender dicom_sex_to_fhir_gender(std::string_view dicom_sex) {
    if (dicom_sex.empty()) {
        return administrative_gender::unknown;
    }

    char sex = static_cast<char>(
        std::toupper(static_cast<unsigned char>(dicom_sex[0])));

    switch (sex) {
        case 'M':
            return administrative_gender::male;
        case 'F':
            return administrative_gender::female;
        case 'O':
            return administrative_gender::other;
        default:
            return administrative_gender::unknown;
    }
}

std::string fhir_gender_to_dicom_sex(administrative_gender gender) {
    switch (gender) {
        case administrative_gender::male:
            return "M";
        case administrative_gender::female:
            return "F";
        case administrative_gender::other:
            return "O";
        case administrative_gender::unknown:
        default:
            return "";
    }
}

}  // namespace pacs::bridge::fhir
