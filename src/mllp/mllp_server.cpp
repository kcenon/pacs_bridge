/**
 * @file mllp_server.cpp
 * @brief MLLP server implementation for HL7 message reception
 *
 * Provides a TCP server implementation for the Minimal Lower Layer Protocol (MLLP)
 * used in healthcare message exchange. Supports concurrent connections, optional
 * TLS encryption, and comprehensive statistics tracking.
 *
 * Key implementation details:
 * - Uses BSD sockets for cross-platform TCP networking
 * - Implements proper MLLP frame detection with VT/FS/CR markers
 * - Supports TLS 1.2/1.3 via OpenSSL when PACS_BRIDGE_HAS_OPENSSL is defined
 * - Thread-safe statistics using std::atomic and std::mutex
 * - Connection management with configurable timeouts
 *
 * @see include/pacs/bridge/mllp/mllp_server.h
 * @see https://github.com/kcenon/pacs_bridge/issues/12
 */

#include "pacs/bridge/mllp/mllp_server.h"

#include "pacs/bridge/monitoring/bridge_metrics.h"
#include "pacs/bridge/tracing/trace_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// Platform-specific socket headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t INVALID_SOCKET_VALUE = -1;
#endif

// OpenSSL headers for TLS support
#ifdef PACS_BRIDGE_HAS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

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
// Internal Session State
// =============================================================================

/**
 * @brief Internal session representation for active connections
 *
 * Manages the state of a single client connection including socket,
 * TLS state, receive buffer, and per-session statistics.
 */
struct internal_session {
    uint64_t id = 0;
    socket_t socket = INVALID_SOCKET_VALUE;
    std::string remote_address;
    uint16_t remote_port = 0;
    std::chrono::system_clock::time_point connected_at;
    std::chrono::system_clock::time_point last_activity;

    // Receive buffer for partial message accumulation
    std::vector<uint8_t> receive_buffer;
    static constexpr size_t INITIAL_BUFFER_SIZE = 4096;

    // Per-session statistics
    std::atomic<size_t> messages_received{0};
    std::atomic<size_t> messages_sent{0};
    std::atomic<size_t> bytes_received{0};
    std::atomic<size_t> bytes_sent{0};

    // TLS state
    bool tls_enabled = false;
#ifdef PACS_BRIDGE_HAS_OPENSSL
    SSL* ssl = nullptr;
#else
    void* ssl = nullptr;
#endif
    std::string tls_version_str;
    std::string tls_cipher_str;
    std::string peer_cert_subject;

    internal_session() { receive_buffer.reserve(INITIAL_BUFFER_SIZE); }

    ~internal_session() { close_socket(); }

    void close_socket() {
#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            ssl = nullptr;
        }
#endif
        if (socket != INVALID_SOCKET_VALUE) {
#ifdef _WIN32
            closesocket(socket);
#else
            ::close(socket);
#endif
            socket = INVALID_SOCKET_VALUE;
        }
    }

    [[nodiscard]] mllp_session_info to_session_info() const {
        mllp_session_info info;
        info.session_id = id;
        info.remote_address = remote_address;
        info.remote_port = remote_port;
        info.local_port = 0;  // Set by server
        info.connected_at = connected_at;
        info.messages_received = messages_received.load();
        info.messages_sent = messages_sent.load();
        info.bytes_received = bytes_received.load();
        info.bytes_sent = bytes_sent.load();
        info.tls_enabled = tls_enabled;
        if (tls_enabled) {
            info.tls_version = tls_version_str;
            info.tls_cipher = tls_cipher_str;
            if (!peer_cert_subject.empty()) {
                info.peer_certificate_subject = peer_cert_subject;
            }
        }
        return info;
    }
};

// =============================================================================
// MLLP Server Implementation
// =============================================================================

/**
 * @brief Private implementation of mllp_server
 *
 * Uses PIMPL idiom to hide implementation details and reduce compilation
 * dependencies. Manages the TCP server socket, client sessions, and
 * background accept/receive threads.
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

        // Initialize platform-specific networking
        if (auto result = initialize_networking(); !result) {
            return result;
        }

        // Initialize TLS if enabled
        if (config_.tls.enabled) {
            if (auto result = initialize_tls(); !result) {
                return result;
            }
        }

        // Create and bind server socket
        if (auto result = create_server_socket(); !result) {
            return result;
        }

        // Start accept thread
        running_ = true;
        stop_requested_ = false;
        accept_thread_ = std::thread([this] { accept_loop(); });

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

        // Close server socket to unblock accept
        if (server_socket_ != INVALID_SOCKET_VALUE) {
#ifdef _WIN32
            closesocket(server_socket_);
#else
            ::close(server_socket_);
#endif
            server_socket_ = INVALID_SOCKET_VALUE;
        }

        // Wait for accept thread
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        // Close all sessions
        if (wait_for_connections) {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline) {
                std::shared_lock lock(sessions_mutex_);
                if (sessions_.empty()) {
                    break;
                }
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
        }

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

        // Cleanup TLS
#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (ssl_ctx_) {
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
        }
#endif

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
        for (const auto& [id, session] : sessions_) {
            auto info = session->to_session_info();
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
            auto session = std::move(it->second);
            sessions_.erase(it);

            // Update metrics: decrement active connection count
            monitoring::bridge_metrics_collector::instance()
                .set_mllp_active_connections(sessions_.size());

            lock.unlock();

            // Notify disconnection
            notify_connection(session->to_session_info(), false);

            // Close socket (graceful shutdown handled by session destructor)
            if (!graceful) {
                session->close_socket();
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

        for (auto& [id, session] : sessions_copy) {
            notify_connection(session->to_session_info(), false);
            if (!graceful) {
                session->close_socket();
            }
        }
    }

private:
    // =========================================================================
    // Platform Initialization
    // =========================================================================

    [[nodiscard]] std::expected<void, mllp_error> initialize_networking() {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            return std::unexpected(mllp_error::socket_error);
        }
#endif
        return {};
    }

    [[nodiscard]] std::expected<void, mllp_error> initialize_tls() {
#ifdef PACS_BRIDGE_HAS_OPENSSL
        const SSL_METHOD* method = TLS_server_method();
        ssl_ctx_ = SSL_CTX_new(method);
        if (!ssl_ctx_) {
            return std::unexpected(mllp_error::invalid_configuration);
        }

        // Set minimum TLS version
        int min_version =
            (config_.tls.min_version == security::tls_version::tls_1_3)
                ? TLS1_3_VERSION
                : TLS1_2_VERSION;
        SSL_CTX_set_min_proto_version(ssl_ctx_, min_version);

        // Load certificate and key
        if (!config_.tls.cert_path.empty()) {
            if (SSL_CTX_use_certificate_chain_file(
                    ssl_ctx_, config_.tls.cert_path.string().c_str()) != 1) {
                SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
                return std::unexpected(mllp_error::invalid_configuration);
            }
        }

        if (!config_.tls.key_path.empty()) {
            if (SSL_CTX_use_PrivateKey_file(
                    ssl_ctx_, config_.tls.key_path.string().c_str(),
                    SSL_FILETYPE_PEM) != 1) {
                SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
                return std::unexpected(mllp_error::invalid_configuration);
            }

            if (SSL_CTX_check_private_key(ssl_ctx_) != 1) {
                SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
                return std::unexpected(mllp_error::invalid_configuration);
            }
        }

        // Configure client authentication
        if (config_.tls.client_auth != security::client_auth_mode::none) {
            if (!config_.tls.ca_path.empty()) {
                if (SSL_CTX_load_verify_locations(
                        ssl_ctx_, config_.tls.ca_path.string().c_str(),
                        nullptr) != 1) {
                    SSL_CTX_free(ssl_ctx_);
                    ssl_ctx_ = nullptr;
                    return std::unexpected(mllp_error::invalid_configuration);
                }
            }

            int verify_mode = SSL_VERIFY_PEER;
            if (config_.tls.client_auth ==
                security::client_auth_mode::required) {
                verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
            }
            SSL_CTX_set_verify(ssl_ctx_, verify_mode, nullptr);
        }

        return {};
#else
        // TLS requested but OpenSSL not available
        return std::unexpected(mllp_error::invalid_configuration);
#endif
    }

    // =========================================================================
    // Server Socket
    // =========================================================================

    [[nodiscard]] std::expected<void, mllp_error> create_server_socket() {
        server_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_socket_ == INVALID_SOCKET_VALUE) {
            return std::unexpected(mllp_error::socket_error);
        }

        // Enable address reuse
        int opt = 1;
#ifdef _WIN32
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        // Bind to address
        struct sockaddr_in server_addr {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(config_.port);

        if (config_.bind_address.empty()) {
            server_addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, config_.bind_address.c_str(),
                          &server_addr.sin_addr) != 1) {
                close_server_socket();
                return std::unexpected(mllp_error::invalid_configuration);
            }
        }

        if (::bind(server_socket_, reinterpret_cast<struct sockaddr*>(&server_addr),
                   sizeof(server_addr)) < 0) {
            close_server_socket();
            return std::unexpected(mllp_error::socket_error);
        }

        // Start listening
        if (::listen(server_socket_, static_cast<int>(config_.max_connections)) <
            0) {
            close_server_socket();
            return std::unexpected(mllp_error::socket_error);
        }

        return {};
    }

    void close_server_socket() {
        if (server_socket_ != INVALID_SOCKET_VALUE) {
#ifdef _WIN32
            closesocket(server_socket_);
#else
            ::close(server_socket_);
#endif
            server_socket_ = INVALID_SOCKET_VALUE;
        }
    }

    // =========================================================================
    // Accept Loop
    // =========================================================================

    void accept_loop() {
        while (!stop_requested_) {
            // Check max connections
            {
                std::shared_lock lock(sessions_mutex_);
                if (sessions_.size() >= config_.max_connections) {
                    lock.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds{100});
                    continue;
                }
            }

            // Accept new connection with timeout using poll
#ifndef _WIN32
            struct pollfd pfd {};
            pfd.fd = server_socket_;
            pfd.events = POLLIN;
            int poll_result = poll(&pfd, 1, 1000);  // 1 second timeout

            if (poll_result <= 0 || stop_requested_) {
                continue;
            }
#endif

            struct sockaddr_in client_addr {};
            socklen_t client_len = sizeof(client_addr);

            socket_t client_socket = ::accept(
                server_socket_, reinterpret_cast<struct sockaddr*>(&client_addr),
                &client_len);

            if (client_socket == INVALID_SOCKET_VALUE) {
                if (stop_requested_) {
                    break;
                }
                increment_stat(&stats_.connection_errors);
                continue;
            }

            // Create session
            auto session = std::make_unique<internal_session>();
            session->id = next_session_id_++;
            session->socket = client_socket;
            session->connected_at = std::chrono::system_clock::now();
            session->last_activity = session->connected_at;

            // Extract remote address
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
            session->remote_address = addr_str;
            session->remote_port = ntohs(client_addr.sin_port);

            // Configure socket options
            configure_socket(client_socket);

            // Perform TLS handshake if enabled
            if (config_.tls.enabled) {
#ifdef PACS_BRIDGE_HAS_OPENSSL
                session->ssl = SSL_new(ssl_ctx_);
                if (!session->ssl) {
                    increment_stat(&stats_.tls_failures);
                    notify_error(mllp_error::connection_failed,
                                 session->to_session_info(),
                                 "Failed to create SSL object");
                    continue;
                }

                SSL_set_fd(session->ssl, static_cast<int>(client_socket));

                if (SSL_accept(session->ssl) <= 0) {
                    increment_stat(&stats_.tls_failures);
                    notify_error(mllp_error::connection_failed,
                                 session->to_session_info(),
                                 "TLS handshake failed");
                    continue;
                }

                session->tls_enabled = true;
                session->tls_version_str = SSL_get_version(session->ssl);
                session->tls_cipher_str = SSL_get_cipher_name(session->ssl);

                // Extract peer certificate info if present
                X509* peer_cert = SSL_get_peer_certificate(session->ssl);
                if (peer_cert) {
                    char* subject = X509_NAME_oneline(
                        X509_get_subject_name(peer_cert), nullptr, 0);
                    if (subject) {
                        session->peer_cert_subject = subject;
                        OPENSSL_free(subject);
                    }
                    X509_free(peer_cert);
                }
#endif
            }

            // Update statistics
            increment_stat(&stats_.total_connections);

            // Notify connection handler
            notify_connection(session->to_session_info(), true);

            // Store session and start handler thread
            uint64_t session_id = session->id;
            {
                std::unique_lock lock(sessions_mutex_);
                sessions_[session_id] = std::move(session);

                // Update metrics: record new connection and active count
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
    }

    void configure_socket(socket_t socket) {
        // Set TCP_NODELAY to disable Nagle's algorithm for low latency
        int flag = 1;
#ifdef _WIN32
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&flag), sizeof(flag));
#else
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#endif

        // Set receive timeout
        auto timeout_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                config_.idle_timeout)
                .count();

#ifdef _WIN32
        DWORD timeout = static_cast<DWORD>(timeout_ms);
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        struct timeval tv {};
        tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
        tv.tv_usec =
            static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    }

    // =========================================================================
    // Session Handling
    // =========================================================================

    void handle_session(uint64_t session_id) {
        std::vector<uint8_t> read_buffer(8192);

        while (!stop_requested_) {
            // Get session pointer
            internal_session* session_ptr = nullptr;
            {
                std::shared_lock lock(sessions_mutex_);
                auto it = sessions_.find(session_id);
                if (it == sessions_.end()) {
                    break;
                }
                session_ptr = it->second.get();
            }

            // Read data from socket
            ssize_t bytes_read = 0;

#ifdef PACS_BRIDGE_HAS_OPENSSL
            if (session_ptr->ssl) {
                bytes_read =
                    SSL_read(session_ptr->ssl, read_buffer.data(),
                             static_cast<int>(read_buffer.size()));
            } else
#endif
            {
                bytes_read =
                    ::recv(session_ptr->socket,
                           reinterpret_cast<char*>(read_buffer.data()),
                           static_cast<int>(read_buffer.size()), 0);
            }

            if (bytes_read <= 0) {
                // Connection closed or error
                if (bytes_read == 0) {
                    // Clean disconnect
                } else {
                    // Error - check for timeout vs actual error
#ifdef _WIN32
                    int err = WSAGetLastError();
                    if (err != WSAETIMEDOUT)
#else
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
#endif
                    {
                        increment_stat(&stats_.connection_errors);
                    }
                }
                break;
            }

            // Update statistics
            session_ptr->bytes_received += static_cast<size_t>(bytes_read);
            session_ptr->last_activity = std::chrono::system_clock::now();
            add_stat(&stats_.bytes_received, static_cast<size_t>(bytes_read));

            // Append to receive buffer
            session_ptr->receive_buffer.insert(
                session_ptr->receive_buffer.end(), read_buffer.begin(),
                read_buffer.begin() + bytes_read);

            // Check buffer size limit
            if (session_ptr->receive_buffer.size() > config_.max_message_size) {
                increment_stat(&stats_.protocol_errors);
                notify_error(mllp_error::message_too_large,
                             session_ptr->to_session_info(),
                             "Message exceeds maximum size");
                break;
            }

            // Process complete MLLP messages
            process_messages(session_ptr);
        }

        // Close session
        close_session(session_id, true);
    }

    void process_messages(internal_session* session) {
        auto& buffer = session->receive_buffer;

        while (true) {
            // Find start of message (VT byte)
            auto start_it = std::find(buffer.begin(), buffer.end(),
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
                .set_attribute("mllp.remote_address", session->remote_address)
                .set_attribute("mllp.remote_port", static_cast<int64_t>(session->remote_port))
                .set_attribute("mllp.session_id", static_cast<int64_t>(session->id));

            // Extract message content (between VT and FS)
            mllp_message msg;
            msg.content.assign(buffer.begin() + 1, end_it);
            msg.session = session->to_session_info();
            msg.received_at = std::chrono::system_clock::now();

            // Add message size to span
            span.set_attribute("mllp.message_size", static_cast<int64_t>(msg.content.size()));

            // Remove processed message from buffer (including CR)
            buffer.erase(buffer.begin(), end_it + 2);

            // Update statistics
            session->messages_received++;
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
                send_response(session, *response);
                span.set_attribute("mllp.response_sent", true);
            } else {
                span.set_attribute("mllp.response_sent", false);
            }

            // Span ends automatically via RAII
        }
    }

    void send_response(internal_session* session, const mllp_message& response) {
        auto framed = response.frame();

        ssize_t bytes_sent = 0;
#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (session->ssl) {
            bytes_sent = SSL_write(session->ssl, framed.data(),
                                   static_cast<int>(framed.size()));
        } else
#endif
        {
            bytes_sent = ::send(session->socket,
                                reinterpret_cast<const char*>(framed.data()),
                                static_cast<int>(framed.size()), 0);
        }

        if (bytes_sent > 0) {
            session->messages_sent++;
            session->bytes_sent += static_cast<size_t>(bytes_sent);
            increment_stat(&stats_.messages_sent);
            add_stat(&stats_.bytes_sent, static_cast<size_t>(bytes_sent));
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
    socket_t server_socket_ = INVALID_SOCKET_VALUE;
    std::atomic<uint64_t> next_session_id_{1};

    // TLS context
#ifdef PACS_BRIDGE_HAS_OPENSSL
    SSL_CTX* ssl_ctx_ = nullptr;
#else
    void* ssl_ctx_ = nullptr;
#endif

    // State management
    mutable std::shared_mutex state_mutex_;
    bool running_ = false;
    std::atomic<bool> stop_requested_{false};

    // Threads
    std::thread accept_thread_;
    std::mutex threads_mutex_;
    std::vector<std::thread> session_threads_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    // Futures for executor-based session tasks
    std::vector<std::future<void>> session_futures_;
#endif

    // Sessions
    mutable std::shared_mutex sessions_mutex_;
    std::unordered_map<uint64_t, std::unique_ptr<internal_session>> sessions_;

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
