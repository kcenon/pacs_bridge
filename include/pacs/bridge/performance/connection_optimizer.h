#ifndef PACS_BRIDGE_PERFORMANCE_CONNECTION_OPTIMIZER_H
#define PACS_BRIDGE_PERFORMANCE_CONNECTION_OPTIMIZER_H

/**
 * @file connection_optimizer.h
 * @brief MLLP connection optimization for high-performance networking
 *
 * Provides optimized connection pooling, pre-warming, and TCP tuning
 * for MLLP message transport. Designed to maintain low latency and
 * high throughput under load.
 *
 * Key Features:
 *   - Connection pooling with health monitoring
 *   - Pre-warming of connections on startup
 *   - TCP_NODELAY and buffer tuning
 *   - Automatic connection recycling
 *   - Load balancing across connections
 *
 * @see docs/SRS.md NFR-1.4 (Concurrent connections >= 50)
 */

#include "pacs/bridge/performance/performance_types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// Connection Health
// =============================================================================

/**
 * @brief Connection health status
 */
enum class connection_health : uint8_t {
    /** Connection is healthy */
    healthy,

    /** Connection is degraded (slow response) */
    degraded,

    /** Connection is unhealthy (errors) */
    unhealthy,

    /** Connection is unknown (not checked) */
    unknown
};

/**
 * @brief Connection statistics
 */
struct connection_stats {
    /** Total bytes sent */
    std::atomic<uint64_t> bytes_sent{0};

    /** Total bytes received */
    std::atomic<uint64_t> bytes_received{0};

    /** Total messages sent */
    std::atomic<uint64_t> messages_sent{0};

    /** Total messages received */
    std::atomic<uint64_t> messages_received{0};

    /** Total errors */
    std::atomic<uint64_t> errors{0};

    /** Average round-trip time in microseconds */
    std::atomic<uint64_t> avg_rtt_us{0};

    /** Last activity timestamp */
    std::atomic<uint64_t> last_activity_ms{0};

    /** Connection creation timestamp */
    uint64_t created_ms = 0;

    /**
     * @brief Get connection age
     */
    [[nodiscard]] std::chrono::milliseconds age() const noexcept;

    /**
     * @brief Get idle time since last activity
     */
    [[nodiscard]] std::chrono::milliseconds idle_time() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset() noexcept;
};

// =============================================================================
// Connection Info
// =============================================================================

/**
 * @brief Information about a pooled connection
 */
struct pooled_connection_info {
    /** Unique connection ID */
    uint64_t id = 0;

    /** Target hostname */
    std::string host;

    /** Target port */
    uint16_t port = 0;

    /** Health status */
    connection_health health = connection_health::unknown;

    /** Is currently in use */
    bool in_use = false;

    /** Connection statistics */
    connection_stats stats;

    /** TLS enabled */
    bool tls_enabled = false;

    /** TLS version (if TLS) */
    std::optional<std::string> tls_version;
};

// =============================================================================
// Connection Pool Statistics
// =============================================================================

/**
 * @brief Statistics for connection pool
 */
struct connection_pool_stats {
    /** Total connections created */
    std::atomic<uint64_t> total_created{0};

    /** Total connections destroyed */
    std::atomic<uint64_t> total_destroyed{0};

    /** Total acquire operations */
    std::atomic<uint64_t> total_acquires{0};

    /** Total release operations */
    std::atomic<uint64_t> total_releases{0};

    /** Acquire operations that reused connection */
    std::atomic<uint64_t> reuse_count{0};

    /** Acquire operations that created new connection */
    std::atomic<uint64_t> creation_count{0};

    /** Acquire operations that waited */
    std::atomic<uint64_t> wait_count{0};

    /** Acquire operations that timed out */
    std::atomic<uint64_t> timeout_count{0};

    /** Current idle connections */
    std::atomic<size_t> idle_connections{0};

    /** Current in-use connections */
    std::atomic<size_t> active_connections{0};

    /** Peak active connections */
    std::atomic<size_t> peak_active{0};

    /** Health check passes */
    std::atomic<uint64_t> health_checks_passed{0};

    /** Health check failures */
    std::atomic<uint64_t> health_checks_failed{0};

    /**
     * @brief Get reuse rate
     */
    [[nodiscard]] double reuse_rate() const noexcept {
        uint64_t total = total_acquires.load(std::memory_order_relaxed);
        if (total == 0) return 0.0;
        uint64_t reused = reuse_count.load(std::memory_order_relaxed);
        return (static_cast<double>(reused) / static_cast<double>(total)) * 100.0;
    }

    /**
     * @brief Reset statistics
     */
    void reset() noexcept;
};

// =============================================================================
// Optimized Connection Pool
// =============================================================================

/**
 * @brief High-performance connection pool for MLLP
 *
 * Manages a pool of reusable MLLP connections with health monitoring,
 * pre-warming, and automatic recycling.
 *
 * Example usage:
 * @code
 *     connection_pool_config config;
 *     config.min_idle_connections = 4;
 *     config.max_connections_per_target = 10;
 *
 *     optimized_connection_pool pool(config);
 *     pool.start();
 *
 *     // Pre-warm connections to target
 *     pool.prewarm("192.168.1.100", 2575);
 *
 *     // Acquire connection
 *     auto conn = pool.acquire("192.168.1.100", 2575);
 *     if (conn) {
 *         // Use connection
 *         conn->send(message);
 *         auto response = conn->receive();
 *
 *         // Connection automatically returned when conn goes out of scope
 *     }
 * @endcode
 */
class optimized_connection_pool {
public:
    // -------------------------------------------------------------------------
    // Types
    // -------------------------------------------------------------------------

    /**
     * @brief Connection handle (RAII wrapper)
     */
    class connection_handle {
    public:
        connection_handle();
        ~connection_handle();

        connection_handle(connection_handle&& other) noexcept;
        connection_handle& operator=(connection_handle&& other) noexcept;

        // Non-copyable
        connection_handle(const connection_handle&) = delete;
        connection_handle& operator=(const connection_handle&) = delete;

        /**
         * @brief Check if handle is valid
         */
        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

        /**
         * @brief Get connection ID
         */
        [[nodiscard]] uint64_t id() const noexcept;

        /**
         * @brief Get connection info
         */
        [[nodiscard]] const pooled_connection_info& info() const;

        /**
         * @brief Send data
         */
        [[nodiscard]] std::expected<size_t, performance_error>
        send(std::span<const uint8_t> data);

        /**
         * @brief Receive data
         */
        [[nodiscard]] std::expected<std::vector<uint8_t>, performance_error>
        receive(std::chrono::milliseconds timeout = std::chrono::seconds{30});

        /**
         * @brief Mark connection as unhealthy (will be destroyed on release)
         */
        void mark_unhealthy();

    private:
        friend class optimized_connection_pool;
        struct impl;
        std::unique_ptr<impl> impl_;
    };

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /**
     * @brief Construct connection pool
     * @param config Pool configuration
     */
    explicit optimized_connection_pool(const connection_pool_config& config = {});

    /** Destructor */
    ~optimized_connection_pool();

    // Non-copyable, non-movable
    optimized_connection_pool(const optimized_connection_pool&) = delete;
    optimized_connection_pool& operator=(const optimized_connection_pool&) = delete;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Start the connection pool
     */
    [[nodiscard]] std::expected<void, performance_error> start();

    /**
     * @brief Stop the connection pool
     *
     * @param graceful Wait for active connections to be released
     * @param timeout Maximum time to wait
     */
    [[nodiscard]] std::expected<void, performance_error>
    stop(bool graceful = true,
         std::chrono::milliseconds timeout = std::chrono::seconds{30});

    /**
     * @brief Check if pool is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // -------------------------------------------------------------------------
    // Connection Operations
    // -------------------------------------------------------------------------

    /**
     * @brief Acquire a connection to target
     *
     * @param host Target hostname
     * @param port Target port
     * @param timeout Maximum time to wait
     * @return Connection handle or error
     */
    [[nodiscard]] std::expected<connection_handle, performance_error>
    acquire(const std::string& host, uint16_t port,
            std::chrono::milliseconds timeout = std::chrono::seconds{5});

    /**
     * @brief Try to acquire without waiting
     *
     * @param host Target hostname
     * @param port Target port
     * @return Connection handle if available
     */
    [[nodiscard]] std::optional<connection_handle>
    try_acquire(const std::string& host, uint16_t port);

    /**
     * @brief Pre-warm connections to target
     *
     * @param host Target hostname
     * @param port Target port
     * @param count Number of connections to create
     * @return Number of connections created
     */
    size_t prewarm(const std::string& host, uint16_t port, size_t count = 0);

    /**
     * @brief Close all connections to target
     *
     * @param host Target hostname
     * @param port Target port
     */
    void close_target(const std::string& host, uint16_t port);

    /**
     * @brief Close all connections
     */
    void close_all();

    // -------------------------------------------------------------------------
    // Health Management
    // -------------------------------------------------------------------------

    /**
     * @brief Run health check on all connections
     *
     * @return Number of unhealthy connections removed
     */
    size_t run_health_check();

    /**
     * @brief Get health of target
     */
    [[nodiscard]] connection_health target_health(const std::string& host,
                                                   uint16_t port) const;

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /**
     * @brief Get total connection count
     */
    [[nodiscard]] size_t total_connections() const noexcept;

    /**
     * @brief Get idle connection count
     */
    [[nodiscard]] size_t idle_connections() const noexcept;

    /**
     * @brief Get active connection count
     */
    [[nodiscard]] size_t active_connections() const noexcept;

    /**
     * @brief Get connections for specific target
     */
    [[nodiscard]] size_t connections_for(const std::string& host,
                                         uint16_t port) const;

    /**
     * @brief Get list of all connections
     */
    [[nodiscard]] std::vector<pooled_connection_info> list_connections() const;

    /**
     * @brief Get statistics
     */
    [[nodiscard]] const connection_pool_stats& statistics() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    /**
     * @brief Get configuration
     */
    [[nodiscard]] const connection_pool_config& config() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// TCP Tuning Options
// =============================================================================

/**
 * @brief TCP socket tuning for optimal performance
 */
struct tcp_tuning_options {
    /** Enable TCP_NODELAY (disable Nagle's algorithm) */
    bool tcp_nodelay = true;

    /** Socket receive buffer size (0 = system default) */
    size_t recv_buffer_size = 65536;

    /** Socket send buffer size (0 = system default) */
    size_t send_buffer_size = 65536;

    /** Enable SO_KEEPALIVE */
    bool keep_alive = true;

    /** Keep-alive idle time (seconds) */
    int keep_alive_idle = 60;

    /** Keep-alive interval (seconds) */
    int keep_alive_interval = 10;

    /** Keep-alive probe count */
    int keep_alive_count = 5;

    /** Enable TCP_QUICKACK (Linux only) */
    bool tcp_quickack = true;

    /** Enable SO_REUSEADDR */
    bool reuse_addr = true;

    /** Connection linger time on close (-1 = disabled) */
    int linger_seconds = -1;
};

/**
 * @brief Apply TCP tuning to socket
 *
 * @param socket_fd Socket file descriptor
 * @param options Tuning options
 * @return Success or error
 */
[[nodiscard]] std::expected<void, performance_error>
apply_tcp_tuning(int socket_fd, const tcp_tuning_options& options);

}  // namespace pacs::bridge::performance

#endif  // PACS_BRIDGE_PERFORMANCE_CONNECTION_OPTIMIZER_H
