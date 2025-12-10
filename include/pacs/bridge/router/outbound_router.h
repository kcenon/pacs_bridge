#ifndef PACS_BRIDGE_ROUTER_OUTBOUND_ROUTER_H
#define PACS_BRIDGE_ROUTER_OUTBOUND_ROUTER_H

/**
 * @file outbound_router.h
 * @brief Outbound message router for HL7 message delivery
 *
 * Provides routing infrastructure for sending HL7 messages to configured
 * external destinations with failover support. Features include:
 *   - Message type-based destination selection
 *   - Priority-based routing with failover
 *   - Health checking for destinations
 *   - Delivery tracking and statistics
 *   - Connection pool integration
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/28
 * @see docs/reference_materials/07_routing_rules.md
 */

#include "pacs/bridge/mllp/mllp_client.h"
#include "pacs/bridge/mllp/mllp_types.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <atomic>
#include <chrono>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::router {

// =============================================================================
// Outbound Router Error Codes (-920 to -929)
// =============================================================================

/**
 * @brief Outbound router specific error codes
 *
 * Allocated range: -920 to -929
 */
enum class outbound_error : int {
    /** No destination configured for message type */
    no_destination = -920,

    /** All destinations are unavailable */
    all_destinations_failed = -921,

    /** Destination not found by name */
    destination_not_found = -922,

    /** Message delivery failed */
    delivery_failed = -923,

    /** Invalid destination configuration */
    invalid_configuration = -924,

    /** Health check failed */
    health_check_failed = -925,

    /** Router is not running */
    not_running = -926,

    /** Router is already running */
    already_running = -927,

    /** Queue is full */
    queue_full = -928,

    /** Delivery timeout */
    timeout = -929
};

/**
 * @brief Convert outbound_error to error code
 */
[[nodiscard]] constexpr int to_error_code(outbound_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(outbound_error error) noexcept {
    switch (error) {
        case outbound_error::no_destination:
            return "No destination configured for message type";
        case outbound_error::all_destinations_failed:
            return "All destinations are unavailable";
        case outbound_error::destination_not_found:
            return "Destination not found";
        case outbound_error::delivery_failed:
            return "Message delivery failed";
        case outbound_error::invalid_configuration:
            return "Invalid destination configuration";
        case outbound_error::health_check_failed:
            return "Health check failed";
        case outbound_error::not_running:
            return "Router is not running";
        case outbound_error::already_running:
            return "Router is already running";
        case outbound_error::queue_full:
            return "Delivery queue is full";
        case outbound_error::timeout:
            return "Delivery timeout";
        default:
            return "Unknown outbound router error";
    }
}

// =============================================================================
// Destination Configuration
// =============================================================================

/**
 * @brief Health status of a destination
 */
enum class destination_health {
    /** Health unknown (not yet checked) */
    unknown,
    /** Destination is healthy and accepting connections */
    healthy,
    /** Destination failed health check but may recover */
    degraded,
    /** Destination is unavailable */
    unavailable
};

/**
 * @brief Get string representation of health status
 */
[[nodiscard]] constexpr const char* to_string(destination_health health) noexcept {
    switch (health) {
        case destination_health::unknown:
            return "unknown";
        case destination_health::healthy:
            return "healthy";
        case destination_health::degraded:
            return "degraded";
        case destination_health::unavailable:
            return "unavailable";
        default:
            return "unknown";
    }
}

/**
 * @brief Outbound destination configuration
 */
struct outbound_destination {
    /** Unique destination identifier */
    std::string name;

    /** Target hostname or IP */
    std::string host;

    /** Target port */
    uint16_t port = mllp::MLLP_DEFAULT_PORT;

    /** Message types to route to this destination (e.g., "ORM^O01", "ORU^R01") */
    std::vector<std::string> message_types;

    /** Priority (lower = higher priority, used for failover) */
    int priority = 100;

    /** Is destination enabled */
    bool enabled = true;

    /** TLS configuration */
    security::tls_config tls;

    /** Connection timeout */
    std::chrono::milliseconds connect_timeout{5000};

    /** Send/receive timeout */
    std::chrono::milliseconds io_timeout{30000};

    /** Retry count on failure */
    size_t retry_count = 3;

    /** Delay between retries */
    std::chrono::milliseconds retry_delay{1000};

    /** Health check interval (0 = disabled) */
    std::chrono::seconds health_check_interval{30};

    /** Maximum consecutive failures before marking unavailable */
    size_t max_consecutive_failures = 3;

    /** Description for logging */
    std::string description;

    /**
     * @brief Validate destination configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (name.empty()) return false;
        if (host.empty()) return false;
        if (port == 0) return false;
        return true;
    }

    /**
     * @brief Create MLLP client configuration from this destination
     */
    [[nodiscard]] mllp::mllp_client_config to_client_config() const {
        mllp::mllp_client_config config;
        config.host = host;
        config.port = port;
        config.connect_timeout = connect_timeout;
        config.io_timeout = io_timeout;
        config.retry_count = retry_count;
        config.retry_delay = retry_delay;
        config.tls = tls;
        config.keep_alive = true;
        return config;
    }
};

// =============================================================================
// Delivery Result
// =============================================================================

/**
 * @brief Result of message delivery attempt
 */
struct delivery_result {
    /** Delivery was successful */
    bool success = false;

    /** Destination name that handled the message */
    std::string destination_name;

    /** Response message (ACK) from destination */
    std::optional<mllp::mllp_message> response;

    /** Round-trip time for delivery */
    std::chrono::milliseconds round_trip_time{0};

    /** Number of retry attempts */
    size_t retry_count = 0;

    /** Number of failover attempts */
    size_t failover_count = 0;

    /** Error message if delivery failed */
    std::string error_message;

    /** Timestamp of delivery attempt */
    std::chrono::system_clock::time_point timestamp;

    /**
     * @brief Create success result
     */
    [[nodiscard]] static delivery_result ok(
        std::string_view dest_name,
        std::chrono::milliseconds rtt = std::chrono::milliseconds{0}) {
        delivery_result r;
        r.success = true;
        r.destination_name = std::string(dest_name);
        r.round_trip_time = rtt;
        r.timestamp = std::chrono::system_clock::now();
        return r;
    }

    /**
     * @brief Create failure result
     */
    [[nodiscard]] static delivery_result error(std::string_view message) {
        delivery_result r;
        r.success = false;
        r.error_message = std::string(message);
        r.timestamp = std::chrono::system_clock::now();
        return r;
    }
};

// =============================================================================
// Router Configuration
// =============================================================================

/**
 * @brief Outbound router configuration
 */
struct outbound_router_config {
    /** Configured destinations */
    std::vector<outbound_destination> destinations;

    /** Enable automatic health checking */
    bool enable_health_check = true;

    /** Default health check interval for destinations without specific interval */
    std::chrono::seconds default_health_check_interval{30};

    /** Queue size for async delivery (0 = sync only) */
    size_t async_queue_size = 1000;

    /** Number of worker threads for async delivery */
    size_t worker_threads = 2;

    /** Enable delivery tracking/logging */
    bool enable_tracking = true;

    /** Maximum tracked deliveries to retain */
    size_t max_tracked_deliveries = 10000;
};

// =============================================================================
// Outbound Router
// =============================================================================

/**
 * @brief Outbound message router for HL7 delivery
 *
 * Routes HL7 messages to configured external destinations based on
 * message type. Supports failover routing, health checking, and
 * async delivery.
 *
 * @example Basic Usage
 * ```cpp
 * outbound_router_config config;
 *
 * outbound_destination ris_primary;
 * ris_primary.name = "RIS_PRIMARY";
 * ris_primary.host = "ris1.hospital.local";
 * ris_primary.port = 2576;
 * ris_primary.message_types = {"ORM^O01", "ORU^R01"};
 * ris_primary.priority = 1;
 * config.destinations.push_back(ris_primary);
 *
 * outbound_destination ris_backup;
 * ris_backup.name = "RIS_BACKUP";
 * ris_backup.host = "ris2.hospital.local";
 * ris_backup.port = 2576;
 * ris_backup.message_types = {"ORM^O01", "ORU^R01"};
 * ris_backup.priority = 2;  // Lower priority = failover
 * config.destinations.push_back(ris_backup);
 *
 * outbound_router router(config);
 * router.start();
 *
 * auto result = router.route(hl7_message);
 * if (result && result->success) {
 *     std::cout << "Delivered to: " << result->destination_name << std::endl;
 * }
 * ```
 *
 * @example Async Delivery
 * ```cpp
 * auto future = router.route_async(hl7_message);
 * // ... do other work ...
 * auto result = future.get();
 * ```
 *
 * @example Health Check Callback
 * ```cpp
 * router.set_health_callback([](const std::string& name, destination_health health) {
 *     std::cout << name << " is now " << to_string(health) << std::endl;
 * });
 * ```
 */
class outbound_router {
public:
    /**
     * @brief Health status change callback
     */
    using health_callback = std::function<void(std::string_view destination_name,
                                                destination_health old_health,
                                                destination_health new_health)>;

    /**
     * @brief Delivery completion callback for async operations
     */
    using delivery_callback = std::function<void(const delivery_result& result,
                                                  const hl7::hl7_message& message)>;

    /**
     * @brief Default constructor with empty configuration
     */
    outbound_router();

    /**
     * @brief Constructor with configuration
     */
    explicit outbound_router(const outbound_router_config& config);

    /**
     * @brief Destructor - stops router if running
     */
    ~outbound_router();

    // Non-copyable, movable
    outbound_router(const outbound_router&) = delete;
    outbound_router& operator=(const outbound_router&) = delete;
    outbound_router(outbound_router&&) noexcept;
    outbound_router& operator=(outbound_router&&) noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Start the router
     *
     * Initializes connection pools and starts health checking.
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, outbound_error> start();

    /**
     * @brief Stop the router
     *
     * Stops health checking and closes all connections.
     */
    void stop();

    /**
     * @brief Check if router is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Message Routing
    // =========================================================================

    /**
     * @brief Route a message to appropriate destination(s)
     *
     * Determines destination based on message type, attempts delivery
     * with failover to backup destinations if primary fails.
     *
     * @param message HL7 message to route
     * @return Delivery result or error
     */
    [[nodiscard]] std::expected<delivery_result, outbound_error>
    route(const hl7::hl7_message& message);

    /**
     * @brief Route a message asynchronously
     *
     * @param message HL7 message to route
     * @return Future containing delivery result
     */
    [[nodiscard]] std::future<std::expected<delivery_result, outbound_error>>
    route_async(const hl7::hl7_message& message);

    /**
     * @brief Route a message with callback
     *
     * @param message HL7 message to route
     * @param callback Callback invoked when delivery completes
     * @return Success if queued, error if queue is full
     */
    [[nodiscard]] std::expected<void, outbound_error>
    route_with_callback(const hl7::hl7_message& message, delivery_callback callback);

    /**
     * @brief Route a serialized message string
     *
     * @param hl7_content Raw HL7 message content
     * @return Delivery result or error
     */
    [[nodiscard]] std::expected<delivery_result, outbound_error>
    route(std::string_view hl7_content);

    // =========================================================================
    // Destination Management
    // =========================================================================

    /**
     * @brief Get destinations for a message type
     *
     * @param message_type Message type string (e.g., "ORM^O01")
     * @return List of destination names in priority order
     */
    [[nodiscard]] std::vector<std::string>
    get_destinations(std::string_view message_type) const;

    /**
     * @brief Get all configured destinations
     */
    [[nodiscard]] std::vector<outbound_destination> destinations() const;

    /**
     * @brief Get destination by name
     */
    [[nodiscard]] const outbound_destination*
    get_destination(std::string_view name) const;

    /**
     * @brief Enable or disable a destination
     *
     * @param name Destination name
     * @param enabled Enable state
     * @return true if destination found and updated
     */
    bool set_destination_enabled(std::string_view name, bool enabled);

    /**
     * @brief Add a new destination
     *
     * @param destination Destination configuration
     * @return Success or error if invalid
     */
    [[nodiscard]] std::expected<void, outbound_error>
    add_destination(const outbound_destination& destination);

    /**
     * @brief Remove a destination
     *
     * @param name Destination name
     * @return true if removed
     */
    bool remove_destination(std::string_view name);

    // =========================================================================
    // Health Management
    // =========================================================================

    /**
     * @brief Get health status of a destination
     */
    [[nodiscard]] destination_health
    get_destination_health(std::string_view name) const;

    /**
     * @brief Get health status of all destinations
     */
    [[nodiscard]] std::unordered_map<std::string, destination_health>
    get_all_health() const;

    /**
     * @brief Trigger immediate health check for a destination
     *
     * @param name Destination name
     * @return Health check result
     */
    [[nodiscard]] std::expected<destination_health, outbound_error>
    check_health(std::string_view name);

    /**
     * @brief Trigger health check for all destinations
     */
    void check_all_health();

    /**
     * @brief Set health status change callback
     */
    void set_health_callback(health_callback callback);

    /**
     * @brief Clear health callback
     */
    void clear_health_callback();

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Router statistics
     */
    struct statistics {
        /** Total messages routed */
        size_t total_messages = 0;

        /** Successful deliveries */
        size_t successful_deliveries = 0;

        /** Failed deliveries */
        size_t failed_deliveries = 0;

        /** Failover events */
        size_t failover_events = 0;

        /** Total retry attempts */
        size_t retry_attempts = 0;

        /** Messages pending in queue */
        size_t queue_pending = 0;

        /** Average delivery time in milliseconds */
        double avg_delivery_time_ms = 0.0;

        /** Per-destination statistics */
        struct destination_stats {
            size_t messages_sent = 0;
            size_t messages_failed = 0;
            size_t bytes_sent = 0;
            double avg_response_time_ms = 0.0;
            destination_health health = destination_health::unknown;
            std::chrono::system_clock::time_point last_success;
            std::chrono::system_clock::time_point last_failure;
            size_t consecutive_failures = 0;
        };

        std::unordered_map<std::string, destination_stats> destination_stats;
    };

    /**
     * @brief Get router statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const outbound_router_config& config() const noexcept;

    /**
     * @brief Update configuration
     *
     * Note: Some changes may require restart to take effect.
     */
    void set_config(const outbound_router_config& config);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Destination Builder (Fluent API)
// =============================================================================

/**
 * @brief Fluent builder for destination configuration
 *
 * @example
 * ```cpp
 * auto dest = destination_builder::create("RIS_PRIMARY")
 *     .host("ris.hospital.local")
 *     .port(2576)
 *     .message_types({"ORM^O01", "ORU^R01"})
 *     .priority(1)
 *     .tls_enabled(true)
 *     .build();
 * ```
 */
class destination_builder {
public:
    /**
     * @brief Create new destination builder
     */
    [[nodiscard]] static destination_builder create(std::string_view name);

    /** Set host */
    destination_builder& host(std::string_view h);

    /** Set port */
    destination_builder& port(uint16_t p);

    /** Add message type */
    destination_builder& message_type(std::string_view type);

    /** Set message types */
    destination_builder& message_types(std::vector<std::string> types);

    /** Set priority */
    destination_builder& priority(int p);

    /** Set enabled state */
    destination_builder& enabled(bool e = true);

    /** Enable TLS */
    destination_builder& tls_enabled(bool enable = true);

    /** Set TLS CA path */
    destination_builder& tls_ca(std::string_view ca_path);

    /** Set TLS client certificate */
    destination_builder& tls_cert(std::string_view cert_path, std::string_view key_path);

    /** Set connect timeout */
    destination_builder& connect_timeout(std::chrono::milliseconds timeout);

    /** Set I/O timeout */
    destination_builder& io_timeout(std::chrono::milliseconds timeout);

    /** Set retry configuration */
    destination_builder& retry(size_t count, std::chrono::milliseconds delay);

    /** Set health check interval */
    destination_builder& health_check_interval(std::chrono::seconds interval);

    /** Set description */
    destination_builder& description(std::string_view desc);

    /** Build the destination */
    [[nodiscard]] outbound_destination build() const;

private:
    explicit destination_builder(std::string_view name);
    outbound_destination dest_;
};

}  // namespace pacs::bridge::router

#endif  // PACS_BRIDGE_ROUTER_OUTBOUND_ROUTER_H
