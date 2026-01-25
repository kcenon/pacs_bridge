#ifndef PACS_BRIDGE_MLLP_BSD_MLLP_SERVER_H
#define PACS_BRIDGE_MLLP_BSD_MLLP_SERVER_H

/**
 * @file bsd_mllp_server.h
 * @brief BSD socket implementation of MLLP network adapter
 *
 * Provides concrete implementations of mllp_session and mllp_server_adapter
 * using Berkeley Socket API (BSD sockets / Winsock2).
 *
 * Platform support:
 * - Windows: Winsock2
 * - POSIX (Linux, macOS, BSD): BSD sockets
 *
 * This implementation does NOT include TLS support - that will be added
 * in a separate implementation (tls_mllp_server).
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/305
 */

#include "pacs/bridge/mllp/mllp_network_adapter.h"

#include <atomic>
#include <mutex>
#include <thread>

// Platform-specific socket types
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t INVALID_SOCKET_VALUE = -1;
#endif

namespace pacs::bridge::mllp {

// =============================================================================
// BSD Socket Session Implementation
// =============================================================================

/**
 * @brief BSD socket implementation of mllp_session
 *
 * Manages a single TCP connection using platform-specific socket API.
 * Handles platform differences between Windows and POSIX systems.
 */
class bsd_mllp_session : public mllp_session {
public:
    /**
     * @brief Constructor
     *
     * @param sock Connected socket
     * @param session_id Unique identifier for this session
     * @param remote_addr Remote peer address
     * @param remote_port Remote peer port
     */
    bsd_mllp_session(socket_t sock, uint64_t session_id,
                     std::string remote_addr, uint16_t remote_port);

    ~bsd_mllp_session() override;

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

private:
    /**
     * @brief Set socket to non-blocking mode for timeout support
     */
    [[nodiscard]] std::expected<void, network_error> set_nonblocking(bool enable);

    /**
     * @brief Wait for socket to be readable/writable with timeout
     *
     * @param for_read true = wait for read, false = wait for write
     * @param timeout Maximum wait time
     * @return true if ready, false if timeout or error
     */
    [[nodiscard]] std::expected<bool, network_error>
    wait_for_io(bool for_read, std::chrono::milliseconds timeout);

    socket_t socket_;
    uint64_t session_id_;
    std::string remote_addr_;
    uint16_t remote_port_;

    // Statistics (thread-safe)
    mutable std::mutex stats_mutex_;
    session_stats stats_;

    std::atomic<bool> is_open_{true};
};

// =============================================================================
// BSD Socket Server Adapter Implementation
// =============================================================================

/**
 * @brief BSD socket implementation of mllp_server_adapter
 *
 * Manages the server socket, accepts incoming connections, and creates
 * bsd_mllp_session instances for each connection.
 */
class bsd_mllp_server : public mllp_server_adapter {
public:
    /**
     * @brief Constructor
     *
     * @param config Server configuration
     */
    explicit bsd_mllp_server(const server_config& config);

    ~bsd_mllp_server() override;

    // Implement mllp_server_adapter interface
    [[nodiscard]] std::expected<void, network_error> start() override;

    void stop(bool wait_for_connections = true) override;

    [[nodiscard]] bool is_running() const noexcept override;

    [[nodiscard]] uint16_t port() const noexcept override;

    void on_connection(on_connection_callback callback) override;

    [[nodiscard]] size_t active_session_count() const noexcept override;

private:
    /**
     * @brief Initialize platform-specific networking (Winsock on Windows)
     */
    [[nodiscard]] std::expected<void, network_error> initialize_networking();

    /**
     * @brief Cleanup platform-specific networking (Winsock on Windows)
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
     * @brief Accept loop - runs in background thread
     */
    void accept_loop();

    /**
     * @brief Generate unique session ID
     */
    uint64_t generate_session_id();

    server_config config_;
    socket_t server_socket_ = INVALID_SOCKET_VALUE;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    std::thread accept_thread_;
    on_connection_callback connection_callback_;

    // Session tracking
    std::atomic<size_t> active_sessions_{0};
    std::atomic<uint64_t> next_session_id_{1};

    mutable std::mutex state_mutex_;

#ifdef _WIN32
    bool winsock_initialized_ = false;
#endif
};

}  // namespace pacs::bridge::mllp

#endif  // PACS_BRIDGE_MLLP_BSD_MLLP_SERVER_H
