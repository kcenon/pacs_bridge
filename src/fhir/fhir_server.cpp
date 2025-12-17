/**
 * @file fhir_server.cpp
 * @brief FHIR REST server implementation
 *
 * Implements the FHIR R4 REST server with URL routing, content negotiation,
 * pagination, and CapabilityStatement generation.
 *
 * @see include/pacs/bridge/fhir/fhir_server.h
 */

#include "pacs/bridge/fhir/fhir_server.h"
#include "pacs/bridge/fhir/operation_outcome.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

namespace pacs::bridge::fhir {

// =============================================================================
// URL Routing Implementation
// =============================================================================

parsed_route parse_fhir_route(http_method method, std::string_view path,
                              std::string_view base_path) {
    parsed_route route;

    // Check if path starts with base_path
    if (path.find(base_path) != 0) {
        route.valid = false;
        return route;
    }

    // Remove base path prefix
    std::string_view remaining = path.substr(base_path.size());

    // Remove leading slash if present
    if (!remaining.empty() && remaining.front() == '/') {
        remaining.remove_prefix(1);
    }

    // Handle empty path -> metadata
    if (remaining.empty()) {
        route.valid = true;
        route.interaction = interaction_type::capabilities;
        return route;
    }

    // Handle metadata endpoint
    if (remaining == "metadata") {
        route.valid = true;
        route.interaction = interaction_type::capabilities;
        return route;
    }

    // Parse path segments
    std::vector<std::string> segments;
    size_t pos = 0;
    while (pos < remaining.size()) {
        auto next = remaining.find('/', pos);
        if (next == std::string_view::npos) {
            segments.emplace_back(remaining.substr(pos));
            break;
        }
        if (next > pos) {
            segments.emplace_back(remaining.substr(pos, next - pos));
        }
        pos = next + 1;
    }

    if (segments.empty()) {
        route.valid = true;
        route.interaction = interaction_type::capabilities;
        return route;
    }

    // First segment is resource type
    route.type_name = segments[0];
    auto resource = parse_resource_type(route.type_name);
    if (resource) {
        route.type = *resource;
    } else {
        route.type = resource_type::unknown;
    }

    // Determine interaction based on method and path
    if (segments.size() == 1) {
        // /{ResourceType}
        switch (method) {
            case http_method::get:
                route.valid = true;
                route.interaction = interaction_type::search;
                break;
            case http_method::post:
                route.valid = true;
                route.interaction = interaction_type::create;
                break;
            default:
                route.valid = false;
                break;
        }
    } else if (segments.size() == 2) {
        // /{ResourceType}/{id} or /{ResourceType}/$operation
        if (segments[1].empty()) {
            route.valid = false;
        } else if (segments[1][0] == '$') {
            // Operation
            route.valid = true;
            route.operation = segments[1].substr(1);
        } else {
            // Resource ID
            route.resource_id = segments[1];
            switch (method) {
                case http_method::get:
                    route.valid = true;
                    route.interaction = interaction_type::read;
                    break;
                case http_method::put:
                    route.valid = true;
                    route.interaction = interaction_type::update;
                    break;
                case http_method::patch:
                    route.valid = true;
                    route.interaction = interaction_type::patch;
                    break;
                case http_method::delete_method:
                    route.valid = true;
                    route.interaction = interaction_type::delete_resource;
                    break;
                default:
                    route.valid = false;
                    break;
            }
        }
    } else if (segments.size() == 3) {
        // /{ResourceType}/{id}/_history or /{ResourceType}/{id}/{compartment}
        route.resource_id = segments[1];
        if (segments[2] == "_history") {
            route.valid = true;
            route.interaction = interaction_type::history_instance;
        } else {
            route.valid = false;  // Compartment not supported yet
        }
    } else if (segments.size() == 4 && segments[2] == "_history") {
        // /{ResourceType}/{id}/_history/{version}
        route.resource_id = segments[1];
        route.version_id = segments[3];
        route.valid = true;
        route.interaction = interaction_type::vread;
    } else {
        route.valid = false;
    }

    return route;
}

// =============================================================================
// Content Negotiation
// =============================================================================

content_type negotiate_content_type(std::string_view accept_header) noexcept {
    return parse_content_type(accept_header);
}

bool is_fhir_content_type(content_type type) noexcept {
    return type == content_type::fhir_json || type == content_type::fhir_xml ||
           type == content_type::json || type == content_type::xml;
}

std::string serialize_resource(const fhir_resource& resource,
                               content_type type) {
    // For now, only JSON is supported
    // XML support would require additional implementation
    (void)type;  // Unused for now
    return resource.to_json();
}

// =============================================================================
// Pagination
// =============================================================================

pagination_params parse_pagination(
    const std::map<std::string, std::string>& params,
    const fhir_server_config& config) {
    pagination_params result;
    result.count = config.default_page_size;

    // Parse _count parameter
    auto count_it = params.find("_count");
    if (count_it != params.end()) {
        try {
            size_t count = std::stoull(count_it->second);
            result.count = std::min(count, config.max_page_size);
        } catch (...) {
            // Use default on parse error
        }
    }

    // Parse _offset parameter (non-standard but commonly used)
    auto offset_it = params.find("_offset");
    if (offset_it != params.end()) {
        try {
            result.offset = std::stoull(offset_it->second);
        } catch (...) {
            // Use default on parse error
        }
    }

    // Parse page cursor if present
    auto cursor_it = params.find("_page");
    if (cursor_it != params.end()) {
        result.cursor = cursor_it->second;
    }

    return result;
}

std::string create_search_bundle(const search_result& result,
                                 std::string_view base_url,
                                 std::string_view resource_type) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"resourceType\": \"Bundle\",\n";
    json << "  \"type\": \"searchset\",\n";
    json << "  \"total\": " << result.total << ",\n";

    // Links
    json << "  \"link\": [\n";
    for (size_t i = 0; i < result.links.size(); ++i) {
        const auto& link = result.links[i];
        json << "    {\n";
        json << "      \"relation\": \"" << link.relation << "\",\n";
        json << "      \"url\": \"" << link.url << "\"\n";
        json << "    }";
        if (i < result.links.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";

    // Entries
    json << "  \"entry\": [\n";
    for (size_t i = 0; i < result.entries.size(); ++i) {
        const auto& entry = result.entries[i];
        if (!entry) continue;

        json << "    {\n";
        json << "      \"fullUrl\": \"" << base_url << "/" << resource_type
             << "/" << entry->id() << "\",\n";
        json << "      \"resource\": " << entry->to_json();

        if (i < result.search_modes.size()) {
            json << ",\n      \"search\": {\n";
            json << "        \"mode\": \"" << result.search_modes[i] << "\"\n";
            json << "      }";
        }

        json << "\n    }";
        if (i < result.entries.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ]\n";

    json << "}";
    return json.str();
}

// =============================================================================
// Server Implementation
// =============================================================================

class fhir_server::impl {
public:
    explicit impl(const fhir_server_config& config) : config_(config) {}

    ~impl() { stop(true); }

    std::expected<void, fhir_error> start() {
        if (running_.exchange(true)) {
            return std::unexpected(fhir_error::server_error);  // Already running
        }
        return {};
    }

    void stop(bool wait_for_requests) {
        if (!running_.exchange(false)) {
            return;  // Not running
        }

        if (wait_for_requests) {
            while (stats_.active_connections > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    [[nodiscard]] bool is_running() const noexcept { return running_; }

    bool register_handler(std::shared_ptr<resource_handler> handler) {
        return handlers_.register_handler(std::move(handler));
    }

    [[nodiscard]] const handler_registry& handlers() const noexcept {
        return handlers_;
    }

    [[nodiscard]] const fhir_server_config& config() const noexcept {
        return config_;
    }

    [[nodiscard]] server_statistics get_statistics() const {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

    [[nodiscard]] std::string base_url() const {
        std::ostringstream url;
        url << (config_.enable_tls ? "https" : "http") << "://";
        url << config_.host << ":" << config_.port;
        url << config_.base_path;
        return url.str();
    }

    [[nodiscard]] http_response handle_request(const http_request& request) {
        auto start_time = std::chrono::steady_clock::now();

        // Update statistics
        {
            std::lock_guard lock(stats_mutex_);
            stats_.total_requests++;
            stats_.active_connections++;
        }

        http_response response;

        try {
            // Parse route
            auto route = parse_fhir_route(request.method, request.path,
                                          config_.base_path);

            if (!route.valid) {
                response = create_outcome_response(
                    operation_outcome::bad_request("Invalid request path"));
            } else if (route.interaction == interaction_type::capabilities) {
                response = handle_capabilities(request);
            } else if (route.type == resource_type::unknown &&
                       !route.type_name.empty()) {
                response = create_outcome_response(
                    operation_outcome::bad_request(
                        "Unknown resource type: " + route.type_name));
            } else {
                response = dispatch_request(request, route);
            }
        } catch (const std::exception& e) {
            response = create_outcome_response(
                operation_outcome::internal_error(e.what()));
        }

        // Update statistics
        {
            std::lock_guard lock(stats_mutex_);
            stats_.active_connections--;

            if (to_int(response.status) >= 400 &&
                to_int(response.status) < 500) {
                stats_.client_errors++;
            } else if (to_int(response.status) >= 500) {
                stats_.server_errors++;
            }

            auto elapsed = std::chrono::steady_clock::now() - start_time;
            double ms =
                std::chrono::duration<double, std::milli>(elapsed).count();

            // Update average response time
            double total_time = stats_.avg_response_time_ms *
                                (stats_.total_requests - 1);
            stats_.avg_response_time_ms =
                (total_time + ms) / stats_.total_requests;
        }

        // Add Content-Type header
        response.headers["Content-Type"] = std::string(
            to_mime_type(response.content));

        return response;
    }

    [[nodiscard]] std::string capability_statement() const {
        std::ostringstream json;
        json << "{\n";
        json << "  \"resourceType\": \"CapabilityStatement\",\n";
        json << "  \"status\": \"active\",\n";
        json << "  \"kind\": \"instance\",\n";
        json << "  \"fhirVersion\": \"" << config_.fhir_version << "\",\n";
        json << "  \"format\": [\"json\", \"xml\"],\n";
        json << "  \"rest\": [\n";
        json << "    {\n";
        json << "      \"mode\": \"server\",\n";
        json << "      \"resource\": [\n";

        auto all_handlers = handlers_.all_handlers();
        for (size_t i = 0; i < all_handlers.size(); ++i) {
            const auto& handler = all_handlers[i];
            json << "        {\n";
            json << "          \"type\": \"" << handler->type_name() << "\",\n";
            json << "          \"interaction\": [\n";

            auto interactions = handler->supported_interactions();
            for (size_t j = 0; j < interactions.size(); ++j) {
                json << "            {\"code\": \""
                     << interaction_code(interactions[j]) << "\"}";
                if (j < interactions.size() - 1) {
                    json << ",";
                }
                json << "\n";
            }

            json << "          ],\n";
            json << "          \"searchParam\": [\n";

            auto search_params = handler->supported_search_params();
            size_t param_idx = 0;
            for (const auto& [name, desc] : search_params) {
                json << "            {\n";
                json << "              \"name\": \"" << name << "\",\n";
                json << "              \"type\": \"string\"\n";
                json << "            }";
                if (++param_idx < search_params.size()) {
                    json << ",";
                }
                json << "\n";
            }

            json << "          ]\n";
            json << "        }";
            if (i < all_handlers.size() - 1) {
                json << ",";
            }
            json << "\n";
        }

        json << "      ]\n";
        json << "    }\n";
        json << "  ]\n";
        json << "}";

        return json.str();
    }

private:
    http_response handle_capabilities(const http_request& /*request*/) {
        return http_response::ok(capability_statement());
    }

    http_response dispatch_request(const http_request& request,
                                   const parsed_route& route) {
        auto handler = handlers_.get_handler(route.type);
        if (!handler) {
            return create_outcome_response(operation_outcome::bad_request(
                "No handler registered for resource type: " + route.type_name));
        }

        // Check if interaction is supported
        if (!handler->supports_interaction(route.interaction)) {
            return create_outcome_response(
                operation_outcome::method_not_allowed(
                    std::string(to_string(request.method)), route.type_name));
        }

        // Update interaction-specific statistics
        {
            std::lock_guard lock(stats_mutex_);
            switch (route.interaction) {
                case interaction_type::read:
                case interaction_type::vread:
                    stats_.read_requests++;
                    break;
                case interaction_type::search:
                    stats_.search_requests++;
                    break;
                case interaction_type::create:
                    stats_.create_requests++;
                    break;
                case interaction_type::update:
                case interaction_type::patch:
                    stats_.update_requests++;
                    break;
                case interaction_type::delete_resource:
                    stats_.delete_requests++;
                    break;
                default:
                    break;
            }
        }

        // Dispatch to handler
        switch (route.interaction) {
            case interaction_type::read:
                return handle_read(handler, *route.resource_id);

            case interaction_type::vread:
                return handle_vread(handler, *route.resource_id,
                                    *route.version_id);

            case interaction_type::search:
                return handle_search(handler, request.query_params);

            case interaction_type::create:
                return handle_create(handler, request.body);

            case interaction_type::update:
                return handle_update(handler, *route.resource_id, request.body);

            case interaction_type::delete_resource:
                return handle_delete(handler, *route.resource_id);

            default:
                return create_outcome_response(
                    operation_outcome::bad_request("Unsupported interaction"));
        }
    }

    http_response handle_read(std::shared_ptr<resource_handler>& handler,
                              const std::string& id) {
        auto result = handler->read(id);
        if (is_success(result)) {
            const auto& resource = get_resource(result);
            return http_response::ok(resource->to_json());
        }
        return create_outcome_response(get_outcome(result));
    }

    http_response handle_vread(std::shared_ptr<resource_handler>& handler,
                               const std::string& id,
                               const std::string& version_id) {
        auto result = handler->vread(id, version_id);
        if (is_success(result)) {
            const auto& resource = get_resource(result);
            return http_response::ok(resource->to_json());
        }
        return create_outcome_response(get_outcome(result));
    }

    http_response handle_search(
        std::shared_ptr<resource_handler>& handler,
        const std::map<std::string, std::string>& params) {
        auto pagination = parse_pagination(params, config_);
        auto result = handler->search(params, pagination);

        if (is_success(result)) {
            const auto& search_res = get_resource(result);
            auto bundle =
                create_search_bundle(search_res, base_url(),
                                     std::string(handler->type_name()));
            return http_response::ok(std::move(bundle));
        }
        return create_outcome_response(get_outcome(result));
    }

    http_response handle_create(std::shared_ptr<resource_handler>& handler,
                                const std::string& body) {
        auto resource = parse_resource(body);
        if (!resource) {
            return create_outcome_response(
                operation_outcome::bad_request("Failed to parse request body"));
        }

        auto result = handler->create(std::move(resource));
        if (is_success(result)) {
            const auto& created = get_resource(result);
            std::string location = base_url() + "/" +
                                   std::string(handler->type_name()) + "/" +
                                   created->id();
            return http_response::created(created->to_json(),
                                          std::move(location));
        }
        return create_outcome_response(get_outcome(result));
    }

    http_response handle_update(std::shared_ptr<resource_handler>& handler,
                                const std::string& id,
                                const std::string& body) {
        auto resource = parse_resource(body);
        if (!resource) {
            return create_outcome_response(
                operation_outcome::bad_request("Failed to parse request body"));
        }

        auto result = handler->update(id, std::move(resource));
        if (is_success(result)) {
            const auto& updated = get_resource(result);
            return http_response::ok(updated->to_json());
        }
        return create_outcome_response(get_outcome(result));
    }

    http_response handle_delete(std::shared_ptr<resource_handler>& handler,
                                const std::string& id) {
        auto result = handler->delete_resource(id);
        if (is_success(result)) {
            return http_response::no_content();
        }
        return create_outcome_response(get_outcome(result));
    }

    static std::string_view interaction_code(interaction_type type) {
        switch (type) {
            case interaction_type::read:
                return "read";
            case interaction_type::vread:
                return "vread";
            case interaction_type::update:
                return "update";
            case interaction_type::patch:
                return "patch";
            case interaction_type::delete_resource:
                return "delete";
            case interaction_type::history_instance:
                return "history-instance";
            case interaction_type::history_type:
                return "history-type";
            case interaction_type::create:
                return "create";
            case interaction_type::search:
                return "search-type";
            case interaction_type::capabilities:
                return "capabilities";
        }
        return "unknown";
    }

    fhir_server_config config_;
    handler_registry handlers_;
    std::atomic<bool> running_{false};

    mutable std::mutex stats_mutex_;
    mutable server_statistics stats_;
};

// =============================================================================
// FHIR Server Public Interface
// =============================================================================

fhir_server::fhir_server(const fhir_server_config& config)
    : impl_(std::make_unique<impl>(config)) {}

fhir_server::~fhir_server() = default;

fhir_server::fhir_server(fhir_server&&) noexcept = default;
fhir_server& fhir_server::operator=(fhir_server&&) noexcept = default;

std::expected<void, fhir_error> fhir_server::start() { return impl_->start(); }

void fhir_server::stop(bool wait_for_requests) {
    impl_->stop(wait_for_requests);
}

bool fhir_server::is_running() const noexcept { return impl_->is_running(); }

bool fhir_server::register_handler(std::shared_ptr<resource_handler> handler) {
    return impl_->register_handler(std::move(handler));
}

const handler_registry& fhir_server::handlers() const noexcept {
    return impl_->handlers();
}

http_response fhir_server::handle_request(const http_request& request) {
    return impl_->handle_request(request);
}

const fhir_server_config& fhir_server::config() const noexcept {
    return impl_->config();
}

server_statistics fhir_server::get_statistics() const {
    return impl_->get_statistics();
}

std::string fhir_server::base_url() const { return impl_->base_url(); }

std::string fhir_server::capability_statement() const {
    return impl_->capability_statement();
}

}  // namespace pacs::bridge::fhir
