#ifndef PACS_BRIDGE_ROUTER_QUEUE_MANAGER_H
#define PACS_BRIDGE_ROUTER_QUEUE_MANAGER_H

/**
 * @file queue_manager.h
 * @brief Persistent message queue for reliable outbound delivery
 *
 * Provides a durable message queue with retry logic and crash recovery
 * for reliable HL7 message delivery. Features include:
 *   - SQLite-based persistent storage
 *   - Priority-based message scheduling
 *   - Exponential backoff retry strategy
 *   - Dead letter queue for failed messages
 *   - Thread-safe operations
 *   - Crash recovery support
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/27
 * @see docs/SDS_COMPONENTS.md (DES-ROUTE-002)
 */

#include <atomic>
#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// IExecutor interface for task execution (when available)
#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include <kcenon/common/interfaces/executor_interface.h>
#endif

namespace pacs::bridge::router {

// =============================================================================
// Queue Error Codes (-910 to -919)
// =============================================================================

/**
 * @brief Queue manager specific error codes
 *
 * Allocated range: -910 to -919
 */
enum class queue_error : int {
    /** Failed to open or initialize database */
    database_error = -910,

    /** Message not found in queue */
    message_not_found = -911,

    /** Queue has reached maximum capacity */
    queue_full = -912,

    /** Invalid message data */
    invalid_message = -913,

    /** Message has expired (TTL exceeded) */
    message_expired = -914,

    /** Failed to serialize/deserialize message */
    serialization_error = -915,

    /** Queue manager is not running */
    not_running = -916,

    /** Queue manager is already running */
    already_running = -917,

    /** Transaction failed */
    transaction_error = -918,

    /** Worker operation failed */
    worker_error = -919
};

/**
 * @brief Convert queue_error to error code
 */
[[nodiscard]] constexpr int to_error_code(queue_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(queue_error error) noexcept {
    switch (error) {
        case queue_error::database_error:
            return "Database operation failed";
        case queue_error::message_not_found:
            return "Message not found in queue";
        case queue_error::queue_full:
            return "Queue has reached maximum capacity";
        case queue_error::invalid_message:
            return "Invalid message data";
        case queue_error::message_expired:
            return "Message has expired";
        case queue_error::serialization_error:
            return "Message serialization failed";
        case queue_error::not_running:
            return "Queue manager is not running";
        case queue_error::already_running:
            return "Queue manager is already running";
        case queue_error::transaction_error:
            return "Transaction failed";
        case queue_error::worker_error:
            return "Worker operation failed";
        default:
            return "Unknown queue error";
    }
}

// =============================================================================
// Message State
// =============================================================================

/**
 * @brief State of a queued message
 */
enum class message_state {
    /** Message is pending delivery */
    pending,
    /** Message is currently being processed */
    processing,
    /** Message delivery is scheduled for retry */
    retry_scheduled,
    /** Message was successfully delivered */
    delivered,
    /** Message moved to dead letter queue */
    dead_letter
};

/**
 * @brief Get string representation of message state
 */
[[nodiscard]] constexpr const char* to_string(message_state state) noexcept {
    switch (state) {
        case message_state::pending:
            return "pending";
        case message_state::processing:
            return "processing";
        case message_state::retry_scheduled:
            return "retry_scheduled";
        case message_state::delivered:
            return "delivered";
        case message_state::dead_letter:
            return "dead_letter";
        default:
            return "unknown";
    }
}

// =============================================================================
// Queue Configuration
// =============================================================================

/**
 * @brief Queue manager configuration
 */
struct queue_config {
    /** Path to SQLite database file */
    std::string database_path = "queue.db";

    /** Maximum number of messages in queue */
    size_t max_queue_size = 50000;

    /** Maximum retry attempts before moving to dead letter */
    size_t max_retry_count = 5;

    /** Initial delay before first retry */
    std::chrono::seconds initial_retry_delay{5};

    /** Multiplier for exponential backoff */
    double retry_backoff_multiplier = 2.0;

    /** Maximum retry delay (cap for exponential backoff) */
    std::chrono::seconds max_retry_delay{600};

    /** Time-to-live for messages (0 = no expiration) */
    std::chrono::hours message_ttl{24};

    /** Number of worker threads for delivery */
    size_t worker_count = 4;

    /** Batch size for dequeue operations */
    size_t batch_size = 10;

    /** Interval for cleanup of expired messages */
    std::chrono::minutes cleanup_interval{5};

    /** Enable WAL mode for better concurrent access */
    bool enable_wal_mode = true;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    /** Optional executor for worker and cleanup task execution (nullptr = use internal std::thread) */
    std::shared_ptr<kcenon::common::interfaces::IExecutor> executor;
#endif

    /**
     * @brief Validate configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (database_path.empty()) return false;
        if (max_queue_size == 0) return false;
        if (max_retry_count == 0) return false;
        if (worker_count == 0) return false;
        if (retry_backoff_multiplier < 1.0) return false;
        return true;
    }
};

// =============================================================================
// Queued Message
// =============================================================================

/**
 * @brief Message stored in the queue
 */
struct queued_message {
    /** Unique message identifier */
    std::string id;

    /** Target destination identifier */
    std::string destination;

    /** Message payload (serialized HL7 content) */
    std::string payload;

    /** Message priority (lower = higher priority) */
    int priority = 0;

    /** Current message state */
    message_state state = message_state::pending;

    /** Timestamp when message was enqueued */
    std::chrono::system_clock::time_point created_at;

    /** Timestamp for next retry attempt */
    std::chrono::system_clock::time_point scheduled_at;

    /** Number of delivery attempts */
    int attempt_count = 0;

    /** Last error message if delivery failed */
    std::string last_error;

    /** Optional correlation ID for tracking */
    std::string correlation_id;

    /** Optional message type (e.g., "ORM^O01") */
    std::string message_type;
};

// =============================================================================
// Dead Letter Entry
// =============================================================================

/**
 * @brief Entry in the dead letter queue
 */
struct dead_letter_entry {
    /** Original message */
    queued_message message;

    /** Reason for moving to dead letter */
    std::string reason;

    /** Timestamp when moved to dead letter */
    std::chrono::system_clock::time_point dead_lettered_at;

    /** All error messages from delivery attempts */
    std::vector<std::string> error_history;
};

// =============================================================================
// Queue Statistics
// =============================================================================

/**
 * @brief Queue manager statistics
 */
struct queue_statistics {
    /** Total messages enqueued since start */
    size_t total_enqueued = 0;

    /** Total messages successfully delivered */
    size_t total_delivered = 0;

    /** Total messages moved to dead letter */
    size_t total_dead_lettered = 0;

    /** Total retry attempts */
    size_t total_retries = 0;

    /** Current pending messages count */
    size_t pending_count = 0;

    /** Current processing messages count */
    size_t processing_count = 0;

    /** Current retry scheduled messages count */
    size_t retry_scheduled_count = 0;

    /** Current dead letter queue size */
    size_t dead_letter_count = 0;

    /** Average delivery time in milliseconds */
    double avg_delivery_time_ms = 0.0;

    /** Queue depth by destination */
    std::vector<std::pair<std::string, size_t>> depth_by_destination;

    /** Messages expired due to TTL */
    size_t expired_count = 0;
};

// =============================================================================
// Queue Manager
// =============================================================================

/**
 * @brief Persistent message queue manager
 *
 * Manages a durable message queue with SQLite storage for reliable
 * message delivery. Supports retry logic with exponential backoff
 * and dead letter handling for failed messages.
 *
 * @example Basic Usage
 * ```cpp
 * queue_config config;
 * config.database_path = "/var/lib/pacs/queue.db";
 * config.max_retry_count = 5;
 * config.worker_count = 4;
 *
 * queue_manager queue(config);
 * queue.start();
 *
 * // Enqueue a message
 * auto result = queue.enqueue("RIS_PRIMARY", hl7_payload);
 * if (result) {
 *     std::cout << "Enqueued with ID: " << *result << std::endl;
 * }
 *
 * // Start workers with sender function
 * queue.start_workers([](const queued_message& msg) {
 *     // Send message to destination
 *     return send_to_mllp(msg.destination, msg.payload);
 * });
 * ```
 *
 * @example Priority Queuing
 * ```cpp
 * // High priority message (lower number = higher priority)
 * queue.enqueue("RIS", urgent_payload, -10);
 *
 * // Normal priority
 * queue.enqueue("RIS", normal_payload, 0);
 *
 * // Low priority
 * queue.enqueue("RIS", batch_payload, 100);
 * ```
 *
 * @example Dead Letter Handling
 * ```cpp
 * // Set callback for dead letter events
 * queue.set_dead_letter_callback([](const dead_letter_entry& entry) {
 *     log_error("Message dead-lettered", {
 *         {"message_id", entry.message.id},
 *         {"reason", entry.reason}
 *     });
 *     alert_operations(entry);
 * });
 *
 * // Retrieve dead letters for manual processing
 * auto dead_letters = queue.get_dead_letters(100);
 * for (const auto& entry : dead_letters) {
 *     // Attempt manual reprocessing or archive
 * }
 * ```
 */
class queue_manager {
public:
    /**
     * @brief Message sender function type
     *
     * Returns success/error result from delivery attempt.
     */
    using sender_function = std::function<std::expected<void, std::string>(
        const queued_message& message)>;

    /**
     * @brief Dead letter callback function type
     */
    using dead_letter_callback = std::function<void(const dead_letter_entry& entry)>;

    /**
     * @brief Delivery completion callback
     */
    using delivery_callback = std::function<void(
        const queued_message& message,
        bool success,
        const std::string& error_message)>;

    /**
     * @brief Default constructor
     */
    queue_manager();

    /**
     * @brief Constructor with configuration
     */
    explicit queue_manager(const queue_config& config);

    /**
     * @brief Destructor - stops workers and closes database
     */
    ~queue_manager();

    // Non-copyable, movable
    queue_manager(const queue_manager&) = delete;
    queue_manager& operator=(const queue_manager&) = delete;
    queue_manager(queue_manager&&) noexcept;
    queue_manager& operator=(queue_manager&&) noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Initialize and start the queue manager
     *
     * Opens the database and recovers any in-progress messages
     * from previous runs.
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, queue_error> start();

    /**
     * @brief Stop the queue manager
     *
     * Stops workers, flushes pending operations, and closes database.
     */
    void stop();

    /**
     * @brief Check if queue manager is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Queue Operations
    // =========================================================================

    /**
     * @brief Enqueue a message for delivery
     *
     * @param destination Target destination identifier
     * @param payload Message payload (serialized HL7)
     * @param priority Message priority (lower = higher priority)
     * @return Message ID or error
     */
    [[nodiscard]] std::expected<std::string, queue_error> enqueue(
        std::string_view destination,
        std::string_view payload,
        int priority = 0);

    /**
     * @brief Enqueue a message with additional metadata
     *
     * @param destination Target destination identifier
     * @param payload Message payload
     * @param priority Message priority
     * @param correlation_id Optional correlation ID
     * @param message_type Optional message type (e.g., "ORM^O01")
     * @return Message ID or error
     */
    [[nodiscard]] std::expected<std::string, queue_error> enqueue(
        std::string_view destination,
        std::string_view payload,
        int priority,
        std::string_view correlation_id,
        std::string_view message_type);

    /**
     * @brief Dequeue a message for processing
     *
     * Returns the highest priority message that is ready for delivery.
     * The message state is changed to 'processing'.
     *
     * @param destination Optional destination filter
     * @return Message or empty if no messages ready
     */
    [[nodiscard]] std::optional<queued_message> dequeue(
        std::string_view destination = "");

    /**
     * @brief Dequeue multiple messages for batch processing
     *
     * @param count Maximum number of messages to dequeue
     * @param destination Optional destination filter
     * @return List of messages ready for delivery
     */
    [[nodiscard]] std::vector<queued_message> dequeue_batch(
        size_t count,
        std::string_view destination = "");

    /**
     * @brief Acknowledge successful delivery
     *
     * Removes the message from the queue.
     *
     * @param message_id Message identifier
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, queue_error> ack(std::string_view message_id);

    /**
     * @brief Negative acknowledge - schedule retry
     *
     * Schedules the message for retry with exponential backoff.
     * If max retries exceeded, moves to dead letter queue.
     *
     * @param message_id Message identifier
     * @param error Error message from delivery attempt
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, queue_error> nack(
        std::string_view message_id,
        std::string_view error);

    /**
     * @brief Move message directly to dead letter queue
     *
     * @param message_id Message identifier
     * @param reason Reason for dead-lettering
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, queue_error> dead_letter(
        std::string_view message_id,
        std::string_view reason);

    // =========================================================================
    // Worker Management
    // =========================================================================

    /**
     * @brief Start worker threads with sender function
     *
     * Workers continuously dequeue messages and call the sender function
     * for delivery. On success, messages are acked. On failure, nacked.
     *
     * @param sender Function to send messages
     */
    void start_workers(sender_function sender);

    /**
     * @brief Stop worker threads
     *
     * Waits for in-progress deliveries to complete.
     */
    void stop_workers();

    /**
     * @brief Check if workers are running
     */
    [[nodiscard]] bool workers_running() const noexcept;

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
     * Moves the message back to pending state with reset retry count.
     *
     * @param message_id Message identifier
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, queue_error> retry_dead_letter(
        std::string_view message_id);

    /**
     * @brief Delete a dead-lettered message
     *
     * @param message_id Message identifier
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, queue_error> delete_dead_letter(
        std::string_view message_id);

    /**
     * @brief Purge all dead letters
     *
     * @return Number of entries purged
     */
    size_t purge_dead_letters();

    /**
     * @brief Set callback for dead letter events
     */
    void set_dead_letter_callback(dead_letter_callback callback);

    /**
     * @brief Clear dead letter callback
     */
    void clear_dead_letter_callback();

    // =========================================================================
    // Queue Inspection
    // =========================================================================

    /**
     * @brief Get message by ID
     *
     * @param message_id Message identifier
     * @return Message or empty if not found
     */
    [[nodiscard]] std::optional<queued_message> get_message(
        std::string_view message_id) const;

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

    /**
     * @brief Get current queue depth
     */
    [[nodiscard]] size_t queue_depth() const;

    /**
     * @brief Get queue depth by destination
     *
     * @param destination Destination identifier
     */
    [[nodiscard]] size_t queue_depth(std::string_view destination) const;

    /**
     * @brief Get list of destinations with pending messages
     */
    [[nodiscard]] std::vector<std::string> destinations() const;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get queue statistics
     */
    [[nodiscard]] queue_statistics get_statistics() const;

    /**
     * @brief Reset statistics counters
     */
    void reset_statistics();

    // =========================================================================
    // Maintenance
    // =========================================================================

    /**
     * @brief Clean up expired messages
     *
     * @return Number of messages expired
     */
    size_t cleanup_expired();

    /**
     * @brief Compact the database
     *
     * Reclaims space from deleted messages.
     */
    void compact();

    /**
     * @brief Recover in-progress messages after crash
     *
     * Resets 'processing' state messages to 'pending'.
     * Called automatically on start().
     *
     * @return Number of messages recovered
     */
    size_t recover();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const queue_config& config() const noexcept;

    /**
     * @brief Set delivery callback
     *
     * Called after each delivery attempt (success or failure).
     */
    void set_delivery_callback(delivery_callback callback);

    /**
     * @brief Clear delivery callback
     */
    void clear_delivery_callback();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Queue Config Builder (Fluent API)
// =============================================================================

/**
 * @brief Fluent builder for queue configuration
 *
 * @example
 * ```cpp
 * auto config = queue_config_builder::create()
 *     .database("/var/lib/pacs/queue.db")
 *     .max_size(100000)
 *     .workers(8)
 *     .retry_policy(5, std::chrono::seconds{10}, 2.0)
 *     .ttl(std::chrono::hours{48})
 *     .build();
 * ```
 */
class queue_config_builder {
public:
    /**
     * @brief Create new builder with defaults
     */
    [[nodiscard]] static queue_config_builder create();

    /** Set database path */
    queue_config_builder& database(std::string_view path);

    /** Set maximum queue size */
    queue_config_builder& max_size(size_t size);

    /** Set worker thread count */
    queue_config_builder& workers(size_t count);

    /** Set retry policy */
    queue_config_builder& retry_policy(
        size_t max_retries,
        std::chrono::seconds initial_delay,
        double backoff_multiplier);

    /** Set maximum retry delay */
    queue_config_builder& max_retry_delay(std::chrono::seconds delay);

    /** Set message TTL */
    queue_config_builder& ttl(std::chrono::hours ttl);

    /** Set batch size */
    queue_config_builder& batch_size(size_t size);

    /** Set cleanup interval */
    queue_config_builder& cleanup_interval(std::chrono::minutes interval);

    /** Enable/disable WAL mode */
    queue_config_builder& wal_mode(bool enable);

    /** Build the configuration */
    [[nodiscard]] queue_config build() const;

private:
    queue_config_builder();
    queue_config config_;
};

}  // namespace pacs::bridge::router

#endif  // PACS_BRIDGE_ROUTER_QUEUE_MANAGER_H
