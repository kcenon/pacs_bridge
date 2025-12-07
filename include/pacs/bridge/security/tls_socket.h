#ifndef PACS_BRIDGE_SECURITY_TLS_SOCKET_H
#define PACS_BRIDGE_SECURITY_TLS_SOCKET_H

/**
 * @file tls_socket.h
 * @brief TLS socket wrapper for encrypted network communication
 *
 * Provides a TLS layer on top of existing TCP sockets, supporting both
 * server-side (accept) and client-side (connect) TLS operations.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/38
 */

#include "tls_context.h"
#include "tls_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace pacs::bridge::security {

/**
 * @brief TLS socket for encrypted communication
 *
 * Wraps an existing socket file descriptor with TLS encryption.
 * Supports both blocking and non-blocking operation modes.
 *
 * @example Server-side TLS Accept
 * ```cpp
 * // After accepting a TCP connection
 * int client_fd = accept(listen_fd, ...);
 *
 * auto tls_socket = tls_socket::accept(server_context, client_fd);
 * if (tls_socket) {
 *     auto bytes = tls_socket->read(buffer);
 *     if (bytes) {
 *         // Process received data
 *     }
 * }
 * ```
 *
 * @example Client-side TLS Connect
 * ```cpp
 * // After establishing TCP connection
 * int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
 * connect(sock_fd, ...);
 *
 * auto tls_socket = tls_socket::connect(client_context, sock_fd, "server.com");
 * if (tls_socket) {
 *     auto result = tls_socket->write(data);
 *     if (result) {
 *         // Data sent successfully
 *     }
 * }
 * ```
 */
class tls_socket {
public:
    /**
     * @brief Handshake completion callback type
     *
     * Called when TLS handshake completes (success or failure).
     *
     * @param success true if handshake succeeded
     * @param peer_cert Information about peer's certificate (if available)
     */
    using handshake_callback = std::function<void(
        bool success,
        const std::optional<security::certificate_info>& peer_cert)>;

    /**
     * @brief Accept an incoming TLS connection
     *
     * Performs TLS handshake on an already-accepted TCP socket.
     * This is a blocking operation that completes the TLS handshake.
     *
     * @param context Server TLS context
     * @param socket_fd Accepted TCP socket file descriptor
     * @return TLS socket or error
     */
    [[nodiscard]] static std::expected<tls_socket, tls_error>
    accept(tls_context& context, int socket_fd);

    /**
     * @brief Connect with TLS to a server
     *
     * Performs TLS handshake on an already-connected TCP socket.
     * This is a blocking operation that completes the TLS handshake.
     *
     * @param context Client TLS context
     * @param socket_fd Connected TCP socket file descriptor
     * @param hostname Server hostname for SNI and verification
     * @return TLS socket or error
     */
    [[nodiscard]] static std::expected<tls_socket, tls_error>
    connect(tls_context& context, int socket_fd, std::string_view hostname);

    /**
     * @brief Create TLS socket for async handshake
     *
     * Creates a TLS socket without performing handshake.
     * Use perform_handshake_step() for non-blocking handshake.
     *
     * @param context TLS context (server or client)
     * @param socket_fd TCP socket file descriptor
     * @param is_server true for server mode, false for client mode
     * @param hostname Server hostname (client mode only)
     * @return TLS socket or error
     */
    [[nodiscard]] static std::expected<tls_socket, tls_error>
    create_pending(tls_context& context, int socket_fd, bool is_server,
                   std::string_view hostname = "");

    /**
     * @brief Destructor - closes TLS connection
     */
    ~tls_socket();

    // Non-copyable
    tls_socket(const tls_socket&) = delete;
    tls_socket& operator=(const tls_socket&) = delete;

    // Movable
    tls_socket(tls_socket&&) noexcept;
    tls_socket& operator=(tls_socket&&) noexcept;

    // =========================================================================
    // Handshake (for async operation)
    // =========================================================================

    /**
     * @brief Handshake status
     */
    enum class handshake_status {
        /** Handshake not started */
        not_started,

        /** Handshake in progress, need to read */
        want_read,

        /** Handshake in progress, need to write */
        want_write,

        /** Handshake completed successfully */
        complete,

        /** Handshake failed */
        failed
    };

    /**
     * @brief Perform one step of TLS handshake
     *
     * For non-blocking sockets, call this repeatedly until it returns
     * complete or failed. Check want_read/want_write to know which
     * I/O event to wait for.
     *
     * @return Current handshake status
     */
    [[nodiscard]] handshake_status perform_handshake_step();

    /**
     * @brief Check if handshake is complete
     */
    [[nodiscard]] bool is_handshake_complete() const noexcept;

    // =========================================================================
    // I/O Operations
    // =========================================================================

    /**
     * @brief Read decrypted data from TLS connection
     *
     * @param buffer Buffer to receive data
     * @return Number of bytes read, or error
     *         Returns 0 if connection was gracefully closed
     */
    [[nodiscard]] std::expected<size_t, tls_error>
    read(std::span<uint8_t> buffer);

    /**
     * @brief Write data to TLS connection (encrypted)
     *
     * @param data Data to send
     * @return Number of bytes written, or error
     */
    [[nodiscard]] std::expected<size_t, tls_error>
    write(std::span<const uint8_t> data);

    /**
     * @brief Read all available data
     *
     * Continues reading until would_block or error.
     *
     * @param max_size Maximum bytes to read
     * @return Data read, or error
     */
    [[nodiscard]] std::expected<std::vector<uint8_t>, tls_error>
    read_all(size_t max_size = 1024 * 1024);

    /**
     * @brief Write all data
     *
     * Continues writing until all data is sent or error.
     *
     * @param data Data to send
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, tls_error>
    write_all(std::span<const uint8_t> data);

    /**
     * @brief Check if there is data available to read
     *
     * Returns true if there is buffered TLS data available
     * without needing to perform a socket read.
     */
    [[nodiscard]] bool has_pending_data() const noexcept;

    // =========================================================================
    // Connection Management
    // =========================================================================

    /**
     * @brief Gracefully close TLS connection
     *
     * Sends TLS close_notify alert and waits for peer's response.
     *
     * @param timeout Maximum time to wait for peer's close_notify
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, tls_error>
    shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});

    /**
     * @brief Force close connection without graceful shutdown
     *
     * Immediately closes the connection without sending close_notify.
     * Use when you don't care about graceful shutdown.
     */
    void close();

    /**
     * @brief Check if connection is open
     */
    [[nodiscard]] bool is_open() const noexcept;

    // =========================================================================
    // Connection Information
    // =========================================================================

    /**
     * @brief Get underlying socket file descriptor
     */
    [[nodiscard]] int socket_fd() const noexcept;

    /**
     * @brief Get peer's certificate information
     *
     * Available after successful handshake.
     *
     * @return Peer certificate info, or nullopt if not available
     */
    [[nodiscard]] std::optional<security::certificate_info>
    peer_certificate() const noexcept;

    /**
     * @brief Get negotiated TLS protocol version
     *
     * @return Protocol version string (e.g., "TLSv1.3")
     */
    [[nodiscard]] std::string protocol_version() const;

    /**
     * @brief Get negotiated cipher suite
     *
     * @return Cipher suite name (e.g., "TLS_AES_256_GCM_SHA384")
     */
    [[nodiscard]] std::string cipher_suite() const;

    /**
     * @brief Check if session was resumed
     *
     * Session resumption speeds up handshake for repeated connections.
     */
    [[nodiscard]] bool is_session_resumed() const noexcept;

    /**
     * @brief Get the last TLS error message
     *
     * Provides detailed error information from OpenSSL.
     */
    [[nodiscard]] std::string last_error_message() const;

    // =========================================================================
    // Non-blocking I/O Support
    // =========================================================================

    /**
     * @brief I/O result for non-blocking operations
     */
    enum class io_status {
        /** Operation completed successfully */
        success,

        /** Operation would block, need to retry after read event */
        want_read,

        /** Operation would block, need to retry after write event */
        want_write,

        /** Connection closed by peer */
        closed,

        /** Operation failed with error */
        error
    };

    /**
     * @brief Non-blocking read
     *
     * @param buffer Buffer to receive data
     * @return Status and bytes read (if success)
     */
    [[nodiscard]] std::pair<io_status, size_t>
    try_read(std::span<uint8_t> buffer);

    /**
     * @brief Non-blocking write
     *
     * @param data Data to send
     * @return Status and bytes written (if success)
     */
    [[nodiscard]] std::pair<io_status, size_t>
    try_write(std::span<const uint8_t> data);

private:
    tls_socket();

    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// String Conversion
// =============================================================================

/**
 * @brief Convert handshake_status to string
 */
[[nodiscard]] constexpr const char*
to_string(tls_socket::handshake_status status) noexcept {
    switch (status) {
        case tls_socket::handshake_status::not_started:
            return "not_started";
        case tls_socket::handshake_status::want_read:
            return "want_read";
        case tls_socket::handshake_status::want_write:
            return "want_write";
        case tls_socket::handshake_status::complete:
            return "complete";
        case tls_socket::handshake_status::failed:
            return "failed";
        default:
            return "unknown";
    }
}

/**
 * @brief Convert io_status to string
 */
[[nodiscard]] constexpr const char*
to_string(tls_socket::io_status status) noexcept {
    switch (status) {
        case tls_socket::io_status::success:
            return "success";
        case tls_socket::io_status::want_read:
            return "want_read";
        case tls_socket::io_status::want_write:
            return "want_write";
        case tls_socket::io_status::closed:
            return "closed";
        case tls_socket::io_status::error:
            return "error";
        default:
            return "unknown";
    }
}

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_TLS_SOCKET_H
