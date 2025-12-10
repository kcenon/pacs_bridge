/**
 * @file smart_discovery.cpp
 * @brief Smart-on-FHIR configuration discovery implementation
 *
 * Implements Smart-on-FHIR discovery from FHIR server's
 * .well-known/smart-configuration endpoint.
 *
 * @see include/pacs/bridge/security/smart_discovery.h
 */

#include "pacs/bridge/security/smart_discovery.h"

#include <mutex>
#include <sstream>

namespace pacs::bridge::security {

// =============================================================================
// JSON Parsing Utilities
// =============================================================================

namespace {

/**
 * @brief Extract string value from JSON
 */
std::string json_get_string(std::string_view json, std::string_view key) {
    std::string search_key = "\"" + std::string(key) + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string_view::npos) {
        return {};
    }

    pos = json.find(':', pos + search_key.length());
    if (pos == std::string_view::npos) {
        return {};
    }

    pos++;
    while (pos < json.size() && std::isspace(json[pos])) {
        pos++;
    }

    if (pos >= json.size() || json[pos] != '"') {
        return {};
    }

    pos++;  // Skip opening quote
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

/**
 * @brief Extract string array from JSON
 */
std::vector<std::string> json_get_string_array(std::string_view json,
                                                std::string_view key) {
    std::vector<std::string> result;

    std::string search_key = "\"" + std::string(key) + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string_view::npos) {
        return result;
    }

    pos = json.find(':', pos + search_key.length());
    if (pos == std::string_view::npos) {
        return result;
    }

    pos++;
    while (pos < json.size() && std::isspace(json[pos])) {
        pos++;
    }

    if (pos >= json.size() || json[pos] != '[') {
        return result;
    }

    pos++;  // Skip opening bracket

    while (pos < json.size()) {
        // Skip whitespace
        while (pos < json.size() && std::isspace(json[pos])) {
            pos++;
        }

        if (pos >= json.size() || json[pos] == ']') {
            break;
        }

        // Skip comma
        if (json[pos] == ',') {
            pos++;
            continue;
        }

        // Parse string element
        if (json[pos] == '"') {
            pos++;  // Skip opening quote
            std::string element;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                    element += json[pos];
                } else {
                    element += json[pos];
                }
                pos++;
            }
            if (pos < json.size()) {
                pos++;  // Skip closing quote
            }
            if (!element.empty()) {
                result.push_back(std::move(element));
            }
        } else {
            pos++;  // Skip unexpected character
        }
    }

    return result;
}

}  // namespace

// =============================================================================
// Smart Discovery Implementation
// =============================================================================

class smart_discovery::impl {
public:
    explicit impl(const smart_discovery_config& config) : config_(config) {}

    impl(const smart_discovery_config& config, http_get_callback http_client)
        : config_(config), http_client_(std::move(http_client)) {}

    smart_discovery_config config_;
    http_get_callback http_client_;

    mutable std::mutex cache_mutex_;
    std::optional<smart_configuration> cached_config_;
    std::chrono::system_clock::time_point cache_time_;
};

smart_discovery::smart_discovery(const smart_discovery_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

smart_discovery::smart_discovery(const smart_discovery_config& config,
                                 http_get_callback http_client)
    : pimpl_(std::make_unique<impl>(config, std::move(http_client))) {}

smart_discovery::~smart_discovery() = default;

smart_discovery::smart_discovery(smart_discovery&&) noexcept = default;
smart_discovery& smart_discovery::operator=(smart_discovery&&) noexcept = default;

auto smart_discovery::discover()
    -> std::expected<smart_configuration, oauth2_error> {
    // Check cache first
    {
        std::lock_guard lock(pimpl_->cache_mutex_);
        if (pimpl_->cached_config_ && pimpl_->config_.cache_enabled) {
            auto age = std::chrono::system_clock::now() - pimpl_->cache_time_;
            if (age < pimpl_->config_.cache_ttl) {
                return *pimpl_->cached_config_;
            }
        }
    }

    return refresh();
}

auto smart_discovery::refresh()
    -> std::expected<smart_configuration, oauth2_error> {
    if (!pimpl_->http_client_) {
        return std::unexpected(oauth2_error::network_error);
    }

    if (!pimpl_->config_.is_valid()) {
        return std::unexpected(oauth2_error::invalid_configuration);
    }

    std::string url = pimpl_->config_.discovery_url();

    auto result = pimpl_->http_client_(url, pimpl_->config_.request_timeout);

    if (!result) {
        return std::unexpected(oauth2_error::discovery_failed);
    }

    auto config = parse_configuration(result.value());
    if (!config) {
        return std::unexpected(config.error());
    }

    // Update cache
    {
        std::lock_guard lock(pimpl_->cache_mutex_);
        pimpl_->cached_config_ = *config;
        pimpl_->cache_time_ = std::chrono::system_clock::now();
    }

    return config;
}

std::optional<smart_configuration> smart_discovery::cached() const noexcept {
    std::lock_guard lock(pimpl_->cache_mutex_);
    return pimpl_->cached_config_;
}

void smart_discovery::clear_cache() noexcept {
    std::lock_guard lock(pimpl_->cache_mutex_);
    pimpl_->cached_config_ = std::nullopt;
}

const smart_discovery_config& smart_discovery::config() const noexcept {
    return pimpl_->config_;
}

void smart_discovery::update_config(const smart_discovery_config& config) {
    std::lock_guard lock(pimpl_->cache_mutex_);

    // Clear cache if URL changed
    if (config.fhir_base_url != pimpl_->config_.fhir_base_url) {
        pimpl_->cached_config_ = std::nullopt;
    }

    pimpl_->config_ = config;
}

auto smart_discovery::parse_configuration(std::string_view json)
    -> std::expected<smart_configuration, oauth2_error> {
    if (json.empty()) {
        return std::unexpected(oauth2_error::invalid_response);
    }

    smart_configuration config;

    // Required fields
    config.token_endpoint = json_get_string(json, "token_endpoint");
    if (config.token_endpoint.empty()) {
        return std::unexpected(oauth2_error::invalid_response);
    }

    // Optional string fields
    config.issuer = json_get_string(json, "issuer");
    config.authorization_endpoint = json_get_string(json, "authorization_endpoint");

    auto jwks = json_get_string(json, "jwks_uri");
    if (!jwks.empty()) {
        config.jwks_uri = std::move(jwks);
    }

    auto revocation = json_get_string(json, "revocation_endpoint");
    if (!revocation.empty()) {
        config.revocation_endpoint = std::move(revocation);
    }

    auto introspection = json_get_string(json, "introspection_endpoint");
    if (!introspection.empty()) {
        config.introspection_endpoint = std::move(introspection);
    }

    auto userinfo = json_get_string(json, "userinfo_endpoint");
    if (!userinfo.empty()) {
        config.userinfo_endpoint = std::move(userinfo);
    }

    auto registration = json_get_string(json, "registration_endpoint");
    if (!registration.empty()) {
        config.registration_endpoint = std::move(registration);
    }

    auto management = json_get_string(json, "management_endpoint");
    if (!management.empty()) {
        config.management_endpoint = std::move(management);
    }

    // Array fields
    config.capabilities = json_get_string_array(json, "capabilities");
    config.scopes_supported = json_get_string_array(json, "scopes_supported");
    config.response_types_supported =
        json_get_string_array(json, "response_types_supported");
    config.grant_types_supported =
        json_get_string_array(json, "grant_types_supported");
    config.code_challenge_methods_supported =
        json_get_string_array(json, "code_challenge_methods_supported");
    config.token_endpoint_auth_methods_supported =
        json_get_string_array(json, "token_endpoint_auth_methods_supported");

    return config;
}

std::string smart_discovery::build_discovery_url(std::string_view fhir_base_url) {
    std::string url{fhir_base_url};
    // Remove trailing slash if present
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url + "/.well-known/smart-configuration";
}

// =============================================================================
// Convenience Functions
// =============================================================================

std::expected<smart_configuration, oauth2_error>
discover_smart_configuration(std::string_view fhir_base_url,
                             std::chrono::seconds timeout) {
    // This would require a default HTTP client implementation
    // For now, return an error indicating no HTTP client
    (void)fhir_base_url;
    (void)timeout;
    return std::unexpected(oauth2_error::network_error);
}

std::expected<smart_configuration, oauth2_error>
discover_smart_configuration(std::string_view fhir_base_url,
                             http_get_callback http_client,
                             std::chrono::seconds timeout) {
    smart_discovery_config config;
    config.fhir_base_url = std::string(fhir_base_url);
    config.request_timeout = timeout;
    config.cache_enabled = false;

    smart_discovery discovery(config, std::move(http_client));
    return discovery.discover();
}

oauth2_config
create_oauth2_config_from_smart(const smart_configuration& smart,
                                std::string_view client_id,
                                std::string_view client_secret,
                                const std::vector<std::string>& scopes) {
    oauth2_config config;
    config.token_url = smart.token_endpoint;
    config.client_id = std::string(client_id);
    config.client_secret = std::string(client_secret);
    config.scopes = scopes;

    if (!smart.authorization_endpoint.empty()) {
        config.authorization_url = smart.authorization_endpoint;
    }

    if (smart.revocation_endpoint) {
        config.revocation_url = *smart.revocation_endpoint;
    }

    return config;
}

}  // namespace pacs::bridge::security
