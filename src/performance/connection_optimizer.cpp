/**
 * @file connection_optimizer.cpp
 * @brief Implementation of MLLP connection optimization
 */

#include "pacs/bridge/performance/connection_optimizer.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pacs::bridge::performance {

// =============================================================================
// Connection Stats Implementation
// =============================================================================

std::chrono::milliseconds connection_stats::age() const noexcept {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    return std::chrono::milliseconds(now - created_ms);
}

std::chrono::milliseconds connection_stats::idle_time() const noexcept {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    auto last = last_activity_ms.load(std::memory_order_relaxed);
    return std::chrono::milliseconds(now - last);
}

void connection_stats::reset() noexcept {
    bytes_sent.store(0, std::memory_order_relaxed);
    bytes_received.store(0, std::memory_order_relaxed);
    messages_sent.store(0, std::memory_order_relaxed);
    messages_received.store(0, std::memory_order_relaxed);
    errors.store(0, std::memory_order_relaxed);
    avg_rtt_us.store(0, std::memory_order_relaxed);
}

void connection_pool_stats::reset() noexcept {
    total_created.store(0, std::memory_order_relaxed);
    total_destroyed.store(0, std::memory_order_relaxed);
    total_acquires.store(0, std::memory_order_relaxed);
    total_releases.store(0, std::memory_order_relaxed);
    reuse_count.store(0, std::memory_order_relaxed);
    creation_count.store(0, std::memory_order_relaxed);
    wait_count.store(0, std::memory_order_relaxed);
    timeout_count.store(0, std::memory_order_relaxed);
    health_checks_passed.store(0, std::memory_order_relaxed);
    health_checks_failed.store(0, std::memory_order_relaxed);
}

// =============================================================================
// Connection Handle Implementation
// =============================================================================

struct optimized_connection_pool::connection_handle::impl {
    optimized_connection_pool* pool = nullptr;
    pooled_connection_info info;
    int socket_fd = -1;
    bool marked_unhealthy = false;
};

optimized_connection_pool::connection_handle::connection_handle()
    : impl_(std::make_unique<impl>()) {}

optimized_connection_pool::connection_handle::~connection_handle() {
    // Return connection to pool if valid
    if (impl_ && impl_->pool && impl_->socket_fd >= 0) {
        // Would call pool->release_internal(*this) if implemented
    }
}

optimized_connection_pool::connection_handle::connection_handle(
    connection_handle&& other) noexcept = default;

optimized_connection_pool::connection_handle&
optimized_connection_pool::connection_handle::operator=(
    connection_handle&& other) noexcept = default;

bool optimized_connection_pool::connection_handle::valid() const noexcept {
    return impl_ && impl_->socket_fd >= 0;
}

uint64_t optimized_connection_pool::connection_handle::id() const noexcept {
    if (!impl_) return 0;
    return impl_->info.id;
}

const pooled_connection_info&
optimized_connection_pool::connection_handle::info() const {
    static const pooled_connection_info empty_info{};
    if (!impl_) return empty_info;
    return impl_->info;
}

std::expected<size_t, performance_error>
optimized_connection_pool::connection_handle::send(
    std::span<const uint8_t> data) {
    if (!valid()) {
        return std::unexpected(performance_error::not_initialized);
    }

#ifdef _WIN32
    auto result = ::send(impl_->socket_fd,
                         reinterpret_cast<const char*>(data.data()),
                         static_cast<int>(data.size()), 0);
#else
    auto result =
        ::send(impl_->socket_fd, data.data(), data.size(), MSG_NOSIGNAL);
#endif

    if (result < 0) {
        impl_->marked_unhealthy = true;
        return std::unexpected(performance_error::allocation_failed);
    }

    impl_->info.stats.bytes_sent.fetch_add(static_cast<uint64_t>(result),
                                           std::memory_order_relaxed);
    impl_->info.stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
    impl_->info.stats.last_activity_ms.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count(),
        std::memory_order_relaxed);

    return static_cast<size_t>(result);
}

std::expected<std::vector<uint8_t>, performance_error>
optimized_connection_pool::connection_handle::receive(
    std::chrono::milliseconds timeout) {
    if (!valid()) {
        return std::unexpected(performance_error::not_initialized);
    }

    // Set receive timeout
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout.count());
    setsockopt(impl_->socket_fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    setsockopt(impl_->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::vector<uint8_t> buffer(8192);

#ifdef _WIN32
    auto result = ::recv(impl_->socket_fd, reinterpret_cast<char*>(buffer.data()),
                         static_cast<int>(buffer.size()), 0);
#else
    auto result = ::recv(impl_->socket_fd, buffer.data(), buffer.size(), 0);
#endif

    if (result < 0) {
        impl_->marked_unhealthy = true;
        return std::unexpected(performance_error::timeout);
    }

    if (result == 0) {
        impl_->marked_unhealthy = true;
        return std::unexpected(performance_error::not_initialized);
    }

    buffer.resize(static_cast<size_t>(result));

    impl_->info.stats.bytes_received.fetch_add(static_cast<uint64_t>(result),
                                               std::memory_order_relaxed);
    impl_->info.stats.messages_received.fetch_add(1, std::memory_order_relaxed);
    impl_->info.stats.last_activity_ms.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count(),
        std::memory_order_relaxed);

    return buffer;
}

void optimized_connection_pool::connection_handle::mark_unhealthy() {
    if (impl_) {
        impl_->marked_unhealthy = true;
        impl_->info.health = connection_health::unhealthy;
    }
}

// =============================================================================
// Optimized Connection Pool Implementation
// =============================================================================

struct optimized_connection_pool::impl {
    connection_pool_config config;
    connection_pool_stats stats;
    mutable std::shared_mutex mutex;
    std::atomic<bool> running{false};
    std::atomic<uint64_t> next_connection_id{1};

    struct target_pool {
        std::string host;
        uint16_t port;
        std::deque<pooled_connection_info> idle_connections;
        std::vector<pooled_connection_info> active_connections;
        connection_health overall_health = connection_health::unknown;
    };

    std::unordered_map<std::string, target_pool> pools;

    std::string make_key(const std::string& host, uint16_t port) const {
        return host + ":" + std::to_string(port);
    }

    explicit impl(const connection_pool_config& cfg) : config(cfg) {}
};

optimized_connection_pool::optimized_connection_pool(
    const connection_pool_config& config)
    : impl_(std::make_unique<impl>(config)) {}

optimized_connection_pool::~optimized_connection_pool() {
    if (impl_->running.load()) {
        (void)stop(true, std::chrono::seconds{10});
    }
}

std::expected<void, performance_error> optimized_connection_pool::start() {
    if (impl_->running.exchange(true)) {
        return std::unexpected(performance_error::invalid_configuration);
    }
    return {};
}

std::expected<void, performance_error>
optimized_connection_pool::stop(bool graceful,
                                std::chrono::milliseconds timeout) {
    if (!impl_->running.exchange(false)) {
        return std::unexpected(performance_error::not_initialized);
    }

    // Close all connections
    close_all();
    return {};
}

bool optimized_connection_pool::is_running() const noexcept {
    return impl_->running.load(std::memory_order_relaxed);
}

std::expected<optimized_connection_pool::connection_handle, performance_error>
optimized_connection_pool::acquire(const std::string& host, uint16_t port,
                                   std::chrono::milliseconds timeout) {
    if (!impl_->running.load()) {
        return std::unexpected(performance_error::not_initialized);
    }

    impl_->stats.total_acquires.fetch_add(1, std::memory_order_relaxed);

    std::string key = impl_->make_key(host, port);

    std::unique_lock lock(impl_->mutex);

    auto& pool = impl_->pools[key];
    pool.host = host;
    pool.port = port;

    // Try to get from idle pool
    if (!pool.idle_connections.empty()) {
        auto conn_info = std::move(pool.idle_connections.front());
        pool.idle_connections.pop_front();

        impl_->stats.reuse_count.fetch_add(1, std::memory_order_relaxed);
        impl_->stats.idle_connections.fetch_sub(1, std::memory_order_relaxed);
        impl_->stats.active_connections.fetch_add(1, std::memory_order_relaxed);

        connection_handle handle;
        handle.impl_->pool = this;
        handle.impl_->info = std::move(conn_info);
        handle.impl_->info.in_use = true;
        // Note: actual socket would be stored in the connection

        return handle;
    }

    // Need to create new connection
    impl_->stats.creation_count.fetch_add(1, std::memory_order_relaxed);

    // Placeholder for actual connection creation
    // In real implementation, would create socket and connect

    connection_handle handle;
    handle.impl_->pool = this;
    handle.impl_->info.id =
        impl_->next_connection_id.fetch_add(1, std::memory_order_relaxed);
    handle.impl_->info.host = host;
    handle.impl_->info.port = port;
    handle.impl_->info.health = connection_health::healthy;
    handle.impl_->info.in_use = true;
    handle.impl_->info.stats.created_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    handle.impl_->socket_fd = 0;  // Would be actual socket

    impl_->stats.total_created.fetch_add(1, std::memory_order_relaxed);
    impl_->stats.active_connections.fetch_add(1, std::memory_order_relaxed);

    return handle;
}

std::optional<optimized_connection_pool::connection_handle>
optimized_connection_pool::try_acquire(const std::string& host, uint16_t port) {
    auto result = acquire(host, port, std::chrono::milliseconds{0});
    if (result) {
        return std::move(*result);
    }
    return std::nullopt;
}

size_t optimized_connection_pool::prewarm(const std::string& host, uint16_t port,
                                          size_t count) {
    if (!impl_->running.load()) return 0;

    size_t actual_count =
        count > 0 ? count : impl_->config.min_idle_connections;
    size_t created = 0;

    for (size_t i = 0; i < actual_count; ++i) {
        // Would create actual connections here
        ++created;
    }

    return created;
}

void optimized_connection_pool::close_target(const std::string& host,
                                             uint16_t port) {
    std::string key = impl_->make_key(host, port);

    std::unique_lock lock(impl_->mutex);
    auto it = impl_->pools.find(key);
    if (it != impl_->pools.end()) {
        size_t idle_count = it->second.idle_connections.size();
        impl_->stats.idle_connections.fetch_sub(idle_count,
                                                std::memory_order_relaxed);
        impl_->stats.total_destroyed.fetch_add(idle_count,
                                               std::memory_order_relaxed);
        it->second.idle_connections.clear();
    }
}

void optimized_connection_pool::close_all() {
    std::unique_lock lock(impl_->mutex);

    for (auto& [key, pool] : impl_->pools) {
        size_t idle_count = pool.idle_connections.size();
        impl_->stats.idle_connections.fetch_sub(idle_count,
                                                std::memory_order_relaxed);
        impl_->stats.total_destroyed.fetch_add(idle_count,
                                               std::memory_order_relaxed);
        pool.idle_connections.clear();
    }
}

size_t optimized_connection_pool::run_health_check() {
    size_t unhealthy_removed = 0;

    std::unique_lock lock(impl_->mutex);

    for (auto& [key, pool] : impl_->pools) {
        auto it = pool.idle_connections.begin();
        while (it != pool.idle_connections.end()) {
            // Check connection health (simplified)
            if (it->health == connection_health::unhealthy ||
                it->stats.idle_time() > impl_->config.idle_timeout) {
                impl_->stats.health_checks_failed.fetch_add(
                    1, std::memory_order_relaxed);
                impl_->stats.idle_connections.fetch_sub(1,
                                                        std::memory_order_relaxed);
                impl_->stats.total_destroyed.fetch_add(1,
                                                       std::memory_order_relaxed);
                it = pool.idle_connections.erase(it);
                ++unhealthy_removed;
            } else {
                impl_->stats.health_checks_passed.fetch_add(
                    1, std::memory_order_relaxed);
                ++it;
            }
        }
    }

    return unhealthy_removed;
}

connection_health
optimized_connection_pool::target_health(const std::string& host,
                                         uint16_t port) const {
    std::string key = impl_->make_key(host, port);

    std::shared_lock lock(impl_->mutex);
    auto it = impl_->pools.find(key);
    if (it == impl_->pools.end()) {
        return connection_health::unknown;
    }
    return it->second.overall_health;
}

size_t optimized_connection_pool::total_connections() const noexcept {
    return impl_->stats.idle_connections.load(std::memory_order_relaxed) +
           impl_->stats.active_connections.load(std::memory_order_relaxed);
}

size_t optimized_connection_pool::idle_connections() const noexcept {
    return impl_->stats.idle_connections.load(std::memory_order_relaxed);
}

size_t optimized_connection_pool::active_connections() const noexcept {
    return impl_->stats.active_connections.load(std::memory_order_relaxed);
}

size_t optimized_connection_pool::connections_for(const std::string& host,
                                                  uint16_t port) const {
    std::string key = impl_->make_key(host, port);

    std::shared_lock lock(impl_->mutex);
    auto it = impl_->pools.find(key);
    if (it == impl_->pools.end()) {
        return 0;
    }
    return it->second.idle_connections.size() +
           it->second.active_connections.size();
}

std::vector<pooled_connection_info>
optimized_connection_pool::list_connections() const {
    std::vector<pooled_connection_info> result;

    std::shared_lock lock(impl_->mutex);
    for (const auto& [key, pool] : impl_->pools) {
        for (const auto& conn : pool.idle_connections) {
            result.push_back(conn);
        }
        for (const auto& conn : pool.active_connections) {
            result.push_back(conn);
        }
    }

    return result;
}

const connection_pool_stats&
optimized_connection_pool::statistics() const noexcept {
    return impl_->stats;
}

void optimized_connection_pool::reset_statistics() {
    impl_->stats.reset();
}

const connection_pool_config& optimized_connection_pool::config() const noexcept {
    return impl_->config;
}

// =============================================================================
// TCP Tuning Implementation
// =============================================================================

std::expected<void, performance_error>
apply_tcp_tuning(int socket_fd, const tcp_tuning_options& options) {
    if (socket_fd < 0) {
        return std::unexpected(performance_error::invalid_configuration);
    }

#ifndef _WIN32
    // TCP_NODELAY
    if (options.tcp_nodelay) {
        int flag = 1;
        if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &flag,
                       sizeof(flag)) < 0) {
            return std::unexpected(performance_error::allocation_failed);
        }
    }

    // Receive buffer size
    if (options.recv_buffer_size > 0) {
        int size = static_cast<int>(options.recv_buffer_size);
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    }

    // Send buffer size
    if (options.send_buffer_size > 0) {
        int size = static_cast<int>(options.send_buffer_size);
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    }

    // Keep-alive
    if (options.keep_alive) {
        int flag = 1;
        setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));

#ifdef __linux__
        setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE,
                   &options.keep_alive_idle, sizeof(options.keep_alive_idle));
        setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPINTVL,
                   &options.keep_alive_interval,
                   sizeof(options.keep_alive_interval));
        setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPCNT,
                   &options.keep_alive_count, sizeof(options.keep_alive_count));
#endif
    }

    // Reuse address
    if (options.reuse_addr) {
        int flag = 1;
        setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    }

    // Linger
    if (options.linger_seconds >= 0) {
        struct linger l;
        l.l_onoff = 1;
        l.l_linger = options.linger_seconds;
        setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    }

#ifdef __linux__
    // TCP_QUICKACK
    if (options.tcp_quickack) {
        int flag = 1;
        setsockopt(socket_fd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
    }
#endif

#endif  // !_WIN32

    return {};
}

}  // namespace pacs::bridge::performance
