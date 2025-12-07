#ifndef PACS_BRIDGE_MLLP_MLLP_SERVER_H
#define PACS_BRIDGE_MLLP_MLLP_SERVER_H

/**
 * @file mllp_server.h
 * @brief MLLP server for receiving HL7 messages with optional TLS support
 *
 * Provides a server implementation for the Minimal Lower Layer Protocol (MLLP)
 * used for HL7 message transmission. Supports both plain TCP and TLS-encrypted
 * connections for HIPAA compliance.
 *
 * Features:
 *   - Concurrent connection handling with thread pool
 *   - TLS 1.2/1.3 support for secure connections
 *   - Optional client certificate authentication (mutual TLS)
 *   - Connection idle timeout
 *   - Message size limits
 *   - Comprehensive statistics
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/38
 * @see docs/reference_materials/04_mllp_protocol.md
 */

#include "mllp_types.h"
#include "pacs/bridge/security/tls_context.h"

#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace pacs::bridge::mllp {

/**
 * @brief MLLP server for HL7 message reception
 *
 * Listens for incoming MLLP connections and processes HL7 messages.
 * Each received message is passed to a registered handler callback,
 * which can generate an HL7 acknowledgment response.
 *
 * @example Basic Usage
 * ```cpp
 * mllp_server_config config;
 * config.port = 2575;
 * config.max_connections = 50;
 *
 * mllp_server server(config);
 *
 * server.set_message_handler([](const mllp_message& msg,
 *                               const mllp_session_info& session)
 *     -> std::optional<mllp_message> {
 *     // Process message and return ACK
 *     return create_ack(msg);
 * });
 *
 * if (server.start()) {
 *     // Server is running
 * }
 * ```
 *
 * @example TLS-Enabled Server
 * ```cpp
 * mllp_server_config config;
 * config.port = 2576;
 * config.tls.enabled = true;
 * config.tls.cert_path = "/etc/pacs/server.crt";
 * config.tls.key_path = "/etc/pacs/server.key";
 * config.tls.ca_path = "/etc/pacs/ca.crt";
 * config.tls.client_auth = client_auth_mode::optional;
 * config.tls.min_version = tls_version::tls_1_2;
 *
 * mllp_server server(config);
 * server.start();
 * ```
 */
class mllp_server {
public:
    /**
     * @brief Message handler callback type
     *
     * Called for each received HL7 message. Should return an optional
     * response message (typically an ACK or NAK).
     *
     * @param message Received HL7 message
     * @param session Information about the connection
     * @return Optional response message to send back
     */
    using message_handler = std::function<std::optional<mllp_message>(
        const mllp_message& message, const mllp_session_info& session)>;

    /**
     * @brief Connection event callback type
     *
     * Called when a new connection is established or closed.
     *
     * @param session Session information
     * @param connected true if connection established, false if closed
     */
    using connection_handler =
        std::function<void(const mllp_session_info& session, bool connected)>;

    /**
     * @brief Error callback type
     *
     * Called when an error occurs during message processing.
     *
     * @param error Error code
     * @param session Session information (if available)
     * @param details Error details
     */
    using error_handler = std::function<void(
        mllp_error error, const std::optional<mllp_session_info>& session,
        std::string_view details)>;

    /**
     * @brief Constructor
     * @param config Server configuration
     */
    explicit mllp_server(const mllp_server_config& config);

    /**
     * @brief Destructor - stops server if running
     */
    ~mllp_server();

    // Non-copyable
    mllp_server(const mllp_server&) = delete;
    mllp_server& operator=(const mllp_server&) = delete;

    // Movable
    mllp_server(mllp_server&&) noexcept;
    mllp_server& operator=(mllp_server&&) noexcept;

    // =========================================================================
    // Handler Registration
    // =========================================================================

    /**
     * @brief Set the message handler callback
     *
     * Must be set before starting the server. The handler is called
     * for each received HL7 message.
     *
     * @param handler Message processing callback
     */
    void set_message_handler(message_handler handler);

    /**
     * @brief Set the connection event handler
     *
     * Optional callback for connection lifecycle events.
     *
     * @param handler Connection event callback
     */
    void set_connection_handler(connection_handler handler);

    /**
     * @brief Set the error handler callback
     *
     * Optional callback for error events.
     *
     * @param handler Error event callback
     */
    void set_error_handler(error_handler handler);

    // =========================================================================
    // Server Lifecycle
    // =========================================================================

    /**
     * @brief Start the MLLP server
     *
     * Begins listening for incoming connections on the configured port.
     * Returns immediately; server runs in background threads.
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, mllp_error> start();

    /**
     * @brief Stop the MLLP server
     *
     * Stops accepting new connections and gracefully closes existing ones.
     *
     * @param wait_for_connections If true, wait for active connections to complete
     * @param timeout Maximum time to wait for connections to close
     */
    void stop(bool wait_for_connections = true,
              std::chrono::seconds timeout = std::chrono::seconds{30});

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Server Information
    // =========================================================================

    /**
     * @brief Get the listening port
     */
    [[nodiscard]] uint16_t port() const noexcept;

    /**
     * @brief Check if TLS is enabled
     */
    [[nodiscard]] bool is_tls_enabled() const noexcept;

    /**
     * @brief Get server statistics
     */
    [[nodiscard]] mllp_server_statistics statistics() const;

    /**
     * @brief Get information about active sessions
     */
    [[nodiscard]] std::vector<mllp_session_info> active_sessions() const;

    /**
     * @brief Get the current configuration
     */
    [[nodiscard]] const mllp_server_config& config() const noexcept;

    // =========================================================================
    // Connection Management
    // =========================================================================

    /**
     * @brief Close a specific session
     *
     * @param session_id Session identifier
     * @param graceful If true, allow pending operations to complete
     */
    void close_session(uint64_t session_id, bool graceful = true);

    /**
     * @brief Close all active sessions
     *
     * @param graceful If true, allow pending operations to complete
     */
    void close_all_sessions(bool graceful = true);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::mllp

#endif  // PACS_BRIDGE_MLLP_MLLP_SERVER_H
