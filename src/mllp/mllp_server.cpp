/**
 * @file mllp_server.cpp
 * @brief MLLP server implementation for HL7 message reception
 *
 * Provides a TCP server implementation for the Minimal Lower Layer Protocol (MLLP)
 * used in healthcare message exchange. Supports concurrent connections, optional
 * TLS encryption, and comprehensive statistics tracking.
 *
 * This implementation delegates network operations to mllp_network_adapter,
 * focusing on MLLP protocol handling and message processing.
 *
 * @see include/pacs/bridge/mllp/mllp_server.h
 * @see https://github.com/kcenon/pacs_bridge/issues/278
 */

#include "pacs/bridge/mllp/mllp_server.h"

#include "pacs/bridge/mllp/mllp_network_adapter.h"
#include "pacs/bridge/monitoring/bridge_metrics.h"
#include "pacs/bridge/tracing/trace_manager.h"

// Include network adapters
#include "bsd_mllp_server.h"

#ifdef PACS_BRIDGE_HAS_OPENSSL
#include "tls_mllp_server.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::mllp {

// =============================================================================
// IExecutor Job Implementations (when available)
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD

/**
 * @brief Job implementation for session handling
 *
 * Wraps a session processing task for execution via IExecutor.
 */
class mllp_session_job : public kcenon::common::interfaces::IJob {
public:
    explicit mllp_session_job(std::function<void()> handler)
        : handler_(std::move(handler)) {}

    kcenon::common::VoidResult execute() override {
        if (handler_) {
            handler_();
        }
        return std::monostate{};
    }

    std::string get_name() const override { return "mllp_session"; }

private:
    std::function<void()> handler_;
};

#endif  // PACS_BRIDGE_STANDALONE_BUILD

// =============================================================================
// Session Wrapper
// =============================================================================

/**
 * @brief Wrapper for mllp_session with additional MLLP-specific state
 *
 * Manages the receive buffer and per-session statistics for MLLP processing.
 * The underlying network I/O is delegated to mllp_session.
 */
struct session_wrapper {
    uint64_t id = 0;
    std::unique_ptr<mllp_session> session;

    // Receive buffer for partial message accumulation
    std::vector<uint8_t> receive_buffer;
    static constexpr size_t INITIAL_BUFFER_SIZE = 4096;

    // Per-session MLLP statistics
    std::atomic<size_t> messages_received{0};
    std::atomic<size_t> messages_sent{0};

    explicit session_wrapper(std::unique_ptr<mllp_session> sess)
        : id(sess ? sess->session_id() : 0), session(std::move(sess)) {
        receive_buffer.reserve(INITIAL_BUFFER_SIZE);
    }

    [[nodiscard]] mllp_session_info to_session_info() const {
        mllp_session_info info;
        info.session_id = id;
        if (session) {
            info.remote_address = session->remote_address();
            info.remote_port = session->remote_port();
            auto stats = session->get_stats();
            info.connected_at = stats.connected_at;
            info.bytes_received = stats.bytes_received;
            info.bytes_sent = stats.bytes_sent;
        }
        info.messages_received = messages_received.load();
        info.messages_sent = messages_sent.load();
        return info;
    }
};

// =============================================================================
// MLLP Server Implementation
// =============================================================================

/**
 * @brief Private implementation of mllp_server
 *
 * Uses PIMPL idiom to hide implementation details. Delegates network
 * operations to mllp_server_adapter while handling MLLP protocol logic.
 */
class mllp_server::impl {
public:
    explicit impl(const mllp_server_config& config) : config_(config) {}

    ~impl() { stop_internal(true, std::chrono::seconds{5}); }

    // Non-copyable
    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    // =========================================================================
    // Server Lifecycle
    // =========================================================================

    [[nodiscard]] std::expected<void, mllp_error> start() {
        std::lock_guard lock(state_mutex_);

        if (running_) {
            return std::unexpected(mllp_error::already_running);
        }

        if (!config_.is_valid()) {
            return std::unexpected(mllp_error::invalid_configuration);
        }

        // Create appropriate server adapter based on TLS configuration
        if (auto result = create_server_adapter(); !result) {
            return result;
        }

        // Set connection callback
        server_adapter_->on_connection(
            [this](std::unique_ptr<mllp_session> session) {
                handle_new_connection(std::move(session));
            });

        // Start the server adapter
        auto start_result = server_adapter_->start();
        if (!start_result) {
            server_adapter_.reset();
            return std::unexpected(mllp_error::socket_error);
        }

        running_ = true;
        stop_requested_ = false;

        // Update statistics
        stats_.started_at = std::chrono::system_clock::now();

        return {};
    }

    void stop_internal(bool wait_for_connections, std::chrono::seconds timeout) {
        {
            std::lock_guard lock(state_mutex_);
            if (!running_) {
                return;
            }
            stop_requested_ = true;
        }

        // Stop the server adapter (stops accepting new connections)
        if (server_adapter_) {
            server_adapter_->stop(wait_for_connections);
        }

        // Close all sessions to unblock any poll() calls.
        // This causes session threads to exit their receive loop.
        close_all_sessions_internal(false);

        // Wait for all session threads/futures to complete
        {
            std::unique_lock lock(threads_mutex_);
            for (auto& t : session_threads_) {
                if (t.joinable()) {
                    t.join();
                }
            }
            session_threads_.clear();

#ifndef PACS_BRIDGE_STANDALONE_BUILD
            // Wait for executor-based session tasks
            for (auto& f : session_futures_) {
                if (f.valid()) {
                    f.wait_for(std::chrono::seconds{5});
                }
            }
            session_futures_.clear();
#endif
        }

        // Cleanup
        server_adapter_.reset();

        {
            std::lock_guard lock(state_mutex_);
            running_ = false;
        }
    }

    [[nodiscard]] bool is_running() const noexcept {
        std::shared_lock lock(state_mutex_);
        return running_;
    }

    // =========================================================================
    // Handler Registration
    // =========================================================================

    void set_message_handler(message_handler handler) {
        std::lock_guard lock(handlers_mutex_);
        message_handler_ = std::move(handler);
    }

    void set_connection_handler(connection_handler handler) {
        std::lock_guard lock(handlers_mutex_);
        connection_handler_ = std::move(handler);
    }

    void set_error_handler(error_handler handler) {
        std::lock_guard lock(handlers_mutex_);
        error_handler_ = std::move(handler);
    }

    // =========================================================================
    // Server Information
    // =========================================================================

    [[nodiscard]] uint16_t port() const noexcept { return config_.port; }

    [[nodiscard]] bool is_tls_enabled() const noexcept {
        return config_.tls.enabled;
    }

    [[nodiscard]] mllp_server_statistics statistics() const {
        std::lock_guard lock(stats_mutex_);
        mllp_server_statistics result = stats_;

        // Count active connections
        std::shared_lock sessions_lock(sessions_mutex_);
        result.active_connections = sessions_.size();

        return result;
    }

    [[nodiscard]] std::vector<mllp_session_info> active_sessions() const {
        std::vector<mllp_session_info> result;
        std::shared_lock lock(sessions_mutex_);
        result.reserve(sessions_.size());
        for (const auto& [id, wrapper] : sessions_) {
            auto info = wrapper->to_session_info();
            info.local_port = config_.port;
            result.push_back(info);
        }
        return result;
    }

    [[nodiscard]] const mllp_server_config& config() const noexcept {
        return config_;
    }

    // =========================================================================
    // Connection Management
    // =========================================================================

    void close_session(uint64_t session_id, bool graceful) {
        std::unique_lock lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            auto wrapper = std::move(it->second);
            sessions_.erase(it);

            // Update metrics: decrement active connection count
            monitoring::bridge_metrics_collector::instance()
                .set_mllp_active_connections(sessions_.size());

            lock.unlock();

            // Notify disconnection
            notify_connection(wrapper->to_session_info(), false);

            // Close session
            if (wrapper->session && !graceful) {
                wrapper->session->close();
            }
        }
    }

    void close_all_sessions_internal(bool graceful) {
        std::unique_lock lock(sessions_mutex_);
        auto sessions_copy = std::move(sessions_);
        sessions_.clear();

        // Update metrics: all connections closed
        monitoring::bridge_metrics_collector::instance()
            .set_mllp_active_connections(0);

        lock.unlock();

        for (auto& [id, wrapper] : sessions_copy) {
            notify_connection(wrapper->to_session_info(), false);
            if (wrapper->session && !graceful) {
                wrapper->session->close();
            }
        }
    }

private:
    // =========================================================================
    // Server Adapter Creation
    // =========================================================================

    [[nodiscard]] std::expected<void, mllp_error> create_server_adapter() {
        // Create server_config from mllp_server_config
        server_config adapter_config;
        adapter_config.port = config_.port;
        adapter_config.bind_address = config_.bind_address;
        adapter_config.backlog = static_cast<int>(config_.max_connections);
        adapter_config.keep_alive = true;
        adapter_config.no_delay = true;
        adapter_config.reuse_addr = true;

        if (config_.tls.enabled) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
            server_adapter_ = std::make_unique<tls_mllp_server>(
                adapter_config, config_.tls);
#else
            // TLS requested but OpenSSL not available
            return std::unexpected(mllp_error::invalid_configuration);
#endif
        } else {
            server_adapter_ = std::make_unique<bsd_mllp_server>(adapter_config);
        }

        return {};
    }

    // =========================================================================
    // Connection Handling
    // =========================================================================

    void handle_new_connection(std::unique_ptr<mllp_session> session) {
        if (!session) {
            return;
        }

        // Check max connections
        {
            std::shared_lock lock(sessions_mutex_);
            if (sessions_.size() >= config_.max_connections) {
                // Reject connection - at capacity
                increment_stat(&stats_.connection_errors);
                session->close();
                return;
            }
        }

        // Update statistics
        increment_stat(&stats_.total_connections);

        // Create session wrapper
        auto wrapper = std::make_unique<session_wrapper>(std::move(session));
        uint64_t session_id = wrapper->id;

        // Notify connection handler
        notify_connection(wrapper->to_session_info(), true);

        // Store session
        {
            std::unique_lock lock(sessions_mutex_);
            sessions_[session_id] = std::move(wrapper);

            // Update metrics
            auto& metrics = monitoring::bridge_metrics_collector::instance();
            metrics.record_mllp_connection();
            metrics.set_mllp_active_connections(sessions_.size());
        }

        // Start session handler
#ifndef PACS_BRIDGE_STANDALONE_BUILD
        if (config_.executor) {
            auto job = std::make_unique<mllp_session_job>([this, session_id] {
                handle_session(session_id);
            });
            auto result = config_.executor->execute(std::move(job));
            if (result.is_ok()) {
                std::lock_guard lock(threads_mutex_);
                session_futures_.push_back(std::move(result.value()));
            }
        } else {
            std::lock_guard lock(threads_mutex_);
            session_threads_.emplace_back([this, session_id] {
                handle_session(session_id);
            });
        }
#else
        {
            std::lock_guard lock(threads_mutex_);
            session_threads_.emplace_back([this, session_id] {
                handle_session(session_id);
            });
        }
#endif
    }

    // =========================================================================
    // Session Handling
    // =========================================================================

    void handle_session(uint64_t session_id) {
        constexpr size_t READ_BUFFER_SIZE = 8192;
        auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            config_.idle_timeout);

        while (!stop_requested_) {
            // Get session wrapper
            session_wrapper* wrapper_ptr = nullptr;
            {
                std::shared_lock lock(sessions_mutex_);
                auto it = sessions_.find(session_id);
                if (it == sessions_.end()) {
                    break;
                }
                wrapper_ptr = it->second.get();
            }

            if (!wrapper_ptr || !wrapper_ptr->session ||
                !wrapper_ptr->session->is_open()) {
                break;
            }

            // Read data from session
            auto read_result =
                wrapper_ptr->session->receive(READ_BUFFER_SIZE, timeout);

            if (!read_result) {
                auto error = read_result.error();
                if (error == network_error::timeout) {
                    // Idle timeout reached - close session
                    // This matches the original behavior where SO_RCVTIMEO
                    // expiration would close the connection
                    break;
                }
                if (error == network_error::connection_closed) {
                    // Clean disconnect
                    break;
                }
                // Other error
                increment_stat(&stats_.connection_errors);
                break;
            }

            const auto& data = read_result.value();
            if (data.empty()) {
                continue;
            }

            // Update statistics
            add_stat(&stats_.bytes_received, data.size());

            // Append to receive buffer
            wrapper_ptr->receive_buffer.insert(wrapper_ptr->receive_buffer.end(),
                                               data.begin(), data.end());

            // Check buffer size limit
            if (wrapper_ptr->receive_buffer.size() > config_.max_message_size) {
                increment_stat(&stats_.protocol_errors);
                notify_error(mllp_error::message_too_large,
                             wrapper_ptr->to_session_info(),
                             "Message exceeds maximum size");
                break;
            }

            // Process complete MLLP messages
            process_messages(wrapper_ptr);
        }

        // Close session
        close_session(session_id, true);
    }

    void process_messages(session_wrapper* wrapper) {
        auto& buffer = wrapper->receive_buffer;

        while (true) {
            // Find start of message (VT byte)
            auto start_it =
                std::find(buffer.begin(), buffer.end(),
                          static_cast<uint8_t>(MLLP_START_BYTE));
            if (start_it == buffer.end()) {
                // No start marker found, clear any garbage data
                buffer.clear();
                break;
            }

            // Remove any data before start marker
            if (start_it != buffer.begin()) {
                buffer.erase(buffer.begin(), start_it);
            }

            // Find end of message (FS + CR)
            auto end_it = buffer.end();
            for (auto it = buffer.begin() + 1; it < buffer.end() - 1; ++it) {
                if (*it == static_cast<uint8_t>(MLLP_END_BYTE) &&
                    *(it + 1) == static_cast<uint8_t>(MLLP_CARRIAGE_RETURN)) {
                    end_it = it;
                    break;
                }
            }

            if (end_it == buffer.end()) {
                // No complete message yet
                break;
            }

            // Start tracing span for message receive
            auto span = tracing::trace_manager::instance().start_span(
                "mllp_receive", tracing::span_kind::server);
            span.set_attribute("mllp.port", static_cast<int64_t>(config_.port))
                .set_attribute("mllp.remote_address",
                               wrapper->session->remote_address())
                .set_attribute("mllp.remote_port",
                               static_cast<int64_t>(wrapper->session->remote_port()))
                .set_attribute("mllp.session_id",
                               static_cast<int64_t>(wrapper->id));

            // Extract message content (between VT and FS)
            mllp_message msg;
            msg.content.assign(buffer.begin() + 1, end_it);
            msg.session = wrapper->to_session_info();
            msg.received_at = std::chrono::system_clock::now();

            // Add message size to span
            span.set_attribute("mllp.message_size",
                               static_cast<int64_t>(msg.content.size()));

            // Remove processed message from buffer (including CR)
            buffer.erase(buffer.begin(), end_it + 2);

            // Update statistics
            wrapper->messages_received++;
            increment_stat(&stats_.messages_received);

            // Call message handler
            std::optional<mllp_message> response;
            {
                std::shared_lock lock(handlers_mutex_);
                if (message_handler_) {
                    response = message_handler_(msg, *msg.session);
                }
            }

            // Send response if provided
            if (response) {
                send_response(wrapper, *response);
                span.set_attribute("mllp.response_sent", true);
            } else {
                span.set_attribute("mllp.response_sent", false);
            }

            // Span ends automatically via RAII
        }
    }

    void send_response(session_wrapper* wrapper, const mllp_message& response) {
        if (!wrapper->session || !wrapper->session->is_open()) {
            return;
        }

        auto framed = response.frame();
        auto send_result = wrapper->session->send(framed);

        if (send_result) {
            wrapper->messages_sent++;
            increment_stat(&stats_.messages_sent);
            add_stat(&stats_.bytes_sent, send_result.value());
        }
    }

    // =========================================================================
    // Statistics Helpers
    // =========================================================================

    void increment_stat(size_t* stat) {
        std::lock_guard lock(stats_mutex_);
        (*stat)++;
    }

    void add_stat(size_t* stat, size_t value) {
        std::lock_guard lock(stats_mutex_);
        (*stat) += value;
    }

    // =========================================================================
    // Handler Notification
    // =========================================================================

    void notify_connection(const mllp_session_info& session, bool connected) {
        std::shared_lock lock(handlers_mutex_);
        if (connection_handler_) {
            connection_handler_(session, connected);
        }
    }

    void notify_error(mllp_error error,
                      const std::optional<mllp_session_info>& session,
                      std::string_view details) {
        std::shared_lock lock(handlers_mutex_);
        if (error_handler_) {
            error_handler_(error, session, details);
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    mllp_server_config config_;

    // Network adapter (BSD or TLS)
    std::unique_ptr<mllp_server_adapter> server_adapter_;

    // State management
    mutable std::shared_mutex state_mutex_;
    bool running_ = false;
    std::atomic<bool> stop_requested_{false};

    // Threads
    std::mutex threads_mutex_;
    std::vector<std::thread> session_threads_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    // Futures for executor-based session tasks
    std::vector<std::future<void>> session_futures_;
#endif

    // Sessions
    mutable std::shared_mutex sessions_mutex_;
    std::unordered_map<uint64_t, std::unique_ptr<session_wrapper>> sessions_;

    // Handlers
    mutable std::shared_mutex handlers_mutex_;
    message_handler message_handler_;
    connection_handler connection_handler_;
    error_handler error_handler_;

    // Statistics
    mutable std::mutex stats_mutex_;
    mllp_server_statistics stats_;
};

// =============================================================================
// MLLP Server Public Interface
// =============================================================================

mllp_server::mllp_server(const mllp_server_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

mllp_server::~mllp_server() = default;

mllp_server::mllp_server(mllp_server&&) noexcept = default;
mllp_server& mllp_server::operator=(mllp_server&&) noexcept = default;

void mllp_server::set_message_handler(message_handler handler) {
    pimpl_->set_message_handler(std::move(handler));
}

void mllp_server::set_connection_handler(connection_handler handler) {
    pimpl_->set_connection_handler(std::move(handler));
}

void mllp_server::set_error_handler(error_handler handler) {
    pimpl_->set_error_handler(std::move(handler));
}

std::expected<void, mllp_error> mllp_server::start() {
    return pimpl_->start();
}

void mllp_server::stop(bool wait_for_connections, std::chrono::seconds timeout) {
    pimpl_->stop_internal(wait_for_connections, timeout);
}

bool mllp_server::is_running() const noexcept {
    return pimpl_->is_running();
}

uint16_t mllp_server::port() const noexcept {
    return pimpl_->port();
}

bool mllp_server::is_tls_enabled() const noexcept {
    return pimpl_->is_tls_enabled();
}

mllp_server_statistics mllp_server::statistics() const {
    return pimpl_->statistics();
}

std::vector<mllp_session_info> mllp_server::active_sessions() const {
    return pimpl_->active_sessions();
}

const mllp_server_config& mllp_server::config() const noexcept {
    return pimpl_->config();
}

void mllp_server::close_session(uint64_t session_id, bool graceful) {
    pimpl_->close_session(session_id, graceful);
}

void mllp_server::close_all_sessions(bool graceful) {
    pimpl_->close_all_sessions_internal(graceful);
}

}  // namespace pacs::bridge::mllp
