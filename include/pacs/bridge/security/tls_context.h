#ifndef PACS_BRIDGE_SECURITY_TLS_CONTEXT_H
#define PACS_BRIDGE_SECURITY_TLS_CONTEXT_H

/**
 * @file tls_context.h
 * @brief TLS context wrapper for OpenSSL SSL_CTX
 *
 * Provides a RAII wrapper around OpenSSL's SSL_CTX with automatic
 * certificate loading, configuration, and resource management.
 *
 * The tls_context can operate in either server or client mode:
 * - Server mode: Accepts incoming TLS connections
 * - Client mode: Initiates outgoing TLS connections
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/38
 */

#include "tls_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace pacs::bridge::security {

// Forward declaration for native handle
struct tls_native_context;

/**
 * @brief TLS context for secure connections
 *
 * Wraps OpenSSL SSL_CTX and provides high-level configuration.
 * Each context can be used to create multiple TLS connections.
 *
 * @example Create Server Context
 * ```cpp
 * tls_config config;
 * config.enabled = true;
 * config.cert_path = "server.crt";
 * config.key_path = "server.key";
 * config.min_version = tls_version::tls_1_2;
 *
 * auto result = tls_context::create_server_context(config);
 * if (result) {
 *     auto& context = result.value();
 *     // Use context for accepting TLS connections
 * }
 * ```
 *
 * @example Create Client Context
 * ```cpp
 * tls_config config;
 * config.enabled = true;
 * config.ca_path = "ca.crt";
 * config.verify_peer = true;
 *
 * auto result = tls_context::create_client_context(config);
 * if (result) {
 *     auto& context = result.value();
 *     // Use context for initiating TLS connections
 * }
 * ```
 */
class tls_context {
public:
    /**
     * @brief Certificate verification callback type
     *
     * Called during handshake to allow custom certificate verification.
     *
     * @param preverify_ok true if the default verification passed
     * @param cert_info Information about the certificate being verified
     * @return true to accept the certificate, false to reject
     */
    using verify_callback = std::function<bool(bool preverify_ok,
                                                const certificate_info& cert_info)>;

    /**
     * @brief Create a TLS context for server-side connections
     *
     * Server contexts require a certificate and private key.
     * Optionally, a CA certificate for client authentication.
     *
     * @param config TLS configuration
     * @return Server context or error
     */
    [[nodiscard]] static std::expected<tls_context, tls_error>
    create_server_context(const tls_config& config);

    /**
     * @brief Create a TLS context for client-side connections
     *
     * Client contexts require a CA certificate for server verification.
     * Optionally, a client certificate and key for mutual TLS.
     *
     * @param config TLS configuration
     * @return Client context or error
     */
    [[nodiscard]] static std::expected<tls_context, tls_error>
    create_client_context(const tls_config& config);

    /**
     * @brief Destructor
     */
    ~tls_context();

    // Non-copyable
    tls_context(const tls_context&) = delete;
    tls_context& operator=(const tls_context&) = delete;

    // Movable
    tls_context(tls_context&&) noexcept;
    tls_context& operator=(tls_context&&) noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Set custom certificate verification callback
     *
     * The callback is invoked for each certificate in the chain during
     * handshake. Use this for custom verification logic beyond default checks.
     *
     * @param callback Verification callback
     */
    void set_verify_callback(verify_callback callback);

    /**
     * @brief Load additional trusted CA certificates
     *
     * Adds certificates to the trust store used for peer verification.
     *
     * @param ca_path Path to CA certificate file or directory
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, tls_error>
    load_ca_certificates(const std::filesystem::path& ca_path);

    /**
     * @brief Set allowed cipher suites
     *
     * @param cipher_string OpenSSL cipher string
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, tls_error>
    set_cipher_suites(std::string_view cipher_string);

    /**
     * @brief Enable session resumption
     *
     * Session resumption allows faster TLS handshakes for repeated
     * connections from the same client.
     *
     * @param cache_size Number of sessions to cache (0 = disabled)
     */
    void enable_session_resumption(size_t cache_size);

    // =========================================================================
    // Information
    // =========================================================================

    /**
     * @brief Check if this is a server context
     */
    [[nodiscard]] bool is_server() const noexcept;

    /**
     * @brief Check if this is a client context
     */
    [[nodiscard]] bool is_client() const noexcept;

    /**
     * @brief Get the configured minimum TLS version
     */
    [[nodiscard]] tls_version min_version() const noexcept;

    /**
     * @brief Get the configured client authentication mode
     */
    [[nodiscard]] client_auth_mode client_auth() const noexcept;

    /**
     * @brief Get information about the loaded certificate
     * @return Certificate info or nullopt if no certificate loaded
     */
    [[nodiscard]] std::optional<certificate_info>
    certificate_info() const noexcept;

    /**
     * @brief Get TLS statistics
     */
    [[nodiscard]] tls_statistics statistics() const noexcept;

    // =========================================================================
    // Native Handle Access
    // =========================================================================

    /**
     * @brief Get native OpenSSL SSL_CTX pointer
     *
     * Use with caution - the returned pointer is managed by this object.
     *
     * @return Native SSL_CTX pointer (void* for ABI stability)
     */
    [[nodiscard]] void* native_handle() noexcept;

    /**
     * @brief Get native OpenSSL SSL_CTX pointer (const)
     */
    [[nodiscard]] const void* native_handle() const noexcept;

private:
    /**
     * @brief Private constructor - use factory methods
     */
    tls_context();

    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Global TLS Initialization
// =============================================================================

/**
 * @brief Initialize TLS library
 *
 * Must be called once before creating any TLS contexts.
 * Thread-safe and idempotent (safe to call multiple times).
 *
 * @return Success or error
 */
[[nodiscard]] std::expected<void, tls_error> initialize_tls();

/**
 * @brief Cleanup TLS library
 *
 * Should be called once at application shutdown.
 * After this call, no TLS operations should be performed.
 */
void cleanup_tls();

/**
 * @brief RAII guard for TLS library initialization
 *
 * Calls initialize_tls() on construction and cleanup_tls() on destruction.
 *
 * @example
 * ```cpp
 * int main() {
 *     tls_library_guard tls_guard;
 *     if (!tls_guard.is_initialized()) {
 *         return 1;
 *     }
 *     // Use TLS...
 *     return 0;
 * }
 * ```
 */
class tls_library_guard {
public:
    tls_library_guard() {
        auto result = initialize_tls();
        initialized_ = result.has_value();
    }

    ~tls_library_guard() {
        if (initialized_) {
            cleanup_tls();
        }
    }

    // Non-copyable, non-movable
    tls_library_guard(const tls_library_guard&) = delete;
    tls_library_guard& operator=(const tls_library_guard&) = delete;
    tls_library_guard(tls_library_guard&&) = delete;
    tls_library_guard& operator=(tls_library_guard&&) = delete;

    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Read certificate information from file
 *
 * @param cert_path Path to certificate file (PEM format)
 * @return Certificate info or error
 */
[[nodiscard]] std::expected<security::certificate_info, tls_error>
read_certificate_info(const std::filesystem::path& cert_path);

/**
 * @brief Verify that private key matches certificate
 *
 * @param cert_path Path to certificate file
 * @param key_path Path to private key file
 * @return Success or error
 */
[[nodiscard]] std::expected<void, tls_error>
verify_key_pair(const std::filesystem::path& cert_path,
                const std::filesystem::path& key_path);

/**
 * @brief Get OpenSSL version string
 */
[[nodiscard]] std::string openssl_version();

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_TLS_CONTEXT_H
