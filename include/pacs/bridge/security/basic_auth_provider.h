#ifndef PACS_BRIDGE_SECURITY_BASIC_AUTH_PROVIDER_H
#define PACS_BRIDGE_SECURITY_BASIC_AUTH_PROVIDER_H

/**
 * @file basic_auth_provider.h
 * @brief HTTP Basic authentication provider for PACS Bridge
 *
 * Provides HTTP Basic authentication support as a fallback for
 * EMR systems that don't support OAuth2.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/103
 * @see https://github.com/kcenon/pacs_bridge/issues/114
 * @see RFC 7617 - The 'Basic' HTTP Authentication Scheme
 */

#include "auth_provider.h"

#include <string>
#include <string_view>

namespace pacs::bridge::security {

/**
 * @brief Configuration for Basic authentication
 */
struct basic_auth_config {
    /** Username for authentication */
    std::string username;

    /** Password for authentication (should be from secure storage) */
    std::string password;

    /**
     * @brief Validate configuration
     * @return true if username and password are non-empty
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !username.empty() && !password.empty();
    }
};

/**
 * @brief HTTP Basic authentication provider
 *
 * Implements HTTP Basic authentication by encoding username:password
 * in Base64 format for the Authorization header.
 *
 * @warning Basic authentication transmits credentials with every request.
 * Always use TLS when using Basic authentication to protect credentials.
 *
 * @example Basic Auth Usage
 * ```cpp
 * basic_auth_config config;
 * config.username = "emr_user";
 * config.password = secure_get_password();  // From secure storage
 *
 * basic_auth_provider provider(config);
 *
 * if (provider.is_authenticated()) {
 *     auto header = provider.get_authorization_header();
 *     if (header) {
 *         // header.value() = "Basic dXNlcm5hbWU6cGFzc3dvcmQ="
 *     }
 * }
 * ```
 */
class basic_auth_provider final : public auth_provider {
public:
    /**
     * @brief Construct from username and password
     * @param username Authentication username
     * @param password Authentication password
     */
    basic_auth_provider(std::string_view username, std::string_view password);

    /**
     * @brief Construct from configuration
     * @param config Basic auth configuration
     */
    explicit basic_auth_provider(const basic_auth_config& config);

    /**
     * @brief Get the Authorization header value
     *
     * Returns "Basic <base64(username:password)>"
     *
     * @return Authorization header or error if invalid config
     */
    [[nodiscard]] auto get_authorization_header()
        -> std::expected<std::string, oauth2_error> override;

    /**
     * @brief Check if authenticated (credentials are valid)
     * @return true if username and password are non-empty
     */
    [[nodiscard]] bool is_authenticated() const noexcept override;

    /**
     * @brief Get authentication type
     * @return "basic"
     */
    [[nodiscard]] std::string_view auth_type() const noexcept override;

    /**
     * @brief Update credentials
     * @param username New username
     * @param password New password
     */
    void update_credentials(std::string_view username, std::string_view password);

    /**
     * @brief Clear credentials
     */
    void invalidate() noexcept override;

private:
    std::string username_;
    std::string password_;
    mutable std::string cached_header_;
    mutable bool header_valid_{false};

    /**
     * @brief Encode credentials to Base64
     */
    void update_header() const;
};

// =============================================================================
// Base64 Encoding Utility
// =============================================================================

/**
 * @brief Encode binary data to Base64
 * @param data Input data
 * @return Base64 encoded string
 */
[[nodiscard]] std::string base64_encode(std::string_view data);

/**
 * @brief Decode Base64 string to binary data
 * @param encoded Base64 encoded string
 * @return Decoded data or empty string on error
 */
[[nodiscard]] std::string base64_decode(std::string_view encoded);

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_BASIC_AUTH_PROVIDER_H
