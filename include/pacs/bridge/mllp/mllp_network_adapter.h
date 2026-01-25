#ifndef PACS_BRIDGE_MLLP_MLLP_NETWORK_ADAPTER_H
#define PACS_BRIDGE_MLLP_MLLP_NETWORK_ADAPTER_H

/**
 * @file mllp_network_adapter.h
 * @brief Network layer abstraction for MLLP server
 *
 * Provides abstract interfaces for network operations, enabling different
 * transport implementations (BSD sockets, TLS) to be used interchangeably
 * with the MLLP server.
 *
 * This abstraction separates protocol handling from network transport,
 * improving testability and enabling future transport options.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/277
 */

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pacs::bridge::mllp {

// =============================================================================
// Error Codes
// =============================================================================

/**
 * @brief Network-specific error codes for MLLP transport
 */
enum class network_error : int {
    /** Operation timed out */
    timeout = -980,

    /** Connection closed by peer */
    connection_closed = -981,

    /** Socket operation failed */
    socket_error = -982,

    /** Failed to bind or listen on port */
    bind_failed = -983,

    /** TLS handshake failed */
    tls_handshake_failed = -984,

    /** Invalid configuration */
    invalid_config = -985,

    /** Operation would block (non-blocking I/O) */
    would_block = -986,

    /** Connection refused by peer */
    connection_refused = -987
};

/**
 * @brief Get human-readable description of network error
 */
[[nodiscard]] constexpr const char* to_string(network_error error) noexcept {
    switch (error) {
        case network_error::timeout:
            return "Operation timed out";
        case network_error::connection_closed:
            return "Connection closed by peer";
        case network_error::socket_error:
            return "Socket operation failed";
        case network_error::bind_failed:
            return "Failed to bind or listen on port";
        case network_error::tls_handshake_failed:
            return "TLS handshake failed";
        case network_error::invalid_config:
            return "Invalid configuration";
        case network_error::would_block:
            return "Operation would block";
        case network_error::connection_refused:
            return "Connection refused by peer";
        default:
            return "Unknown network error";
    }
}

// =============================================================================
// Configuration Structures
// =============================================================================

/**
 * @brief Server configuration for network adapter
 */
struct server_config {
    /** Port to listen on */
    uint16_t port = 2575;

    /** Bind address (empty = all interfaces) */
    std::string bind_address;

    /** Maximum pending connections in listen backlog */
    int backlog = 128;

    /** Socket receive buffer size (0 = system default) */
    size_t recv_buffer_size = 0;

    /** Socket send buffer size (0 = system default) */
    size_t send_buffer_size = 0;

    /** Enable TCP keep-alive */
    bool keep_alive = true;

    /** TCP keep-alive idle time (seconds) */
    int keep_alive_idle = 60;

    /** TCP keep-alive interval (seconds) */
    int keep_alive_interval = 10;

    /** TCP keep-alive probe count */
    int keep_alive_count = 3;

    /** Disable Nagle's algorithm (enable TCP_NODELAY) */
    bool no_delay = true;

    /** Reuse address (SO_REUSEADDR) */
    bool reuse_addr = true;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        return port > 0 && backlog > 0;
    }
};

/**
 * @brief Session statistics
 */
struct session_stats {
    /** Total bytes received on this session */
    size_t bytes_received = 0;

    /** Total bytes sent on this session */
    size_t bytes_sent = 0;

    /** Messages received */
    size_t messages_received = 0;

    /** Messages sent */
    size_t messages_sent = 0;

    /** Session start time */
    std::chrono::system_clock::time_point connected_at;

    /** Last activity time */
    std::chrono::system_clock::time_point last_activity;
};

// =============================================================================
// Session Interface
// =============================================================================

/**
 * @brief Abstract interface for a network session (connection)
 *
 * Represents a single TCP connection with send/receive capabilities.
 * Implementations handle the underlying transport (BSD sockets, TLS, etc.).
 */
class mllp_session {
public:
    virtual ~mllp_session() = default;

    // Non-copyable
    mllp_session(const mllp_session&) = delete;
    mllp_session& operator=(const mllp_session&) = delete;

    // Movable
    mllp_session(mllp_session&&) = default;
    mllp_session& operator=(mllp_session&&) = default;

    /**
     * @brief Receive data from the connection
     *
     * Attempts to receive up to max_bytes within the specified timeout.
     * Returns partial data if less than max_bytes is available.
     *
     * @param max_bytes Maximum bytes to receive
     * @param timeout Maximum time to wait for data
     * @return Received data or error
     */
    [[nodiscard]] virtual std::expected<std::vector<uint8_t>, network_error>
    receive(size_t max_bytes, std::chrono::milliseconds timeout) = 0;

    /**
     * @brief Send data over the connection
     *
     * Sends all bytes in the provided span. Blocks until all data is sent
     * or an error occurs.
     *
     * @param data Data to send
     * @return Number of bytes sent, or error
     */
    [[nodiscard]] virtual std::expected<size_t, network_error>
    send(std::span<const uint8_t> data) = 0;

    /**
     * @brief Close the connection
     *
     * Gracefully closes the connection. After calling close(), the session
     * should not be used for further I/O operations.
     */
    virtual void close() = 0;

    /**
     * @brief Check if the connection is still open
     */
    [[nodiscard]] virtual bool is_open() const noexcept = 0;

    /**
     * @brief Get session statistics
     */
    [[nodiscard]] virtual session_stats get_stats() const noexcept = 0;

    /**
     * @brief Get remote peer address
     */
    [[nodiscard]] virtual std::string remote_address() const noexcept = 0;

    /**
     * @brief Get remote peer port
     */
    [[nodiscard]] virtual uint16_t remote_port() const noexcept = 0;

    /**
     * @brief Get unique session identifier
     */
    [[nodiscard]] virtual uint64_t session_id() const noexcept = 0;

protected:
    mllp_session() = default;
};

// =============================================================================
// Server Adapter Interface
// =============================================================================

/**
 * @brief Abstract interface for MLLP server network adapter
 *
 * Manages the server socket and accepts incoming connections.
 * Implementations handle platform-specific networking (BSD sockets, Winsock).
 */
class mllp_server_adapter {
public:
    /**
     * @brief Callback type for new connections
     *
     * Called when a new connection is accepted. The callback receives
     * ownership of the session.
     *
     * @param session Newly accepted connection
     */
    using on_connection_callback =
        std::function<void(std::unique_ptr<mllp_session> session)>;

    virtual ~mllp_server_adapter() = default;

    // Non-copyable, non-movable (manages server socket)
    mllp_server_adapter(const mllp_server_adapter&) = delete;
    mllp_server_adapter& operator=(const mllp_server_adapter&) = delete;
    mllp_server_adapter(mllp_server_adapter&&) = delete;
    mllp_server_adapter& operator=(mllp_server_adapter&&) = delete;

    /**
     * @brief Start the server and begin listening
     *
     * Binds to the configured port and starts accepting connections.
     * The on_connection callback will be invoked for each new connection.
     *
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, network_error> start() = 0;

    /**
     * @brief Stop the server
     *
     * Stops accepting new connections. Existing connections are not affected.
     *
     * @param wait_for_connections If true, waits for active connections to close
     */
    virtual void stop(bool wait_for_connections = true) = 0;

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] virtual bool is_running() const noexcept = 0;

    /**
     * @brief Get the listening port
     */
    [[nodiscard]] virtual uint16_t port() const noexcept = 0;

    /**
     * @brief Set the callback for new connections
     *
     * Must be called before start().
     *
     * @param callback Function to call for each new connection
     */
    virtual void on_connection(on_connection_callback callback) = 0;

    /**
     * @brief Get current active session count
     */
    [[nodiscard]] virtual size_t active_session_count() const noexcept = 0;

protected:
    mllp_server_adapter() = default;
};

}  // namespace pacs::bridge::mllp

#endif  // PACS_BRIDGE_MLLP_MLLP_NETWORK_ADAPTER_H
