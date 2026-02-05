/**
 * @file bsd_mllp_server.cpp
 * @brief BSD socket implementation of MLLP network adapter
 *
 * Platform-specific socket implementation supporting:
 * - Windows (Winsock2)
 * - POSIX systems (Linux, macOS, BSD)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/305
 */

#include "bsd_mllp_server.h"

#include <algorithm>
#include <cstring>
#include <vector>

// Platform-specific headers
#ifdef _WIN32
// Windows socket headers already included in bsd_mllp_server.h
#pragma comment(lib, "ws2_32.lib")
// ssize_t is POSIX-specific, define for Windows
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

}  // anonymous namespace

// =============================================================================
// BSD Session Implementation
// =============================================================================

bsd_mllp_session::bsd_mllp_session(socket_t sock, uint64_t session_id,
                                   std::string remote_addr,
                                   uint16_t remote_port)
    : socket_(sock),
      session_id_(session_id),
      remote_addr_(std::move(remote_addr)),
      remote_port_(remote_port) {
    stats_.connected_at = std::chrono::system_clock::now();
    stats_.last_activity = stats_.connected_at;
}

bsd_mllp_session::~bsd_mllp_session() { close(); }

std::expected<std::vector<uint8_t>, network_error>
bsd_mllp_session::receive(size_t max_bytes,
                          std::chrono::milliseconds timeout) {
    if (!is_open_) {
        return std::unexpected(network_error::connection_closed);
    }

    // Wait for data to be available
    auto ready_result = wait_for_io(true, timeout);
    if (!ready_result) {
        return std::unexpected(ready_result.error());
    }

    if (!ready_result.value()) {
        // Timeout occurred
        return std::unexpected(network_error::timeout);
    }

    // Receive data
    std::vector<uint8_t> buffer(max_bytes);

#ifdef _WIN32
    int bytes_received =
        ::recv(socket_, reinterpret_cast<char*>(buffer.data()),
               static_cast<int>(max_bytes), 0);
#else
    ssize_t bytes_received = ::recv(socket_, buffer.data(), max_bytes, 0);
#endif

    if (bytes_received < 0) {
        int error = get_last_socket_error();
        if (is_would_block_error(error)) {
            return std::unexpected(network_error::would_block);
        }
        is_open_ = false;
        return std::unexpected(network_error::socket_error);
    }

    if (bytes_received == 0) {
        // Connection closed by peer
        is_open_ = false;
        return std::unexpected(network_error::connection_closed);
    }

    // Update statistics
    {
        std::lock_guard lock(stats_mutex_);
        stats_.bytes_received += static_cast<size_t>(bytes_received);
        stats_.last_activity = std::chrono::system_clock::now();
    }

    buffer.resize(static_cast<size_t>(bytes_received));
    return buffer;
}

std::expected<size_t, network_error>
bsd_mllp_session::send(std::span<const uint8_t> data) {
    if (!is_open_) {
        return std::unexpected(network_error::connection_closed);
    }

    size_t total_sent = 0;
    const uint8_t* ptr = data.data();
    size_t remaining = data.size();

    while (remaining > 0) {
#ifdef _WIN32
        int sent = ::send(socket_, reinterpret_cast<const char*>(ptr),
                          static_cast<int>(remaining), 0);
#else
        ssize_t sent = ::send(socket_, ptr, remaining, 0);
#endif

        if (sent < 0) {
            int error = get_last_socket_error();
            if (is_would_block_error(error)) {
                // Wait for socket to be writable
                auto ready_result =
                    wait_for_io(false, std::chrono::milliseconds{5000});
                if (!ready_result || !ready_result.value()) {
                    return std::unexpected(network_error::timeout);
                }
                continue;  // Retry send
            }
            is_open_ = false;
            return std::unexpected(network_error::socket_error);
        }

        if (sent == 0) {
            // Connection closed
            is_open_ = false;
            return std::unexpected(network_error::connection_closed);
        }

        total_sent += static_cast<size_t>(sent);
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }

    // Update statistics
    {
        std::lock_guard lock(stats_mutex_);
        stats_.bytes_sent += total_sent;
        stats_.last_activity = std::chrono::system_clock::now();
    }

    return total_sent;
}

void bsd_mllp_session::close() {
    if (is_open_.exchange(false)) {
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

bool bsd_mllp_session::is_open() const noexcept { return is_open_; }

session_stats bsd_mllp_session::get_stats() const noexcept {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

std::string bsd_mllp_session::remote_address() const noexcept {
    return remote_addr_;
}

uint16_t bsd_mllp_session::remote_port() const noexcept {
    return remote_port_;
}

uint64_t bsd_mllp_session::session_id() const noexcept { return session_id_; }

std::expected<void, network_error>
bsd_mllp_session::set_nonblocking(bool enable) {
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
bsd_mllp_session::wait_for_io(bool for_read,
                              std::chrono::milliseconds timeout) {
#ifdef _WIN32
    // Use select() on Windows
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(socket_, &fds);

    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

    int result;
    if (for_read) {
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
    pfd.events = for_read ? POLLIN : POLLOUT;
    pfd.revents = 0;

    int timeout_ms = static_cast<int>(timeout.count());
    int result = poll(&pfd, 1, timeout_ms);

    if (result < 0) {
        return std::unexpected(network_error::socket_error);
    }

    if (result == 0) {
        // Timeout
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
// BSD Server Implementation
// =============================================================================

bsd_mllp_server::bsd_mllp_server(const server_config& config)
    : config_(config) {}

bsd_mllp_server::~bsd_mllp_server() {
    stop(false);
    cleanup_networking();
}

std::expected<void, network_error> bsd_mllp_server::start() {
    std::lock_guard lock(state_mutex_);

    if (running_) {
        return std::unexpected(network_error::socket_error);
    }

    if (!config_.is_valid()) {
        return std::unexpected(network_error::invalid_config);
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

void bsd_mllp_server::stop(bool wait_for_connections) {
    {
        std::lock_guard lock(state_mutex_);
        if (!running_) {
            return;
        }
        stop_requested_ = true;
    }

    // Close server socket to unblock accept
    close_socket(server_socket_);
    server_socket_ = INVALID_SOCKET_VALUE;

    // Wait for accept thread
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // Note: Session lifecycle is managed by mllp_server::impl, not by this
    // adapter. The adapter only handles accept() and creates sessions.
    // mllp_server::impl::stop_internal() is responsible for waiting on sessions.
    (void)wait_for_connections;

    running_ = false;
}

bool bsd_mllp_server::is_running() const noexcept { return running_; }

uint16_t bsd_mllp_server::port() const noexcept { return config_.port; }

void bsd_mllp_server::on_connection(on_connection_callback callback) {
    connection_callback_ = std::move(callback);
}

size_t bsd_mllp_server::active_session_count() const noexcept {
    return active_sessions_;
}

std::expected<void, network_error> bsd_mllp_server::initialize_networking() {
#ifdef _WIN32
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        return std::unexpected(network_error::socket_error);
    }
    winsock_initialized_ = true;
#endif
    return {};
}

void bsd_mllp_server::cleanup_networking() {
#ifdef _WIN32
    if (winsock_initialized_) {
        WSACleanup();
        winsock_initialized_ = false;
    }
#endif
}

std::expected<void, network_error> bsd_mllp_server::create_server_socket() {
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

    // Bind to address
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config_.port);

    if (config_.bind_address.empty()) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, config_.bind_address.c_str(),
                      &server_addr.sin_addr) <= 0) {
            close_socket(server_socket_);
            server_socket_ = INVALID_SOCKET_VALUE;
            return std::unexpected(network_error::invalid_config);
        }
    }

    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&server_addr),
             sizeof(server_addr)) < 0) {
        close_socket(server_socket_);
        server_socket_ = INVALID_SOCKET_VALUE;
        return std::unexpected(network_error::bind_failed);
    }

    // Listen for connections
    if (listen(server_socket_, config_.backlog) < 0) {
        close_socket(server_socket_);
        server_socket_ = INVALID_SOCKET_VALUE;
        return std::unexpected(network_error::bind_failed);
    }

    return {};
}

std::expected<void, network_error>
bsd_mllp_server::configure_socket_options(socket_t sock) {
    // SO_REUSEADDR
    if (config_.reuse_addr) {
        int reuse = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<const char*>(&reuse),
                       sizeof(reuse)) < 0) {
            return std::unexpected(network_error::socket_error);
        }
    }

    // TCP_NODELAY (disable Nagle's algorithm)
    if (config_.no_delay) {
        int nodelay = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&nodelay),
                       sizeof(nodelay)) < 0) {
            return std::unexpected(network_error::socket_error);
        }
    }

    // SO_KEEPALIVE
    if (config_.keep_alive) {
        int keepalive = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
                       reinterpret_cast<const char*>(&keepalive),
                       sizeof(keepalive)) < 0) {
            return std::unexpected(network_error::socket_error);
        }

#if defined(__linux__)
        // Linux keep-alive settings
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE,
                       &config_.keep_alive_idle,
                       sizeof(config_.keep_alive_idle)) < 0) {
            return std::unexpected(network_error::socket_error);
        }

        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL,
                       &config_.keep_alive_interval,
                       sizeof(config_.keep_alive_interval)) < 0) {
            return std::unexpected(network_error::socket_error);
        }

        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT,
                       &config_.keep_alive_count,
                       sizeof(config_.keep_alive_count)) < 0) {
            return std::unexpected(network_error::socket_error);
        }
#elif defined(__APPLE__)
        // macOS keep-alive settings (uses TCP_KEEPALIVE instead of TCP_KEEPIDLE)
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPALIVE,
                       &config_.keep_alive_idle,
                       sizeof(config_.keep_alive_idle)) < 0) {
            return std::unexpected(network_error::socket_error);
        }
        // Note: macOS doesn't support TCP_KEEPINTVL and TCP_KEEPCNT
#endif
    }

    // Socket buffer sizes
    if (config_.recv_buffer_size > 0) {
        int size = static_cast<int>(config_.recv_buffer_size);
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&size), sizeof(size));
    }

    if (config_.send_buffer_size > 0) {
        int size = static_cast<int>(config_.send_buffer_size);
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&size), sizeof(size));
    }

    return {};
}

void bsd_mllp_server::accept_loop() {
    while (!stop_requested_) {
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int client_addr_len = sizeof(client_addr);
#else
        socklen_t client_addr_len = sizeof(client_addr);
#endif

        socket_t client_socket =
            accept(server_socket_,
                   reinterpret_cast<struct sockaddr*>(&client_addr),
                   &client_addr_len);

        if (client_socket == INVALID_SOCKET_VALUE) {
            if (stop_requested_) {
                break;
            }
            // Accept failed, continue or break depending on error
            continue;
        }

        // Configure client socket options
        if (auto result = configure_socket_options(client_socket); !result) {
            close_socket(client_socket);
            continue;
        }

        // Extract client address info
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
        uint16_t client_port = ntohs(client_addr.sin_port);

        // Create session
        uint64_t session_id = generate_session_id();
        auto session = std::make_unique<bsd_mllp_session>(
            client_socket, session_id, std::string(addr_str), client_port);

        // Increment active session count
        ++active_sessions_;

        // Notify callback
        if (connection_callback_) {
            connection_callback_(std::move(session));
        }
    }
}

uint64_t bsd_mllp_server::generate_session_id() {
    return next_session_id_++;
}

}  // namespace pacs::bridge::mllp
