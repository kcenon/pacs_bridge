#ifndef PACS_BRIDGE_SECURITY_TLS_TYPES_H
#define PACS_BRIDGE_SECURITY_TLS_TYPES_H

/**
 * @file tls_types.h
 * @brief TLS/SSL type definitions for PACS Bridge
 *
 * Provides common types for TLS configuration including certificate paths,
 * protocol versions, cipher suites, and client authentication settings.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/38
 */

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::security {

// =============================================================================
// Error Codes (-990 to -999)
// =============================================================================

/**
 * @brief TLS specific error codes
 *
 * Allocated range: -990 to -999
 * @see docs/SDS_COMPONENTS.md for error code allocation
 */
enum class tls_error : int {
    /** TLS library initialization failed */
    initialization_failed = -990,

    /** Certificate file not found or invalid */
    certificate_invalid = -991,

    /** Private key file not found or invalid */
    private_key_invalid = -992,

    /** CA certificate file not found or invalid */
    ca_certificate_invalid = -993,

    /** Private key does not match certificate */
    key_certificate_mismatch = -994,

    /** TLS handshake failed */
    handshake_failed = -995,

    /** Client certificate verification failed */
    client_verification_failed = -996,

    /** Unsupported TLS version requested */
    unsupported_version = -997,

    /** Invalid cipher suite configuration */
    invalid_cipher_suite = -998,

    /** TLS connection closed unexpectedly */
    connection_closed = -999
};

/**
 * @brief Convert tls_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(tls_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of TLS error
 */
[[nodiscard]] constexpr const char* to_string(tls_error error) noexcept {
    switch (error) {
        case tls_error::initialization_failed:
            return "TLS library initialization failed";
        case tls_error::certificate_invalid:
            return "Certificate file not found or invalid";
        case tls_error::private_key_invalid:
            return "Private key file not found or invalid";
        case tls_error::ca_certificate_invalid:
            return "CA certificate file not found or invalid";
        case tls_error::key_certificate_mismatch:
            return "Private key does not match certificate";
        case tls_error::handshake_failed:
            return "TLS handshake failed";
        case tls_error::client_verification_failed:
            return "Client certificate verification failed";
        case tls_error::unsupported_version:
            return "Unsupported TLS version requested";
        case tls_error::invalid_cipher_suite:
            return "Invalid cipher suite configuration";
        case tls_error::connection_closed:
            return "TLS connection closed unexpectedly";
        default:
            return "Unknown TLS error";
    }
}

// =============================================================================
// TLS Version
// =============================================================================

/**
 * @brief Minimum TLS protocol version
 *
 * Defines the minimum acceptable TLS version for connections.
 * TLS 1.2 is the recommended minimum for healthcare applications.
 */
enum class tls_version {
    /** TLS 1.2 - Minimum recommended for HIPAA compliance */
    tls_1_2,

    /** TLS 1.3 - Latest version with improved security */
    tls_1_3
};

/**
 * @brief Convert tls_version to string representation
 */
[[nodiscard]] constexpr const char* to_string(tls_version version) noexcept {
    switch (version) {
        case tls_version::tls_1_2:
            return "TLS1.2";
        case tls_version::tls_1_3:
            return "TLS1.3";
        default:
            return "Unknown";
    }
}

/**
 * @brief Parse tls_version from string
 * @param str Version string ("TLS1.2", "TLS1.3", "1.2", "1.3")
 * @return Parsed version or nullopt if invalid
 */
[[nodiscard]] inline std::optional<tls_version>
parse_tls_version(std::string_view str) noexcept {
    if (str == "TLS1.2" || str == "1.2" || str == "tls1.2") {
        return tls_version::tls_1_2;
    }
    if (str == "TLS1.3" || str == "1.3" || str == "tls1.3") {
        return tls_version::tls_1_3;
    }
    return std::nullopt;
}

// =============================================================================
// Client Authentication Mode
// =============================================================================

/**
 * @brief Client certificate authentication mode
 *
 * Defines how the server handles client certificates for mutual TLS (mTLS).
 */
enum class client_auth_mode {
    /** Do not request client certificate */
    none,

    /** Request client certificate but don't require it */
    optional,

    /** Require valid client certificate (mutual TLS) */
    required
};

/**
 * @brief Convert client_auth_mode to string representation
 */
[[nodiscard]] constexpr const char* to_string(client_auth_mode mode) noexcept {
    switch (mode) {
        case client_auth_mode::none:
            return "none";
        case client_auth_mode::optional:
            return "optional";
        case client_auth_mode::required:
            return "required";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse client_auth_mode from string
 * @param str Mode string ("none", "optional", "required")
 * @return Parsed mode or nullopt if invalid
 */
[[nodiscard]] inline std::optional<client_auth_mode>
parse_client_auth_mode(std::string_view str) noexcept {
    if (str == "none" || str == "false" || str == "0") {
        return client_auth_mode::none;
    }
    if (str == "optional" || str == "request") {
        return client_auth_mode::optional;
    }
    if (str == "required" || str == "true" || str == "1") {
        return client_auth_mode::required;
    }
    return std::nullopt;
}

// =============================================================================
// TLS Configuration
// =============================================================================

/**
 * @brief TLS/SSL configuration for secure connections
 *
 * Contains all settings needed to configure TLS for MLLP or HTTPS connections.
 * Supports server-side TLS (for incoming connections) and client-side TLS
 * (for outgoing connections).
 *
 * @example Server TLS Configuration
 * ```cpp
 * tls_config config;
 * config.enabled = true;
 * config.cert_path = "/etc/pacs_bridge/server.crt";
 * config.key_path = "/etc/pacs_bridge/server.key";
 * config.ca_path = "/etc/pacs_bridge/ca.crt";
 * config.client_auth = client_auth_mode::optional;
 * config.min_version = tls_version::tls_1_2;
 * ```
 *
 * @example Client TLS Configuration
 * ```cpp
 * tls_config config;
 * config.enabled = true;
 * config.ca_path = "/etc/pacs_bridge/ca.crt";  // For server verification
 * // Optional: client certificate for mutual TLS
 * config.cert_path = "/etc/pacs_bridge/client.crt";
 * config.key_path = "/etc/pacs_bridge/client.key";
 * ```
 */
struct tls_config {
    /** Enable TLS for this connection */
    bool enabled = false;

    /**
     * @brief Path to the certificate file (PEM format)
     *
     * For servers: The server certificate presented to clients
     * For clients: The client certificate for mutual TLS (optional)
     */
    std::filesystem::path cert_path;

    /**
     * @brief Path to the private key file (PEM format)
     *
     * Must match the certificate. Should be readable only by the service user.
     */
    std::filesystem::path key_path;

    /**
     * @brief Path to CA certificate file or directory (PEM format)
     *
     * For servers: Used to verify client certificates (if client_auth enabled)
     * For clients: Used to verify server certificate
     */
    std::filesystem::path ca_path;

    /**
     * @brief Client certificate authentication mode (server-side only)
     *
     * Determines whether the server requires client certificates.
     */
    client_auth_mode client_auth = client_auth_mode::none;

    /**
     * @brief Minimum TLS protocol version
     *
     * TLS 1.2 is required for HIPAA compliance.
     * TLS 1.3 is preferred when both sides support it.
     */
    tls_version min_version = tls_version::tls_1_2;

    /**
     * @brief Allowed cipher suites (empty = use defaults)
     *
     * OpenSSL cipher string format. Examples:
     * - "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256"
     * - "HIGH:!aNULL:!MD5"
     *
     * If empty, a secure default set is used.
     */
    std::vector<std::string> cipher_suites;

    /**
     * @brief Verify peer certificate (hostname/identity check)
     *
     * When true:
     * - Servers verify client certificates match CA
     * - Clients verify server hostname matches certificate
     */
    bool verify_peer = true;

    /**
     * @brief Expected hostname for certificate verification (client-side)
     *
     * If set, the client will verify the server certificate contains this
     * hostname. If empty, the connection hostname is used.
     */
    std::optional<std::string> verify_hostname;

    /**
     * @brief TLS handshake timeout
     *
     * Maximum time to complete TLS handshake before timing out.
     */
    std::chrono::milliseconds handshake_timeout{5000};

    /**
     * @brief Session cache size (0 = disabled)
     *
     * Number of TLS sessions to cache for session resumption.
     * Improves performance for repeated connections from the same client.
     */
    size_t session_cache_size = 1024;

    /**
     * @brief Check if the configuration is valid for a server
     * @return true if all required server settings are present
     */
    [[nodiscard]] bool is_valid_for_server() const noexcept {
        if (!enabled) return true;
        return !cert_path.empty() && !key_path.empty();
    }

    /**
     * @brief Check if the configuration is valid for a client
     * @return true if all required client settings are present
     */
    [[nodiscard]] bool is_valid_for_client() const noexcept {
        if (!enabled) return true;
        // CA path is recommended but not strictly required
        // (system CA store might be used)
        return true;
    }

    /**
     * @brief Check if mutual TLS is configured
     * @return true if client authentication is enabled
     */
    [[nodiscard]] bool is_mutual_tls() const noexcept {
        return enabled && client_auth != client_auth_mode::none;
    }
};

// =============================================================================
// TLS Statistics
// =============================================================================

/**
 * @brief TLS connection statistics
 *
 * Provides metrics for monitoring TLS connection health and performance.
 */
struct tls_statistics {
    /** Total TLS handshakes attempted */
    size_t handshakes_attempted = 0;

    /** Successful TLS handshakes */
    size_t handshakes_succeeded = 0;

    /** Failed TLS handshakes */
    size_t handshakes_failed = 0;

    /** Client certificate verification failures */
    size_t client_auth_failures = 0;

    /** TLS sessions resumed from cache */
    size_t sessions_resumed = 0;

    /** Average handshake duration in milliseconds */
    double avg_handshake_ms = 0.0;

    /** Current active TLS connections */
    size_t active_connections = 0;

    /**
     * @brief Calculate handshake success rate
     * @return Success rate as percentage (0.0 - 100.0)
     */
    [[nodiscard]] double success_rate() const noexcept {
        if (handshakes_attempted == 0) return 100.0;
        return (static_cast<double>(handshakes_succeeded) /
                static_cast<double>(handshakes_attempted)) * 100.0;
    }

    /**
     * @brief Calculate session resumption rate
     * @return Resumption rate as percentage (0.0 - 100.0)
     */
    [[nodiscard]] double resumption_rate() const noexcept {
        if (handshakes_succeeded == 0) return 0.0;
        return (static_cast<double>(sessions_resumed) /
                static_cast<double>(handshakes_succeeded)) * 100.0;
    }
};

// =============================================================================
// Certificate Information
// =============================================================================

/**
 * @brief Certificate information extracted from X.509 certificate
 */
struct certificate_info {
    /** Certificate subject (CN, O, OU, etc.) */
    std::string subject;

    /** Certificate issuer */
    std::string issuer;

    /** Serial number (hex string) */
    std::string serial_number;

    /** Not before (validity start) */
    std::chrono::system_clock::time_point not_before;

    /** Not after (validity end) */
    std::chrono::system_clock::time_point not_after;

    /** Subject alternative names (DNS names, IPs) */
    std::vector<std::string> san_entries;

    /** SHA-256 fingerprint (hex string) */
    std::string fingerprint_sha256;

    /**
     * @brief Check if certificate is currently valid
     */
    [[nodiscard]] bool is_valid() const noexcept {
        auto now = std::chrono::system_clock::now();
        return now >= not_before && now <= not_after;
    }

    /**
     * @brief Check if certificate expires within given duration
     * @param within Duration to check
     * @return true if certificate expires within the specified time
     */
    [[nodiscard]] bool expires_within(
        std::chrono::hours within) const noexcept {
        auto now = std::chrono::system_clock::now();
        return (not_after - now) <= within;
    }

    /**
     * @brief Get remaining validity duration
     * @return Remaining time until expiration (negative if expired)
     */
    [[nodiscard]] std::chrono::hours remaining_validity() const noexcept {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::hours>(not_after - now);
    }
};

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_TLS_TYPES_H
