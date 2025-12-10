/**
 * @file fhir_bundle.cpp
 * @brief Implementation of FHIR Bundle handling
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 */

#include "pacs/bridge/emr/fhir_bundle.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace pacs::bridge::emr {

namespace {

// Simple JSON escaping
std::string escape_json(std::string_view str) {
    std::string result;
    result.reserve(str.size() + 10);
    for (char c : str) {
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
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

// Case-insensitive string comparison
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Skip whitespace in JSON
size_t skip_whitespace(std::string_view json, size_t pos) {
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
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
    while (pos < json.size() && !std::isspace(static_cast<unsigned char>(json[pos])) &&
           json[pos] != ',' && json[pos] != '}' && json[pos] != ']') {
        ++pos;
    }
    return pos;
}

// Extract a JSON object as string (assumes pos is at opening brace)
std::pair<std::string, size_t> extract_json_object(std::string_view json,
                                                    size_t pos) {
    size_t start = pos;
    size_t end = find_json_value_end(json, pos);
    if (end == std::string_view::npos || end <= start) {
        return {"", std::string_view::npos};
    }
    return {std::string(json.substr(start, end - start)), end};
}

// Simple JSON field finder
std::string_view find_json_field(std::string_view json, std::string_view field) {
    // Look for "field":
    std::string pattern = "\"";
    pattern += field;
    pattern += "\"";

    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) {
        return {};
    }

    pos += pattern.size();
    pos = skip_whitespace(json, pos);
    if (pos >= json.size() || json[pos] != ':') {
        return {};
    }
    ++pos;
    pos = skip_whitespace(json, pos);

    size_t end = find_json_value_end(json, pos);
    if (end == std::string_view::npos) {
        return {};
    }

    // If it's a string, strip the quotes
    if (json[pos] == '"' && end > pos + 1 && json[end - 1] == '"') {
        return json.substr(pos + 1, end - pos - 2);
    }

    return json.substr(pos, end - pos);
}

}  // namespace

std::optional<bundle_type> parse_bundle_type(std::string_view type_str) noexcept {
    if (iequals(type_str, "document")) {
        return bundle_type::document;
    }
    if (iequals(type_str, "message")) {
        return bundle_type::message;
    }
    if (iequals(type_str, "transaction")) {
        return bundle_type::transaction;
    }
    if (iequals(type_str, "transaction-response")) {
        return bundle_type::transaction_response;
    }
    if (iequals(type_str, "batch")) {
        return bundle_type::batch;
    }
    if (iequals(type_str, "batch-response")) {
        return bundle_type::batch_response;
    }
    if (iequals(type_str, "history")) {
        return bundle_type::history;
    }
    if (iequals(type_str, "searchset")) {
        return bundle_type::searchset;
    }
    if (iequals(type_str, "collection")) {
        return bundle_type::collection;
    }
    return std::nullopt;
}

std::optional<link_relation> parse_link_relation(
    std::string_view relation_str) noexcept {
    if (iequals(relation_str, "self")) {
        return link_relation::self;
    }
    if (iequals(relation_str, "first")) {
        return link_relation::first;
    }
    if (iequals(relation_str, "last")) {
        return link_relation::last;
    }
    if (iequals(relation_str, "next")) {
        return link_relation::next;
    }
    if (iequals(relation_str, "previous") || iequals(relation_str, "prev")) {
        return link_relation::previous;
    }
    return std::nullopt;
}

std::optional<fhir_bundle> fhir_bundle::parse(std::string_view json) {
    // Verify it's a Bundle
    auto resource_type = find_json_field(json, "resourceType");
    if (resource_type.empty() || resource_type != "Bundle") {
        return std::nullopt;
    }

    fhir_bundle bundle;

    // Parse id
    auto id_val = find_json_field(json, "id");
    if (!id_val.empty()) {
        bundle.id = std::string(id_val);
    }

    // Parse type
    auto type_val = find_json_field(json, "type");
    if (!type_val.empty()) {
        auto parsed_type = parse_bundle_type(type_val);
        if (parsed_type) {
            bundle.type = *parsed_type;
        }
    }

    // Parse total
    auto total_val = find_json_field(json, "total");
    if (!total_val.empty()) {
        try {
            bundle.total = std::stoull(std::string(total_val));
        } catch (...) {
            // Ignore parse errors
        }
    }

    // Parse timestamp
    auto timestamp_val = find_json_field(json, "timestamp");
    if (!timestamp_val.empty()) {
        bundle.timestamp = std::string(timestamp_val);
    }

    // Parse links array
    size_t link_pos = json.find("\"link\"");
    if (link_pos != std::string_view::npos) {
        link_pos += 6;
        link_pos = skip_whitespace(json, link_pos);
        if (link_pos < json.size() && json[link_pos] == ':') {
            ++link_pos;
            link_pos = skip_whitespace(json, link_pos);
            if (link_pos < json.size() && json[link_pos] == '[') {
                ++link_pos;
                // Parse link objects
                while (link_pos < json.size()) {
                    link_pos = skip_whitespace(json, link_pos);
                    if (json[link_pos] == ']') {
                        break;
                    }
                    if (json[link_pos] == ',') {
                        ++link_pos;
                        continue;
                    }
                    if (json[link_pos] == '{') {
                        auto [link_obj, end] = extract_json_object(json, link_pos);
                        if (!link_obj.empty()) {
                            auto relation = find_json_field(link_obj, "relation");
                            auto url = find_json_field(link_obj, "url");
                            if (!relation.empty() && !url.empty()) {
                                auto rel = parse_link_relation(relation);
                                if (rel) {
                                    bundle.links.push_back(
                                        {*rel, std::string(url)});
                                }
                            }
                            link_pos = end;
                        } else {
                            break;
                        }
                    } else {
                        ++link_pos;
                    }
                }
            }
        }
    }

    // Parse entries array
    size_t entry_pos = json.find("\"entry\"");
    if (entry_pos != std::string_view::npos) {
        entry_pos += 7;
        entry_pos = skip_whitespace(json, entry_pos);
        if (entry_pos < json.size() && json[entry_pos] == ':') {
            ++entry_pos;
            entry_pos = skip_whitespace(json, entry_pos);
            if (entry_pos < json.size() && json[entry_pos] == '[') {
                ++entry_pos;
                // Parse entry objects
                while (entry_pos < json.size()) {
                    entry_pos = skip_whitespace(json, entry_pos);
                    if (json[entry_pos] == ']') {
                        break;
                    }
                    if (json[entry_pos] == ',') {
                        ++entry_pos;
                        continue;
                    }
                    if (json[entry_pos] == '{') {
                        auto [entry_obj, end] = extract_json_object(json, entry_pos);
                        if (!entry_obj.empty()) {
                            bundle_entry entry;

                            // Parse fullUrl
                            auto full_url = find_json_field(entry_obj, "fullUrl");
                            if (!full_url.empty()) {
                                entry.full_url = std::string(full_url);
                            }

                            // Parse resource
                            size_t res_pos = entry_obj.find("\"resource\"");
                            if (res_pos != std::string_view::npos) {
                                res_pos += 10;
                                res_pos = skip_whitespace(entry_obj, res_pos);
                                if (res_pos < entry_obj.size() &&
                                    entry_obj[res_pos] == ':') {
                                    ++res_pos;
                                    res_pos =
                                        skip_whitespace(entry_obj, res_pos);
                                    if (res_pos < entry_obj.size() &&
                                        entry_obj[res_pos] == '{') {
                                        auto [resource, res_end] =
                                            extract_json_object(entry_obj,
                                                                res_pos);
                                        if (!resource.empty()) {
                                            entry.resource = resource;
                                            entry.resource_type = std::string(
                                                find_json_field(resource,
                                                                "resourceType"));
                                            auto id_field =
                                                find_json_field(resource, "id");
                                            if (!id_field.empty()) {
                                                entry.resource_id =
                                                    std::string(id_field);
                                            }
                                        }
                                    }
                                }
                            }

                            // Parse search info
                            size_t search_pos = entry_obj.find("\"search\"");
                            if (search_pos != std::string_view::npos) {
                                search_pos += 8;
                                search_pos =
                                    skip_whitespace(entry_obj, search_pos);
                                if (search_pos < entry_obj.size() &&
                                    entry_obj[search_pos] == ':') {
                                    ++search_pos;
                                    search_pos =
                                        skip_whitespace(entry_obj, search_pos);
                                    if (search_pos < entry_obj.size() &&
                                        entry_obj[search_pos] == '{') {
                                        auto [search_obj, search_end] =
                                            extract_json_object(entry_obj,
                                                                search_pos);
                                        if (!search_obj.empty()) {
                                            entry_search search;
                                            auto mode =
                                                find_json_field(search_obj, "mode");
                                            if (mode == "match") {
                                                search.mode = search_mode::match;
                                            } else if (mode == "include") {
                                                search.mode = search_mode::include;
                                            } else if (mode == "outcome") {
                                                search.mode = search_mode::outcome;
                                            }
                                            entry.search = search;
                                        }
                                    }
                                }
                            }

                            bundle.entries.push_back(std::move(entry));
                            entry_pos = end;
                        } else {
                            break;
                        }
                    } else {
                        ++entry_pos;
                    }
                }
            }
        }
    }

    return bundle;
}

std::string fhir_bundle::to_json() const {
    std::ostringstream oss;
    oss << "{\"resourceType\":\"Bundle\"";

    if (id.has_value()) {
        oss << ",\"id\":\"" << escape_json(*id) << "\"";
    }

    oss << ",\"type\":\"" << to_string(type) << "\"";

    if (total.has_value()) {
        oss << ",\"total\":" << *total;
    }

    if (timestamp.has_value()) {
        oss << ",\"timestamp\":\"" << escape_json(*timestamp) << "\"";
    }

    if (!links.empty()) {
        oss << ",\"link\":[";
        bool first = true;
        for (const auto& link : links) {
            if (!first) {
                oss << ",";
            }
            first = false;
            oss << "{\"relation\":\"" << to_string(link.relation)
                << "\",\"url\":\"" << escape_json(link.url) << "\"}";
        }
        oss << "]";
    }

    if (!entries.empty()) {
        oss << ",\"entry\":[";
        bool first = true;
        for (const auto& entry : entries) {
            if (!first) {
                oss << ",";
            }
            first = false;
            oss << "{";
            bool has_prev = false;
            if (entry.full_url.has_value()) {
                oss << "\"fullUrl\":\"" << escape_json(*entry.full_url) << "\"";
                has_prev = true;
            }
            if (!entry.resource.empty()) {
                if (has_prev) {
                    oss << ",";
                }
                oss << "\"resource\":" << entry.resource;
                has_prev = true;
            }
            if (entry.request.has_value()) {
                if (has_prev) {
                    oss << ",";
                }
                oss << "\"request\":{\"method\":\""
                    << to_string(entry.request->method) << "\",\"url\":\""
                    << escape_json(entry.request->url) << "\"";
                if (entry.request->if_match.has_value()) {
                    oss << ",\"ifMatch\":\"" << escape_json(*entry.request->if_match) << "\"";
                }
                if (entry.request->if_none_exist.has_value()) {
                    oss << ",\"ifNoneExist\":\"" << escape_json(*entry.request->if_none_exist) << "\"";
                }
                oss << "}";
            }
            oss << "}";
        }
        oss << "]";
    }

    oss << "}";
    return oss.str();
}

// =============================================================================
// bundle_builder implementation
// =============================================================================

bundle_builder::bundle_builder(bundle_type type) : type_(type) {}

bundle_builder& bundle_builder::add_create(
    std::string_view resource_type, std::string resource,
    std::optional<std::string> conditional_create) {
    bundle_entry entry;
    entry.resource = std::move(resource);
    entry.resource_type = std::string(resource_type);

    entry_request req;
    req.method = http_method::post;
    req.url = std::string(resource_type);
    if (conditional_create.has_value()) {
        req.if_none_exist = std::move(conditional_create);
    }
    entry.request = std::move(req);

    entries_.push_back(std::move(entry));
    return *this;
}

bundle_builder& bundle_builder::add_update(std::string_view url,
                                            std::string resource,
                                            std::optional<std::string> if_match) {
    bundle_entry entry;
    entry.resource = std::move(resource);

    // Extract resource type from URL
    auto slash_pos = url.find('/');
    if (slash_pos != std::string_view::npos) {
        entry.resource_type = std::string(url.substr(0, slash_pos));
    }

    entry_request req;
    req.method = http_method::put;
    req.url = std::string(url);
    if (if_match.has_value()) {
        req.if_match = std::move(if_match);
    }
    entry.request = std::move(req);

    entries_.push_back(std::move(entry));
    return *this;
}

bundle_builder& bundle_builder::add_patch(std::string_view url,
                                           std::string patch_body) {
    bundle_entry entry;
    entry.resource = std::move(patch_body);

    auto slash_pos = url.find('/');
    if (slash_pos != std::string_view::npos) {
        entry.resource_type = std::string(url.substr(0, slash_pos));
    }

    entry_request req;
    req.method = http_method::patch;
    req.url = std::string(url);
    entry.request = std::move(req);

    entries_.push_back(std::move(entry));
    return *this;
}

bundle_builder& bundle_builder::add_delete(std::string_view url) {
    bundle_entry entry;

    auto slash_pos = url.find('/');
    if (slash_pos != std::string_view::npos) {
        entry.resource_type = std::string(url.substr(0, slash_pos));
    }

    entry_request req;
    req.method = http_method::delete_method;
    req.url = std::string(url);
    entry.request = std::move(req);

    entries_.push_back(std::move(entry));
    return *this;
}

bundle_builder& bundle_builder::add_read(std::string_view url) {
    bundle_entry entry;

    auto slash_pos = url.find('/');
    if (slash_pos != std::string_view::npos) {
        entry.resource_type = std::string(url.substr(0, slash_pos));
    }

    entry_request req;
    req.method = http_method::get;
    req.url = std::string(url);
    entry.request = std::move(req);

    entries_.push_back(std::move(entry));
    return *this;
}

bundle_builder& bundle_builder::add_search(std::string_view url) {
    bundle_entry entry;

    // For search, extract resource type before '?'
    auto q_pos = url.find('?');
    std::string_view base = (q_pos != std::string_view::npos) ? url.substr(0, q_pos) : url;
    entry.resource_type = std::string(base);

    entry_request req;
    req.method = http_method::get;
    req.url = std::string(url);
    entry.request = std::move(req);

    entries_.push_back(std::move(entry));
    return *this;
}

fhir_bundle bundle_builder::build() const {
    fhir_bundle bundle;
    bundle.type = type_;
    bundle.entries = entries_;
    return bundle;
}

std::string bundle_builder::to_json() const {
    return build().to_json();
}

}  // namespace pacs::bridge::emr
