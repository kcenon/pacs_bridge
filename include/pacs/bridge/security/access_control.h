#ifndef PACS_BRIDGE_SECURITY_ACCESS_CONTROL_H
#define PACS_BRIDGE_SECURITY_ACCESS_CONTROL_H

/**
 * @file access_control.h
 * @brief Network access control with IP whitelisting and blacklisting
 *
 * Provides network-level access control to restrict connections based on
 * IP addresses and CIDR ranges. Essential for limiting exposure and
 * preventing unauthorized access to healthcare data.
 *
 * Features:
 *   - IP whitelist (allow only listed IPs)
 *   - IP blacklist (block specific IPs)
 *   - CIDR range support (e.g., 192.168.1.0/24)
 *   - IPv4 and IPv6 support
 *   - Dynamic rule updates
 *   - Connection attempt logging
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::security {

// =============================================================================
// Error Codes (-950 to -959)
// =============================================================================

/**
 * @brief Access control error codes
 *
 * Allocated range: -950 to -959
 */
enum class access_error : int {
    /** IP address is not in whitelist */
    not_whitelisted = -950,

    /** IP address is in blacklist */
    blacklisted = -951,

    /** Invalid IP address format */
    invalid_ip_address = -952,

    /** Invalid CIDR notation */
    invalid_cidr = -953,

    /** Access control not initialized */
    not_initialized = -954,

    /** Configuration error */
    config_error = -955,

    /** Rate limit exceeded */
    rate_limited = -956,

    /** Too many failed attempts */
    too_many_failures = -957,

    /** Connection rejected */
    connection_rejected = -958
};

/**
 * @brief Convert access_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(access_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of access error
 */
[[nodiscard]] constexpr const char* to_string(access_error error) noexcept {
    switch (error) {
        case access_error::not_whitelisted:
            return "IP address is not in whitelist";
        case access_error::blacklisted:
            return "IP address is in blacklist";
        case access_error::invalid_ip_address:
            return "Invalid IP address format";
        case access_error::invalid_cidr:
            return "Invalid CIDR notation";
        case access_error::not_initialized:
            return "Access control not initialized";
        case access_error::config_error:
            return "Access control configuration error";
        case access_error::rate_limited:
            return "Rate limit exceeded";
        case access_error::too_many_failures:
            return "Too many failed attempts";
        case access_error::connection_rejected:
            return "Connection rejected";
        default:
            return "Unknown access control error";
    }
}

// =============================================================================
// Access Control Configuration
// =============================================================================

/**
 * @brief Access control mode
 */
enum class access_mode {
    /** Allow all connections (no IP filtering) */
    allow_all,

    /** Whitelist mode - only allow IPs in whitelist */
    whitelist_only,

    /** Blacklist mode - allow all except IPs in blacklist */
    blacklist_only,

    /** Combined mode - check blacklist first, then whitelist */
    whitelist_and_blacklist
};

/**
 * @brief IP range specification (single IP or CIDR)
 */
struct ip_range {
    /** IP address or network address */
    std::string address;

    /** CIDR prefix length (32 for single IPv4, 128 for single IPv6) */
    uint8_t prefix_length = 32;

    /** Description for logging/documentation */
    std::string description;

    /** When this rule was added */
    std::chrono::system_clock::time_point added_at;

    /** Expiration time (empty = never expires) */
    std::optional<std::chrono::system_clock::time_point> expires_at;

    /**
     * @brief Create from CIDR notation string
     * @param cidr CIDR string like "192.168.1.0/24" or single IP
     * @param desc Optional description
     * @return IP range or nullopt if invalid
     */
    [[nodiscard]] static std::optional<ip_range> from_cidr(
        std::string_view cidr,
        std::string_view desc = "");

    /**
     * @brief Check if an IP address matches this range
     * @param ip IP address to check
     * @return true if IP is within this range
     */
    [[nodiscard]] bool matches(std::string_view ip) const;

    /**
     * @brief Check if this range has expired
     */
    [[nodiscard]] bool is_expired() const noexcept;

    /**
     * @brief Convert to CIDR notation string
     */
    [[nodiscard]] std::string to_cidr() const;
};

/**
 * @brief Access control configuration
 */
struct access_control_config {
    /** Access control mode */
    access_mode mode = access_mode::allow_all;

    /** Enable access control */
    bool enabled = true;

    /** Whitelisted IP ranges */
    std::vector<ip_range> whitelist;

    /** Blacklisted IP ranges */
    std::vector<ip_range> blacklist;

    /** Allow localhost connections always */
    bool always_allow_localhost = true;

    /** Block private IP ranges in production mode */
    bool block_private_ranges = false;

    /** Log all access attempts */
    bool log_all_attempts = true;

    /** Log only denied attempts */
    bool log_denied_only = false;

    /** Maximum connections per IP (0 = unlimited) */
    size_t max_connections_per_ip = 10;

    /** Time window for connection counting */
    std::chrono::seconds connection_window{60};

    /** Block IP after N failed attempts (0 = disabled) */
    size_t block_after_failures = 5;

    /** Duration to block after too many failures */
    std::chrono::minutes block_duration{30};

    /** Auto-expire temporary blacklist entries */
    bool auto_expire_blocks = true;
};

// =============================================================================
// Access Control Result
// =============================================================================

/**
 * @brief Result of an access check
 */
struct access_result {
    /** Access granted */
    bool allowed = false;

    /** Reason for denial (if denied) */
    std::optional<access_error> error;

    /** Matched rule description */
    std::string matched_rule;

    /** Connection count for this IP in current window */
    size_t connection_count = 0;

    /** Failed attempt count for this IP */
    size_t failure_count = 0;

    /** Time until block expires (if blocked) */
    std::optional<std::chrono::seconds> block_remaining;

    /**
     * @brief Create allowed result
     */
    [[nodiscard]] static access_result allow(std::string_view rule = "") {
        access_result result;
        result.allowed = true;
        result.matched_rule = std::string(rule);
        return result;
    }

    /**
     * @brief Create denied result
     */
    [[nodiscard]] static access_result deny(access_error err,
                                            std::string_view rule = "") {
        access_result result;
        result.allowed = false;
        result.error = err;
        result.matched_rule = std::string(rule);
        return result;
    }
};

// =============================================================================
// Access Controller
// =============================================================================

/**
 * @brief Network access controller with IP filtering
 *
 * Manages IP-based access control for incoming connections.
 * Supports whitelist/blacklist modes, CIDR ranges, and automatic
 * blocking of misbehaving clients.
 *
 * @example Whitelist Mode
 * ```cpp
 * access_control_config config;
 * config.mode = access_mode::whitelist_only;
 * config.whitelist.push_back(
 *     ip_range::from_cidr("192.168.1.0/24", "Local network").value());
 * config.whitelist.push_back(
 *     ip_range::from_cidr("10.0.0.100", "PACS Server").value());
 *
 * access_controller controller(config);
 *
 * auto result = controller.check("192.168.1.50");
 * if (result.allowed) {
 *     accept_connection();
 * } else {
 *     reject_connection(result.error.value());
 * }
 * ```
 *
 * @example Dynamic Updates
 * ```cpp
 * access_controller controller(config);
 *
 * // Temporarily block an IP
 * controller.block("192.168.1.100", std::chrono::minutes{30},
 *                  "Suspicious activity");
 *
 * // Add to permanent whitelist
 * controller.add_to_whitelist("10.0.0.200", "New RIS server");
 *
 * // Remove from whitelist
 * controller.remove_from_whitelist("10.0.0.150");
 * ```
 */
class access_controller {
public:
    /**
     * @brief Access attempt callback type
     *
     * Called for each access check, useful for logging and monitoring.
     */
    using access_callback = std::function<void(
        std::string_view ip_address, const access_result& result)>;

    /**
     * @brief Constructor with configuration
     * @param config Access control configuration
     */
    explicit access_controller(const access_control_config& config = {});

    /**
     * @brief Destructor
     */
    ~access_controller();

    // Non-copyable, movable
    access_controller(const access_controller&) = delete;
    access_controller& operator=(const access_controller&) = delete;
    access_controller(access_controller&&) noexcept;
    access_controller& operator=(access_controller&&) noexcept;

    // =========================================================================
    // Access Checking
    // =========================================================================

    /**
     * @brief Check if an IP address is allowed to connect
     *
     * @param ip_address IP address to check (IPv4 or IPv6)
     * @return Access result with details
     */
    [[nodiscard]] access_result check(std::string_view ip_address) const;

    /**
     * @brief Check and record a connection attempt
     *
     * Same as check() but also updates connection counters.
     *
     * @param ip_address IP address to check
     * @return Access result
     */
    [[nodiscard]] access_result check_and_record(std::string_view ip_address);

    /**
     * @brief Record a failed attempt for an IP
     *
     * Used to track failed authentication attempts for auto-blocking.
     *
     * @param ip_address IP address
     */
    void record_failure(std::string_view ip_address);

    /**
     * @brief Reset failure count for an IP
     *
     * Called after successful authentication.
     *
     * @param ip_address IP address
     */
    void reset_failures(std::string_view ip_address);

    // =========================================================================
    // Whitelist Management
    // =========================================================================

    /**
     * @brief Add IP or range to whitelist
     * @param cidr IP address or CIDR range
     * @param description Rule description
     * @return true if added successfully
     */
    bool add_to_whitelist(std::string_view cidr,
                          std::string_view description = "");

    /**
     * @brief Remove IP or range from whitelist
     * @param cidr IP address or CIDR range
     * @return true if removed
     */
    bool remove_from_whitelist(std::string_view cidr);

    /**
     * @brief Get all whitelist entries
     */
    [[nodiscard]] std::vector<ip_range> get_whitelist() const;

    /**
     * @brief Clear all whitelist entries
     */
    void clear_whitelist();

    // =========================================================================
    // Blacklist Management
    // =========================================================================

    /**
     * @brief Add IP or range to blacklist
     * @param cidr IP address or CIDR range
     * @param description Rule description
     * @return true if added successfully
     */
    bool add_to_blacklist(std::string_view cidr,
                          std::string_view description = "");

    /**
     * @brief Remove IP or range from blacklist
     * @param cidr IP address or CIDR range
     * @return true if removed
     */
    bool remove_from_blacklist(std::string_view cidr);

    /**
     * @brief Get all blacklist entries
     */
    [[nodiscard]] std::vector<ip_range> get_blacklist() const;

    /**
     * @brief Clear all blacklist entries
     */
    void clear_blacklist();

    // =========================================================================
    // Temporary Blocking
    // =========================================================================

    /**
     * @brief Temporarily block an IP address
     *
     * @param ip_address IP to block
     * @param duration Block duration
     * @param reason Reason for blocking
     */
    void block(std::string_view ip_address,
               std::chrono::minutes duration,
               std::string_view reason = "");

    /**
     * @brief Unblock a temporarily blocked IP
     * @param ip_address IP to unblock
     */
    void unblock(std::string_view ip_address);

    /**
     * @brief Get all temporarily blocked IPs
     * @return Vector of blocked IP addresses with expiration times
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::chrono::system_clock::time_point>>
    get_blocked_ips() const;

    /**
     * @brief Remove expired blocks
     */
    void cleanup_expired_blocks();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Update configuration
     * @param config New configuration
     */
    void set_config(const access_control_config& config);

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const access_control_config& config() const noexcept;

    /**
     * @brief Set access mode
     * @param mode New access mode
     */
    void set_mode(access_mode mode);

    /**
     * @brief Enable or disable access control
     * @param enabled true to enable
     */
    void set_enabled(bool enabled);

    /**
     * @brief Check if access control is enabled
     */
    [[nodiscard]] bool is_enabled() const noexcept;

    // =========================================================================
    // Callbacks
    // =========================================================================

    /**
     * @brief Set callback for access attempts
     * @param callback Callback function
     */
    void set_access_callback(access_callback callback);

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Access control statistics
     */
    struct statistics {
        /** Total access checks performed */
        size_t total_checks = 0;

        /** Checks that were allowed */
        size_t allowed_count = 0;

        /** Checks that were denied */
        size_t denied_count = 0;

        /** Denied due to whitelist */
        size_t denied_not_whitelisted = 0;

        /** Denied due to blacklist */
        size_t denied_blacklisted = 0;

        /** Denied due to rate limiting */
        size_t denied_rate_limited = 0;

        /** Denied due to too many failures */
        size_t denied_too_many_failures = 0;

        /** Currently blocked IPs */
        size_t currently_blocked = 0;

        /** Unique IPs seen */
        size_t unique_ips = 0;
    };

    /**
     * @brief Get statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Validate an IP address string
 * @param ip IP address string
 * @return true if valid IPv4 or IPv6 address
 */
[[nodiscard]] bool is_valid_ip(std::string_view ip);

/**
 * @brief Check if IP is a private/local address
 * @param ip IP address
 * @return true if private range (10.x, 172.16-31.x, 192.168.x, etc.)
 */
[[nodiscard]] bool is_private_ip(std::string_view ip);

/**
 * @brief Check if IP is localhost
 * @param ip IP address
 * @return true if 127.x.x.x or ::1
 */
[[nodiscard]] bool is_localhost(std::string_view ip);

/**
 * @brief Parse CIDR notation
 * @param cidr CIDR string like "192.168.1.0/24"
 * @return Pair of address and prefix length, or nullopt if invalid
 */
[[nodiscard]] std::optional<std::pair<std::string, uint8_t>>
parse_cidr(std::string_view cidr);

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_ACCESS_CONTROL_H
