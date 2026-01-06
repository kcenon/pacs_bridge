#ifndef PACS_BRIDGE_MLLP_MLLP_CLIENT_H
#define PACS_BRIDGE_MLLP_MLLP_CLIENT_H

/**
 * @file mllp_client.h
 * @brief MLLP client for sending HL7 messages with optional TLS support
 *
 * Provides a client implementation for sending HL7 messages using the
 * Minimal Lower Layer Protocol (MLLP). Supports connection pooling,
 * automatic retry, and TLS encryption for HIPAA compliance.
 *
 * Features:
 *   - Connection pooling for performance
 *   - TLS 1.2/1.3 support for secure connections
 *   - Automatic reconnection on failure
 *   - Configurable retry with exponential backoff
 *   - Synchronous and asynchronous send operations
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/38
 * @see docs/reference_materials/04_mllp_protocol.md
 */

#include "mllp_types.h"
#include "pacs/bridge/security/tls_context.h"

#include <expected>
#include <functional>
#include <future>
#include <memory>

namespace pacs::bridge::mllp {

/**
 * @brief MLLP client for HL7 message transmission
 *
 * Connects to an MLLP server and sends HL7 messages, receiving
 * acknowledgment responses. Supports both blocking and async operations.
 *
 * @example Basic Usage
 * ```cpp
 * mllp_client_config config;
 * config.host = "ris.hospital.local";
 * config.port = 2575;
 * config.connect_timeout = std::chrono::milliseconds{5000};
 *
 * mllp_client client(config);
 *
 * auto connect_result = client.connect();
 * if (connect_result) {
 *     auto send_result = client.send(hl7_message);
 *     if (send_result) {
 *         auto& ack = send_result.value();
 *         // Process acknowledgment
 *     }
 * }
 * ```
 *
 * @example TLS-Enabled Client
 * ```cpp
 * mllp_client_config config;
 * config.host = "ris.hospital.local";
 * config.port = 2576;
 * config.tls.enabled = true;
 * config.tls.ca_path = "/etc/pacs/ca.crt";
 * config.tls.verify_peer = true;
 * // Optional: client certificate for mutual TLS
 * config.tls.cert_path = "/etc/pacs/client.crt";
 * config.tls.key_path = "/etc/pacs/client.key";
 *
 * mllp_client client(config);
 * client.connect();
 * ```
 */
class mllp_client {
public:
    /**
     * @brief Send result containing response and timing
     */
    struct send_result {
        /** Response message (ACK/NAK) */
        mllp_message response;

        /** Round-trip time for the send operation */
        std::chrono::milliseconds round_trip_time;

        /** Number of retry attempts needed */
        size_t retry_count = 0;
    };

    /**
     * @brief Constructor
     * @param config Client configuration
     */
    explicit mllp_client(const mllp_client_config& config);

    /**
     * @brief Destructor - closes connection
     */
    ~mllp_client();

    // Non-copyable
    mllp_client(const mllp_client&) = delete;
    mllp_client& operator=(const mllp_client&) = delete;

    // Movable
    mllp_client(mllp_client&&) noexcept;
    mllp_client& operator=(mllp_client&&) noexcept;

    // =========================================================================
    // Connection Management
    // =========================================================================

    /**
     * @brief Establish connection to the MLLP server
     *
     * Connects to the configured host and port. If TLS is enabled,
     * performs TLS handshake.
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, mllp_error> connect();

    /**
     * @brief Close the connection
     *
     * @param graceful If true, send TLS close_notify before closing
     */
    void disconnect(bool graceful = true);

    /**
     * @brief Check if connected
     */
    [[nodiscard]] bool is_connected() const noexcept;

    /**
     * @brief Reconnect after disconnection
     *
     * Closes existing connection (if any) and establishes a new one.
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, mllp_error> reconnect();

    // =========================================================================
    // Message Operations
    // =========================================================================

    /**
     * @brief Send an HL7 message and wait for response
     *
     * Sends the message using MLLP framing and waits for the server's
     * acknowledgment response.
     *
     * @param message HL7 message to send
     * @return Response message and timing, or error
     */
    [[nodiscard]] std::expected<send_result, mllp_error>
    send(const mllp_message& message);

    /**
     * @brief Send an HL7 message string and wait for response
     *
     * Convenience overload that accepts a string.
     *
     * @param hl7_content HL7 message content
     * @return Response message and timing, or error
     */
    [[nodiscard]] std::expected<send_result, mllp_error>
    send(std::string_view hl7_content);

    /**
     * @brief Send message asynchronously
     *
     * Returns immediately with a future that will contain the result.
     *
     * @param message HL7 message to send
     * @return Future containing response or error
     */
    [[nodiscard]] std::future<std::expected<send_result, mllp_error>>
    send_async(const mllp_message& message);

    /**
     * @brief Send message without waiting for response
     *
     * Sends the message and returns immediately without reading response.
     * Use when acknowledgment is not required.
     *
     * @param message HL7 message to send
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, mllp_error>
    send_no_ack(const mllp_message& message);

    // =========================================================================
    // Connection Information
    // =========================================================================

    /**
     * @brief Get session information
     *
     * Returns information about the current connection.
     *
     * @return Session info or nullopt if not connected
     */
    [[nodiscard]] std::optional<mllp_session_info> session_info() const;

    /**
     * @brief Get the current configuration
     */
    [[nodiscard]] const mllp_client_config& config() const noexcept;

    /**
     * @brief Check if TLS is enabled and active
     */
    [[nodiscard]] bool is_tls_active() const noexcept;

    /**
     * @brief Get TLS protocol version (if TLS active)
     */
    [[nodiscard]] std::optional<std::string> tls_version() const;

    /**
     * @brief Get TLS cipher suite (if TLS active)
     */
    [[nodiscard]] std::optional<std::string> tls_cipher() const;

    /**
     * @brief Get server certificate info (if TLS active)
     */
    [[nodiscard]] std::optional<security::certificate_info>
    server_certificate() const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Client statistics
     */
    struct statistics {
        /** Total messages sent */
        size_t messages_sent = 0;

        /** Total messages received (ACKs) */
        size_t messages_received = 0;

        /** Total bytes sent */
        size_t bytes_sent = 0;

        /** Total bytes received */
        size_t bytes_received = 0;

        /** Send errors */
        size_t send_errors = 0;

        /** Connection attempts */
        size_t connect_attempts = 0;

        /** Successful connections */
        size_t connect_successes = 0;

        /** Reconnections */
        size_t reconnections = 0;

        /** Average round-trip time in milliseconds */
        double avg_round_trip_ms = 0.0;
    };

    /**
     * @brief Get client statistics
     */
    [[nodiscard]] statistics get_statistics() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Connection Pool (for high-throughput scenarios)
// =============================================================================

/**
 * @brief Configuration for MLLP connection pool
 */
struct mllp_pool_config {
    /** Client configuration template */
    mllp_client_config client_config;

    /** Minimum number of connections to maintain */
    size_t min_connections = 1;

    /** Maximum number of connections */
    size_t max_connections = 10;

    /** Connection idle timeout before closing */
    std::chrono::seconds idle_timeout{60};

    /** Health check interval */
    std::chrono::seconds health_check_interval{30};

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    /** Optional executor for task execution (nullptr = use internal std::thread) */
    std::shared_ptr<kcenon::common::interfaces::IExecutor> executor;
#endif
};

/**
 * @brief Connection pool for high-throughput MLLP operations
 *
 * Maintains a pool of MLLP connections for efficient message sending.
 * Connections are reused across multiple send operations.
 */
class mllp_connection_pool {
public:
    /**
     * @brief Constructor
     * @param config Pool configuration
     */
    explicit mllp_connection_pool(const mllp_pool_config& config);

    /**
     * @brief Destructor - closes all connections
     */
    ~mllp_connection_pool();

    // Non-copyable, non-movable
    mllp_connection_pool(const mllp_connection_pool&) = delete;
    mllp_connection_pool& operator=(const mllp_connection_pool&) = delete;

    /**
     * @brief Send message using pooled connection
     *
     * Acquires a connection from the pool, sends the message,
     * and returns the connection to the pool.
     *
     * @param message Message to send
     * @return Response or error
     */
    [[nodiscard]] std::expected<mllp_client::send_result, mllp_error>
    send(const mllp_message& message);

    /**
     * @brief Get current pool statistics
     */
    struct pool_statistics {
        size_t active_connections = 0;
        size_t idle_connections = 0;
        size_t total_created = 0;
        size_t total_closed = 0;
        size_t waiting_requests = 0;
    };

    [[nodiscard]] pool_statistics statistics() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::mllp

#endif  // PACS_BRIDGE_MLLP_MLLP_CLIENT_H
