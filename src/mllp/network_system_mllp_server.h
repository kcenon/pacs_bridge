#ifndef PACS_BRIDGE_MLLP_NETWORK_SYSTEM_MLLP_SERVER_H
#define PACS_BRIDGE_MLLP_NETWORK_SYSTEM_MLLP_SERVER_H

/**
 * @file network_system_mllp_server.h
 * @brief network_system-based implementation of MLLP network adapter
 *
 * Provides concrete implementations of mllp_session and mllp_server_adapter
 * using kcenon ecosystem's network_system (ASIO-based messaging sessions).
 *
 * This implementation bridges the async callback model of network_system
 * with the synchronous receive() interface of mllp_session, using a
 * producer-consumer pattern with mutex + condition_variable.
 *
 * Only available when building with kcenon ecosystem dependencies
 * (BRIDGE_STANDALONE_BUILD=OFF).
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/279
 */

#ifndef PACS_BRIDGE_STANDALONE_BUILD

#include "pacs/bridge/mllp/mllp_network_adapter.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations to avoid heavy ASIO headers in this header.
// Full definitions are included in the .cpp file.
namespace kcenon::network::session {
class messaging_session;
}

namespace pacs::bridge::mllp {

// =============================================================================
// network_system Session Implementation
// =============================================================================

/**
 * @brief network_system-based implementation of mllp_session
 *
 * Wraps a messaging_session from kcenon's network_system. Bridges the
 * async receive callback into a synchronous receive() call using a
 * producer-consumer buffer pattern (same approach as network_adapter.cpp).
 *
 * Lifecycle:
 * - ASIO io_context thread pushes received data into the buffer via callback
 * - MLLP session handler thread consumes data via synchronous receive()
 * - close() stops the underlying session and unblocks any waiting receive()
 */
class network_system_session : public mllp_session {
public:
    /**
     * @brief Constructor
     *
     * Sets up receive/disconnect/error callbacks on the messaging_session
     * to bridge async I/O into the synchronous mllp_session interface.
     *
     * @param session Connected messaging_session (callbacks will be set)
     * @param session_id Unique identifier for this session
     * @param remote_addr Remote peer address (captured at accept time)
     * @param remote_port Remote peer port (captured at accept time)
     * @param active_count Shared counter for active session tracking
     */
    network_system_session(
        std::shared_ptr<kcenon::network::session::messaging_session> session,
        uint64_t session_id,
        std::string remote_addr,
        uint16_t remote_port,
        std::shared_ptr<std::atomic<size_t>> active_count);

    ~network_system_session() override;

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
    std::shared_ptr<kcenon::network::session::messaging_session> session_;
    uint64_t session_id_;
    std::string remote_addr_;
    uint16_t remote_port_;

    // Producer-consumer buffer for async → sync bridging
    mutable std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    std::vector<uint8_t> receive_buffer_;

    std::atomic<bool> closed_{false};

    // Statistics (thread-safe)
    mutable std::mutex stats_mutex_;
    session_stats stats_;

    // Shared active session counter (decremented in destructor)
    std::shared_ptr<std::atomic<size_t>> active_count_;
};

// =============================================================================
// network_system Server Adapter Implementation
// =============================================================================

/**
 * @brief network_system-based implementation of mllp_server_adapter
 *
 * Manages an ASIO TCP acceptor directly (rather than using messaging_server)
 * to capture remote endpoint information at accept time. Accepted sockets
 * are wrapped in messaging_session instances for async I/O.
 *
 * Architecture:
 * - Own asio::io_context + background thread for ASIO operations
 * - async_accept loop creates messaging_session per connection
 * - Each messaging_session is wrapped in network_system_session
 * - Sessions delivered to MLLP server via on_connection callback
 */
class network_system_mllp_server : public mllp_server_adapter {
public:
    explicit network_system_mllp_server(const server_config& config);

    ~network_system_mllp_server() override;

    // Implement mllp_server_adapter interface
    [[nodiscard]] std::expected<void, network_error> start() override;

    void stop(bool wait_for_connections = true) override;

    [[nodiscard]] bool is_running() const noexcept override;

    [[nodiscard]] uint16_t port() const noexcept override;

    void on_connection(on_connection_callback callback) override;

    [[nodiscard]] size_t active_session_count() const noexcept override;

private:
    /**
     * @brief Post an async_accept on the acceptor
     */
    void start_accept();

    /**
     * @brief Configure socket options on an accepted connection
     */
    void configure_socket(auto& socket);

    // PIMPL to hide ASIO types from the header
    struct asio_impl;
    std::unique_ptr<asio_impl> asio_;

    server_config config_;
    on_connection_callback connection_callback_;
    std::atomic<bool> running_{false};
    std::shared_ptr<std::atomic<size_t>> active_sessions_;
    std::atomic<uint64_t> next_session_id_{1};
};

}  // namespace pacs::bridge::mllp

#endif  // PACS_BRIDGE_STANDALONE_BUILD
#endif  // PACS_BRIDGE_MLLP_NETWORK_SYSTEM_MLLP_SERVER_H
