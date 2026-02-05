#ifndef PACS_BRIDGE_MLLP_TLS_MLLP_SERVER_H
#define PACS_BRIDGE_MLLP_TLS_MLLP_SERVER_H

/**
 * @file tls_mllp_server.h
 * @brief TLS-enabled implementation of MLLP network adapter
 *
 * Extends BSD socket adapter with OpenSSL TLS support for secure
 * MLLP communication. Supports:
 * - TLS 1.2 and TLS 1.3
 * - Mutual TLS (client certificate authentication)
 * - Configurable cipher suites
 * - Certificate verification
 *
 * Requires PACS_BRIDGE_HAS_OPENSSL to be defined.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/306
 */

#include "pacs/bridge/mllp/mllp_network_adapter.h"
#include "pacs/bridge/security/tls_context.h"
#include "pacs/bridge/security/tls_types.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

// Platform-specific socket types
// Note: These may already be defined by bsd_mllp_server.h
#ifndef PACS_BRIDGE_SOCKET_TYPES_DEFINED
#define PACS_BRIDGE_SOCKET_TYPES_DEFINED
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t INVALID_SOCKET_VALUE = -1;
#endif
#endif  // PACS_BRIDGE_SOCKET_TYPES_DEFINED

// OpenSSL forward declarations
#ifdef PACS_BRIDGE_HAS_OPENSSL
// OpenSSL headers must be included for SSL structure definition
#include <openssl/ssl.h>
#endif

namespace pacs::bridge::mllp {

// =============================================================================
// TLS Session Implementation
// =============================================================================

/**
 * @brief TLS-enabled implementation of mllp_session
 *
 * Wraps a TCP socket with OpenSSL for encrypted communication.
 * Handles TLS handshake, encrypted send/receive, and proper cleanup.
 */
class tls_mllp_session : public mllp_session {
public:
    /**
     * @brief Constructor
     *
     * @param sock Connected TCP socket (handshake not yet performed)
     * @param session_id Unique identifier for this session
     * @param remote_addr Remote peer address
     * @param remote_port Remote peer port
     * @param ssl OpenSSL SSL structure (owned by this session)
     */
    tls_mllp_session(socket_t sock, uint64_t session_id,
                     std::string remote_addr, uint16_t remote_port, SSL* ssl);

    ~tls_mllp_session() override;

    // Implement mllp_session interface
    [[nodiscard]] std::expected<std::vector<uint8_t>, network_error>
    receive(size_t max_bytes, std::chrono::milliseconds timeout) override;

    [[nodiscard]] std::expected<size_t, network_error>
    send(std::span<const uint8_t> data) override;

    void close() override;

    [[nodiscard]] bool is_open() const noexcept override;

    [[nodiscard]] session_stats get_stats() const noexcept override;

    [[nodiscard]] std::string remote_address() const noexcept override;

    [[nodiscard]] uint16_t remote_port() const noexcept override;

    [[nodiscard]] uint64_t session_id() const noexcept override;

    // TLS-specific information
    [[nodiscard]] std::string tls_version() const noexcept;
    [[nodiscard]] std::string tls_cipher() const noexcept;
    [[nodiscard]] std::optional<security::certificate_info>
    peer_certificate() const noexcept;

    /**
     * @brief Perform TLS handshake
     *
     * Must be called immediately after construction.
     * Public so tls_mllp_server can call it during accept.
     *
     * @param timeout Maximum time for handshake completion
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, network_error>
    perform_handshake(std::chrono::milliseconds timeout);

private:

    /**
     * @brief Wait for SSL I/O readiness
     *
     * Handles SSL_ERROR_WANT_READ and SSL_ERROR_WANT_WRITE.
     *
     * @param ssl_error SSL error code from last operation
     * @param timeout Maximum wait time
     * @return true if ready, false if timeout or error
     */
    [[nodiscard]] std::expected<bool, network_error>
    wait_for_ssl_io(int ssl_error, std::chrono::milliseconds timeout);

    /**
     * @brief Set socket to non-blocking mode
     */
    [[nodiscard]] std::expected<void, network_error> set_nonblocking(bool enable);

    socket_t socket_;
    uint64_t session_id_;
    std::string remote_addr_;
    uint16_t remote_port_;
    SSL* ssl_;

    // Statistics (thread-safe)
    mutable std::mutex stats_mutex_;
    session_stats stats_;

    std::atomic<bool> is_open_{true};
    bool handshake_completed_ = false;

    // Cached TLS info
    mutable std::string tls_version_str_;
    mutable std::string tls_cipher_str_;
};

// =============================================================================
// TLS Server Adapter Implementation
// =============================================================================

/**
 * @brief TLS-enabled implementation of mllp_server_adapter
 *
 * Creates a TCP server socket and performs TLS handshake for each
 * accepted connection using OpenSSL.
 */
class tls_mllp_server : public mllp_server_adapter {
public:
    /**
     * @brief Constructor
     *
     * @param config Server socket configuration
     * @param tls_config TLS configuration (certificates, versions, etc.)
     */
    tls_mllp_server(const server_config& config,
                    const security::tls_config& tls_config);

    ~tls_mllp_server() override;

    // Implement mllp_server_adapter interface
    [[nodiscard]] std::expected<void, network_error> start() override;

    void stop(bool wait_for_connections = true) override;

    [[nodiscard]] bool is_running() const noexcept override;

    [[nodiscard]] uint16_t port() const noexcept override;

    void on_connection(on_connection_callback callback) override;

    [[nodiscard]] size_t active_session_count() const noexcept override;

    // TLS-specific methods
    [[nodiscard]] security::tls_statistics tls_statistics() const noexcept;

private:
    /**
     * @brief Initialize platform-specific networking (Winsock on Windows)
     */
    [[nodiscard]] std::expected<void, network_error> initialize_networking();

    /**
     * @brief Cleanup platform-specific networking
     */
    void cleanup_networking();

    /**
     * @brief Create and configure the server socket
     */
    [[nodiscard]] std::expected<void, network_error> create_server_socket();

    /**
     * @brief Configure socket options (keep-alive, nodelay, etc.)
     */
    [[nodiscard]] std::expected<void, network_error>
    configure_socket_options(socket_t sock);

    /**
     * @brief Initialize TLS context from configuration
     */
    [[nodiscard]] std::expected<void, network_error> initialize_tls_context();

    /**
     * @brief Accept loop - runs in background thread
     *
     * Accepts connections and performs TLS handshake for each.
     */
    void accept_loop();

    /**
     * @brief Generate unique session ID
     */
    uint64_t generate_session_id();

    server_config config_;
    security::tls_config tls_config_;
    socket_t server_socket_ = INVALID_SOCKET_VALUE;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    std::thread accept_thread_;
    on_connection_callback connection_callback_;

    // Session tracking
    std::atomic<size_t> active_sessions_{0};
    std::atomic<uint64_t> next_session_id_{1};

    // TLS context
    std::unique_ptr<security::tls_context> tls_ctx_;

    mutable std::mutex state_mutex_;

#ifdef _WIN32
    bool winsock_initialized_ = false;
#endif
};

}  // namespace pacs::bridge::mllp

#endif  // PACS_BRIDGE_MLLP_TLS_MLLP_SERVER_H
