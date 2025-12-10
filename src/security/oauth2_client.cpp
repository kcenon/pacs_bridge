/**
 * @file oauth2_client.cpp
 * @brief OAuth2 client implementation for PACS Bridge
 *
 * Implements OAuth2 client credentials flow with token caching
 * and automatic refresh for EMR system authentication.
 *
 * @see include/pacs/bridge/security/oauth2_client.h
 */

#include "pacs/bridge/security/oauth2_client.h"

#include <iomanip>
#include <mutex>
#include <shared_mutex>
#include <sstream>

namespace pacs::bridge::security {

// =============================================================================
// URL Encoding Utility
// =============================================================================

namespace {

/**
 * @brief URL encode a string for form data
 */
std::string url_encode(std::string_view str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << std::uppercase;
            encoded << '%' << std::setw(2)
                    << static_cast<int>(static_cast<unsigned char>(c));
            encoded << std::nouppercase;
        }
    }

    return encoded.str();
}

/**
 * @brief Simple JSON string extraction
 * @param json JSON string
 * @param key Key to find
 * @return Value or empty if not found
 */
std::string json_get_string(std::string_view json, std::string_view key) {
    std::string search_key = "\"" + std::string(key) + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string_view::npos) {
        return {};
    }

    // Find the colon after the key
    pos = json.find(':', pos + search_key.length());
    if (pos == std::string_view::npos) {
        return {};
    }

    // Skip whitespace
    pos++;
    while (pos < json.size() && std::isspace(json[pos])) {
        pos++;
    }

    if (pos >= json.size()) {
        return {};
    }

    // Check if it's a string value
    if (json[pos] == '"') {
        pos++;  // Skip opening quote
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;  // Skip escape character
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

    return {};
}

/**
 * @brief Simple JSON integer extraction
 */
int64_t json_get_int(std::string_view json, std::string_view key) {
    std::string search_key = "\"" + std::string(key) + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string_view::npos) {
        return 0;
    }

    pos = json.find(':', pos + search_key.length());
    if (pos == std::string_view::npos) {
        return 0;
    }

    pos++;
    while (pos < json.size() && std::isspace(json[pos])) {
        pos++;
    }

    if (pos >= json.size() || (!std::isdigit(json[pos]) && json[pos] != '-')) {
        return 0;
    }

    std::string num_str;
    while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '-')) {
        num_str += json[pos];
        pos++;
    }

    try {
        return std::stoll(num_str);
    } catch (...) {
        return 0;
    }
}

}  // namespace

// =============================================================================
// Token Response Parsing
// =============================================================================

std::expected<token_response, oauth2_error>
parse_token_response(std::string_view json) {
    if (json.empty()) {
        return std::unexpected(oauth2_error::invalid_response);
    }

    token_response response;

    // Check for error response first
    response.error = json_get_string(json, "error");
    if (!response.error->empty()) {
        response.error_description = json_get_string(json, "error_description");

        // Map OAuth2 error codes to our error enum
        if (*response.error == "invalid_client" ||
            *response.error == "invalid_grant") {
            return std::unexpected(oauth2_error::invalid_credentials);
        }
        if (*response.error == "invalid_scope") {
            return std::unexpected(oauth2_error::scope_denied);
        }
        if (*response.error == "unauthorized_client") {
            return std::unexpected(oauth2_error::invalid_credentials);
        }
        if (*response.error == "server_error") {
            return std::unexpected(oauth2_error::server_error);
        }

        return std::unexpected(oauth2_error::token_request_failed);
    }
    response.error = std::nullopt;

    // Parse successful response
    response.access_token = json_get_string(json, "access_token");
    if (response.access_token.empty()) {
        return std::unexpected(oauth2_error::invalid_response);
    }

    response.token_type = json_get_string(json, "token_type");
    response.expires_in = json_get_int(json, "expires_in");

    auto refresh = json_get_string(json, "refresh_token");
    if (!refresh.empty()) {
        response.refresh_token = std::move(refresh);
    }

    auto scope = json_get_string(json, "scope");
    if (!scope.empty()) {
        response.scope = std::move(scope);
    }

    auto id_token = json_get_string(json, "id_token");
    if (!id_token.empty()) {
        response.id_token = std::move(id_token);
    }

    return response;
}

// =============================================================================
// Request Body Building
// =============================================================================

std::string build_token_request_body(const oauth2_config& config) {
    std::ostringstream body;
    body << "grant_type=client_credentials";
    body << "&client_id=" << url_encode(config.client_id);
    body << "&client_secret=" << url_encode(config.client_secret);

    if (!config.scopes.empty()) {
        body << "&scope=" << url_encode(config.scopes_string());
    }

    return body.str();
}

std::string build_refresh_request_body(const oauth2_config& config,
                                       std::string_view refresh_token) {
    std::ostringstream body;
    body << "grant_type=refresh_token";
    body << "&refresh_token=" << url_encode(refresh_token);
    body << "&client_id=" << url_encode(config.client_id);
    body << "&client_secret=" << url_encode(config.client_secret);

    return body.str();
}

// =============================================================================
// OAuth2 Client Implementation
// =============================================================================

class oauth2_client::impl {
public:
    explicit impl(const oauth2_config& config) : config_(config) {}

    impl(const oauth2_config& config, http_post_callback http_client)
        : config_(config), http_client_(std::move(http_client)) {}

    oauth2_config config_;
    http_post_callback http_client_;

    mutable std::shared_mutex token_mutex_;
    std::optional<oauth2_token> cached_token_;
};

oauth2_client::oauth2_client(const oauth2_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

oauth2_client::oauth2_client(const oauth2_config& config,
                             http_post_callback http_client)
    : pimpl_(std::make_unique<impl>(config, std::move(http_client))) {}

oauth2_client::~oauth2_client() = default;

oauth2_client::oauth2_client(oauth2_client&&) noexcept = default;
oauth2_client& oauth2_client::operator=(oauth2_client&&) noexcept = default;

auto oauth2_client::get_access_token()
    -> std::expected<std::string, oauth2_error> {
    auto token_result = get_token();
    if (!token_result) {
        return std::unexpected(token_result.error());
    }
    return token_result->access_token;
}

auto oauth2_client::get_token()
    -> std::expected<oauth2_token, oauth2_error> {
    // Check if we have a valid cached token
    {
        std::shared_lock lock(pimpl_->token_mutex_);
        if (pimpl_->cached_token_ &&
            !pimpl_->cached_token_->needs_refresh(pimpl_->config_.token_refresh_margin)) {
            return *pimpl_->cached_token_;
        }
    }

    // Need to fetch or refresh token
    std::unique_lock lock(pimpl_->token_mutex_);

    // Double-check after acquiring exclusive lock
    if (pimpl_->cached_token_ &&
        !pimpl_->cached_token_->needs_refresh(pimpl_->config_.token_refresh_margin)) {
        return *pimpl_->cached_token_;
    }

    // Try to refresh if we have a refresh token
    if (pimpl_->cached_token_ && pimpl_->cached_token_->refresh_token) {
        auto refresh_result = refresh_token_internal(lock);
        if (refresh_result) {
            return *pimpl_->cached_token_;
        }
        // Refresh failed, fall through to fetch new token
    }

    // Fetch new token
    auto fetch_result = fetch_token_internal(lock);
    if (!fetch_result) {
        return std::unexpected(fetch_result.error());
    }

    return *pimpl_->cached_token_;
}

auto oauth2_client::refresh_token()
    -> std::expected<void, oauth2_error> {
    std::unique_lock lock(pimpl_->token_mutex_);
    return refresh_token_internal(lock);
}

auto oauth2_client::revoke_token()
    -> std::expected<void, oauth2_error> {
    if (!pimpl_->config_.revocation_url) {
        // No revocation endpoint configured
        invalidate();
        return {};
    }

    std::unique_lock lock(pimpl_->token_mutex_);

    if (!pimpl_->cached_token_) {
        return {};  // Nothing to revoke
    }

    if (!pimpl_->http_client_) {
        invalidate();
        return {};
    }

    // Build revocation request
    std::ostringstream body;
    body << "token=" << url_encode(pimpl_->cached_token_->access_token);
    body << "&token_type_hint=access_token";
    body << "&client_id=" << url_encode(pimpl_->config_.client_id);
    body << "&client_secret=" << url_encode(pimpl_->config_.client_secret);

    auto result = pimpl_->http_client_(
        *pimpl_->config_.revocation_url,
        "application/x-www-form-urlencoded",
        body.str(),
        pimpl_->config_.request_timeout
    );

    // Clear cached token regardless of result
    pimpl_->cached_token_ = std::nullopt;

    if (!result) {
        return std::unexpected(result.error());
    }

    return {};
}

bool oauth2_client::is_authenticated() const noexcept {
    std::shared_lock lock(pimpl_->token_mutex_);
    return pimpl_->cached_token_ && pimpl_->cached_token_->is_valid();
}

bool oauth2_client::needs_refresh() const noexcept {
    std::shared_lock lock(pimpl_->token_mutex_);
    if (!pimpl_->cached_token_) {
        return true;
    }
    return pimpl_->cached_token_->needs_refresh(
        pimpl_->config_.token_refresh_margin);
}

void oauth2_client::invalidate() noexcept {
    std::unique_lock lock(pimpl_->token_mutex_);
    pimpl_->cached_token_ = std::nullopt;
}

const oauth2_config& oauth2_client::config() const noexcept {
    return pimpl_->config_;
}

void oauth2_client::update_config(const oauth2_config& config) {
    std::unique_lock lock(pimpl_->token_mutex_);

    // Invalidate token if critical settings changed
    if (config.token_url != pimpl_->config_.token_url ||
        config.client_id != pimpl_->config_.client_id ||
        config.client_secret != pimpl_->config_.client_secret) {
        pimpl_->cached_token_ = std::nullopt;
    }

    pimpl_->config_ = config;
}

// Private helper: fetch new token (called with lock held)
std::expected<void, oauth2_error>
oauth2_client::fetch_token_internal(std::unique_lock<std::shared_mutex>&) {
    if (!pimpl_->http_client_) {
        return std::unexpected(oauth2_error::network_error);
    }

    if (!pimpl_->config_.is_valid()) {
        return std::unexpected(oauth2_error::invalid_configuration);
    }

    auto body = build_token_request_body(pimpl_->config_);

    auto result = pimpl_->http_client_(
        pimpl_->config_.token_url,
        "application/x-www-form-urlencoded",
        body,
        pimpl_->config_.request_timeout
    );

    if (!result) {
        return std::unexpected(result.error());
    }

    auto response = parse_token_response(result.value());
    if (!response) {
        return std::unexpected(response.error());
    }

    pimpl_->cached_token_ = response->to_token();
    return {};
}

// Private helper: refresh token (called with lock held)
std::expected<void, oauth2_error>
oauth2_client::refresh_token_internal(std::unique_lock<std::shared_mutex>&) {
    if (!pimpl_->http_client_) {
        return std::unexpected(oauth2_error::network_error);
    }

    if (!pimpl_->cached_token_ || !pimpl_->cached_token_->refresh_token) {
        return std::unexpected(oauth2_error::refresh_failed);
    }

    auto body = build_refresh_request_body(
        pimpl_->config_,
        *pimpl_->cached_token_->refresh_token
    );

    auto result = pimpl_->http_client_(
        pimpl_->config_.token_url,
        "application/x-www-form-urlencoded",
        body,
        pimpl_->config_.request_timeout
    );

    if (!result) {
        return std::unexpected(oauth2_error::refresh_failed);
    }

    auto response = parse_token_response(result.value());
    if (!response) {
        return std::unexpected(oauth2_error::refresh_failed);
    }

    auto new_token = response->to_token();

    // Preserve refresh token if not returned in response
    if (!new_token.refresh_token && pimpl_->cached_token_->refresh_token) {
        new_token.refresh_token = pimpl_->cached_token_->refresh_token;
    }

    pimpl_->cached_token_ = std::move(new_token);
    return {};
}

// =============================================================================
// OAuth2 Auth Provider Implementation
// =============================================================================

oauth2_auth_provider::oauth2_auth_provider(const oauth2_config& config)
    : client_(std::make_shared<oauth2_client>(config)) {}

oauth2_auth_provider::oauth2_auth_provider(std::shared_ptr<oauth2_client> client)
    : client_(std::move(client)) {}

oauth2_auth_provider::~oauth2_auth_provider() = default;

oauth2_auth_provider::oauth2_auth_provider(oauth2_auth_provider&&) noexcept = default;
oauth2_auth_provider& oauth2_auth_provider::operator=(oauth2_auth_provider&&) noexcept = default;

auto oauth2_auth_provider::get_authorization_header()
    -> std::expected<std::string, oauth2_error> {
    auto token_result = client_->get_token();
    if (!token_result) {
        return std::unexpected(token_result.error());
    }
    return token_result->authorization_header();
}

bool oauth2_auth_provider::is_authenticated() const noexcept {
    return client_->is_authenticated();
}

std::string_view oauth2_auth_provider::auth_type() const noexcept {
    return "oauth2";
}

auto oauth2_auth_provider::refresh()
    -> std::expected<void, oauth2_error> {
    return client_->refresh_token();
}

void oauth2_auth_provider::invalidate() noexcept {
    client_->invalidate();
}

bool oauth2_auth_provider::can_refresh() const noexcept {
    return true;
}

std::shared_ptr<oauth2_client> oauth2_auth_provider::client() const noexcept {
    return client_;
}

}  // namespace pacs::bridge::security
