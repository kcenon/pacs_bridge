/**
 * @file tls_mllp_server.cpp
 * @brief TLS-enabled implementation of MLLP network adapter
 *
 * Provides secure MLLP communication using OpenSSL for TLS encryption.
 * Supports TLS 1.2/1.3 with configurable cipher suites and client authentication.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/306
 */

#include "tls_mllp_server.h"

#include <algorithm>
#include <cstring>
#include <vector>

// Platform-specific headers
#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
using ssize_t = std::ptrdiff_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// OpenSSL headers
#ifdef PACS_BRIDGE_HAS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#endif

namespace pacs::bridge::mllp {

namespace {

/**
 * @brief Get last socket error code in a platform-independent way
 */
[[nodiscard]] int get_last_socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

/**
 * @brief Check if error indicates operation would block
 */
[[nodiscard]] bool is_would_block_error(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EWOULDBLOCK || error == EAGAIN;
#endif
}

/**
 * @brief Close socket in platform-specific way
 */
void close_socket(socket_t sock) {
    if (sock == INVALID_SOCKET_VALUE) {
        return;
    }
#ifdef _WIN32
    closesocket(sock);
#else
    ::close(sock);
#endif
}

#ifdef PACS_BRIDGE_HAS_OPENSSL

/**
 * @brief Get OpenSSL error string from error queue
 */
[[nodiscard]] std::string get_openssl_error() {
    char buffer[256];
    unsigned long error = ERR_get_error();
    if (error == 0) {
        return "Unknown SSL error";
    }
    ERR_error_string_n(error, buffer, sizeof(buffer));
    return std::string(buffer);
}

#endif  // PACS_BRIDGE_HAS_OPENSSL

}  // anonymous namespace

// =============================================================================
// TLS Session Implementation
// =============================================================================

#ifdef PACS_BRIDGE_HAS_OPENSSL

tls_mllp_session::tls_mllp_session(socket_t sock, uint64_t session_id,
                                   std::string remote_addr,
                                   uint16_t remote_port, SSL* ssl)
    : socket_(sock),
      session_id_(session_id),
      remote_addr_(std::move(remote_addr)),
      remote_port_(remote_port),
      ssl_(ssl) {
    stats_.connected_at = std::chrono::system_clock::now();
    stats_.last_activity = stats_.connected_at;
}

tls_mllp_session::~tls_mllp_session() { close(); }

std::expected<void, network_error>
tls_mllp_session::perform_handshake(std::chrono::milliseconds timeout) {
    if (handshake_completed_) {
        return {};
    }

    // Set socket to non-blocking mode for timeout support
    if (auto result = set_nonblocking(true); !result) {
        return result;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        int ret = SSL_accept(ssl_);

        if (ret == 1) {
            // Handshake successful
            handshake_completed_ = true;

            // Cache TLS information
            const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl_);
            if (cipher) {
                tls_cipher_str_ = SSL_CIPHER_get_name(cipher);
            }

            const char* version = SSL_get_version(ssl_);
            if (version) {
                tls_version_str_ = version;
            }

            return {};
        }

        int ssl_error = SSL_get_error(ssl_, ret);

        if (ssl_error == SSL_ERROR_WANT_READ ||
            ssl_error == SSL_ERROR_WANT_WRITE) {
            // Check timeout
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

            if (remaining.count() <= 0) {
                return std::unexpected(network_error::timeout);
            }

            // Wait for I/O readiness
            auto ready_result = wait_for_ssl_io(ssl_error, remaining);
            if (!ready_result) {
                return std::unexpected(ready_result.error());
            }

            if (!ready_result.value()) {
                return std::unexpected(network_error::timeout);
            }

            // Retry handshake
            continue;
        }

        // Handshake failed
        is_open_ = false;
        return std::unexpected(network_error::tls_handshake_failed);
    }
}

std::expected<std::vector<uint8_t>, network_error>
tls_mllp_session::receive(size_t max_bytes, std::chrono::milliseconds timeout) {
    if (!is_open_) {
        return std::unexpected(network_error::connection_closed);
    }

    if (!handshake_completed_) {
        if (auto result = perform_handshake(timeout); !result) {
            return std::unexpected(result.error());
        }
    }

    std::vector<uint8_t> buffer(max_bytes);
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        int bytes_received = SSL_read(ssl_, buffer.data(),
                                      static_cast<int>(max_bytes));

        if (bytes_received > 0) {
            // Success
            std::lock_guard lock(stats_mutex_);
            stats_.bytes_received += static_cast<size_t>(bytes_received);
            stats_.messages_received++;
            stats_.last_activity = std::chrono::system_clock::now();

            buffer.resize(static_cast<size_t>(bytes_received));
            return buffer;
        }

        int ssl_error = SSL_get_error(ssl_, bytes_received);

        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            // Clean TLS shutdown by peer
            is_open_ = false;
            return std::unexpected(network_error::connection_closed);
        }

        if (ssl_error == SSL_ERROR_WANT_READ ||
            ssl_error == SSL_ERROR_WANT_WRITE) {
            // Check timeout
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

            if (remaining.count() <= 0) {
                return std::unexpected(network_error::timeout);
            }

            // Wait for I/O readiness
            auto ready_result = wait_for_ssl_io(ssl_error, remaining);
            if (!ready_result) {
                return std::unexpected(ready_result.error());
            }

            if (!ready_result.value()) {
                return std::unexpected(network_error::timeout);
            }

            // Retry read
            continue;
        }

        // SSL error occurred
        is_open_ = false;
        return std::unexpected(network_error::socket_error);
    }
}

std::expected<size_t, network_error>
tls_mllp_session::send(std::span<const uint8_t> data) {
    if (!is_open_) {
        return std::unexpected(network_error::connection_closed);
    }

    if (!handshake_completed_) {
        // Handshake should have been completed in receive()
        return std::unexpected(network_error::socket_error);
    }

    size_t total_sent = 0;
    const uint8_t* ptr = data.data();
    size_t remaining = data.size();

    while (remaining > 0) {
        int bytes_sent = SSL_write(ssl_, ptr, static_cast<int>(remaining));

        if (bytes_sent > 0) {
            total_sent += static_cast<size_t>(bytes_sent);
            ptr += bytes_sent;
            remaining -= static_cast<size_t>(bytes_sent);

            // Update statistics
            std::lock_guard lock(stats_mutex_);
            stats_.bytes_sent += static_cast<size_t>(bytes_sent);
            stats_.messages_sent++;
            stats_.last_activity = std::chrono::system_clock::now();

            continue;
        }

        int ssl_error = SSL_get_error(ssl_, bytes_sent);

        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            // Connection closed
            is_open_ = false;
            return std::unexpected(network_error::connection_closed);
        }

        if (ssl_error == SSL_ERROR_WANT_READ ||
            ssl_error == SSL_ERROR_WANT_WRITE) {
            // Wait for I/O readiness with timeout
            auto ready_result = wait_for_ssl_io(
                ssl_error, std::chrono::milliseconds{5000});
            if (!ready_result) {
                return std::unexpected(ready_result.error());
            }

            if (!ready_result.value()) {
                return std::unexpected(network_error::timeout);
            }

            // Retry write
            continue;
        }

        // SSL error occurred
        is_open_ = false;
        return std::unexpected(network_error::socket_error);
    }

    return total_sent;
}

void tls_mllp_session::close() {
    if (is_open_.exchange(false)) {
        if (ssl_) {
            // Attempt graceful SSL shutdown
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        // Use shutdown() to immediately wake up any blocked poll()/recv()
        // calls on this socket from other threads. This is necessary because
        // close() alone may not immediately unblock poll() on some systems.
#ifdef _WIN32
        shutdown(socket_, SD_BOTH);
#else
        shutdown(socket_, SHUT_RDWR);
#endif
        close_socket(socket_);
        socket_ = INVALID_SOCKET_VALUE;
    }
}

bool tls_mllp_session::is_open() const noexcept { return is_open_; }

session_stats tls_mllp_session::get_stats() const noexcept {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

std::string tls_mllp_session::remote_address() const noexcept {
    return remote_addr_;
}

uint16_t tls_mllp_session::remote_port() const noexcept {
    return remote_port_;
}

uint64_t tls_mllp_session::session_id() const noexcept { return session_id_; }

std::string tls_mllp_session::tls_version() const noexcept {
    return tls_version_str_;
}

std::string tls_mllp_session::tls_cipher() const noexcept {
    return tls_cipher_str_;
}

std::optional<security::certificate_info>
tls_mllp_session::peer_certificate() const noexcept {
    if (!ssl_) {
        return std::nullopt;
    }

    X509* cert = SSL_get_peer_certificate(ssl_);
    if (!cert) {
        return std::nullopt;
    }

    security::certificate_info info;

    // Extract subject
    char* subject = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    if (subject) {
        info.subject = subject;
        OPENSSL_free(subject);
    }

    // Extract issuer
    char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    if (issuer) {
        info.issuer = issuer;
        OPENSSL_free(issuer);
    }

    X509_free(cert);
    return info;
}

std::expected<void, network_error>
tls_mllp_session::set_nonblocking(bool enable) {
#ifdef _WIN32
    u_long mode = enable ? 1 : 0;
    if (ioctlsocket(socket_, FIONBIO, &mode) != 0) {
        return std::unexpected(network_error::socket_error);
    }
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags < 0) {
        return std::unexpected(network_error::socket_error);
    }

    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(socket_, F_SETFL, flags) < 0) {
        return std::unexpected(network_error::socket_error);
    }
#endif

    return {};
}

std::expected<bool, network_error>
tls_mllp_session::wait_for_ssl_io(int ssl_error,
                                  std::chrono::milliseconds timeout) {
    bool wait_for_read = (ssl_error == SSL_ERROR_WANT_READ);

#ifdef _WIN32
    // Use select() on Windows
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(socket_, &fds);

    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    int result;
    if (wait_for_read) {
        result = select(0, &fds, nullptr, nullptr, &tv);
    } else {
        result = select(0, nullptr, &fds, nullptr, &tv);
    }

    if (result < 0) {
        return std::unexpected(network_error::socket_error);
    }

    return result > 0;
#else
    // Use poll() on POSIX
    struct pollfd pfd;
    pfd.fd = socket_;
    pfd.events = wait_for_read ? POLLIN : POLLOUT;
    pfd.revents = 0;

    int timeout_ms = static_cast<int>(timeout.count());
    int result = poll(&pfd, 1, timeout_ms);

    if (result < 0) {
        return std::unexpected(network_error::socket_error);
    }

    if (result == 0) {
        return false;
    }

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        is_open_ = false;
        return std::unexpected(network_error::connection_closed);
    }

    return (pfd.revents & pfd.events) != 0;
#endif
}

// =============================================================================
// TLS Server Implementation
// =============================================================================

tls_mllp_server::tls_mllp_server(const server_config& config,
                                 const security::tls_config& tls_config)
    : config_(config), tls_config_(tls_config) {}

tls_mllp_server::~tls_mllp_server() {
    stop(false);
    cleanup_networking();
}

std::expected<void, network_error> tls_mllp_server::start() {
    std::lock_guard lock(state_mutex_);

    if (running_) {
        return std::unexpected(network_error::socket_error);
    }

    if (!config_.is_valid()) {
        return std::unexpected(network_error::invalid_config);
    }

    // Initialize TLS context
    if (auto result = initialize_tls_context(); !result) {
        return result;
    }

    // Initialize platform-specific networking
    if (auto result = initialize_networking(); !result) {
        return result;
    }

    // Create server socket
    if (auto result = create_server_socket(); !result) {
        cleanup_networking();
        return result;
    }

    // Start accept loop
    running_ = true;
    stop_requested_ = false;
    accept_thread_ = std::thread([this] { accept_loop(); });

    return {};
}

void tls_mllp_server::stop(bool wait_for_connections) {
    {
        std::lock_guard lock(state_mutex_);
        if (!running_) {
            return;
        }
        stop_requested_ = true;
    }

    // Close server socket to interrupt accept()
    if (server_socket_ != INVALID_SOCKET_VALUE) {
        close_socket(server_socket_);
        server_socket_ = INVALID_SOCKET_VALUE;
    }

    // Wait for accept thread to finish
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // Note: Session lifecycle is managed by mllp_server::impl, not by this
    // adapter. The adapter only handles accept() and creates sessions.
    // mllp_server::impl::stop_internal() is responsible for waiting on sessions.
    (void)wait_for_connections;

    running_ = false;
}

bool tls_mllp_server::is_running() const noexcept { return running_; }

uint16_t tls_mllp_server::port() const noexcept { return config_.port; }

void tls_mllp_server::on_connection(on_connection_callback callback) {
    connection_callback_ = std::move(callback);
}

size_t tls_mllp_server::active_session_count() const noexcept {
    return active_sessions_;
}

security::tls_statistics tls_mllp_server::tls_statistics() const noexcept {
    if (tls_ctx_) {
        return tls_ctx_->statistics();
    }
    return {};
}

std::expected<void, network_error> tls_mllp_server::initialize_networking() {
#ifdef _WIN32
    if (!winsock_initialized_) {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            return std::unexpected(network_error::socket_error);
        }
        winsock_initialized_ = true;
    }
#endif
    return {};
}

void tls_mllp_server::cleanup_networking() {
#ifdef _WIN32
    if (winsock_initialized_) {
        WSACleanup();
        winsock_initialized_ = false;
    }
#endif
}

std::expected<void, network_error> tls_mllp_server::create_server_socket() {
    // Create socket
    server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket_ == INVALID_SOCKET_VALUE) {
        return std::unexpected(network_error::socket_error);
    }

    // Configure socket options
    if (auto result = configure_socket_options(server_socket_); !result) {
        close_socket(server_socket_);
        server_socket_ = INVALID_SOCKET_VALUE;
        return result;
    }

    // Bind to address and port
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);

    if (config_.bind_address.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, config_.bind_address.c_str(), &addr.sin_addr) !=
            1) {
            close_socket(server_socket_);
            server_socket_ = INVALID_SOCKET_VALUE;
            return std::unexpected(network_error::invalid_config);
        }
    }

    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
        close_socket(server_socket_);
        server_socket_ = INVALID_SOCKET_VALUE;
        return std::unexpected(network_error::bind_failed);
    }

    // Start listening
    if (listen(server_socket_, config_.backlog) < 0) {
        close_socket(server_socket_);
        server_socket_ = INVALID_SOCKET_VALUE;
        return std::unexpected(network_error::bind_failed);
    }

    return {};
}

std::expected<void, network_error>
tls_mllp_server::configure_socket_options(socket_t sock) {
    // Enable address reuse
    if (config_.reuse_addr) {
        int reuse = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<const char*>(&reuse),
                       sizeof(reuse)) < 0) {
            return std::unexpected(network_error::socket_error);
        }
    }

    // Disable Nagle's algorithm if requested
    if (config_.no_delay) {
        int nodelay = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&nodelay),
                       sizeof(nodelay)) < 0) {
            return std::unexpected(network_error::socket_error);
        }
    }

    // Configure keep-alive
    if (config_.keep_alive) {
        int keepalive = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
                       reinterpret_cast<const char*>(&keepalive),
                       sizeof(keepalive)) < 0) {
            return std::unexpected(network_error::socket_error);
        }

#ifdef __linux__
        // Linux-specific keep-alive configuration
        int idle = config_.keep_alive_idle;
        int interval = config_.keep_alive_interval;
        int count = config_.keep_alive_count;

        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE,
                   reinterpret_cast<const char*>(&idle), sizeof(idle));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL,
                   reinterpret_cast<const char*>(&interval), sizeof(interval));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT,
                   reinterpret_cast<const char*>(&count), sizeof(count));
#elif defined(__APPLE__)
        // macOS keep-alive configuration
        int idle = config_.keep_alive_idle;
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPALIVE,
                   reinterpret_cast<const char*>(&idle), sizeof(idle));
#endif
    }

    // Set send/receive buffer sizes if specified
    if (config_.send_buffer_size > 0) {
        int size = static_cast<int>(config_.send_buffer_size);
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&size), sizeof(size));
    }

    if (config_.recv_buffer_size > 0) {
        int size = static_cast<int>(config_.recv_buffer_size);
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&size), sizeof(size));
    }

    return {};
}

std::expected<void, network_error> tls_mllp_server::initialize_tls_context() {
    if (!tls_config_.enabled) {
        return std::unexpected(network_error::invalid_config);
    }

    // Initialize TLS library
    auto tls_init_result = security::initialize_tls();
    if (!tls_init_result) {
        return std::unexpected(network_error::invalid_config);
    }

    // Create server context
    auto ctx_result = security::tls_context::create_server_context(tls_config_);
    if (!ctx_result) {
        return std::unexpected(network_error::invalid_config);
    }

    tls_ctx_ = std::make_unique<security::tls_context>(std::move(ctx_result.value()));

    return {};
}

void tls_mllp_server::accept_loop() {
    while (!stop_requested_) {
        // Accept connection
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        socket_t client_socket = accept(
            server_socket_, reinterpret_cast<struct sockaddr*>(&client_addr),
            &addr_len);

        if (client_socket == INVALID_SOCKET_VALUE) {
            if (stop_requested_) {
                break;
            }
            // Accept failed, continue
            continue;
        }

        // Extract client address
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
        std::string remote_addr(addr_str);
        uint16_t remote_port = ntohs(client_addr.sin_port);

        // Generate session ID
        uint64_t session_id = generate_session_id();

        // Create SSL structure for this connection
        SSL* ssl = SSL_new(static_cast<SSL_CTX*>(tls_ctx_->native_handle()));
        if (!ssl) {
            close_socket(client_socket);
            continue;
        }

        // Associate socket with SSL
        if (SSL_set_fd(ssl, static_cast<int>(client_socket)) != 1) {
            SSL_free(ssl);
            close_socket(client_socket);
            continue;
        }

        // Create session
        auto session = std::make_unique<tls_mllp_session>(
            client_socket, session_id, std::move(remote_addr), remote_port, ssl);

        // Perform TLS handshake
        if (auto result = session->perform_handshake(
                tls_config_.handshake_timeout);
            !result) {
            // Handshake failed, session will be destroyed
            continue;
        }

        // Increment active session count
        active_sessions_++;

        // Invoke connection callback
        if (connection_callback_) {
            connection_callback_(std::move(session));
        }

        // Note: Session ownership transferred to callback
        // Callback is responsible for managing session lifecycle
    }
}

uint64_t tls_mllp_server::generate_session_id() {
    return next_session_id_.fetch_add(1, std::memory_order_relaxed);
}

#else  // !PACS_BRIDGE_HAS_OPENSSL

// Stub implementation when OpenSSL is not available
tls_mllp_server::tls_mllp_server(const server_config&,
                                 const security::tls_config&) {
    // TLS not available
}

tls_mllp_server::~tls_mllp_server() = default;

std::expected<void, network_error> tls_mllp_server::start() {
    return std::unexpected(network_error::invalid_config);
}

void tls_mllp_server::stop(bool) {}

bool tls_mllp_server::is_running() const noexcept { return false; }

uint16_t tls_mllp_server::port() const noexcept { return 0; }

void tls_mllp_server::on_connection(on_connection_callback) {}

size_t tls_mllp_server::active_session_count() const noexcept { return 0; }

security::tls_statistics tls_mllp_server::tls_statistics() const noexcept {
    return {};
}

std::expected<void, network_error> tls_mllp_server::initialize_networking() {
    return std::unexpected(network_error::invalid_config);
}

void tls_mllp_server::cleanup_networking() {}

std::expected<void, network_error> tls_mllp_server::create_server_socket() {
    return std::unexpected(network_error::invalid_config);
}

std::expected<void, network_error>
tls_mllp_server::configure_socket_options(socket_t) {
    return std::unexpected(network_error::invalid_config);
}

std::expected<void, network_error> tls_mllp_server::initialize_tls_context() {
    return std::unexpected(network_error::invalid_config);
}

void tls_mllp_server::accept_loop() {}

uint64_t tls_mllp_server::generate_session_id() { return 0; }

#endif  // PACS_BRIDGE_HAS_OPENSSL

}  // namespace pacs::bridge::mllp
