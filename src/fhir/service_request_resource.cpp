/**
 * @file service_request_resource.cpp
 * @brief FHIR ServiceRequest resource implementation
 *
 * Implements the ServiceRequest resource and handler for FHIR R4.
 *
 * @see include/pacs/bridge/fhir/service_request_resource.h
 * @see https://github.com/kcenon/pacs_bridge/issues/33
 */

#include "pacs/bridge/fhir/service_request_resource.h"

#include "pacs/bridge/cache/patient_cache.h"
#include "pacs/bridge/mapping/fhir_dicom_mapper.h"
#include "pacs/bridge/mapping/hl7_dicom_mapper.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <random>
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

/**
 * @brief Extract first coding from a CodeableConcept in JSON
 */
service_request_coding extract_first_coding(const std::string& json,
                                            const std::string& key) {
    service_request_coding coding;

    // Find the key
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        return coding;
    }

    // Find the coding array
    auto coding_pos = json.find("\"coding\"", pos);
    if (coding_pos == std::string::npos) {
        return coding;
    }

    // Extract system, code, display from first coding
    auto bracket_pos = json.find('[', coding_pos);
    if (bracket_pos == std::string::npos) {
        return coding;
    }

    auto end_bracket = json.find(']', bracket_pos);
    if (end_bracket == std::string::npos) {
        return coding;
    }

    std::string coding_section = json.substr(bracket_pos, end_bracket - bracket_pos + 1);

    // Simple extraction (assumes first coding object)
    auto system_pos = coding_section.find("\"system\"");
    if (system_pos != std::string::npos) {
        auto quote_start = coding_section.find('"', system_pos + 8);
        if (quote_start != std::string::npos) {
            quote_start++;
            auto quote_end = coding_section.find('"', quote_start);
            if (quote_end != std::string::npos) {
                coding.system = coding_section.substr(quote_start, quote_end - quote_start);
            }
        }
    }

    auto code_pos = coding_section.find("\"code\"");
    if (code_pos != std::string::npos) {
        auto quote_start = coding_section.find('"', code_pos + 6);
        if (quote_start != std::string::npos) {
            quote_start++;
            auto quote_end = coding_section.find('"', quote_start);
            if (quote_end != std::string::npos) {
                coding.code = coding_section.substr(quote_start, quote_end - quote_start);
            }
        }
    }

    auto display_pos = coding_section.find("\"display\"");
    if (display_pos != std::string::npos) {
        auto quote_start = coding_section.find('"', display_pos + 9);
        if (quote_start != std::string::npos) {
            quote_start++;
            auto quote_end = coding_section.find('"', quote_start);
            if (quote_end != std::string::npos) {
                coding.display = coding_section.substr(quote_start, quote_end - quote_start);
            }
        }
    }

    return coding;
}

/**
 * @brief Extract reference from JSON
 */
service_request_reference extract_reference(const std::string& json,
                                            const std::string& key) {
    service_request_reference ref;

    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        return ref;
    }

    // Find opening brace
    auto brace_pos = json.find('{', pos);
    if (brace_pos == std::string::npos) {
        return ref;
    }

    auto end_brace = json.find('}', brace_pos);
    if (end_brace == std::string::npos) {
        return ref;
    }

    std::string ref_section = json.substr(brace_pos, end_brace - brace_pos + 1);

    // Extract reference field
    auto ref_pos = ref_section.find("\"reference\"");
    if (ref_pos != std::string::npos) {
        auto quote_start = ref_section.find('"', ref_pos + 11);
        if (quote_start != std::string::npos) {
            quote_start++;
            auto quote_end = ref_section.find('"', quote_start);
            if (quote_end != std::string::npos) {
                ref.reference = ref_section.substr(quote_start, quote_end - quote_start);
            }
        }
    }

    // Extract display field
    auto display_pos = ref_section.find("\"display\"");
    if (display_pos != std::string::npos) {
        auto quote_start = ref_section.find('"', display_pos + 9);
        if (quote_start != std::string::npos) {
            quote_start++;
            auto quote_end = ref_section.find('"', quote_start);
            if (quote_end != std::string::npos) {
                ref.display = ref_section.substr(quote_start, quote_end - quote_start);
            }
        }
    }

    return ref;
}

}  // namespace

// =============================================================================
// ServiceRequest Status
// =============================================================================

std::optional<service_request_status> parse_service_request_status(
    std::string_view status_str) noexcept {
    std::string lower = to_lower(status_str);

    if (lower == "draft") return service_request_status::draft;
    if (lower == "active") return service_request_status::active;
    if (lower == "on-hold") return service_request_status::on_hold;
    if (lower == "revoked") return service_request_status::revoked;
    if (lower == "completed") return service_request_status::completed;
    if (lower == "entered-in-error") return service_request_status::entered_in_error;
    if (lower == "unknown") return service_request_status::unknown;

    return std::nullopt;
}

// =============================================================================
// ServiceRequest Intent
// =============================================================================

std::optional<service_request_intent> parse_service_request_intent(
    std::string_view intent_str) noexcept {
    std::string lower = to_lower(intent_str);

    if (lower == "proposal") return service_request_intent::proposal;
    if (lower == "plan") return service_request_intent::plan;
    if (lower == "directive") return service_request_intent::directive;
    if (lower == "order") return service_request_intent::order;
    if (lower == "original-order") return service_request_intent::original_order;
    if (lower == "reflex-order") return service_request_intent::reflex_order;
    if (lower == "filler-order") return service_request_intent::filler_order;
    if (lower == "instance-order") return service_request_intent::instance_order;
    if (lower == "option") return service_request_intent::option;

    return std::nullopt;
}

// =============================================================================
// ServiceRequest Priority
// =============================================================================

std::optional<service_request_priority> parse_service_request_priority(
    std::string_view priority_str) noexcept {
    std::string lower = to_lower(priority_str);

    if (lower == "routine") return service_request_priority::routine;
    if (lower == "urgent") return service_request_priority::urgent;
    if (lower == "asap") return service_request_priority::asap;
    if (lower == "stat") return service_request_priority::stat;

    return std::nullopt;
}

// =============================================================================
// ServiceRequest Resource Implementation
// =============================================================================

class service_request_resource::impl {
public:
    std::vector<service_request_identifier> identifiers;
    service_request_status status_ = service_request_status::draft;
    service_request_intent intent_ = service_request_intent::order;
    std::optional<service_request_priority> priority_;
    std::optional<service_request_codeable_concept> code_;
    std::optional<service_request_codeable_concept> category_;
    std::optional<service_request_reference> subject_;
    std::optional<service_request_reference> requester_;
    std::vector<service_request_reference> performers_;
    std::optional<std::string> occurrence_date_time_;
    std::optional<std::string> reason_code_;
    std::optional<std::string> note_;
};

service_request_resource::service_request_resource()
    : pimpl_(std::make_unique<impl>()) {}

service_request_resource::~service_request_resource() = default;

service_request_resource::service_request_resource(
    service_request_resource&&) noexcept = default;

service_request_resource& service_request_resource::operator=(
    service_request_resource&&) noexcept = default;

resource_type service_request_resource::type() const noexcept {
    return resource_type::service_request;
}

std::string service_request_resource::type_name() const {
    return "ServiceRequest";
}

std::string service_request_resource::to_json() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"resourceType\": \"ServiceRequest\"";

    // ID
    if (!id().empty()) {
        json << ",\n  \"id\": \"" << json_escape(id()) << "\"";
    }

    // Status (required)
    json << ",\n  \"status\": \"" << to_string(pimpl_->status_) << "\"";

    // Intent (required)
    json << ",\n  \"intent\": \"" << to_string(pimpl_->intent_) << "\"";

    // Priority
    if (pimpl_->priority_.has_value()) {
        json << ",\n  \"priority\": \"" << to_string(*pimpl_->priority_) << "\"";
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
                if (!first_field) json << ",\n";
                json << "      \"system\": \"" << json_escape(*ident.system) << "\"";
                first_field = false;
            }

            if (!first_field) json << ",\n";
            json << "      \"value\": \"" << json_escape(ident.value) << "\"";

            json << "\n    }";
            if (i < pimpl_->identifiers.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
    }

    // Category
    if (pimpl_->category_.has_value()) {
        const auto& cat = *pimpl_->category_;
        json << ",\n  \"category\": [{\n";
        if (!cat.coding.empty()) {
            json << "    \"coding\": [\n";
            for (size_t i = 0; i < cat.coding.size(); ++i) {
                const auto& c = cat.coding[i];
                json << "      {\n";
                json << "        \"system\": \"" << json_escape(c.system) << "\",\n";
                json << "        \"code\": \"" << json_escape(c.code) << "\",\n";
                json << "        \"display\": \"" << json_escape(c.display) << "\"";
                json << "\n      }";
                if (i < cat.coding.size() - 1) json << ",";
                json << "\n";
            }
            json << "    ]";
        }
        if (cat.text.has_value()) {
            if (!cat.coding.empty()) json << ",\n";
            json << "    \"text\": \"" << json_escape(*cat.text) << "\"";
        }
        json << "\n  }]";
    }

    // Code
    if (pimpl_->code_.has_value()) {
        const auto& code = *pimpl_->code_;
        json << ",\n  \"code\": {\n";
        if (!code.coding.empty()) {
            json << "    \"coding\": [\n";
            for (size_t i = 0; i < code.coding.size(); ++i) {
                const auto& c = code.coding[i];
                json << "      {\n";
                json << "        \"system\": \"" << json_escape(c.system) << "\",\n";
                json << "        \"code\": \"" << json_escape(c.code) << "\",\n";
                json << "        \"display\": \"" << json_escape(c.display) << "\"";
                json << "\n      }";
                if (i < code.coding.size() - 1) json << ",";
                json << "\n";
            }
            json << "    ]";
        }
        if (code.text.has_value()) {
            if (!code.coding.empty()) json << ",\n";
            json << "    \"text\": \"" << json_escape(*code.text) << "\"";
        }
        json << "\n  }";
    }

    // Subject
    if (pimpl_->subject_.has_value()) {
        const auto& subj = *pimpl_->subject_;
        json << ",\n  \"subject\": {\n";
        bool first = true;
        if (subj.reference.has_value()) {
            json << "    \"reference\": \"" << json_escape(*subj.reference) << "\"";
            first = false;
        }
        if (subj.display.has_value()) {
            if (!first) json << ",\n";
            json << "    \"display\": \"" << json_escape(*subj.display) << "\"";
        }
        json << "\n  }";
    }

    // Requester
    if (pimpl_->requester_.has_value()) {
        const auto& req = *pimpl_->requester_;
        json << ",\n  \"requester\": {\n";
        bool first = true;
        if (req.reference.has_value()) {
            json << "    \"reference\": \"" << json_escape(*req.reference) << "\"";
            first = false;
        }
        if (req.display.has_value()) {
            if (!first) json << ",\n";
            json << "    \"display\": \"" << json_escape(*req.display) << "\"";
        }
        json << "\n  }";
    }

    // Performers
    if (!pimpl_->performers_.empty()) {
        json << ",\n  \"performer\": [\n";
        for (size_t i = 0; i < pimpl_->performers_.size(); ++i) {
            const auto& perf = pimpl_->performers_[i];
            json << "    {\n";
            bool first = true;
            if (perf.reference.has_value()) {
                json << "      \"reference\": \"" << json_escape(*perf.reference) << "\"";
                first = false;
            }
            if (perf.display.has_value()) {
                if (!first) json << ",\n";
                json << "      \"display\": \"" << json_escape(*perf.display) << "\"";
            }
            json << "\n    }";
            if (i < pimpl_->performers_.size() - 1) json << ",";
            json << "\n";
        }
        json << "  ]";
    }

    // OccurrenceDateTime
    if (pimpl_->occurrence_date_time_.has_value()) {
        json << ",\n  \"occurrenceDateTime\": \""
             << json_escape(*pimpl_->occurrence_date_time_) << "\"";
    }

    // Note
    if (pimpl_->note_.has_value()) {
        json << ",\n  \"note\": [{\n";
        json << "    \"text\": \"" << json_escape(*pimpl_->note_) << "\"";
        json << "\n  }]";
    }

    json << "\n}";
    return json.str();
}

bool service_request_resource::validate() const {
    // Required fields: status, intent, subject
    if (!pimpl_->subject_.has_value()) {
        return false;
    }
    if (!pimpl_->subject_->reference.has_value() ||
        pimpl_->subject_->reference->empty()) {
        return false;
    }
    return true;
}

void service_request_resource::add_identifier(
    const service_request_identifier& identifier) {
    pimpl_->identifiers.push_back(identifier);
}

const std::vector<service_request_identifier>&
service_request_resource::identifiers() const noexcept {
    return pimpl_->identifiers;
}

void service_request_resource::clear_identifiers() {
    pimpl_->identifiers.clear();
}

void service_request_resource::set_status(service_request_status status) {
    pimpl_->status_ = status;
}

service_request_status service_request_resource::status() const noexcept {
    return pimpl_->status_;
}

void service_request_resource::set_intent(service_request_intent intent) {
    pimpl_->intent_ = intent;
}

service_request_intent service_request_resource::intent() const noexcept {
    return pimpl_->intent_;
}

void service_request_resource::set_priority(service_request_priority priority) {
    pimpl_->priority_ = priority;
}

std::optional<service_request_priority>
service_request_resource::priority() const noexcept {
    return pimpl_->priority_;
}

void service_request_resource::set_code(
    const service_request_codeable_concept& code) {
    pimpl_->code_ = code;
}

const std::optional<service_request_codeable_concept>&
service_request_resource::code() const noexcept {
    return pimpl_->code_;
}

void service_request_resource::set_category(
    const service_request_codeable_concept& category) {
    pimpl_->category_ = category;
}

const std::optional<service_request_codeable_concept>&
service_request_resource::category() const noexcept {
    return pimpl_->category_;
}

void service_request_resource::set_subject(
    const service_request_reference& subject) {
    pimpl_->subject_ = subject;
}

const std::optional<service_request_reference>&
service_request_resource::subject() const noexcept {
    return pimpl_->subject_;
}

void service_request_resource::set_requester(
    const service_request_reference& requester) {
    pimpl_->requester_ = requester;
}

const std::optional<service_request_reference>&
service_request_resource::requester() const noexcept {
    return pimpl_->requester_;
}

void service_request_resource::add_performer(
    const service_request_reference& performer) {
    pimpl_->performers_.push_back(performer);
}

const std::vector<service_request_reference>&
service_request_resource::performers() const noexcept {
    return pimpl_->performers_;
}

void service_request_resource::clear_performers() {
    pimpl_->performers_.clear();
}

void service_request_resource::set_occurrence_date_time(std::string datetime) {
    pimpl_->occurrence_date_time_ = std::move(datetime);
}

const std::optional<std::string>&
service_request_resource::occurrence_date_time() const noexcept {
    return pimpl_->occurrence_date_time_;
}

void service_request_resource::set_reason_code(std::string reason) {
    pimpl_->reason_code_ = std::move(reason);
}

const std::optional<std::string>&
service_request_resource::reason_code() const noexcept {
    return pimpl_->reason_code_;
}

void service_request_resource::set_note(std::string note) {
    pimpl_->note_ = std::move(note);
}

const std::optional<std::string>&
service_request_resource::note() const noexcept {
    return pimpl_->note_;
}

std::unique_ptr<service_request_resource> service_request_resource::from_json(
    const std::string& json) {
    // Check resourceType
    std::string resource_type_str = extract_json_string(json, "resourceType");
    if (resource_type_str != "ServiceRequest") {
        return nullptr;
    }

    auto request = std::make_unique<service_request_resource>();

    // Extract id
    std::string id_str = extract_json_string(json, "id");
    if (!id_str.empty()) {
        request->set_id(std::move(id_str));
    }

    // Extract status
    std::string status_str = extract_json_string(json, "status");
    if (!status_str.empty()) {
        auto status = parse_service_request_status(status_str);
        if (status.has_value()) {
            request->set_status(*status);
        }
    }

    // Extract intent
    std::string intent_str = extract_json_string(json, "intent");
    if (!intent_str.empty()) {
        auto intent = parse_service_request_intent(intent_str);
        if (intent.has_value()) {
            request->set_intent(*intent);
        }
    }

    // Extract priority
    std::string priority_str = extract_json_string(json, "priority");
    if (!priority_str.empty()) {
        auto priority = parse_service_request_priority(priority_str);
        if (priority.has_value()) {
            request->set_priority(*priority);
        }
    }

    // Extract code
    auto code_coding = extract_first_coding(json, "code");
    if (!code_coding.code.empty()) {
        service_request_codeable_concept code;
        code.coding.push_back(code_coding);
        request->set_code(code);
    }

    // Extract subject
    auto subject_ref = extract_reference(json, "subject");
    if (subject_ref.reference.has_value()) {
        request->set_subject(subject_ref);
    }

    // Extract requester
    auto requester_ref = extract_reference(json, "requester");
    if (requester_ref.reference.has_value()) {
        request->set_requester(requester_ref);
    }

    // Extract occurrenceDateTime
    std::string occurrence_str = extract_json_string(json, "occurrenceDateTime");
    if (!occurrence_str.empty()) {
        request->set_occurrence_date_time(std::move(occurrence_str));
    }

    return request;
}

mapping::fhir_service_request service_request_resource::to_mapping_struct() const {
    mapping::fhir_service_request result;

    result.id = id();
    result.status = std::string(to_string(pimpl_->status_));
    result.intent = std::string(to_string(pimpl_->intent_));

    if (pimpl_->priority_.has_value()) {
        result.priority = std::string(to_string(*pimpl_->priority_));
    }

    // Convert identifiers
    for (const auto& ident : pimpl_->identifiers) {
        result.identifiers.emplace_back(
            ident.system.value_or(""), ident.value);
    }

    // Convert code
    if (pimpl_->code_.has_value() && !pimpl_->code_->coding.empty()) {
        mapping::fhir_codeable_concept code;
        for (const auto& c : pimpl_->code_->coding) {
            mapping::fhir_coding coding;
            coding.system = c.system;
            coding.version = c.version;
            coding.code = c.code;
            coding.display = c.display;
            code.coding.push_back(coding);
        }
        code.text = pimpl_->code_->text;
        result.code = code;
    }

    // Convert category
    if (pimpl_->category_.has_value() && !pimpl_->category_->coding.empty()) {
        mapping::fhir_codeable_concept category;
        for (const auto& c : pimpl_->category_->coding) {
            mapping::fhir_coding coding;
            coding.system = c.system;
            coding.version = c.version;
            coding.code = c.code;
            coding.display = c.display;
            category.coding.push_back(coding);
        }
        category.text = pimpl_->category_->text;
        result.category = category;
    }

    // Convert subject
    if (pimpl_->subject_.has_value()) {
        mapping::fhir_reference subject;
        subject.reference = pimpl_->subject_->reference;
        subject.type = pimpl_->subject_->type;
        subject.identifier = pimpl_->subject_->identifier;
        subject.display = pimpl_->subject_->display;
        result.subject = subject;
    }

    // Convert requester
    if (pimpl_->requester_.has_value()) {
        mapping::fhir_reference requester;
        requester.reference = pimpl_->requester_->reference;
        requester.type = pimpl_->requester_->type;
        requester.identifier = pimpl_->requester_->identifier;
        requester.display = pimpl_->requester_->display;
        result.requester = requester;
    }

    // Convert performers
    for (const auto& p : pimpl_->performers_) {
        mapping::fhir_reference performer;
        performer.reference = p.reference;
        performer.type = p.type;
        performer.identifier = p.identifier;
        performer.display = p.display;
        result.performer.push_back(performer);
    }

    result.occurrence_date_time = pimpl_->occurrence_date_time_;
    result.reason_code = pimpl_->reason_code_;
    result.note = pimpl_->note_;

    return result;
}

std::unique_ptr<service_request_resource>
service_request_resource::from_mapping_struct(
    const mapping::fhir_service_request& request) {
    auto result = std::make_unique<service_request_resource>();

    result->set_id(request.id);

    auto status = parse_service_request_status(request.status);
    if (status.has_value()) {
        result->set_status(*status);
    }

    auto intent = parse_service_request_intent(request.intent);
    if (intent.has_value()) {
        result->set_intent(*intent);
    }

    auto priority = parse_service_request_priority(request.priority);
    if (priority.has_value()) {
        result->set_priority(*priority);
    }

    // Convert identifiers
    for (const auto& [sys, val] : request.identifiers) {
        service_request_identifier ident;
        if (!sys.empty()) {
            ident.system = sys;
        }
        ident.value = val;
        result->add_identifier(ident);
    }

    // Convert code
    if (!request.code.coding.empty()) {
        service_request_codeable_concept code;
        for (const auto& c : request.code.coding) {
            service_request_coding coding;
            coding.system = c.system;
            coding.version = c.version;
            coding.code = c.code;
            coding.display = c.display;
            code.coding.push_back(coding);
        }
        code.text = request.code.text;
        result->set_code(code);
    }

    // Convert category
    if (request.category.has_value() && !request.category->coding.empty()) {
        service_request_codeable_concept category;
        for (const auto& c : request.category->coding) {
            service_request_coding coding;
            coding.system = c.system;
            coding.version = c.version;
            coding.code = c.code;
            coding.display = c.display;
            category.coding.push_back(coding);
        }
        category.text = request.category->text;
        result->set_category(category);
    }

    // Convert subject
    service_request_reference subject;
    subject.reference = request.subject.reference;
    subject.type = request.subject.type;
    subject.identifier = request.subject.identifier;
    subject.display = request.subject.display;
    result->set_subject(subject);

    // Convert requester
    if (request.requester.has_value()) {
        service_request_reference requester;
        requester.reference = request.requester->reference;
        requester.type = request.requester->type;
        requester.identifier = request.requester->identifier;
        requester.display = request.requester->display;
        result->set_requester(requester);
    }

    // Convert performers
    for (const auto& p : request.performer) {
        service_request_reference performer;
        performer.reference = p.reference;
        performer.type = p.type;
        performer.identifier = p.identifier;
        performer.display = p.display;
        result->add_performer(performer);
    }

    if (request.occurrence_date_time.has_value()) {
        result->set_occurrence_date_time(*request.occurrence_date_time);
    }

    if (request.reason_code.has_value()) {
        result->set_reason_code(*request.reason_code);
    }

    if (request.note.has_value()) {
        result->set_note(*request.note);
    }

    return result;
}

// =============================================================================
// In-Memory MWL Storage Implementation
// =============================================================================

class in_memory_mwl_storage::impl {
public:
    mutable std::shared_mutex mutex;
    std::unordered_map<std::string, mapping::mwl_item> items;
};

in_memory_mwl_storage::in_memory_mwl_storage()
    : pimpl_(std::make_unique<impl>()) {}

in_memory_mwl_storage::~in_memory_mwl_storage() = default;

bool in_memory_mwl_storage::store(const std::string& id,
                                  const mapping::mwl_item& item) {
    std::unique_lock lock(pimpl_->mutex);
    pimpl_->items[id] = item;
    return true;
}

std::optional<mapping::mwl_item> in_memory_mwl_storage::get(
    const std::string& id) const {
    std::shared_lock lock(pimpl_->mutex);
    auto it = pimpl_->items.find(id);
    if (it == pimpl_->items.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool in_memory_mwl_storage::update(const std::string& id,
                                   const mapping::mwl_item& item) {
    std::unique_lock lock(pimpl_->mutex);
    auto it = pimpl_->items.find(id);
    if (it == pimpl_->items.end()) {
        return false;
    }
    it->second = item;
    return true;
}

bool in_memory_mwl_storage::remove(const std::string& id) {
    std::unique_lock lock(pimpl_->mutex);
    return pimpl_->items.erase(id) > 0;
}

std::vector<std::string> in_memory_mwl_storage::keys() const {
    std::shared_lock lock(pimpl_->mutex);
    std::vector<std::string> result;
    result.reserve(pimpl_->items.size());
    for (const auto& [key, _] : pimpl_->items) {
        result.push_back(key);
    }
    return result;
}

// =============================================================================
// ServiceRequest Handler Implementation
// =============================================================================

class service_request_handler::impl {
public:
    impl(std::shared_ptr<cache::patient_cache> patient_cache,
         std::shared_ptr<mapping::fhir_dicom_mapper> mapper,
         std::shared_ptr<mwl_storage> storage)
        : patient_cache_(std::move(patient_cache)),
          mapper_(std::move(mapper)),
          storage_(std::move(storage)) {}

    std::shared_ptr<cache::patient_cache> patient_cache_;
    std::shared_ptr<mapping::fhir_dicom_mapper> mapper_;
    std::shared_ptr<mwl_storage> storage_;
    mutable std::shared_mutex mutex_;

    // Store service request data for search operations
    std::unordered_map<std::string, mapping::fhir_service_request> requests_;
};

service_request_handler::service_request_handler(
    std::shared_ptr<cache::patient_cache> patient_cache,
    std::shared_ptr<mapping::fhir_dicom_mapper> mapper,
    std::shared_ptr<mwl_storage> storage)
    : pimpl_(std::make_unique<impl>(std::move(patient_cache),
                                    std::move(mapper), std::move(storage))) {}

service_request_handler::~service_request_handler() = default;

service_request_handler::service_request_handler(
    service_request_handler&&) noexcept = default;

service_request_handler& service_request_handler::operator=(
    service_request_handler&&) noexcept = default;

resource_type service_request_handler::handled_type() const noexcept {
    return resource_type::service_request;
}

std::string_view service_request_handler::type_name() const noexcept {
    return "ServiceRequest";
}

resource_result<std::unique_ptr<fhir_resource>> service_request_handler::read(
    const std::string& id) {
    std::shared_lock lock(pimpl_->mutex_);

    auto it = pimpl_->requests_.find(id);
    if (it == pimpl_->requests_.end()) {
        return resource_not_found(id);
    }

    return service_request_resource::from_mapping_struct(it->second);
}

resource_result<std::unique_ptr<fhir_resource>> service_request_handler::create(
    std::unique_ptr<fhir_resource> resource) {
    auto* sr = dynamic_cast<service_request_resource*>(resource.get());
    if (!sr) {
        return operation_outcome::bad_request(
            "Expected ServiceRequest resource");
    }

    // Validate required fields
    if (!sr->validate()) {
        return operation_outcome::validation_error(
            "ServiceRequest validation failed: subject is required");
    }

    // Generate ID if not provided
    std::string resource_id = sr->id();
    if (resource_id.empty()) {
        resource_id = generate_resource_id();
        sr->set_id(resource_id);
    }

    // Get patient data for MWL mapping
    std::string patient_ref;
    if (sr->subject().has_value() && sr->subject()->reference.has_value()) {
        patient_ref = *sr->subject()->reference;
    }

    // Extract patient ID from reference (e.g., "Patient/123" -> "123")
    std::string patient_id;
    auto slash_pos = patient_ref.find('/');
    if (slash_pos != std::string::npos) {
        patient_id = patient_ref.substr(slash_pos + 1);
    } else {
        patient_id = patient_ref;
    }

    // Look up patient in cache
    mapping::dicom_patient patient;
    if (pimpl_->patient_cache_) {
        auto patient_result = pimpl_->patient_cache_->get(patient_id);
        if (patient_result.has_value()) {
            patient = patient_result.value();
        } else {
            // Patient not found - create minimal patient data
            patient.patient_id = patient_id;
        }
    } else {
        patient.patient_id = patient_id;
    }

    // Convert to mapping structure
    auto mapping_request = sr->to_mapping_struct();

    // Map to MWL
    if (pimpl_->mapper_) {
        auto mwl_result = pimpl_->mapper_->service_request_to_mwl(
            mapping_request, patient);
        if (mwl_result.is_ok()) {
            // Store MWL item
            if (pimpl_->storage_) {
                pimpl_->storage_->store(resource_id, mwl_result.unwrap());
            }
        }
    }

    // Store the service request
    {
        std::unique_lock lock(pimpl_->mutex_);
        pimpl_->requests_[resource_id] = mapping_request;
    }

    // Return the created resource
    return service_request_resource::from_mapping_struct(mapping_request);
}

resource_result<std::unique_ptr<fhir_resource>> service_request_handler::update(
    const std::string& id, std::unique_ptr<fhir_resource> resource) {
    auto* sr = dynamic_cast<service_request_resource*>(resource.get());
    if (!sr) {
        return operation_outcome::bad_request(
            "Expected ServiceRequest resource");
    }

    // Check if resource exists
    {
        std::shared_lock lock(pimpl_->mutex_);
        if (pimpl_->requests_.find(id) == pimpl_->requests_.end()) {
            return resource_not_found(id);
        }
    }

    // Validate
    if (!sr->validate()) {
        return operation_outcome::validation_error(
            "ServiceRequest validation failed: subject is required");
    }

    // Ensure ID matches
    sr->set_id(id);

    // Get patient data for MWL update
    std::string patient_ref;
    if (sr->subject().has_value() && sr->subject()->reference.has_value()) {
        patient_ref = *sr->subject()->reference;
    }

    std::string patient_id;
    auto slash_pos = patient_ref.find('/');
    if (slash_pos != std::string::npos) {
        patient_id = patient_ref.substr(slash_pos + 1);
    } else {
        patient_id = patient_ref;
    }

    mapping::dicom_patient patient;
    if (pimpl_->patient_cache_) {
        auto patient_result = pimpl_->patient_cache_->get(patient_id);
        if (patient_result.has_value()) {
            patient = patient_result.value();
        } else {
            patient.patient_id = patient_id;
        }
    } else {
        patient.patient_id = patient_id;
    }

    auto mapping_request = sr->to_mapping_struct();

    // Update MWL
    if (pimpl_->mapper_ && pimpl_->storage_) {
        auto mwl_result = pimpl_->mapper_->service_request_to_mwl(
            mapping_request, patient);
        if (mwl_result.is_ok()) {
            pimpl_->storage_->update(id, mwl_result.unwrap());
        }
    }

    // Update stored request
    {
        std::unique_lock lock(pimpl_->mutex_);
        pimpl_->requests_[id] = mapping_request;
    }

    return service_request_resource::from_mapping_struct(mapping_request);
}

resource_result<search_result> service_request_handler::search(
    const std::map<std::string, std::string>& params,
    const pagination_params& pagination) {
    std::shared_lock lock(pimpl_->mutex_);

    search_result result;
    std::vector<std::string> matching_ids;

    for (const auto& [id, request] : pimpl_->requests_) {
        bool matches = true;

        for (const auto& [param_name, param_value] : params) {
            if (param_name == "_id") {
                if (id != param_value) {
                    matches = false;
                    break;
                }
            } else if (param_name == "patient") {
                // Match by patient reference
                bool patient_match = false;
                if (request.subject.reference.has_value()) {
                    const auto& ref = *request.subject.reference;
                    if (ref == param_value || ref.find(param_value) != std::string::npos) {
                        patient_match = true;
                    }
                }
                if (!patient_match) {
                    matches = false;
                    break;
                }
            } else if (param_name == "status") {
                if (request.status != param_value) {
                    matches = false;
                    break;
                }
            } else if (param_name == "code") {
                // Match by procedure code
                bool code_match = false;
                for (const auto& coding : request.code.coding) {
                    if (coding.code == param_value) {
                        code_match = true;
                        break;
                    }
                }
                if (!code_match) {
                    matches = false;
                    break;
                }
            }
        }

        if (matches) {
            matching_ids.push_back(id);
        }
    }

    result.total = matching_ids.size();

    // Apply pagination
    size_t start = pagination.offset;
    size_t count = pagination.count;

    if (start >= matching_ids.size()) {
        return result;
    }

    size_t end = std::min(start + count, matching_ids.size());

    for (size_t i = start; i < end; ++i) {
        const auto& id = matching_ids[i];
        auto it = pimpl_->requests_.find(id);
        if (it != pimpl_->requests_.end()) {
            result.entries.push_back(
                service_request_resource::from_mapping_struct(it->second));
            result.search_modes.push_back("match");
        }
    }

    return result;
}

std::map<std::string, std::string>
service_request_handler::supported_search_params() const {
    return {{"_id", "Resource ID"},
            {"patient", "Patient reference"},
            {"status", "ServiceRequest status"},
            {"code", "Procedure code"}};
}

bool service_request_handler::supports_interaction(
    interaction_type type) const noexcept {
    switch (type) {
        case interaction_type::read:
        case interaction_type::create:
        case interaction_type::update:
        case interaction_type::search:
            return true;
        default:
            return false;
    }
}

std::vector<interaction_type>
service_request_handler::supported_interactions() const {
    return {interaction_type::read, interaction_type::create,
            interaction_type::update, interaction_type::search};
}

// =============================================================================
// Utility Functions
// =============================================================================

std::string generate_resource_id() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << std::hex << timestamp << "-" << dis(gen);
    return oss.str();
}

}  // namespace pacs::bridge::fhir
