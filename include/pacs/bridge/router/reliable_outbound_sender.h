#ifndef PACS_BRIDGE_ROUTER_RELIABLE_OUTBOUND_SENDER_H
#define PACS_BRIDGE_ROUTER_RELIABLE_OUTBOUND_SENDER_H

/**
 * @file reliable_outbound_sender.h
 * @brief Reliable outbound message delivery with persistence and retry
 *
 * Integrates queue_manager (persistence/retry/DLQ) with outbound_router
 * (destination selection + MLLP send) to provide guaranteed delivery
 * semantics for outbound HL7 messages.
 *
 * Features:
 *   - SQLite-backed persistent queue
 *   - Automatic retry with exponential backoff
 *   - Dead letter queue for failed messages
 *   - Crash recovery support
 *   - Health-aware destination selection with failover
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/174
 * @see docs/SDS_COMPONENTS.md (DES-ROUTE-002)
 */

#include "pacs/bridge/router/outbound_router.h"
#include "pacs/bridge/router/queue_manager.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::router {

// =============================================================================
// Reliable Outbound Sender Error Codes (-930 to -939)
// =============================================================================

/**
 * @brief Reliable outbound sender specific error codes
 *
 * Allocated range: -930 to -939
 */
enum class reliable_sender_error : int {
    /** Sender is not running */
    not_running = -930,

    /** Sender is already running */
    already_running = -931,

    /** Failed to initialize queue */
    queue_init_failed = -932,

    /** Failed to initialize router */
    router_init_failed = -933,

    /** Message enqueue failed */
    enqueue_failed = -934,

    /** Invalid configuration */
    invalid_configuration = -935,

    /** Destination not found */
    destination_not_found = -936,

    /** Internal error */
    internal_error = -937
};

/**
 * @brief Convert reliable_sender_error to error code
 */
[[nodiscard]] constexpr int to_error_code(reliable_sender_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(reliable_sender_error error) noexcept {
    switch (error) {
        case reliable_sender_error::not_running:
            return "Reliable sender is not running";
        case reliable_sender_error::already_running:
            return "Reliable sender is already running";
        case reliable_sender_error::queue_init_failed:
            return "Failed to initialize queue";
        case reliable_sender_error::router_init_failed:
            return "Failed to initialize router";
        case reliable_sender_error::enqueue_failed:
            return "Failed to enqueue message";
        case reliable_sender_error::invalid_configuration:
            return "Invalid configuration";
        case reliable_sender_error::destination_not_found:
            return "Destination not found";
        case reliable_sender_error::internal_error:
            return "Internal error";
        default:
            return "Unknown reliable sender error";
    }
}

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief Configuration for reliable outbound sender
 */
struct reliable_sender_config {
    /** Queue manager configuration */
    queue_config queue;

    /** Outbound router configuration */
    outbound_router_config router;

    /** Enable auto-start of workers on start() */
    bool auto_start_workers = true;

    /**
     * @brief Validate configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return queue.is_valid();
    }
};

// =============================================================================
// Enqueue Request
// =============================================================================

/**
 * @brief Request to enqueue a message for reliable delivery
 */
struct enqueue_request {
    /** Target destination identifier (logical name) */
    std::string destination;

    /** Message payload (serialized HL7 content) */
    std::string payload;

    /** Correlation ID for end-to-end tracking */
    std::string correlation_id;

    /** Message type (e.g., "ORM^O01", "ORU^R01") */
    std::string message_type;

    /** Message priority (lower = higher priority) */
    int priority = 0;

    /**
     * @brief Validate request
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !destination.empty() && !payload.empty();
    }
};

// =============================================================================
// Delivery Event
// =============================================================================

/**
 * @brief Event emitted on delivery success or failure
 */
struct delivery_event {
    /** Message ID */
    std::string message_id;

    /** Destination name */
    std::string destination;

    /** Correlation ID */
    std::string correlation_id;

    /** Message type */
    std::string message_type;

    /** Delivery was successful */
    bool success = false;

    /** Error message if failed */
    std::string error;

    /** Round-trip time */
    std::chrono::milliseconds round_trip_time{0};

    /** Number of attempts made */
    int attempt_count = 0;

    /** Timestamp of the event */
    std::chrono::system_clock::time_point timestamp;
};

// =============================================================================
// Statistics
// =============================================================================

/**
 * @brief Combined statistics for reliable sender
 */
struct reliable_sender_statistics {
    /** Queue statistics */
    queue_statistics queue_stats;

    /** Router statistics */
    outbound_router::statistics router_stats;

    /** Total messages enqueued */
    size_t total_enqueued = 0;

    /** Total messages delivered successfully */
    size_t total_delivered = 0;

    /** Total messages failed (moved to DLQ) */
    size_t total_failed = 0;

    /** Current queue depth */
    size_t queue_depth = 0;

    /** Current DLQ depth */
    size_t dlq_depth = 0;

    /** Average delivery latency in milliseconds */
    double avg_delivery_latency_ms = 0.0;
};

// =============================================================================
// Reliable Outbound Sender
// =============================================================================

/**
 * @brief Reliable outbound message sender with persistence and retry
 *
 * Combines queue_manager (persistence/retry/DLQ) with outbound_router
 * (destination selection + MLLP send) to provide guaranteed delivery
 * semantics for outbound HL7 messages.
 *
 * @example Basic Usage
 * ```cpp
 * reliable_sender_config config;
 * config.queue.database_path = "/var/lib/pacs/outbound_queue.db";
 * config.queue.max_retry_count = 5;
 * config.queue.worker_count = 4;
 *
 * // Configure destinations
 * auto ris = destination_builder::create("RIS_PRIMARY")
 *     .host("ris.hospital.local")
 *     .port(2576)
 *     .message_types({"ORM^O01", "ORU^R01"})
 *     .build();
 * config.router.destinations.push_back(ris);
 *
 * reliable_outbound_sender sender(config);
 * auto result = sender.start();
 * if (!result) {
 *     std::cerr << "Failed to start: " << to_string(result.error()) << std::endl;
 *     return;
 * }
 *
 * // Enqueue a message for reliable delivery
 * enqueue_request request;
 * request.destination = "RIS_PRIMARY";
 * request.payload = hl7_content;
 * request.correlation_id = "ORDER-12345";
 * request.message_type = "ORM^O01";
 * request.priority = 0;
 *
 * auto msg_id = sender.enqueue(request);
 * if (msg_id) {
 *     std::cout << "Enqueued with ID: " << *msg_id << std::endl;
 * }
 * ```
 *
 * @example With Delivery Callback
 * ```cpp
 * sender.set_delivery_callback([](const delivery_event& event) {
 *     if (event.success) {
 *         log_info("Delivered", {
 *             {"message_id", event.message_id},
 *             {"destination", event.destination},
 *             {"rtt_ms", event.round_trip_time.count()}
 *         });
 *     } else {
 *         log_error("Delivery failed", {
 *             {"message_id", event.message_id},
 *             {"error", event.error}
 *         });
 *     }
 * });
 * ```
 *
 * @example Recovery After Restart
 * ```cpp
 * // On restart, the sender automatically recovers in-progress messages
 * reliable_outbound_sender sender(config);
 * sender.start();  // Recovers any messages that were processing before crash
 *
 * // Check recovered count
 * auto stats = sender.get_statistics();
 * std::cout << "Pending: " << stats.queue_stats.pending_count << std::endl;
 * ```
 */
class reliable_outbound_sender {
public:
    /**
     * @brief Delivery event callback
     */
    using delivery_callback = std::function<void(const delivery_event& event)>;

    /**
     * @brief Dead letter callback
     */
    using dead_letter_callback = std::function<void(const dead_letter_entry& entry)>;

    /**
     * @brief Default constructor with default configuration
     */
    reliable_outbound_sender();

    /**
     * @brief Constructor with configuration
     */
    explicit reliable_outbound_sender(const reliable_sender_config& config);

    /**
     * @brief Destructor - stops sender if running
     */
    ~reliable_outbound_sender();

    // Non-copyable, movable
    reliable_outbound_sender(const reliable_outbound_sender&) = delete;
    reliable_outbound_sender& operator=(const reliable_outbound_sender&) = delete;
    reliable_outbound_sender(reliable_outbound_sender&&) noexcept;
    reliable_outbound_sender& operator=(reliable_outbound_sender&&) noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Start the reliable sender
     *
     * Initializes the queue and router, recovers any in-progress messages,
     * and starts worker threads for delivery.
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, reliable_sender_error> start();

    /**
     * @brief Stop the reliable sender
     *
     * Stops workers, flushes pending operations, and closes resources.
     * In-progress messages will be recovered on next start.
     */
    void stop();

    /**
     * @brief Check if sender is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    /**
     * @brief Wait for shutdown signal
     *
     * Blocks until stop() is called.
     */
    void wait_for_shutdown();

    // =========================================================================
    // Message Enqueueing
    // =========================================================================

    /**
     * @brief Enqueue a message for reliable delivery
     *
     * @param request Enqueue request containing destination, payload, etc.
     * @return Message ID or error
     */
    [[nodiscard]] std::expected<std::string, reliable_sender_error>
    enqueue(const enqueue_request& request);

    /**
     * @brief Enqueue a message with individual parameters
     *
     * @param destination Target destination identifier
     * @param payload Serialized HL7 content
     * @param priority Message priority (lower = higher priority)
     * @param correlation_id Optional correlation ID for tracking
     * @param message_type Optional message type (e.g., "ORM^O01")
     * @return Message ID or error
     */
    [[nodiscard]] std::expected<std::string, reliable_sender_error>
    enqueue(std::string_view destination,
            std::string_view payload,
            int priority = 0,
            std::string_view correlation_id = "",
            std::string_view message_type = "");

    // =========================================================================
    // Destination Management
    // =========================================================================

    /**
     * @brief Add a destination
     *
     * @param destination Destination configuration
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, reliable_sender_error>
    add_destination(const outbound_destination& destination);

    /**
     * @brief Remove a destination
     *
     * @param name Destination name
     * @return true if removed
     */
    bool remove_destination(std::string_view name);

    /**
     * @brief Get all configured destinations
     */
    [[nodiscard]] std::vector<outbound_destination> destinations() const;

    /**
     * @brief Check if a destination exists
     */
    [[nodiscard]] bool has_destination(std::string_view name) const;

    /**
     * @brief Get destination health status
     */
    [[nodiscard]] destination_health get_destination_health(std::string_view name) const;

    // =========================================================================
    // Queue Inspection
    // =========================================================================

    /**
     * @brief Get current queue depth
     */
    [[nodiscard]] size_t queue_depth() const;

    /**
     * @brief Get queue depth for a specific destination
     */
    [[nodiscard]] size_t queue_depth(std::string_view destination) const;

    /**
     * @brief Get pending messages for a destination
     *
     * @param destination Destination identifier
     * @param limit Maximum messages to retrieve
     * @return List of pending messages
     */
    [[nodiscard]] std::vector<queued_message> get_pending(
        std::string_view destination,
        size_t limit = 100) const;

    // =========================================================================
    // Dead Letter Queue
    // =========================================================================

    /**
     * @brief Get dead letter entries
     *
     * @param limit Maximum entries to retrieve
     * @param offset Offset for pagination
     * @return List of dead letter entries
     */
    [[nodiscard]] std::vector<dead_letter_entry> get_dead_letters(
        size_t limit = 100,
        size_t offset = 0) const;

    /**
     * @brief Get dead letter count
     */
    [[nodiscard]] size_t dead_letter_count() const;

    /**
     * @brief Retry a dead-lettered message
     *
     * @param message_id Message identifier
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, reliable_sender_error>
    retry_dead_letter(std::string_view message_id);

    /**
     * @brief Delete a dead-lettered message
     *
     * @param message_id Message identifier
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, reliable_sender_error>
    delete_dead_letter(std::string_view message_id);

    /**
     * @brief Purge all dead letters
     *
     * @return Number of entries purged
     */
    size_t purge_dead_letters();

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get combined statistics
     */
    [[nodiscard]] reliable_sender_statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    // =========================================================================
    // Callbacks
    // =========================================================================

    /**
     * @brief Set delivery event callback
     *
     * Called on each delivery attempt (success or failure).
     */
    void set_delivery_callback(delivery_callback callback);

    /**
     * @brief Clear delivery callback
     */
    void clear_delivery_callback();

    /**
     * @brief Set dead letter callback
     *
     * Called when a message is moved to the dead letter queue.
     */
    void set_dead_letter_callback(dead_letter_callback callback);

    /**
     * @brief Clear dead letter callback
     */
    void clear_dead_letter_callback();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const reliable_sender_config& config() const noexcept;

    // =========================================================================
    // Component Access (for advanced use)
    // =========================================================================

    /**
     * @brief Get the underlying queue manager
     *
     * @warning Direct manipulation may affect reliability guarantees
     */
    [[nodiscard]] queue_manager& get_queue_manager();

    /**
     * @brief Get the underlying outbound router
     *
     * @warning Direct manipulation may affect reliability guarantees
     */
    [[nodiscard]] outbound_router& get_outbound_router();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Config Builder (Fluent API)
// =============================================================================

/**
 * @brief Fluent builder for reliable sender configuration
 *
 * @example
 * ```cpp
 * auto config = reliable_sender_config_builder::create()
 *     .database("/var/lib/pacs/queue.db")
 *     .workers(4)
 *     .retry_policy(5, std::chrono::seconds{5}, 2.0)
 *     .add_destination(
 *         destination_builder::create("RIS")
 *             .host("ris.local")
 *             .port(2576)
 *             .build())
 *     .build();
 * ```
 */
class reliable_sender_config_builder {
public:
    /**
     * @brief Create new builder with defaults
     */
    [[nodiscard]] static reliable_sender_config_builder create();

    /** Set database path */
    reliable_sender_config_builder& database(std::string_view path);

    /** Set maximum queue size */
    reliable_sender_config_builder& max_queue_size(size_t size);

    /** Set worker thread count */
    reliable_sender_config_builder& workers(size_t count);

    /** Set retry policy */
    reliable_sender_config_builder& retry_policy(
        size_t max_retries,
        std::chrono::seconds initial_delay,
        double backoff_multiplier);

    /** Set message TTL */
    reliable_sender_config_builder& ttl(std::chrono::hours ttl);

    /** Add a destination */
    reliable_sender_config_builder& add_destination(const outbound_destination& dest);

    /** Enable/disable health checking */
    reliable_sender_config_builder& health_check(bool enable);

    /** Enable/disable auto-start workers */
    reliable_sender_config_builder& auto_start_workers(bool enable);

    /** Build the configuration */
    [[nodiscard]] reliable_sender_config build() const;

private:
    reliable_sender_config_builder();
    reliable_sender_config config_;
};

}  // namespace pacs::bridge::router

#endif  // PACS_BRIDGE_ROUTER_RELIABLE_OUTBOUND_SENDER_H
