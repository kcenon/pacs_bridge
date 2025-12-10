#ifndef PACS_BRIDGE_SECURITY_OAUTH2_TYPES_H
#define PACS_BRIDGE_SECURITY_OAUTH2_TYPES_H

/**
 * @file oauth2_types.h
 * @brief OAuth2 type definitions for PACS Bridge EMR integration
 *
 * Provides OAuth2 error codes, token structures, and configuration types
 * for authenticating with external EMR systems using OAuth2 and Smart-on-FHIR.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/103
 * @see https://github.com/kcenon/pacs_bridge/issues/110
 */

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::security {

// =============================================================================
// Error Codes (-1020 to -1039)
// =============================================================================

/**
 * @brief OAuth2 specific error codes
 *
 * Allocated range: -1020 to -1039
 * @see docs/SDS_COMPONENTS.md for error code allocation
 */
enum class oauth2_error : int {
    /** Token request to authorization server failed */
    token_request_failed = -1020,

    /** Invalid client credentials (client_id or client_secret) */
    invalid_credentials = -1021,

    /** Access token has expired */
    token_expired = -1022,

    /** Token refresh failed */
    refresh_failed = -1023,

    /** Requested scope was denied by the authorization server */
    scope_denied = -1024,

    /** Smart-on-FHIR discovery endpoint request failed */
    discovery_failed = -1025,

    /** Invalid or malformed response from authorization server */
    invalid_response = -1026,

    /** Network error during OAuth2 request */
    network_error = -1027,

    /** Access token is invalid or malformed */
    invalid_token = -1028,

    /** Access token has been revoked */
    token_revoked = -1029,

    /** Invalid OAuth2 configuration */
    invalid_configuration = -1030,

    /** Missing required OAuth2 parameter */
    missing_parameter = -1031,

    /** Unsupported grant type */
    unsupported_grant_type = -1032,

    /** Authorization server error response */
    server_error = -1033,

    /** Request timeout */
    timeout = -1034
};

/**
 * @brief Convert oauth2_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(oauth2_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of OAuth2 error
 */
[[nodiscard]] constexpr const char* to_string(oauth2_error error) noexcept {
    switch (error) {
        case oauth2_error::token_request_failed:
            return "Token request to authorization server failed";
        case oauth2_error::invalid_credentials:
            return "Invalid client credentials";
        case oauth2_error::token_expired:
            return "Access token has expired";
        case oauth2_error::refresh_failed:
            return "Token refresh failed";
        case oauth2_error::scope_denied:
            return "Requested scope was denied";
        case oauth2_error::discovery_failed:
            return "Smart-on-FHIR discovery failed";
        case oauth2_error::invalid_response:
            return "Invalid response from authorization server";
        case oauth2_error::network_error:
            return "Network error during OAuth2 request";
        case oauth2_error::invalid_token:
            return "Access token is invalid or malformed";
        case oauth2_error::token_revoked:
            return "Access token has been revoked";
        case oauth2_error::invalid_configuration:
            return "Invalid OAuth2 configuration";
        case oauth2_error::missing_parameter:
            return "Missing required OAuth2 parameter";
        case oauth2_error::unsupported_grant_type:
            return "Unsupported grant type";
        case oauth2_error::server_error:
            return "Authorization server error";
        case oauth2_error::timeout:
            return "OAuth2 request timeout";
        default:
            return "Unknown OAuth2 error";
    }
}

// =============================================================================
// Grant Types
// =============================================================================

/**
 * @brief OAuth2 grant types supported
 */
enum class oauth2_grant_type {
    /** Client credentials grant (machine-to-machine) */
    client_credentials,

    /** Authorization code grant (user-delegated, future) */
    authorization_code,

    /** Refresh token grant */
    refresh_token
};

/**
 * @brief Convert grant type to string for OAuth2 requests
 */
[[nodiscard]] constexpr const char* to_string(oauth2_grant_type grant) noexcept {
    switch (grant) {
        case oauth2_grant_type::client_credentials:
            return "client_credentials";
        case oauth2_grant_type::authorization_code:
            return "authorization_code";
        case oauth2_grant_type::refresh_token:
            return "refresh_token";
        default:
            return "unknown";
    }
}

// =============================================================================
// Token Structure
// =============================================================================

/**
 * @brief OAuth2 access token representation
 *
 * Contains the access token and associated metadata from the OAuth2 server.
 * Provides helper methods for checking token expiration.
 *
 * @example Token Usage
 * ```cpp
 * oauth2_token token;
 * token.access_token = "eyJhbGciOiJSUzI1NiIs...";
 * token.token_type = "Bearer";
 * token.expires_in = std::chrono::seconds{3600};
 * token.issued_at = std::chrono::system_clock::now();
 *
 * if (token.is_expired()) {
 *     // Need to refresh token
 * }
 *
 * if (token.needs_refresh(std::chrono::seconds{60})) {
 *     // Token expires soon, proactively refresh
 * }
 * ```
 */
struct oauth2_token {
    /** The access token string */
    std::string access_token;

    /** Token type, typically "Bearer" */
    std::string token_type{"Bearer"};

    /** Token lifetime duration */
    std::chrono::seconds expires_in{0};

    /** Optional refresh token for token renewal */
    std::optional<std::string> refresh_token;

    /** Scopes granted by the authorization server */
    std::vector<std::string> scopes;

    /** Timestamp when the token was issued */
    std::chrono::system_clock::time_point issued_at;

    /** Optional ID token (for OIDC flows) */
    std::optional<std::string> id_token;

    /**
     * @brief Check if the token has expired
     * @return true if the token is expired
     */
    [[nodiscard]] bool is_expired() const noexcept {
        if (expires_in.count() == 0) {
            return false;  // No expiration set
        }
        auto now = std::chrono::system_clock::now();
        return now >= (issued_at + expires_in);
    }

    /**
     * @brief Check if the token needs refresh within a margin
     *
     * Used for proactive token refresh before actual expiration.
     *
     * @param margin Time margin before expiration to trigger refresh
     * @return true if token expires within the margin
     */
    [[nodiscard]] bool needs_refresh(std::chrono::seconds margin) const noexcept {
        if (expires_in.count() == 0) {
            return false;  // No expiration set
        }
        auto now = std::chrono::system_clock::now();
        auto expiry_time = issued_at + expires_in;
        return now >= (expiry_time - margin);
    }

    /**
     * @brief Get remaining validity duration
     * @return Remaining time until expiration (may be negative if expired)
     */
    [[nodiscard]] std::chrono::seconds remaining_validity() const noexcept {
        if (expires_in.count() == 0) {
            return std::chrono::seconds::max();  // No expiration
        }
        auto now = std::chrono::system_clock::now();
        auto expiry_time = issued_at + expires_in;
        return std::chrono::duration_cast<std::chrono::seconds>(expiry_time - now);
    }

    /**
     * @brief Check if the token is valid (non-empty and not expired)
     * @return true if token can be used for authorization
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !access_token.empty() && !is_expired();
    }

    /**
     * @brief Get the Authorization header value
     * @return Full header value (e.g., "Bearer eyJhbGci...")
     */
    [[nodiscard]] std::string authorization_header() const {
        return token_type + " " + access_token;
    }
};

// =============================================================================
// Configuration Structure
// =============================================================================

/**
 * @brief OAuth2 client configuration
 *
 * Contains all settings needed for OAuth2 authentication with an EMR system.
 * Supports client credentials flow for backend service authentication.
 *
 * @example Client Credentials Configuration
 * ```cpp
 * oauth2_config config;
 * config.token_url = "https://emr.hospital.local/oauth/token";
 * config.client_id = "pacs_bridge_client";
 * config.client_secret = "secret_from_env";
 * config.scopes = {"patient/*.read", "patient/DiagnosticReport.write"};
 * config.token_refresh_margin = std::chrono::seconds{60};
 *
 * if (!config.is_valid()) {
 *     // Handle invalid configuration
 * }
 * ```
 */
struct oauth2_config {
    /** OAuth2 token endpoint URL */
    std::string token_url;

    /** Client identifier */
    std::string client_id;

    /** Client secret (should be from secure storage) */
    std::string client_secret;

    /** Requested OAuth2 scopes */
    std::vector<std::string> scopes;

    /**
     * @brief Time margin before token expiration to trigger refresh
     *
     * The client will proactively refresh tokens this many seconds
     * before they expire to avoid request failures.
     */
    std::chrono::seconds token_refresh_margin{60};

    /** HTTP request timeout for token operations */
    std::chrono::seconds request_timeout{30};

    /** Maximum number of retry attempts for failed requests */
    size_t max_retries{3};

    /** Initial backoff duration for retries */
    std::chrono::milliseconds retry_backoff{1000};

    /** Optional: Authorization endpoint (for authorization code flow) */
    std::optional<std::string> authorization_url;

    /** Optional: Token revocation endpoint */
    std::optional<std::string> revocation_url;

    /**
     * @brief Validate the configuration
     * @return true if all required fields are present and valid
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (token_url.empty()) return false;
        if (client_id.empty()) return false;
        if (client_secret.empty()) return false;
        if (request_timeout.count() <= 0) return false;
        return true;
    }

    /**
     * @brief Get scopes as space-separated string for OAuth2 requests
     * @return Scopes string (e.g., "patient/*.read patient/*.write")
     */
    [[nodiscard]] std::string scopes_string() const {
        std::string result;
        for (size_t i = 0; i < scopes.size(); ++i) {
            if (i > 0) result += " ";
            result += scopes[i];
        }
        return result;
    }
};

// =============================================================================
// Authentication Type
// =============================================================================

/**
 * @brief Authentication method type
 *
 * Determines which authentication method to use for EMR connections.
 */
enum class auth_type {
    /** No authentication */
    none,

    /** HTTP Basic authentication */
    basic,

    /** OAuth2 authentication */
    oauth2,

    /** API key authentication */
    api_key
};

/**
 * @brief Convert auth_type to string representation
 */
[[nodiscard]] constexpr const char* to_string(auth_type type) noexcept {
    switch (type) {
        case auth_type::none:
            return "none";
        case auth_type::basic:
            return "basic";
        case auth_type::oauth2:
            return "oauth2";
        case auth_type::api_key:
            return "api_key";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse auth_type from string
 * @param str Type string ("none", "basic", "oauth2", "api_key")
 * @return Parsed type or nullopt if invalid
 */
[[nodiscard]] inline std::optional<auth_type>
parse_auth_type(std::string_view str) noexcept {
    if (str == "none" || str == "false" || str == "0") {
        return auth_type::none;
    }
    if (str == "basic" || str == "Basic") {
        return auth_type::basic;
    }
    if (str == "oauth2" || str == "OAuth2" || str == "oauth") {
        return auth_type::oauth2;
    }
    if (str == "api_key" || str == "apikey" || str == "ApiKey") {
        return auth_type::api_key;
    }
    return std::nullopt;
}

// =============================================================================
// Token Response (for parsing server responses)
// =============================================================================

/**
 * @brief Raw token response from OAuth2 server
 *
 * Used for parsing JSON responses from the token endpoint.
 * Convert to oauth2_token after validation.
 */
struct token_response {
    std::string access_token;
    std::string token_type;
    int64_t expires_in{0};
    std::optional<std::string> refresh_token;
    std::optional<std::string> scope;
    std::optional<std::string> id_token;

    /** Error fields (for error responses) */
    std::optional<std::string> error;
    std::optional<std::string> error_description;

    /**
     * @brief Check if this is an error response
     */
    [[nodiscard]] bool is_error() const noexcept {
        return error.has_value();
    }

    /**
     * @brief Convert to oauth2_token
     */
    [[nodiscard]] oauth2_token to_token() const {
        oauth2_token token;
        token.access_token = access_token;
        token.token_type = token_type.empty() ? "Bearer" : token_type;
        token.expires_in = std::chrono::seconds{expires_in};
        token.refresh_token = refresh_token;
        token.issued_at = std::chrono::system_clock::now();
        token.id_token = id_token;

        // Parse scope string to vector
        if (scope.has_value()) {
            std::string_view sv = scope.value();
            size_t start = 0;
            while (start < sv.size()) {
                size_t end = sv.find(' ', start);
                if (end == std::string_view::npos) {
                    token.scopes.emplace_back(sv.substr(start));
                    break;
                }
                token.scopes.emplace_back(sv.substr(start, end - start));
                start = end + 1;
            }
        }

        return token;
    }
};

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_OAUTH2_TYPES_H
