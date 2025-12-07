#ifndef PACS_BRIDGE_SECURITY_RATE_LIMITER_H
#define PACS_BRIDGE_SECURITY_RATE_LIMITER_H

/**
 * @file rate_limiter.h
 * @brief Rate limiting for connection and message throttling
 *
 * Provides rate limiting functionality to protect against DoS attacks,
 * prevent resource exhaustion, and ensure fair resource allocation.
 * Supports multiple rate limiting algorithms and configurable limits.
 *
 * Algorithms:
 *   - Token Bucket: Smooth rate limiting with burst allowance
 *   - Sliding Window: Accurate counting over time windows
 *   - Fixed Window: Simple per-window counting
 *
 * Features:
 *   - Per-IP rate limiting
 *   - Per-application rate limiting (MSH-3)
 *   - Global rate limiting
 *   - Message size-based limiting
 *   - Configurable penalties for violations
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::security {

// =============================================================================
// Rate Limit Result
// =============================================================================

/**
 * @brief Result of a rate limit check
 */
struct rate_limit_result {
    /** Request is allowed */
    bool allowed = false;

    /** Current request count in window */
    size_t current_count = 0;

    /** Maximum allowed in window */
    size_t limit = 0;

    /** Remaining requests in current window */
    size_t remaining = 0;

    /** Time until limit resets */
    std::chrono::milliseconds reset_after{0};

    /** Time until next request allowed (if denied) */
    std::chrono::milliseconds retry_after{0};

    /** Limit key that was checked (IP, app, global) */
    std::string limit_key;

    /**
     * @brief Create allowed result
     */
    [[nodiscard]] static rate_limit_result allow(size_t current, size_t max,
                                                  std::chrono::milliseconds reset) {
        rate_limit_result result;
        result.allowed = true;
        result.current_count = current;
        result.limit = max;
        result.remaining = (current < max) ? (max - current) : 0;
        result.reset_after = reset;
        return result;
    }

    /**
     * @brief Create denied result
     */
    [[nodiscard]] static rate_limit_result deny(size_t current, size_t max,
                                                 std::chrono::milliseconds retry) {
        rate_limit_result result;
        result.allowed = false;
        result.current_count = current;
        result.limit = max;
        result.remaining = 0;
        result.retry_after = retry;
        return result;
    }
};

// =============================================================================
// Rate Limit Configuration
// =============================================================================

/**
 * @brief Rate limiting algorithm type
 */
enum class rate_limit_algorithm {
    /** Token bucket - allows bursts, smooth limiting */
    token_bucket,

    /** Sliding window - accurate counting */
    sliding_window,

    /** Fixed window - simple per-window counting */
    fixed_window
};

/**
 * @brief Rate limit tier configuration
 *
 * Defines rate limits for a specific tier (IP, application, global).
 */
struct rate_limit_tier {
    /** Maximum requests per window */
    size_t max_requests = 100;

    /** Time window duration */
    std::chrono::seconds window_duration{60};

    /** Burst allowance (token bucket only) */
    size_t burst_size = 10;

    /** Token refill rate per second (token bucket only) */
    double refill_rate = 10.0;

    /** Enable this tier */
    bool enabled = true;
};

/**
 * @brief Complete rate limiter configuration
 */
struct rate_limit_config {
    /** Enable rate limiting */
    bool enabled = true;

    /** Rate limiting algorithm */
    rate_limit_algorithm algorithm = rate_limit_algorithm::sliding_window;

    /** Per-IP limits */
    rate_limit_tier per_ip_limit = {
        .max_requests = 100,
        .window_duration = std::chrono::seconds{60},
        .burst_size = 20,
        .refill_rate = 2.0,
        .enabled = true
    };

    /** Per-application limits (MSH-3) */
    rate_limit_tier per_app_limit = {
        .max_requests = 500,
        .window_duration = std::chrono::seconds{60},
        .burst_size = 50,
        .refill_rate = 10.0,
        .enabled = true
    };

    /** Global limits */
    rate_limit_tier global_limit = {
        .max_requests = 1000,
        .window_duration = std::chrono::seconds{60},
        .burst_size = 100,
        .refill_rate = 20.0,
        .enabled = true
    };

    /** Message size limits (bytes per window) */
    struct {
        bool enabled = true;
        size_t max_bytes_per_ip = 100 * 1024 * 1024;     // 100MB
        size_t max_bytes_global = 1024 * 1024 * 1024;    // 1GB
        std::chrono::seconds window_duration{60};
    } size_limits;

    /** Connection rate limits (new connections per window) */
    struct {
        bool enabled = true;
        size_t max_connections_per_ip = 10;
        size_t max_connections_global = 100;
        std::chrono::seconds window_duration{10};
    } connection_limits;

    /** Penalty multiplier for repeated violations */
    double penalty_multiplier = 1.5;

    /** Maximum penalty (in window units) */
    size_t max_penalty_windows = 10;

    /** Cleanup interval for expired entries */
    std::chrono::seconds cleanup_interval{300};

    /** Exempt IPs from rate limiting */
    std::vector<std::string> exempt_ips;

    /** Exempt applications from rate limiting */
    std::vector<std::string> exempt_apps;
};

// =============================================================================
// Rate Limiter
// =============================================================================

/**
 * @brief Multi-tier rate limiter
 *
 * Provides comprehensive rate limiting with per-IP, per-application,
 * and global limits. Supports multiple algorithms and configurable
 * penalties for repeated violations.
 *
 * @example Basic Usage
 * ```cpp
 * rate_limit_config config;
 * config.per_ip_limit.max_requests = 100;
 * config.per_ip_limit.window_duration = std::chrono::seconds{60};
 *
 * rate_limiter limiter(config);
 *
 * // Check rate limit for incoming request
 * auto result = limiter.check_request("192.168.1.100", "PACS_APP");
 * if (!result.allowed) {
 *     // Return 429 Too Many Requests
 *     send_retry_after(result.retry_after);
 *     return;
 * }
 *
 * // Process request
 * process_message(msg);
 *
 * // Record message size for size-based limiting
 * limiter.record_bytes("192.168.1.100", msg.size());
 * ```
 *
 * @example Connection Limiting
 * ```cpp
 * rate_limiter limiter(config);
 *
 * // On new connection
 * auto result = limiter.check_connection("192.168.1.100");
 * if (!result.allowed) {
 *     reject_connection("rate limited");
 *     return;
 * }
 *
 * accept_connection();
 * ```
 */
class rate_limiter {
public:
    /**
     * @brief Rate limit violation callback type
     */
    using violation_callback = std::function<void(
        std::string_view key,
        const rate_limit_result& result)>;

    /**
     * @brief Constructor with configuration
     * @param config Rate limit configuration
     */
    explicit rate_limiter(const rate_limit_config& config = {});

    /**
     * @brief Destructor
     */
    ~rate_limiter();

    // Non-copyable, movable
    rate_limiter(const rate_limiter&) = delete;
    rate_limiter& operator=(const rate_limiter&) = delete;
    rate_limiter(rate_limiter&&) noexcept;
    rate_limiter& operator=(rate_limiter&&) noexcept;

    // =========================================================================
    // Rate Limit Checking
    // =========================================================================

    /**
     * @brief Check and record a request
     *
     * Checks all applicable rate limits (IP, application, global).
     *
     * @param ip_address Client IP address
     * @param application Application identifier (MSH-3, optional)
     * @return Rate limit result
     */
    [[nodiscard]] rate_limit_result check_request(
        std::string_view ip_address,
        std::string_view application = "");

    /**
     * @brief Check without recording
     *
     * Peek at current rate limit status without incrementing counters.
     *
     * @param ip_address Client IP address
     * @param application Application identifier
     * @return Rate limit result
     */
    [[nodiscard]] rate_limit_result peek(
        std::string_view ip_address,
        std::string_view application = "") const;

    /**
     * @brief Check connection rate limit
     *
     * Special rate limit for new connections (typically stricter).
     *
     * @param ip_address Client IP address
     * @return Rate limit result
     */
    [[nodiscard]] rate_limit_result check_connection(
        std::string_view ip_address);

    /**
     * @brief Check per-IP limit only
     * @param ip_address IP address
     * @return Rate limit result
     */
    [[nodiscard]] rate_limit_result check_ip_limit(
        std::string_view ip_address);

    /**
     * @brief Check per-application limit only
     * @param application Application identifier
     * @return Rate limit result
     */
    [[nodiscard]] rate_limit_result check_app_limit(
        std::string_view application);

    /**
     * @brief Check global limit only
     * @return Rate limit result
     */
    [[nodiscard]] rate_limit_result check_global_limit();

    // =========================================================================
    // Size-Based Limiting
    // =========================================================================

    /**
     * @brief Check and record bytes transferred
     *
     * @param ip_address Client IP address
     * @param bytes Number of bytes
     * @return Rate limit result based on size limits
     */
    [[nodiscard]] rate_limit_result check_bytes(
        std::string_view ip_address,
        size_t bytes);

    /**
     * @brief Record bytes without checking
     *
     * @param ip_address Client IP address
     * @param bytes Number of bytes
     */
    void record_bytes(std::string_view ip_address, size_t bytes);

    /**
     * @brief Get bytes transferred for IP in current window
     */
    [[nodiscard]] size_t get_bytes_transferred(
        std::string_view ip_address) const;

    /**
     * @brief Get total bytes transferred in current window
     */
    [[nodiscard]] size_t get_total_bytes_transferred() const;

    // =========================================================================
    // Penalty Management
    // =========================================================================

    /**
     * @brief Apply penalty to a client
     *
     * Increases rate limit window duration as penalty.
     *
     * @param key IP address or application identifier
     * @param multiplier Penalty multiplier
     */
    void apply_penalty(std::string_view key, double multiplier = 0.0);

    /**
     * @brief Reset penalties for a client
     * @param key IP address or application identifier
     */
    void reset_penalty(std::string_view key);

    /**
     * @brief Get current penalty multiplier for a client
     */
    [[nodiscard]] double get_penalty(std::string_view key) const;

    // =========================================================================
    // Exemptions
    // =========================================================================

    /**
     * @brief Add IP to exempt list
     * @param ip_address IP to exempt
     */
    void add_exempt_ip(std::string_view ip_address);

    /**
     * @brief Remove IP from exempt list
     * @param ip_address IP to remove
     */
    void remove_exempt_ip(std::string_view ip_address);

    /**
     * @brief Check if IP is exempt
     */
    [[nodiscard]] bool is_exempt_ip(std::string_view ip_address) const;

    /**
     * @brief Add application to exempt list
     * @param application Application identifier
     */
    void add_exempt_app(std::string_view application);

    /**
     * @brief Remove application from exempt list
     */
    void remove_exempt_app(std::string_view application);

    /**
     * @brief Check if application is exempt
     */
    [[nodiscard]] bool is_exempt_app(std::string_view application) const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Update configuration
     * @param config New configuration
     */
    void set_config(const rate_limit_config& config);

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const rate_limit_config& config() const noexcept;

    /**
     * @brief Enable or disable rate limiting
     */
    void set_enabled(bool enabled);

    /**
     * @brief Check if rate limiting is enabled
     */
    [[nodiscard]] bool is_enabled() const noexcept;

    // =========================================================================
    // Callbacks
    // =========================================================================

    /**
     * @brief Set callback for rate limit violations
     */
    void set_violation_callback(violation_callback callback);

    // =========================================================================
    // Maintenance
    // =========================================================================

    /**
     * @brief Clean up expired entries
     */
    void cleanup();

    /**
     * @brief Reset all rate limit counters
     */
    void reset();

    /**
     * @brief Reset counters for a specific IP
     */
    void reset_ip(std::string_view ip_address);

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Rate limiter statistics
     */
    struct statistics {
        /** Total requests checked */
        size_t total_requests = 0;

        /** Requests allowed */
        size_t allowed_requests = 0;

        /** Requests denied */
        size_t denied_requests = 0;

        /** Denied by IP limit */
        size_t denied_by_ip = 0;

        /** Denied by app limit */
        size_t denied_by_app = 0;

        /** Denied by global limit */
        size_t denied_by_global = 0;

        /** Denied by size limit */
        size_t denied_by_size = 0;

        /** Denied by connection limit */
        size_t denied_by_connection = 0;

        /** Total bytes processed */
        size_t total_bytes = 0;

        /** Currently tracked IPs */
        size_t tracked_ips = 0;

        /** Currently tracked apps */
        size_t tracked_apps = 0;

        /** Active penalties */
        size_t active_penalties = 0;
    };

    /**
     * @brief Get statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    // =========================================================================
    // Status Information
    // =========================================================================

    /**
     * @brief Get current rate limit status for an IP
     */
    struct client_status {
        std::string ip_address;
        size_t requests_in_window = 0;
        size_t requests_limit = 0;
        size_t bytes_in_window = 0;
        size_t bytes_limit = 0;
        double penalty_multiplier = 1.0;
        std::chrono::milliseconds window_reset;
        bool is_exempt = false;
    };

    /**
     * @brief Get status for a specific IP
     */
    [[nodiscard]] client_status get_client_status(
        std::string_view ip_address) const;

    /**
     * @brief Get all tracked client statuses
     */
    [[nodiscard]] std::vector<client_status> get_all_client_statuses() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// HTTP Header Helpers
// =============================================================================

/**
 * @brief Generate rate limit headers for HTTP response
 *
 * Returns headers conforming to RFC 6585 and draft-ietf-httpapi-ratelimit-headers.
 *
 * @param result Rate limit result
 * @return Map of header name -> value
 */
[[nodiscard]] std::unordered_map<std::string, std::string>
make_rate_limit_headers(const rate_limit_result& result);

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_RATE_LIMITER_H
