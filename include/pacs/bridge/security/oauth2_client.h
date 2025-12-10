#ifndef PACS_BRIDGE_SECURITY_OAUTH2_CLIENT_H
#define PACS_BRIDGE_SECURITY_OAUTH2_CLIENT_H

/**
 * @file oauth2_client.h
 * @brief OAuth2 client for PACS Bridge EMR integration
 *
 * Provides OAuth2 authentication with support for client credentials
 * grant type. Handles token acquisition, caching, and automatic refresh.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/103
 * @see https://github.com/kcenon/pacs_bridge/issues/111
 * @see RFC 6749 - The OAuth 2.0 Authorization Framework
 */

#include "auth_provider.h"
#include "oauth2_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>

namespace pacs::bridge::security {

/**
 * @brief HTTP client callback type for OAuth2 requests
 *
 * Allows injection of HTTP client for token requests.
 * Returns JSON response body or empty string on failure.
 */
using http_post_callback = std::function<std::expected<std::string, oauth2_error>(
    std::string_view url,
    std::string_view content_type,
    std::string_view body,
    std::chrono::seconds timeout
)>;

/**
 * @brief OAuth2 client for token management
 *
 * Handles OAuth2 authentication using the client credentials grant type.
 * Provides automatic token caching and refresh functionality.
 *
 * @example Client Credentials Flow
 * ```cpp
 * oauth2_config config;
 * config.token_url = "https://emr.hospital.local/oauth/token";
 * config.client_id = "pacs_bridge";
 * config.client_secret = get_secret();
 * config.scopes = {"patient/*.read"};
 *
 * oauth2_client client(config);
 *
 * // Get access token (fetches new token if needed)
 * auto result = client.get_access_token();
 * if (result) {
 *     auto token = result.value();
 *     // Use token for API calls
 * }
 *
 * // Token is automatically cached and refreshed
 * auto result2 = client.get_access_token();  // Uses cached token
 * ```
 *
 * @example With Custom HTTP Client
 * ```cpp
 * auto http_client = [](auto url, auto ct, auto body, auto timeout)
 *     -> std::expected<std::string, oauth2_error> {
 *     // Custom HTTP implementation
 *     return json_response;
 * };
 *
 * oauth2_client client(config, http_client);
 * ```
 */
class oauth2_client {
public:
    /**
     * @brief Construct OAuth2 client with configuration
     * @param config OAuth2 configuration
     */
    explicit oauth2_client(const oauth2_config& config);

    /**
     * @brief Construct OAuth2 client with custom HTTP client
     * @param config OAuth2 configuration
     * @param http_client HTTP POST callback for token requests
     */
    oauth2_client(const oauth2_config& config, http_post_callback http_client);

    /**
     * @brief Destructor
     */
    ~oauth2_client();

    // Non-copyable
    oauth2_client(const oauth2_client&) = delete;
    oauth2_client& operator=(const oauth2_client&) = delete;

    // Movable
    oauth2_client(oauth2_client&&) noexcept;
    oauth2_client& operator=(oauth2_client&&) noexcept;

    // =========================================================================
    // Token Operations
    // =========================================================================

    /**
     * @brief Get valid access token
     *
     * Returns the current access token if valid, otherwise fetches a new one.
     * Automatically refreshes the token if it's about to expire.
     *
     * @return Access token string or error
     */
    [[nodiscard]] auto get_access_token()
        -> std::expected<std::string, oauth2_error>;

    /**
     * @brief Get the full token object
     *
     * Returns the complete token including metadata like expiration.
     *
     * @return Token object or error
     */
    [[nodiscard]] auto get_token()
        -> std::expected<oauth2_token, oauth2_error>;

    /**
     * @brief Force token refresh
     *
     * Fetches a new token even if the current one is still valid.
     * Useful when a token has been revoked.
     *
     * @return Success or error
     */
    [[nodiscard]] auto refresh_token()
        -> std::expected<void, oauth2_error>;

    /**
     * @brief Revoke the current token
     *
     * Notifies the authorization server to revoke the token.
     * Only works if revocation endpoint is configured.
     *
     * @return Success or error
     */
    [[nodiscard]] auto revoke_token()
        -> std::expected<void, oauth2_error>;

    // =========================================================================
    // Status
    // =========================================================================

    /**
     * @brief Check if currently authenticated
     *
     * Returns true if a valid (non-expired) token is cached.
     *
     * @return true if authenticated
     */
    [[nodiscard]] bool is_authenticated() const noexcept;

    /**
     * @brief Check if token needs refresh
     *
     * Returns true if the token will expire within the configured margin.
     *
     * @return true if refresh is needed
     */
    [[nodiscard]] bool needs_refresh() const noexcept;

    /**
     * @brief Invalidate cached token
     *
     * Clears the cached token. Next call to get_access_token() will
     * fetch a new token.
     */
    void invalidate() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get the current configuration
     * @return Configuration reference
     */
    [[nodiscard]] const oauth2_config& config() const noexcept;

    /**
     * @brief Update configuration
     *
     * Invalidates cached token if critical settings changed.
     *
     * @param config New configuration
     */
    void update_config(const oauth2_config& config);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;

    // Private helper functions (called with lock held)
    [[nodiscard]] auto fetch_token_internal(std::unique_lock<std::shared_mutex>&)
        -> std::expected<void, oauth2_error>;
    [[nodiscard]] auto refresh_token_internal(std::unique_lock<std::shared_mutex>&)
        -> std::expected<void, oauth2_error>;
};

/**
 * @brief OAuth2 authentication provider adapter
 *
 * Wraps oauth2_client to provide the auth_provider interface.
 *
 * @example Usage with EMR Client
 * ```cpp
 * oauth2_config config = load_oauth_config();
 * auto auth = std::make_unique<oauth2_auth_provider>(config);
 *
 * emr_client client(connection_config, std::move(auth));
 * ```
 */
class oauth2_auth_provider final : public auth_provider {
public:
    /**
     * @brief Construct from OAuth2 configuration
     * @param config OAuth2 configuration
     */
    explicit oauth2_auth_provider(const oauth2_config& config);

    /**
     * @brief Construct from existing OAuth2 client
     * @param client Shared OAuth2 client
     */
    explicit oauth2_auth_provider(std::shared_ptr<oauth2_client> client);

    /**
     * @brief Destructor
     */
    ~oauth2_auth_provider();

    // Movable
    oauth2_auth_provider(oauth2_auth_provider&&) noexcept;
    oauth2_auth_provider& operator=(oauth2_auth_provider&&) noexcept;

    /**
     * @brief Get Authorization header
     * @return "Bearer <token>" or error
     */
    [[nodiscard]] auto get_authorization_header()
        -> std::expected<std::string, oauth2_error> override;

    /**
     * @brief Check if authenticated
     * @return true if valid token exists
     */
    [[nodiscard]] bool is_authenticated() const noexcept override;

    /**
     * @brief Get auth type
     * @return "oauth2"
     */
    [[nodiscard]] std::string_view auth_type() const noexcept override;

    /**
     * @brief Refresh token
     * @return Success or error
     */
    [[nodiscard]] auto refresh()
        -> std::expected<void, oauth2_error> override;

    /**
     * @brief Invalidate cached token
     */
    void invalidate() noexcept override;

    /**
     * @brief Check if refresh is supported
     * @return true (OAuth2 supports refresh)
     */
    [[nodiscard]] bool can_refresh() const noexcept override;

    /**
     * @brief Get the underlying OAuth2 client
     * @return Shared pointer to OAuth2 client
     */
    [[nodiscard]] std::shared_ptr<oauth2_client> client() const noexcept;

private:
    std::shared_ptr<oauth2_client> client_;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Parse token response JSON
 *
 * Parses the JSON response from an OAuth2 token endpoint.
 *
 * @param json JSON response string
 * @return Parsed token response or error
 */
[[nodiscard]] std::expected<token_response, oauth2_error>
parse_token_response(std::string_view json);

/**
 * @brief Build token request body
 *
 * Creates URL-encoded body for client credentials grant.
 *
 * @param config OAuth2 configuration
 * @return Request body string
 */
[[nodiscard]] std::string
build_token_request_body(const oauth2_config& config);

/**
 * @brief Build refresh token request body
 *
 * Creates URL-encoded body for refresh token grant.
 *
 * @param config OAuth2 configuration
 * @param refresh_token Refresh token string
 * @return Request body string
 */
[[nodiscard]] std::string
build_refresh_request_body(const oauth2_config& config,
                           std::string_view refresh_token);

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_OAUTH2_CLIENT_H
