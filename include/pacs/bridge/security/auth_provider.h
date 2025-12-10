#ifndef PACS_BRIDGE_SECURITY_AUTH_PROVIDER_H
#define PACS_BRIDGE_SECURITY_AUTH_PROVIDER_H

/**
 * @file auth_provider.h
 * @brief Abstract authentication provider interface for PACS Bridge
 *
 * Defines a common interface for different authentication methods
 * (OAuth2, Basic Auth, API Key) used for EMR system connections.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/103
 * @see https://github.com/kcenon/pacs_bridge/issues/114
 */

#include "oauth2_types.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::security {

/**
 * @brief Abstract interface for authentication providers
 *
 * Provides a common interface for different authentication methods.
 * All authentication providers must implement this interface to be
 * used with EMR client connections.
 *
 * @example Custom Auth Provider
 * ```cpp
 * class my_auth_provider : public auth_provider {
 * public:
 *     auto get_authorization_header()
 *         -> std::expected<std::string, oauth2_error> override {
 *         return "Bearer " + get_token();
 *     }
 *
 *     bool is_authenticated() const noexcept override {
 *         return has_valid_token();
 *     }
 *
 *     std::string_view auth_type() const noexcept override {
 *         return "custom";
 *     }
 * };
 * ```
 */
class auth_provider {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~auth_provider() = default;

    // Non-copyable
    auth_provider(const auth_provider&) = delete;
    auth_provider& operator=(const auth_provider&) = delete;

    // Movable
    auth_provider(auth_provider&&) noexcept = default;
    auth_provider& operator=(auth_provider&&) noexcept = default;

    // =========================================================================
    // Core Interface
    // =========================================================================

    /**
     * @brief Get the Authorization header value
     *
     * Returns the full value for the HTTP Authorization header.
     * For OAuth2, this would be "Bearer <token>".
     * For Basic auth, this would be "Basic <base64>".
     *
     * @return Authorization header value or error
     */
    [[nodiscard]] virtual auto get_authorization_header()
        -> std::expected<std::string, oauth2_error> = 0;

    /**
     * @brief Check if currently authenticated
     *
     * Returns whether valid credentials are available.
     * For OAuth2, this checks if a valid (non-expired) token exists.
     *
     * @return true if authenticated and credentials are valid
     */
    [[nodiscard]] virtual bool is_authenticated() const noexcept = 0;

    /**
     * @brief Get the authentication type identifier
     *
     * Returns a string identifying the authentication method.
     * Examples: "oauth2", "basic", "api_key", "none"
     *
     * @return Authentication type string
     */
    [[nodiscard]] virtual std::string_view auth_type() const noexcept = 0;

    // =========================================================================
    // Optional Operations
    // =========================================================================

    /**
     * @brief Refresh credentials if supported
     *
     * For OAuth2, this refreshes the access token.
     * For other methods, this may be a no-op.
     *
     * @return Success or error
     */
    [[nodiscard]] virtual auto refresh()
        -> std::expected<void, oauth2_error> {
        // Default implementation: no-op (success)
        return {};
    }

    /**
     * @brief Invalidate current credentials
     *
     * Clears cached tokens or credentials.
     * Next call to get_authorization_header() will require re-authentication.
     */
    virtual void invalidate() noexcept {
        // Default implementation: no-op
    }

    /**
     * @brief Check if credentials can be refreshed
     *
     * @return true if refresh() is supported and may succeed
     */
    [[nodiscard]] virtual bool can_refresh() const noexcept {
        return false;
    }

protected:
    /**
     * @brief Protected default constructor
     */
    auth_provider() = default;
};

/**
 * @brief No-op authentication provider (no auth)
 *
 * Used when no authentication is required.
 * Always returns empty authorization header.
 */
class no_auth_provider final : public auth_provider {
public:
    no_auth_provider() = default;

    [[nodiscard]] auto get_authorization_header()
        -> std::expected<std::string, oauth2_error> override {
        return std::string{};
    }

    [[nodiscard]] bool is_authenticated() const noexcept override {
        return true;  // No auth needed
    }

    [[nodiscard]] std::string_view auth_type() const noexcept override {
        return "none";
    }
};

/**
 * @brief Factory function type for creating auth providers
 */
using auth_provider_factory = std::unique_ptr<auth_provider>(*)();

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_AUTH_PROVIDER_H
