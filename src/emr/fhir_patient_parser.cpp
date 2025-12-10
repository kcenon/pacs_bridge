/**
 * @file fhir_patient_parser.cpp
 * @brief FHIR Patient resource parser implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/104
 */

#include "pacs/bridge/emr/patient_lookup.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace pacs::bridge::emr {

namespace {

// =============================================================================
// JSON Parsing Helpers
// =============================================================================

// Skip whitespace in JSON
size_t skip_whitespace(std::string_view json, size_t pos) {
    while (pos < json.size() &&
           std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    return pos;
}

// Parse a JSON string value (assumes pos is at opening quote)
std::pair<std::string, size_t> parse_json_string(std::string_view json,
                                                  size_t pos) {
    if (pos >= json.size() || json[pos] != '"') {
        return {"", std::string_view::npos};
    }
    ++pos;  // Skip opening quote

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
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

    if (pos >= json.size()) {
        return {"", std::string_view::npos};
    }
    return {result, pos + 1};  // Skip closing quote
}

// Find the end of a JSON value (string, number, object, array, or literal)
size_t find_json_value_end(std::string_view json, size_t pos) {
    pos = skip_whitespace(json, pos);
    if (pos >= json.size()) {
        return std::string_view::npos;
    }

    char c = json[pos];

    // String
    if (c == '"') {
        auto [str, end] = parse_json_string(json, pos);
        return end;
    }

    // Object or Array
    if (c == '{' || c == '[') {
        char open = c;
        char close = (c == '{') ? '}' : ']';
        int depth = 1;
        ++pos;
        bool in_string = false;
        while (pos < json.size() && depth > 0) {
            if (json[pos] == '"' && (pos == 0 || json[pos - 1] != '\\')) {
                in_string = !in_string;
            } else if (!in_string) {
                if (json[pos] == open) {
                    ++depth;
                } else if (json[pos] == close) {
                    --depth;
                }
            }
            ++pos;
        }
        return pos;
    }

    // Number, true, false, null
    while (pos < json.size() &&
           !std::isspace(static_cast<unsigned char>(json[pos])) &&
           json[pos] != ',' && json[pos] != '}' && json[pos] != ']') {
        ++pos;
    }
    return pos;
}

// Simple JSON field finder
std::pair<std::string_view, size_t> find_json_field_with_pos(
    std::string_view json, std::string_view field, size_t start_pos = 0) {
    // Look for "field":
    std::string pattern = "\"";
    pattern += field;
    pattern += "\"";

    size_t pos = json.find(pattern, start_pos);
    if (pos == std::string_view::npos) {
        return {{}, std::string_view::npos};
    }

    pos += pattern.size();
    pos = skip_whitespace(json, pos);
    if (pos >= json.size() || json[pos] != ':') {
        return {{}, std::string_view::npos};
    }
    ++pos;
    pos = skip_whitespace(json, pos);

    size_t end = find_json_value_end(json, pos);
    if (end == std::string_view::npos) {
        return {{}, std::string_view::npos};
    }

    // If it's a string, strip the quotes
    if (json[pos] == '"' && end > pos + 1 && json[end - 1] == '"') {
        return {json.substr(pos + 1, end - pos - 2), end};
    }

    return {json.substr(pos, end - pos), end};
}

std::string_view find_json_field(std::string_view json, std::string_view field) {
    auto [value, pos] = find_json_field_with_pos(json, field);
    return value;
}

// Get string field value
std::optional<std::string> get_string_field(std::string_view json,
                                             std::string_view field) {
    auto value = find_json_field(json, field);
    if (value.empty()) {
        return std::nullopt;
    }
    return std::string(value);
}

// Get bool field value
std::optional<bool> get_bool_field(std::string_view json,
                                    std::string_view field) {
    auto value = find_json_field(json, field);
    if (value.empty()) {
        return std::nullopt;
    }
    if (value == "true") return true;
    if (value == "false") return false;
    return std::nullopt;
}

// Parse JSON array and call handler for each element
void parse_json_array(std::string_view json, std::string_view field,
                      std::function<void(std::string_view)> handler) {
    auto [value, pos] = find_json_field_with_pos(json, field);
    if (value.empty() || value[0] != '[') {
        return;
    }

    // Find array content
    std::string pattern = "\"";
    pattern += field;
    pattern += "\"";
    size_t array_start = json.find(pattern);
    if (array_start == std::string_view::npos) return;

    array_start += pattern.size();
    array_start = skip_whitespace(json, array_start);
    if (array_start >= json.size() || json[array_start] != ':') return;
    ++array_start;
    array_start = skip_whitespace(json, array_start);
    if (array_start >= json.size() || json[array_start] != '[') return;
    ++array_start;

    size_t current_pos = array_start;
    while (current_pos < json.size()) {
        current_pos = skip_whitespace(json, current_pos);
        if (current_pos >= json.size() || json[current_pos] == ']') break;

        size_t elem_end = find_json_value_end(json, current_pos);
        if (elem_end == std::string_view::npos) break;

        auto element = json.substr(current_pos, elem_end - current_pos);
        handler(element);

        current_pos = skip_whitespace(json, elem_end);
        if (current_pos < json.size() && json[current_pos] == ',') {
            ++current_pos;
        }
    }
}

// =============================================================================
// Parse Patient Identifier
// =============================================================================

patient_identifier parse_identifier_from_json(std::string_view id_json) {
    patient_identifier id;

    id.value = get_string_field(id_json, "value").value_or("");
    id.system = get_string_field(id_json, "system");
    id.use = get_string_field(id_json, "use");

    // Parse type
    auto type_value = find_json_field(id_json, "type");
    if (!type_value.empty()) {
        auto coding_value = find_json_field(type_value, "coding");
        if (!coding_value.empty()) {
            // Find first coding element
            auto code_value = find_json_field(coding_value, "code");
            if (!code_value.empty()) {
                id.type_code = std::string(code_value);
            }
            auto display_value = find_json_field(coding_value, "display");
            if (!display_value.empty()) {
                id.type_display = std::string(display_value);
            }
        }
        if (!id.type_display.has_value()) {
            auto text = get_string_field(type_value, "text");
            if (text.has_value()) {
                id.type_display = text;
            }
        }
    }

    return id;
}

// =============================================================================
// Parse Patient Name
// =============================================================================

patient_name parse_name_from_json(std::string_view name_json) {
    patient_name name;

    name.use = get_string_field(name_json, "use");
    name.text = get_string_field(name_json, "text");
    name.family = get_string_field(name_json, "family");

    // Parse given names array
    parse_json_array(name_json, "given", [&](std::string_view elem) {
        // Remove quotes if present
        if (elem.size() >= 2 && elem[0] == '"' &&
            elem[elem.size() - 1] == '"') {
            name.given.push_back(std::string(elem.substr(1, elem.size() - 2)));
        } else {
            name.given.push_back(std::string(elem));
        }
    });

    // Parse prefix array
    parse_json_array(name_json, "prefix", [&](std::string_view elem) {
        if (elem.size() >= 2 && elem[0] == '"' &&
            elem[elem.size() - 1] == '"') {
            name.prefix.push_back(std::string(elem.substr(1, elem.size() - 2)));
        } else {
            name.prefix.push_back(std::string(elem));
        }
    });

    // Parse suffix array
    parse_json_array(name_json, "suffix", [&](std::string_view elem) {
        if (elem.size() >= 2 && elem[0] == '"' &&
            elem[elem.size() - 1] == '"') {
            name.suffix.push_back(std::string(elem.substr(1, elem.size() - 2)));
        } else {
            name.suffix.push_back(std::string(elem));
        }
    });

    return name;
}

// =============================================================================
// Parse Patient Address
// =============================================================================

patient_address parse_address_from_json(std::string_view addr_json) {
    patient_address addr;

    addr.use = get_string_field(addr_json, "use");
    addr.type = get_string_field(addr_json, "type");
    addr.text = get_string_field(addr_json, "text");
    addr.city = get_string_field(addr_json, "city");
    addr.district = get_string_field(addr_json, "district");
    addr.state = get_string_field(addr_json, "state");
    addr.postal_code = get_string_field(addr_json, "postalCode");
    addr.country = get_string_field(addr_json, "country");

    // Parse line array
    parse_json_array(addr_json, "line", [&](std::string_view elem) {
        if (elem.size() >= 2 && elem[0] == '"' &&
            elem[elem.size() - 1] == '"') {
            addr.lines.push_back(std::string(elem.substr(1, elem.size() - 2)));
        } else {
            addr.lines.push_back(std::string(elem));
        }
    });

    return addr;
}

// =============================================================================
// Parse Contact Point
// =============================================================================

patient_contact_point parse_telecom_from_json(std::string_view telecom_json) {
    patient_contact_point contact;

    contact.system = get_string_field(telecom_json, "system").value_or("other");
    contact.value = get_string_field(telecom_json, "value").value_or("");
    contact.use = get_string_field(telecom_json, "use");

    auto rank_str = find_json_field(telecom_json, "rank");
    if (!rank_str.empty()) {
        try {
            contact.rank = std::stoi(std::string(rank_str));
        } catch (...) {
            // Ignore parse errors
        }
    }

    return contact;
}

// =============================================================================
// Find MRN from Identifiers
// =============================================================================

std::string find_mrn(const std::vector<patient_identifier>& identifiers) {
    // First, look for explicit MR type
    for (const auto& id : identifiers) {
        if (id.is_mrn()) {
            return id.value;
        }
    }

    // Look for common MRN system patterns
    for (const auto& id : identifiers) {
        if (id.system.has_value()) {
            const auto& sys = *id.system;
            if (sys.find("mrn") != std::string::npos ||
                sys.find("MRN") != std::string::npos ||
                sys.find("medical-record") != std::string::npos ||
                sys.find("patient-id") != std::string::npos) {
                return id.value;
            }
        }
    }

    // Fall back to first "usual" identifier
    for (const auto& id : identifiers) {
        if (id.use.has_value() && *id.use == "usual") {
            return id.value;
        }
    }

    // Fall back to first identifier
    if (!identifiers.empty()) {
        return identifiers.front().value;
    }

    return {};
}

}  // namespace

// =============================================================================
// Public Functions
// =============================================================================

std::expected<patient_record, patient_error> parse_fhir_patient(
    std::string_view json_str) {

    // Verify resource type
    auto resource_type = get_string_field(json_str, "resourceType");
    if (!resource_type.has_value() || *resource_type != "Patient") {
        return std::unexpected(patient_error::invalid_data);
    }

    patient_record patient;

    // Resource ID
    patient.id = get_string_field(json_str, "id").value_or("");

    // Version and metadata
    auto meta_value = find_json_field(json_str, "meta");
    if (!meta_value.empty()) {
        patient.version_id = get_string_field(meta_value, "versionId");
        patient.last_updated = get_string_field(meta_value, "lastUpdated");
    }

    // Identifiers
    parse_json_array(json_str, "identifier", [&](std::string_view elem) {
        patient.identifiers.push_back(parse_identifier_from_json(elem));
    });

    // Determine MRN
    patient.mrn = find_mrn(patient.identifiers);

    // Names
    parse_json_array(json_str, "name", [&](std::string_view elem) {
        patient.names.push_back(parse_name_from_json(elem));
    });

    // Birth date
    patient.birth_date = get_string_field(json_str, "birthDate");

    // Gender
    patient.sex = get_string_field(json_str, "gender");

    // Addresses
    parse_json_array(json_str, "address", [&](std::string_view elem) {
        patient.addresses.push_back(parse_address_from_json(elem));
    });

    // Telecom
    parse_json_array(json_str, "telecom", [&](std::string_view elem) {
        patient.telecom.push_back(parse_telecom_from_json(elem));
    });

    // Active status
    patient.active = get_bool_field(json_str, "active").value_or(true);

    // Deceased
    auto deceased_bool = get_bool_field(json_str, "deceasedBoolean");
    if (deceased_bool.has_value()) {
        patient.deceased = deceased_bool;
    } else {
        auto deceased_dt = get_string_field(json_str, "deceasedDateTime");
        if (deceased_dt.has_value()) {
            patient.deceased = true;
            patient.deceased_datetime = deceased_dt;
        }
    }

    // Language from communication array
    auto comm_value = find_json_field(json_str, "communication");
    if (!comm_value.empty()) {
        auto lang_value = find_json_field(comm_value, "language");
        if (!lang_value.empty()) {
            auto coding_value = find_json_field(lang_value, "coding");
            if (!coding_value.empty()) {
                patient.language = get_string_field(coding_value, "code");
            }
        }
    }

    // Managing organization
    auto org_value = find_json_field(json_str, "managingOrganization");
    if (!org_value.empty()) {
        patient.managing_organization = get_string_field(org_value, "reference");
    }

    // Links (for merged patients)
    auto link_value = find_json_field(json_str, "link");
    if (!link_value.empty()) {
        auto other_value = find_json_field(link_value, "other");
        if (!other_value.empty()) {
            patient.link_reference = get_string_field(other_value, "reference");
        }
        patient.link_type = get_string_field(link_value, "type");
    }

    return patient;
}

std::vector<patient_record> parse_patient_bundle(const fhir_bundle& bundle) {
    std::vector<patient_record> patients;
    patients.reserve(bundle.entries.size());

    for (const auto& entry : bundle.entries) {
        if (entry.resource_type != "Patient") {
            continue;
        }

        auto result = parse_fhir_patient(entry.resource);
        if (result) {
            // Use entry's full_url as ID if not present
            if (result->id.empty() && entry.full_url.has_value()) {
                auto& full_url = *entry.full_url;
                auto pos = full_url.rfind('/');
                if (pos != std::string::npos) {
                    result->id = full_url.substr(pos + 1);
                }
            }

            patients.push_back(std::move(*result));
        }
    }

    return patients;
}

}  // namespace pacs::bridge::emr
