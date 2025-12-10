/**
 * @file subscription_resource.cpp
 * @brief FHIR Subscription resource implementation
 *
 * Implements the Subscription resource for FHIR R4.
 *
 * @see include/pacs/bridge/fhir/subscription_resource.h
 * @see https://github.com/kcenon/pacs_bridge/issues/36
 */

#include "pacs/bridge/fhir/subscription_resource.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace pacs::bridge::fhir {

// =============================================================================
// JSON Utilities
// =============================================================================

namespace {

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

std::string to_lower(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        return "";
    }

    pos += search.size();

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

std::vector<std::string> extract_json_string_array(
    const std::string& json,
    const std::string& key) {
    std::vector<std::string> result;

    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        return result;
    }

    pos += search.size();

    while (pos < json.size() &&
           (std::isspace(static_cast<unsigned char>(json[pos])) ||
            json[pos] == ':')) {
        ++pos;
    }

    if (pos >= json.size() || json[pos] != '[') {
        return result;
    }
    ++pos;

    while (pos < json.size() && json[pos] != ']') {
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
            ++pos;
        }

        if (pos < json.size() && json[pos] == '"') {
            ++pos;
            std::string value;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    ++pos;
                    value += json[pos];
                } else {
                    value += json[pos];
                }
                ++pos;
            }
            ++pos;
            result.push_back(value);
        }

        while (pos < json.size() &&
               (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ',')) {
            ++pos;
        }
    }

    return result;
}

}  // namespace

// =============================================================================
// Subscription Status
// =============================================================================

std::optional<subscription_status> parse_subscription_status(
    std::string_view status_str) noexcept {
    std::string lower = to_lower(status_str);

    if (lower == "requested") {
        return subscription_status::requested;
    }
    if (lower == "active") {
        return subscription_status::active;
    }
    if (lower == "error") {
        return subscription_status::error;
    }
    if (lower == "off") {
        return subscription_status::off;
    }

    return std::nullopt;
}

// =============================================================================
// Subscription Channel Type
// =============================================================================

std::optional<subscription_channel_type> parse_channel_type(
    std::string_view type_str) noexcept {
    std::string lower = to_lower(type_str);

    if (lower == "rest-hook") {
        return subscription_channel_type::rest_hook;
    }
    if (lower == "websocket") {
        return subscription_channel_type::websocket;
    }
    if (lower == "email") {
        return subscription_channel_type::email;
    }
    if (lower == "message") {
        return subscription_channel_type::message;
    }

    return std::nullopt;
}

// =============================================================================
// Delivery Status
// =============================================================================

std::optional<delivery_status> parse_delivery_status(
    std::string_view status_str) noexcept {
    std::string lower = to_lower(status_str);

    if (lower == "pending") {
        return delivery_status::pending;
    }
    if (lower == "in-progress") {
        return delivery_status::in_progress;
    }
    if (lower == "completed") {
        return delivery_status::completed;
    }
    if (lower == "failed") {
        return delivery_status::failed;
    }
    if (lower == "abandoned") {
        return delivery_status::abandoned;
    }

    return std::nullopt;
}

// =============================================================================
// Subscription Resource Implementation
// =============================================================================

class subscription_resource::impl {
public:
    subscription_status status = subscription_status::requested;
    std::vector<std::string> contacts;
    std::optional<std::string> end;
    std::optional<std::string> reason;
    std::string criteria;
    std::optional<std::string> error;
    subscription_channel channel;
};

subscription_resource::subscription_resource() : pimpl_(std::make_unique<impl>()) {}

subscription_resource::~subscription_resource() = default;

subscription_resource::subscription_resource(subscription_resource&&) noexcept = default;

subscription_resource& subscription_resource::operator=(subscription_resource&&) noexcept =
    default;

resource_type subscription_resource::type() const noexcept {
    return resource_type::subscription;
}

std::string subscription_resource::type_name() const { return "Subscription"; }

std::string subscription_resource::to_json() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"resourceType\": \"Subscription\"";

    // ID
    if (!id().empty()) {
        json << ",\n  \"id\": \"" << json_escape(id()) << "\"";
    }

    // Status (required)
    json << ",\n  \"status\": \"" << to_string(pimpl_->status) << "\"";

    // Contact
    if (!pimpl_->contacts.empty()) {
        json << ",\n  \"contact\": [\n";
        for (size_t i = 0; i < pimpl_->contacts.size(); ++i) {
            json << "    {\n";
            json << "      \"system\": \"email\",\n";
            json << "      \"value\": \"" << json_escape(pimpl_->contacts[i]) << "\"\n";
            json << "    }";
            if (i < pimpl_->contacts.size() - 1) {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
    }

    // End
    if (pimpl_->end.has_value()) {
        json << ",\n  \"end\": \"" << json_escape(*pimpl_->end) << "\"";
    }

    // Reason
    if (pimpl_->reason.has_value()) {
        json << ",\n  \"reason\": \"" << json_escape(*pimpl_->reason) << "\"";
    }

    // Criteria (required)
    json << ",\n  \"criteria\": \"" << json_escape(pimpl_->criteria) << "\"";

    // Error
    if (pimpl_->error.has_value()) {
        json << ",\n  \"error\": \"" << json_escape(*pimpl_->error) << "\"";
    }

    // Channel (required)
    json << ",\n  \"channel\": {\n";
    json << "    \"type\": \"" << to_string(pimpl_->channel.type) << "\"";
    json << ",\n    \"endpoint\": \"" << json_escape(pimpl_->channel.endpoint) << "\"";

    if (pimpl_->channel.payload.has_value()) {
        json << ",\n    \"payload\": \"" << json_escape(*pimpl_->channel.payload) << "\"";
    }

    if (!pimpl_->channel.header.empty()) {
        json << ",\n    \"header\": [";
        for (size_t i = 0; i < pimpl_->channel.header.size(); ++i) {
            if (i > 0) {
                json << ", ";
            }
            json << "\"" << json_escape(pimpl_->channel.header[i]) << "\"";
        }
        json << "]";
    }

    json << "\n  }";

    json << "\n}";
    return json.str();
}

bool subscription_resource::validate() const {
    // Required: status, criteria, channel.type, channel.endpoint
    if (pimpl_->criteria.empty()) {
        return false;
    }
    if (pimpl_->channel.endpoint.empty()) {
        return false;
    }
    return true;
}

void subscription_resource::set_status(subscription_status status) {
    pimpl_->status = status;
}

subscription_status subscription_resource::status() const noexcept {
    return pimpl_->status;
}

void subscription_resource::add_contact(const std::string& contact) {
    pimpl_->contacts.push_back(contact);
}

const std::vector<std::string>& subscription_resource::contacts() const noexcept {
    return pimpl_->contacts;
}

void subscription_resource::clear_contacts() {
    pimpl_->contacts.clear();
}

void subscription_resource::set_end(std::string datetime) {
    pimpl_->end = std::move(datetime);
}

const std::optional<std::string>& subscription_resource::end() const noexcept {
    return pimpl_->end;
}

void subscription_resource::set_reason(std::string reason) {
    pimpl_->reason = std::move(reason);
}

const std::optional<std::string>& subscription_resource::reason() const noexcept {
    return pimpl_->reason;
}

void subscription_resource::set_criteria(std::string criteria) {
    pimpl_->criteria = std::move(criteria);
}

const std::string& subscription_resource::criteria() const noexcept {
    return pimpl_->criteria;
}

void subscription_resource::set_error(std::string error) {
    pimpl_->error = std::move(error);
}

const std::optional<std::string>& subscription_resource::error() const noexcept {
    return pimpl_->error;
}

void subscription_resource::set_channel(const subscription_channel& channel) {
    pimpl_->channel = channel;
}

const subscription_channel& subscription_resource::channel() const noexcept {
    return pimpl_->channel;
}

std::unique_ptr<subscription_resource> subscription_resource::from_json(
    const std::string& json) {
    std::string resource_type_str = extract_json_string(json, "resourceType");
    if (resource_type_str != "Subscription") {
        return nullptr;
    }

    auto subscription = std::make_unique<subscription_resource>();

    // Extract id
    std::string id_str = extract_json_string(json, "id");
    if (!id_str.empty()) {
        subscription->set_id(std::move(id_str));
    }

    // Extract status
    std::string status_str = extract_json_string(json, "status");
    if (!status_str.empty()) {
        auto status = parse_subscription_status(status_str);
        if (status.has_value()) {
            subscription->set_status(*status);
        }
    }

    // Extract end
    std::string end_str = extract_json_string(json, "end");
    if (!end_str.empty()) {
        subscription->set_end(std::move(end_str));
    }

    // Extract reason
    std::string reason_str = extract_json_string(json, "reason");
    if (!reason_str.empty()) {
        subscription->set_reason(std::move(reason_str));
    }

    // Extract criteria
    std::string criteria_str = extract_json_string(json, "criteria");
    if (!criteria_str.empty()) {
        subscription->set_criteria(std::move(criteria_str));
    }

    // Extract error
    std::string error_str = extract_json_string(json, "error");
    if (!error_str.empty()) {
        subscription->set_error(std::move(error_str));
    }

    // Extract channel
    subscription_channel channel;

    // Find channel object
    auto channel_pos = json.find("\"channel\"");
    if (channel_pos != std::string::npos) {
        auto brace_pos = json.find('{', channel_pos);
        if (brace_pos != std::string::npos) {
            auto end_brace = json.find('}', brace_pos);
            if (end_brace != std::string::npos) {
                std::string channel_json = json.substr(brace_pos, end_brace - brace_pos + 1);

                std::string type_str = extract_json_string(channel_json, "type");
                if (!type_str.empty()) {
                    auto type = parse_channel_type(type_str);
                    if (type.has_value()) {
                        channel.type = *type;
                    }
                }

                channel.endpoint = extract_json_string(channel_json, "endpoint");

                std::string payload_str = extract_json_string(channel_json, "payload");
                if (!payload_str.empty()) {
                    channel.payload = payload_str;
                }

                channel.header = extract_json_string_array(channel_json, "header");
            }
        }
    }

    subscription->set_channel(channel);

    return subscription;
}

// =============================================================================
// Criteria Parsing
// =============================================================================

std::optional<parsed_criteria> parse_subscription_criteria(
    std::string_view criteria) noexcept {
    if (criteria.empty()) {
        return std::nullopt;
    }

    parsed_criteria result;

    // Find '?' separator
    auto query_pos = criteria.find('?');
    if (query_pos == std::string_view::npos) {
        // No query parameters, just resource type
        result.resource_type = std::string(criteria);
        return result;
    }

    result.resource_type = std::string(criteria.substr(0, query_pos));

    // Parse query parameters
    std::string_view query = criteria.substr(query_pos + 1);

    size_t pos = 0;
    while (pos < query.size()) {
        // Find '=' separator
        auto eq_pos = query.find('=', pos);
        if (eq_pos == std::string_view::npos) {
            break;
        }

        std::string key(query.substr(pos, eq_pos - pos));

        // Find '&' separator or end
        auto amp_pos = query.find('&', eq_pos + 1);
        std::string value;
        if (amp_pos == std::string_view::npos) {
            value = std::string(query.substr(eq_pos + 1));
            pos = query.size();
        } else {
            value = std::string(query.substr(eq_pos + 1, amp_pos - eq_pos - 1));
            pos = amp_pos + 1;
        }

        if (!key.empty()) {
            result.params[key] = value;
        }
    }

    return result;
}

bool matches_criteria(
    const fhir_resource& resource,
    const parsed_criteria& criteria) {
    // Check resource type
    if (resource.type_name() != criteria.resource_type) {
        return false;
    }

    // If no parameters, match all resources of this type
    if (criteria.params.empty()) {
        return true;
    }

    // Check specific parameters based on resource type
    // This is a simplified implementation - full implementation would
    // need to parse the resource JSON and check each parameter

    std::string resource_json = resource.to_json();

    for (const auto& [key, value] : criteria.params) {
        // Special handling for status parameter
        if (key == "status") {
            std::string status_str = extract_json_string(resource_json, "status");
            if (status_str != value) {
                return false;
            }
        }
        // Other parameters would need specific handling
        // For now, we do a simple substring search as a fallback
        else {
            std::string search_key = "\"" + key + "\"";
            std::string search_value = "\"" + value + "\"";
            if (resource_json.find(search_key) != std::string::npos) {
                if (resource_json.find(search_value) == std::string::npos) {
                    return false;
                }
            }
        }
    }

    return true;
}

}  // namespace pacs::bridge::fhir
