/**
 * @file mllp_client.cpp
 * @brief MLLP client implementation for HL7 message transmission
 *
 * Provides a TCP client implementation for sending HL7 messages using the
 * Minimal Lower Layer Protocol (MLLP). Supports connection management,
 * automatic retry with exponential backoff, and TLS encryption.
 *
 * Key implementation details:
 * - Uses BSD sockets for cross-platform TCP networking
 * - Implements proper MLLP framing with VT/FS/CR markers
 * - Supports TLS 1.2/1.3 via OpenSSL when PACS_BRIDGE_HAS_OPENSSL is defined
 * - Thread-safe operations for concurrent message sending
 * - Connection pooling support via mllp_connection_pool
 *
 * @see include/pacs/bridge/mllp/mllp_client.h
 * @see https://github.com/kcenon/pacs_bridge/issues/13
 */

#include "pacs/bridge/mllp/mllp_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <queue>
#include <set>
#include <shared_mutex>
#include <thread>
#include <condition_variable>

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
#include <netdb.h>
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
#include <openssl/x509.h>
#endif

namespace pacs::bridge::mllp {

// =============================================================================
// MLLP Client Implementation
// =============================================================================

/**
 * @brief Private implementation of mllp_client
 *
 * Uses PIMPL idiom to hide implementation details. Manages TCP connection,
 * TLS handshake, message framing, and response reading.
 */
class mllp_client::impl {
public:
    explicit impl(const mllp_client_config& config) : config_(config) {
        receive_buffer_.reserve(8192);
    }

    ~impl() { disconnect_internal(false); }

    // Non-copyable
    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    // =========================================================================
    // Connection Management
    // =========================================================================

    [[nodiscard]] std::expected<void, mllp_error> connect() {
        std::lock_guard lock(connection_mutex_);

        if (connected_) {
            return {};  // Already connected
        }

        if (!config_.is_valid()) {
            return std::unexpected(mllp_error::invalid_configuration);
        }

        increment_stat(&stats_.connect_attempts);

        // Initialize platform networking
        if (auto result = initialize_networking(); !result) {
            return result;
        }

        // Resolve hostname
        struct addrinfo hints {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result_info = nullptr;
        std::string port_str = std::to_string(config_.port);

        int resolve_result =
            getaddrinfo(config_.host.c_str(), port_str.c_str(), &hints, &result_info);
        if (resolve_result != 0 || !result_info) {
            return std::unexpected(mllp_error::connection_failed);
        }

        // Create socket
        socket_ = ::socket(result_info->ai_family, result_info->ai_socktype,
                           result_info->ai_protocol);
        if (socket_ == INVALID_SOCKET_VALUE) {
            freeaddrinfo(result_info);
            return std::unexpected(mllp_error::socket_error);
        }

        // Set socket to non-blocking for connect timeout
        set_socket_nonblocking(true);

        // Attempt connection
        int connect_result =
            ::connect(socket_, result_info->ai_addr,
                     static_cast<socklen_t>(result_info->ai_addrlen));
        freeaddrinfo(result_info);

        if (connect_result < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK)
#else
            if (errno != EINPROGRESS)
#endif
            {
                close_socket();
                return std::unexpected(mllp_error::connection_failed);
            }

            // Wait for connection with timeout
            if (!wait_for_connect()) {
                close_socket();
                return std::unexpected(mllp_error::timeout);
            }
        }

        // Set socket back to blocking mode
        set_socket_nonblocking(false);

        // Configure socket options
        configure_socket();

        // Perform TLS handshake if enabled
        if (config_.tls.enabled) {
            if (auto tls_result = perform_tls_handshake(); !tls_result) {
                close_socket();
                return tls_result;
            }
        }

        // Update session info
        session_info_.session_id = ++session_counter_;
        session_info_.remote_address = config_.host;
        session_info_.remote_port = config_.port;
        session_info_.connected_at = std::chrono::system_clock::now();
        session_info_.tls_enabled = config_.tls.enabled;

        connected_ = true;
        increment_stat(&stats_.connect_successes);

        return {};
    }

    void disconnect_internal(bool graceful) {
        std::lock_guard lock(connection_mutex_);

        if (!connected_) {
            return;
        }

#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (ssl_) {
            if (graceful) {
                SSL_shutdown(ssl_);
            }
            SSL_free(ssl_);
            ssl_ = nullptr;
        }

        if (ssl_ctx_) {
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
        }
#endif

        close_socket();
        connected_ = false;
        receive_buffer_.clear();
    }

    [[nodiscard]] bool is_connected() const noexcept {
        std::shared_lock lock(connection_mutex_);
        return connected_;
    }

    [[nodiscard]] std::expected<void, mllp_error> reconnect() {
        disconnect_internal(true);
        increment_stat(&stats_.reconnections);
        return connect();
    }

    // =========================================================================
    // Message Operations
    // =========================================================================

    [[nodiscard]] std::expected<send_result, mllp_error>
    send(const mllp_message& message) {
        std::lock_guard lock(send_mutex_);

        if (!is_connected()) {
            // Try to connect if keep_alive is enabled
            if (config_.keep_alive) {
                if (auto result = connect(); !result) {
                    return std::unexpected(result.error());
                }
            } else {
                return std::unexpected(mllp_error::not_running);
            }
        }

        auto start_time = std::chrono::steady_clock::now();
        size_t retry_count = 0;

        while (retry_count <= config_.retry_count) {
            if (retry_count > 0) {
                // Wait before retry with exponential backoff
                auto delay = config_.retry_delay * (1 << (retry_count - 1));
                std::this_thread::sleep_for(delay);

                // Reconnect
                if (auto result = reconnect(); !result) {
                    retry_count++;
                    continue;
                }
            }

            // Frame and send message
            auto framed = message.frame();
            if (auto result = send_data(framed); !result) {
                increment_stat(&stats_.send_errors);
                retry_count++;
                continue;
            }

            // Wait for response
            auto response = receive_response();
            if (!response) {
                increment_stat(&stats_.send_errors);
                retry_count++;
                continue;
            }

            // Calculate round-trip time
            auto end_time = std::chrono::steady_clock::now();
            auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time);

            // Update statistics
            increment_stat(&stats_.messages_sent);
            increment_stat(&stats_.messages_received);
            update_average_rtt(rtt.count());

            send_result result;
            result.response = std::move(*response);
            result.round_trip_time = rtt;
            result.retry_count = retry_count;

            return result;
        }

        return std::unexpected(mllp_error::connection_failed);
    }

    [[nodiscard]] std::expected<send_result, mllp_error>
    send(std::string_view hl7_content) {
        return send(mllp_message::from_string(hl7_content));
    }

    [[nodiscard]] std::future<std::expected<send_result, mllp_error>>
    send_async(const mllp_message& message) {
        return std::async(std::launch::async, [this, message] {
            return send(message);
        });
    }

    [[nodiscard]] std::expected<void, mllp_error>
    send_no_ack(const mllp_message& message) {
        std::lock_guard lock(send_mutex_);

        if (!is_connected()) {
            if (config_.keep_alive) {
                if (auto result = connect(); !result) {
                    return std::unexpected(result.error());
                }
            } else {
                return std::unexpected(mllp_error::not_running);
            }
        }

        auto framed = message.frame();
        if (auto result = send_data(framed); !result) {
            increment_stat(&stats_.send_errors);
            return result;
        }

        increment_stat(&stats_.messages_sent);
        return {};
    }

    // =========================================================================
    // Connection Information
    // =========================================================================

    [[nodiscard]] std::optional<mllp_session_info> session_info() const {
        std::shared_lock lock(connection_mutex_);
        if (connected_) {
            return session_info_;
        }
        return std::nullopt;
    }

    [[nodiscard]] const mllp_client_config& config() const noexcept {
        return config_;
    }

    [[nodiscard]] bool is_tls_active() const noexcept {
        std::shared_lock lock(connection_mutex_);
        return connected_ && config_.tls.enabled;
    }

    [[nodiscard]] std::optional<std::string> tls_version() const {
        std::shared_lock lock(connection_mutex_);
#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (connected_ && ssl_) {
            return std::string(SSL_get_version(ssl_));
        }
#endif
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> tls_cipher() const {
        std::shared_lock lock(connection_mutex_);
#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (connected_ && ssl_) {
            return std::string(SSL_get_cipher_name(ssl_));
        }
#endif
        return std::nullopt;
    }

    [[nodiscard]] std::optional<security::certificate_info> server_certificate() const {
        std::shared_lock lock(connection_mutex_);
#ifdef PACS_BRIDGE_HAS_OPENSSL
        if (connected_ && ssl_) {
            X509* cert = SSL_get_peer_certificate(ssl_);
            if (cert) {
                security::certificate_info info;

                // Extract subject
                char* subject = X509_NAME_oneline(
                    X509_get_subject_name(cert), nullptr, 0);
                if (subject) {
                    info.subject = subject;
                    OPENSSL_free(subject);
                }

                // Extract issuer
                char* issuer = X509_NAME_oneline(
                    X509_get_issuer_name(cert), nullptr, 0);
                if (issuer) {
                    info.issuer = issuer;
                    OPENSSL_free(issuer);
                }

                X509_free(cert);
                return info;
            }
        }
#endif
        return std::nullopt;
    }

    [[nodiscard]] statistics get_statistics() const {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

private:
    // =========================================================================
    // Platform Initialization
    // =========================================================================

    [[nodiscard]] std::expected<void, mllp_error> initialize_networking() {
#ifdef _WIN32
        static std::once_flag init_flag;
        static bool init_result = false;

        std::call_once(init_flag, [] {
            WSADATA wsa_data;
            init_result = (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0);
        });

        if (!init_result) {
            return std::unexpected(mllp_error::socket_error);
        }
#endif
        return {};
    }

    // =========================================================================
    // Socket Operations
    // =========================================================================

    void close_socket() {
        if (socket_ != INVALID_SOCKET_VALUE) {
#ifdef _WIN32
            closesocket(socket_);
#else
            ::close(socket_);
#endif
            socket_ = INVALID_SOCKET_VALUE;
        }
    }

    void set_socket_nonblocking(bool nonblocking) {
#ifdef _WIN32
        u_long mode = nonblocking ? 1 : 0;
        ioctlsocket(socket_, FIONBIO, &mode);
#else
        int flags = fcntl(socket_, F_GETFL, 0);
        if (nonblocking) {
            fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
        } else {
            fcntl(socket_, F_SETFL, flags & ~O_NONBLOCK);
        }
#endif
    }

    [[nodiscard]] bool wait_for_connect() {
        auto timeout_ms = config_.connect_timeout.count();

#ifdef _WIN32
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(socket_, &write_fds);

        struct timeval tv;
        tv.tv_sec = static_cast<long>(timeout_ms / 1000);
        tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);

        int result = select(0, nullptr, &write_fds, nullptr, &tv);
        return result > 0;
#else
        struct pollfd pfd {};
        pfd.fd = socket_;
        pfd.events = POLLOUT;

        int result = poll(&pfd, 1, static_cast<int>(timeout_ms));
        if (result > 0 && (pfd.revents & POLLOUT)) {
            // Check for connection errors
            int error = 0;
            socklen_t len = sizeof(error);
            getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &len);
            return error == 0;
        }
        return false;
#endif
    }

    void configure_socket() {
        // Set TCP_NODELAY for low latency
        int flag = 1;
#ifdef _WIN32
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&flag), sizeof(flag));
#else
        setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#endif

        // Set read/write timeout
        auto timeout_ms = config_.io_timeout.count();

#ifdef _WIN32
        DWORD timeout = static_cast<DWORD>(timeout_ms);
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        struct timeval tv {};
        tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
        tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    }

    // =========================================================================
    // TLS Operations
    // =========================================================================

    [[nodiscard]] std::expected<void, mllp_error> perform_tls_handshake() {
#ifdef PACS_BRIDGE_HAS_OPENSSL
        // Create SSL context
        const SSL_METHOD* method = TLS_client_method();
        ssl_ctx_ = SSL_CTX_new(method);
        if (!ssl_ctx_) {
            return std::unexpected(mllp_error::connection_failed);
        }

        // Set minimum TLS version
        int min_version =
            (config_.tls.min_version == security::tls_version::tls_1_3)
                ? TLS1_3_VERSION
                : TLS1_2_VERSION;
        SSL_CTX_set_min_proto_version(ssl_ctx_, min_version);

        // Load CA certificate for server verification
        if (!config_.tls.ca_path.empty()) {
            if (SSL_CTX_load_verify_locations(
                    ssl_ctx_, config_.tls.ca_path.string().c_str(),
                    nullptr) != 1) {
                SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
                return std::unexpected(mllp_error::connection_failed);
            }
        }

        // Load client certificate for mutual TLS
        if (!config_.tls.cert_path.empty()) {
            if (SSL_CTX_use_certificate_chain_file(
                    ssl_ctx_, config_.tls.cert_path.string().c_str()) != 1) {
                SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
                return std::unexpected(mllp_error::connection_failed);
            }
        }

        // Load client private key
        if (!config_.tls.key_path.empty()) {
            if (SSL_CTX_use_PrivateKey_file(
                    ssl_ctx_, config_.tls.key_path.string().c_str(),
                    SSL_FILETYPE_PEM) != 1) {
                SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
                return std::unexpected(mllp_error::connection_failed);
            }

            if (SSL_CTX_check_private_key(ssl_ctx_) != 1) {
                SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
                return std::unexpected(mllp_error::connection_failed);
            }
        }

        // Configure server verification
        if (config_.tls.verify_peer) {
            SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
        }

        // Create SSL object
        ssl_ = SSL_new(ssl_ctx_);
        if (!ssl_) {
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
            return std::unexpected(mllp_error::connection_failed);
        }

        SSL_set_fd(ssl_, static_cast<int>(socket_));

        // Set SNI hostname
        if (config_.tls.verify_hostname) {
            SSL_set_tlsext_host_name(ssl_, config_.tls.verify_hostname->c_str());
        } else {
            SSL_set_tlsext_host_name(ssl_, config_.host.c_str());
        }

        // Perform handshake
        if (SSL_connect(ssl_) <= 0) {
            SSL_free(ssl_);
            ssl_ = nullptr;
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
            return std::unexpected(mllp_error::connection_failed);
        }

        // Store TLS info in session
        session_info_.tls_version = std::string(SSL_get_version(ssl_));
        session_info_.tls_cipher = std::string(SSL_get_cipher_name(ssl_));

        // Get server certificate info
        X509* cert = SSL_get_peer_certificate(ssl_);
        if (cert) {
            char* subject = X509_NAME_oneline(
                X509_get_subject_name(cert), nullptr, 0);
            if (subject) {
                session_info_.peer_certificate_subject = std::string(subject);
                OPENSSL_free(subject);
            }
            X509_free(cert);
        }

        return {};
#else
        // TLS requested but OpenSSL not available
        return std::unexpected(mllp_error::connection_failed);
#endif
    }

    // =========================================================================
    // Data Transfer
    // =========================================================================

    [[nodiscard]] std::expected<void, mllp_error>
    send_data(const std::vector<uint8_t>& data) {
        size_t total_sent = 0;

        while (total_sent < data.size()) {
            ssize_t sent = 0;

#ifdef PACS_BRIDGE_HAS_OPENSSL
            if (ssl_) {
                sent = SSL_write(ssl_, data.data() + total_sent,
                                 static_cast<int>(data.size() - total_sent));
            } else
#endif
            {
                sent = ::send(socket_,
                              reinterpret_cast<const char*>(data.data() + total_sent),
                              static_cast<int>(data.size() - total_sent), 0);
            }

            if (sent <= 0) {
                return std::unexpected(mllp_error::socket_error);
            }

            total_sent += static_cast<size_t>(sent);
            add_stat(&stats_.bytes_sent, static_cast<size_t>(sent));
            session_info_.bytes_sent += static_cast<size_t>(sent);
        }

        return {};
    }

    [[nodiscard]] std::optional<mllp_message> receive_response() {
        receive_buffer_.clear();
        std::vector<uint8_t> read_buffer(4096);

        auto start_time = std::chrono::steady_clock::now();
        auto timeout = config_.io_timeout;

        while (true) {
            // Check timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= timeout) {
                return std::nullopt;
            }

            // Read data
            ssize_t bytes_read = 0;

#ifdef PACS_BRIDGE_HAS_OPENSSL
            if (ssl_) {
                bytes_read = SSL_read(ssl_, read_buffer.data(),
                                      static_cast<int>(read_buffer.size()));
            } else
#endif
            {
                bytes_read = ::recv(socket_,
                                    reinterpret_cast<char*>(read_buffer.data()),
                                    static_cast<int>(read_buffer.size()), 0);
            }

            if (bytes_read <= 0) {
                return std::nullopt;
            }

            add_stat(&stats_.bytes_received, static_cast<size_t>(bytes_read));
            session_info_.bytes_received += static_cast<size_t>(bytes_read);

            // Append to buffer
            receive_buffer_.insert(receive_buffer_.end(),
                                   read_buffer.begin(),
                                   read_buffer.begin() + bytes_read);

            // Check for complete MLLP message
            auto msg = extract_message();
            if (msg) {
                return msg;
            }
        }
    }

    [[nodiscard]] std::optional<mllp_message> extract_message() {
        // Look for start byte
        auto start_it = std::find(receive_buffer_.begin(), receive_buffer_.end(),
                                   static_cast<uint8_t>(MLLP_START_BYTE));
        if (start_it == receive_buffer_.end()) {
            return std::nullopt;
        }

        // Look for end sequence (FS + CR)
        for (auto it = start_it + 1; it < receive_buffer_.end() - 1; ++it) {
            if (*it == static_cast<uint8_t>(MLLP_END_BYTE) &&
                *(it + 1) == static_cast<uint8_t>(MLLP_CARRIAGE_RETURN)) {
                // Found complete message
                mllp_message msg;
                msg.content.assign(start_it + 1, it);
                msg.received_at = std::chrono::system_clock::now();

                // Remove processed data from buffer
                receive_buffer_.erase(receive_buffer_.begin(), it + 2);

                return msg;
            }
        }

        return std::nullopt;
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

    void update_average_rtt(double rtt_ms) {
        std::lock_guard lock(stats_mutex_);
        size_t count = stats_.messages_sent;
        if (count == 0) {
            stats_.avg_round_trip_ms = rtt_ms;
        } else {
            // Running average
            stats_.avg_round_trip_ms =
                (stats_.avg_round_trip_ms * (count - 1) + rtt_ms) / count;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    mllp_client_config config_;
    socket_t socket_ = INVALID_SOCKET_VALUE;

    // TLS state
#ifdef PACS_BRIDGE_HAS_OPENSSL
    SSL_CTX* ssl_ctx_ = nullptr;
    SSL* ssl_ = nullptr;
#else
    void* ssl_ctx_ = nullptr;
    void* ssl_ = nullptr;
#endif

    // Connection state
    mutable std::shared_mutex connection_mutex_;
    std::mutex send_mutex_;
    bool connected_ = false;

    // Session info
    static inline std::atomic<uint64_t> session_counter_{0};
    mllp_session_info session_info_;

    // Receive buffer
    std::vector<uint8_t> receive_buffer_;

    // Statistics
    mutable std::mutex stats_mutex_;
    statistics stats_;
};

// =============================================================================
// MLLP Client Public Interface
// =============================================================================

mllp_client::mllp_client(const mllp_client_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

mllp_client::~mllp_client() = default;

mllp_client::mllp_client(mllp_client&&) noexcept = default;
mllp_client& mllp_client::operator=(mllp_client&&) noexcept = default;

std::expected<void, mllp_error> mllp_client::connect() {
    return pimpl_->connect();
}

void mllp_client::disconnect(bool graceful) {
    pimpl_->disconnect_internal(graceful);
}

bool mllp_client::is_connected() const noexcept {
    return pimpl_->is_connected();
}

std::expected<void, mllp_error> mllp_client::reconnect() {
    return pimpl_->reconnect();
}

std::expected<mllp_client::send_result, mllp_error>
mllp_client::send(const mllp_message& message) {
    return pimpl_->send(message);
}

std::expected<mllp_client::send_result, mllp_error>
mllp_client::send(std::string_view hl7_content) {
    return pimpl_->send(hl7_content);
}

std::future<std::expected<mllp_client::send_result, mllp_error>>
mllp_client::send_async(const mllp_message& message) {
    return pimpl_->send_async(message);
}

std::expected<void, mllp_error>
mllp_client::send_no_ack(const mllp_message& message) {
    return pimpl_->send_no_ack(message);
}

std::optional<mllp_session_info> mllp_client::session_info() const {
    return pimpl_->session_info();
}

const mllp_client_config& mllp_client::config() const noexcept {
    return pimpl_->config();
}

bool mllp_client::is_tls_active() const noexcept {
    return pimpl_->is_tls_active();
}

std::optional<std::string> mllp_client::tls_version() const {
    return pimpl_->tls_version();
}

std::optional<std::string> mllp_client::tls_cipher() const {
    return pimpl_->tls_cipher();
}

std::optional<security::certificate_info> mllp_client::server_certificate() const {
    return pimpl_->server_certificate();
}

mllp_client::statistics mllp_client::get_statistics() const {
    return pimpl_->get_statistics();
}

// =============================================================================
// MLLP Connection Pool Implementation
// =============================================================================

/**
 * @brief Private implementation of mllp_connection_pool
 *
 * Manages a pool of mllp_client connections for high-throughput scenarios.
 * Provides connection reuse, health checking, and automatic scaling.
 */
class mllp_connection_pool::impl {
public:
    explicit impl(const mllp_pool_config& config) : config_(config) {
        // Pre-create minimum connections
        for (size_t i = 0; i < config_.min_connections; ++i) {
            create_connection();
        }

        // Start health check thread
        running_ = true;
        health_check_thread_ = std::thread([this] { health_check_loop(); });
    }

    ~impl() {
        running_ = false;
        cv_.notify_all();

        if (health_check_thread_.joinable()) {
            health_check_thread_.join();
        }

        // Close all connections
        std::lock_guard lock(mutex_);
        // std::queue doesn't have clear(), swap with empty queue
        std::queue<std::unique_ptr<mllp_client>> empty_queue;
        idle_connections_.swap(empty_queue);
        active_connections_.clear();
    }

    [[nodiscard]] std::expected<mllp_client::send_result, mllp_error>
    send(const mllp_message& message) {
        // Acquire connection
        auto client = acquire_connection();
        if (!client) {
            return std::unexpected(mllp_error::connection_failed);
        }

        // Send message
        auto result = client->send(message);

        // Release connection
        release_connection(std::move(client));

        return result;
    }

    [[nodiscard]] pool_statistics statistics() const {
        std::lock_guard lock(mutex_);
        pool_statistics stats;
        stats.active_connections = active_connections_.size();
        stats.idle_connections = idle_connections_.size();
        stats.total_created = total_created_;
        stats.total_closed = total_closed_;
        stats.waiting_requests = waiting_count_;
        return stats;
    }

private:
    void create_connection() {
        auto client = std::make_unique<mllp_client>(config_.client_config);
        if (client->connect()) {
            idle_connections_.push(std::move(client));
            total_created_++;
        }
    }

    [[nodiscard]] std::unique_ptr<mllp_client> acquire_connection() {
        std::unique_lock lock(mutex_);

        waiting_count_++;

        // Wait for available connection with timeout
        bool available = cv_.wait_for(lock, std::chrono::seconds{30}, [this] {
            return !running_ || !idle_connections_.empty() ||
                   (idle_connections_.size() + active_connections_.size()) <
                       config_.max_connections;
        });

        waiting_count_--;

        if (!running_ || !available) {
            return nullptr;
        }

        // Get idle connection or create new one
        if (!idle_connections_.empty()) {
            auto client = std::move(idle_connections_.front());
            idle_connections_.pop();
            active_connections_.insert(client.get());
            return client;
        }

        // Create new connection if under limit
        if (active_connections_.size() < config_.max_connections) {
            auto client =
                std::make_unique<mllp_client>(config_.client_config);
            if (client->connect()) {
                total_created_++;
                active_connections_.insert(client.get());
                return client;
            }
        }

        return nullptr;
    }

    void release_connection(std::unique_ptr<mllp_client> client) {
        if (!client) return;

        std::lock_guard lock(mutex_);
        active_connections_.erase(client.get());

        if (client->is_connected() && running_) {
            idle_connections_.push(std::move(client));
            cv_.notify_one();
        } else {
            total_closed_++;
        }
    }

    void health_check_loop() {
        while (running_) {
            std::this_thread::sleep_for(config_.health_check_interval);

            if (!running_) break;

            std::lock_guard lock(mutex_);

            // Check and close idle connections that are stale
            size_t idle_count = idle_connections_.size();
            std::queue<std::unique_ptr<mllp_client>> valid_connections;

            while (!idle_connections_.empty()) {
                auto client = std::move(idle_connections_.front());
                idle_connections_.pop();

                if (client->is_connected()) {
                    valid_connections.push(std::move(client));
                } else {
                    total_closed_++;
                }
            }

            idle_connections_ = std::move(valid_connections);

            // Ensure minimum connections
            while (idle_connections_.size() + active_connections_.size() <
                   config_.min_connections) {
                create_connection();
            }
        }
    }

    mllp_pool_config config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::queue<std::unique_ptr<mllp_client>> idle_connections_;
    std::set<mllp_client*> active_connections_;

    std::atomic<bool> running_{false};
    std::thread health_check_thread_;

    size_t total_created_ = 0;
    size_t total_closed_ = 0;
    size_t waiting_count_ = 0;
};

// =============================================================================
// MLLP Connection Pool Public Interface
// =============================================================================

mllp_connection_pool::mllp_connection_pool(const mllp_pool_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

mllp_connection_pool::~mllp_connection_pool() = default;

std::expected<mllp_client::send_result, mllp_error>
mllp_connection_pool::send(const mllp_message& message) {
    return pimpl_->send(message);
}

mllp_connection_pool::pool_statistics mllp_connection_pool::statistics() const {
    return pimpl_->statistics();
}

}  // namespace pacs::bridge::mllp
