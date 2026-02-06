/**
 * @file network_system_mllp_server.cpp
 * @brief Implementation of network_system-based MLLP network adapter
 *
 * Bridges kcenon ecosystem's async ASIO networking with the synchronous
 * mllp_session interface using producer-consumer buffering.
 *
 * @see network_system_mllp_server.h
 * @see https://github.com/kcenon/pacs_bridge/issues/279
 */

#ifndef PACS_BRIDGE_STANDALONE_BUILD

#include "network_system_mllp_server.h"

#include <kcenon/network/detail/session/messaging_session.h>

#include <asio.hpp>

#include <algorithm>
#include <thread>

namespace pacs::bridge::mllp {

// =============================================================================
// network_system_session Implementation
// =============================================================================

network_system_session::network_system_session(
    std::shared_ptr<kcenon::network::session::messaging_session> session,
    uint64_t session_id,
    std::string remote_addr,
    uint16_t remote_port,
    std::shared_ptr<std::atomic<size_t>> active_count)
    : session_(std::move(session))
    , session_id_(session_id)
    , remote_addr_(std::move(remote_addr))
    , remote_port_(remote_port)
    , active_count_(std::move(active_count))
{
    stats_.connected_at = std::chrono::system_clock::now();
    stats_.last_activity = stats_.connected_at;

    // Bridge async callbacks → synchronous receive buffer.
    // The receive callback fires on the ASIO io_context thread,
    // while receive() blocks on the MLLP session handler thread.
    session_->set_receive_callback(
        [this](const std::vector<uint8_t>& data) {
            std::lock_guard lock(buffer_mutex_);
            if (!closed_.load(std::memory_order_relaxed)) {
                receive_buffer_.insert(
                    receive_buffer_.end(), data.begin(), data.end());
                buffer_cv_.notify_one();
            }
        });

    session_->set_disconnection_callback(
        [this](const std::string& /*session_id*/) {
            closed_.store(true, std::memory_order_release);
            std::lock_guard lock(buffer_mutex_);
            buffer_cv_.notify_all();
        });

    session_->set_error_callback(
        [this](std::error_code /*ec*/) {
            closed_.store(true, std::memory_order_release);
            std::lock_guard lock(buffer_mutex_);
            buffer_cv_.notify_all();
        });
}

network_system_session::~network_system_session() {
    close();
    if (active_count_) {
        active_count_->fetch_sub(1, std::memory_order_relaxed);
    }
}

std::expected<std::vector<uint8_t>, network_error>
network_system_session::receive(size_t max_bytes,
                                std::chrono::milliseconds timeout) {
    std::unique_lock lock(buffer_mutex_);

    auto deadline = std::chrono::steady_clock::now() + timeout;

    // Wait until data arrives, connection closes, or timeout
    while (receive_buffer_.empty()) {
        if (closed_.load(std::memory_order_acquire)) {
            return std::unexpected(network_error::connection_closed);
        }

        if (buffer_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            if (receive_buffer_.empty()) {
                return std::unexpected(network_error::timeout);
            }
            break;  // Data arrived just before timeout
        }
    }

    if (receive_buffer_.empty()) {
        return std::unexpected(network_error::connection_closed);
    }

    // Extract up to max_bytes from the buffer
    size_t to_read = std::min(max_bytes, receive_buffer_.size());
    std::vector<uint8_t> result(
        receive_buffer_.begin(),
        receive_buffer_.begin() + static_cast<ptrdiff_t>(to_read));
    receive_buffer_.erase(
        receive_buffer_.begin(),
        receive_buffer_.begin() + static_cast<ptrdiff_t>(to_read));

    // Update statistics
    {
        std::lock_guard stats_lock(stats_mutex_);
        stats_.bytes_received += to_read;
        stats_.messages_received++;
        stats_.last_activity = std::chrono::system_clock::now();
    }

    return result;
}

std::expected<size_t, network_error>
network_system_session::send(std::span<const uint8_t> data) {
    if (closed_.load(std::memory_order_acquire) || session_->is_stopped()) {
        return std::unexpected(network_error::connection_closed);
    }

    // Copy span data to vector (send_packet takes vector by rvalue ref)
    std::vector<uint8_t> buffer(data.begin(), data.end());
    size_t bytes = buffer.size();
    session_->send_packet(std::move(buffer));

    // Update statistics
    {
        std::lock_guard lock(stats_mutex_);
        stats_.bytes_sent += bytes;
        stats_.messages_sent++;
        stats_.last_activity = std::chrono::system_clock::now();
    }

    return bytes;
}

void network_system_session::close() {
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true)) {
        return;  // Already closed
    }

    if (session_) {
        session_->stop_session();
    }

    // Unblock any waiting receive() call
    std::lock_guard lock(buffer_mutex_);
    buffer_cv_.notify_all();
}

bool network_system_session::is_open() const noexcept {
    return !closed_.load(std::memory_order_acquire);
}

session_stats network_system_session::get_stats() const noexcept {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

std::string network_system_session::remote_address() const noexcept {
    return remote_addr_;
}

uint16_t network_system_session::remote_port() const noexcept {
    return remote_port_;
}

uint64_t network_system_session::session_id() const noexcept {
    return session_id_;
}

// =============================================================================
// network_system_mllp_server Implementation
// =============================================================================

/**
 * @brief PIMPL for ASIO types
 *
 * Hides asio::io_context, acceptor, work_guard, and thread from the header,
 * so consumers of the header don't need ASIO headers.
 */
struct network_system_mllp_server::asio_impl {
    asio::io_context io_context;
    std::unique_ptr<
        asio::executor_work_guard<asio::io_context::executor_type>>
        work_guard;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
    std::thread io_thread;
};

network_system_mllp_server::network_system_mllp_server(
    const server_config& config)
    : asio_(std::make_unique<asio_impl>())
    , config_(config)
    , active_sessions_(std::make_shared<std::atomic<size_t>>(0))
{}

network_system_mllp_server::~network_system_mllp_server() {
    stop(false);
}

std::expected<void, network_error> network_system_mllp_server::start() {
    if (running_.load()) {
        return std::unexpected(network_error::invalid_config);
    }

    if (!config_.is_valid()) {
        return std::unexpected(network_error::invalid_config);
    }

    try {
        // Keep io_context alive while server is running
        asio_->work_guard = std::make_unique<
            asio::executor_work_guard<asio::io_context::executor_type>>(
            asio_->io_context.get_executor());

        // Resolve bind endpoint
        asio::ip::tcp::endpoint endpoint;
        if (config_.bind_address.empty()) {
            endpoint = asio::ip::tcp::endpoint(
                asio::ip::tcp::v4(), config_.port);
        } else {
            endpoint = asio::ip::tcp::endpoint(
                asio::ip::make_address(config_.bind_address), config_.port);
        }

        // Create and configure acceptor
        asio_->acceptor = std::make_unique<asio::ip::tcp::acceptor>(
            asio_->io_context);
        asio_->acceptor->open(endpoint.protocol());

        if (config_.reuse_addr) {
            asio_->acceptor->set_option(
                asio::socket_base::reuse_address(true));
        }

        asio_->acceptor->bind(endpoint);
        asio_->acceptor->listen(config_.backlog);

        running_.store(true);

        // Begin async accept loop
        start_accept();

        // Run io_context on dedicated thread
        asio_->io_thread = std::thread([this] {
            asio_->io_context.run();
        });

        return {};
    } catch (const asio::system_error& e) {
        // Clean up on failure
        asio_->work_guard.reset();
        asio_->acceptor.reset();

        if (e.code() == asio::error::address_in_use) {
            return std::unexpected(network_error::bind_failed);
        }
        return std::unexpected(network_error::socket_error);
    }
}

void network_system_mllp_server::stop(bool /*wait_for_connections*/) {
    if (!running_.exchange(false)) {
        return;
    }

    // Close acceptor to cancel pending async_accept
    if (asio_->acceptor && asio_->acceptor->is_open()) {
        std::error_code ec;
        asio_->acceptor->close(ec);
    }

    // Release work guard so io_context::run() can return
    asio_->work_guard.reset();

    // Wait for io thread to finish all pending handlers
    if (asio_->io_thread.joinable()) {
        asio_->io_thread.join();
    }

    // Reset io_context for potential restart
    asio_->io_context.restart();
}

bool network_system_mllp_server::is_running() const noexcept {
    return running_.load();
}

uint16_t network_system_mllp_server::port() const noexcept {
    return config_.port;
}

void network_system_mllp_server::on_connection(
    on_connection_callback callback) {
    connection_callback_ = std::move(callback);
}

size_t network_system_mllp_server::active_session_count() const noexcept {
    return active_sessions_->load(std::memory_order_relaxed);
}

void network_system_mllp_server::start_accept() {
    asio_->acceptor->async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec && running_.load()) {
                // Capture remote endpoint before moving the socket
                std::string remote_addr;
                uint16_t remote_port = 0;
                try {
                    auto ep = socket.remote_endpoint();
                    remote_addr = ep.address().to_string();
                    remote_port = ep.port();
                } catch (...) {
                    // Socket may have been closed before we read endpoint
                }

                // Apply socket options from server_config
                configure_socket(socket);

                uint64_t sid = next_session_id_.fetch_add(1);

                // Create messaging_session from the accepted socket
                auto ms = std::make_shared<
                    kcenon::network::session::messaging_session>(
                    std::move(socket), "mllp_network_system");

                // Wrap in MLLP session (sets up async callbacks on ms)
                auto session =
                    std::make_unique<network_system_session>(
                        ms, sid, std::move(remote_addr), remote_port,
                        active_sessions_);

                // Start async reads on the messaging_session
                ms->start_session();

                // Deliver to MLLP server
                active_sessions_->fetch_add(1, std::memory_order_relaxed);
                if (connection_callback_) {
                    connection_callback_(std::move(session));
                }
            }

            // Continue accepting if still running
            if (running_.load()) {
                start_accept();
            }
        });
}

void network_system_mllp_server::configure_socket(auto& socket) {
    try {
        if (config_.no_delay) {
            socket.set_option(asio::ip::tcp::no_delay(true));
        }
        if (config_.keep_alive) {
            socket.set_option(asio::socket_base::keep_alive(true));
        }
    } catch (...) {
        // Non-fatal: proceed with OS defaults
    }
}

}  // namespace pacs::bridge::mllp

#endif  // PACS_BRIDGE_STANDALONE_BUILD
