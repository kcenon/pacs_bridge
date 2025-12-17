#ifndef PACS_BRIDGE_MESSAGING_HL7_MESSAGE_BUS_H
#define PACS_BRIDGE_MESSAGING_HL7_MESSAGE_BUS_H

/**
 * @file hl7_message_bus.h
 * @brief HL7 message Pub/Sub pattern integration
 *
 * Provides topic-based message distribution for HL7 messages using
 * messaging_system's Pub/Sub pattern. Supports:
 *   - Topic hierarchy (hl7.adt.a01, hl7.orm.o01, etc.)
 *   - Wildcard subscriptions (hl7.adt.*, hl7.#)
 *   - Message filtering
 *   - Priority-based delivery
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 * @see https://github.com/kcenon/pacs_bridge/issues/153
 */

#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::messaging {

// =============================================================================
// HL7 Message Bus Error Codes (-800 to -809)
// =============================================================================

/**
 * @brief Message bus specific error codes
 *
 * Allocated range: -800 to -809
 */
enum class message_bus_error : int {
    /** Message bus not started */
    not_started = -800,

    /** Message bus already started */
    already_started = -801,

    /** Failed to publish message */
    publish_failed = -802,

    /** Failed to subscribe */
    subscribe_failed = -803,

    /** Invalid topic pattern */
    invalid_topic = -804,

    /** Subscription not found */
    subscription_not_found = -805,

    /** Message bus shutdown in progress */
    shutting_down = -806,

    /** Backend initialization failed */
    backend_init_failed = -807,

    /** Message conversion failed */
    conversion_failed = -808
};

/**
 * @brief Convert message_bus_error to error code
 */
[[nodiscard]] constexpr int to_error_code(message_bus_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(message_bus_error error) noexcept {
    switch (error) {
        case message_bus_error::not_started:
            return "Message bus not started";
        case message_bus_error::already_started:
            return "Message bus already started";
        case message_bus_error::publish_failed:
            return "Failed to publish message";
        case message_bus_error::subscribe_failed:
            return "Failed to subscribe to topic";
        case message_bus_error::invalid_topic:
            return "Invalid topic pattern";
        case message_bus_error::subscription_not_found:
            return "Subscription not found";
        case message_bus_error::shutting_down:
            return "Message bus is shutting down";
        case message_bus_error::backend_init_failed:
            return "Backend initialization failed";
        case message_bus_error::conversion_failed:
            return "Message conversion failed";
        default:
            return "Unknown message bus error";
    }
}

// =============================================================================
// HL7 Topic Definitions
// =============================================================================

/**
 * @brief Standard HL7 message topic prefixes
 *
 * Topic hierarchy follows: hl7.<message_type>.<trigger_event>
 * Example: hl7.adt.a01, hl7.orm.o01
 */
namespace topics {
    /** Base prefix for all HL7 messages */
    inline constexpr const char* HL7_BASE = "hl7";

    /** ADT messages (Admit/Discharge/Transfer) */
    inline constexpr const char* HL7_ADT = "hl7.adt";

    /** ORM messages (Order) */
    inline constexpr const char* HL7_ORM = "hl7.orm";

    /** ORU messages (Observation Result) */
    inline constexpr const char* HL7_ORU = "hl7.oru";

    /** SIU messages (Scheduling Information) */
    inline constexpr const char* HL7_SIU = "hl7.siu";

    /** ACK messages (Acknowledgment) */
    inline constexpr const char* HL7_ACK = "hl7.ack";

    /** MDM messages (Medical Document Management) */
    inline constexpr const char* HL7_MDM = "hl7.mdm";

    /** DFT messages (Detailed Financial Transaction) */
    inline constexpr const char* HL7_DFT = "hl7.dft";

    /** Wildcard for all HL7 messages */
    inline constexpr const char* HL7_ALL = "hl7.#";

    /** Wildcard for all ADT events */
    inline constexpr const char* HL7_ADT_ALL = "hl7.adt.*";

    /** Wildcard for all ORM events */
    inline constexpr const char* HL7_ORM_ALL = "hl7.orm.*";

    /** Wildcard for all ORU events */
    inline constexpr const char* HL7_ORU_ALL = "hl7.oru.*";

    /**
     * @brief Build topic string from message type and trigger
     *
     * @param message_type Message type (ADT, ORM, etc.)
     * @param trigger_event Trigger event (A01, O01, etc.)
     * @return Topic string (e.g., "hl7.adt.a01")
     */
    [[nodiscard]] std::string build_topic(std::string_view message_type,
                                           std::string_view trigger_event);

    /**
     * @brief Build topic from HL7 message
     *
     * Extracts message type and trigger event from MSH segment.
     *
     * @param message HL7 message
     * @return Topic string
     */
    [[nodiscard]] std::string build_topic(const hl7::hl7_message& message);
}  // namespace topics

// =============================================================================
// Message Subscription
// =============================================================================

/**
 * @brief Message priority for delivery ordering
 */
enum class message_priority {
    low = 0,
    normal = 5,
    high = 10
};

/**
 * @brief Subscription callback result
 */
struct subscription_result {
    /** Processing was successful */
    bool success = true;

    /** Error message if not successful */
    std::string error_message;

    /** Stop further processing for this message */
    bool stop_propagation = false;

    /**
     * @brief Create success result
     */
    [[nodiscard]] static subscription_result ok() {
        return {true, {}, false};
    }

    /**
     * @brief Create error result
     */
    [[nodiscard]] static subscription_result error(std::string_view msg) {
        return {false, std::string(msg), false};
    }

    /**
     * @brief Create result that stops propagation
     */
    [[nodiscard]] static subscription_result stop() {
        return {true, {}, true};
    }
};

/**
 * @brief Callback type for message subscriptions
 */
using message_callback =
    std::function<subscription_result(const hl7::hl7_message& message)>;

/**
 * @brief Filter function type - returns true to accept message
 */
using message_filter = std::function<bool(const hl7::hl7_message& message)>;

/**
 * @brief Subscription handle for managing subscriptions
 */
struct subscription_handle {
    /** Unique subscription identifier */
    uint64_t id = 0;

    /** Topic pattern subscribed to */
    std::string topic_pattern;

    /** Is subscription active */
    bool active = false;

    /**
     * @brief Check if handle is valid
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return id != 0 && active;
    }
};

// =============================================================================
// HL7 Message Bus Configuration
// =============================================================================

/**
 * @brief Configuration for HL7 message bus
 */
struct hl7_message_bus_config {
    /** Number of worker threads (0 = auto-detect) */
    size_t worker_threads = 0;

    /** Message queue capacity */
    size_t queue_capacity = 10000;

    /** Enable message persistence */
    bool enable_persistence = false;

    /** Enable dead letter queue for failed messages */
    bool enable_dead_letter_queue = true;

    /** Maximum retry count for failed deliveries */
    size_t max_retry_count = 3;

    /** Retry delay between attempts */
    std::chrono::milliseconds retry_delay{100};

    /** Message TTL (0 = no expiry) */
    std::chrono::seconds message_ttl{0};

    /** Enable statistics collection */
    bool enable_statistics = true;

    /**
     * @brief Create default configuration
     */
    [[nodiscard]] static hl7_message_bus_config defaults() {
        return {};
    }

    /**
     * @brief Create high-throughput configuration
     */
    [[nodiscard]] static hl7_message_bus_config high_throughput() {
        hl7_message_bus_config config;
        config.worker_threads = 4;
        config.queue_capacity = 50000;
        config.enable_statistics = false;
        return config;
    }
};

// =============================================================================
// HL7 Message Bus
// =============================================================================

/**
 * @brief HL7 message distribution using Pub/Sub pattern
 *
 * Integrates messaging_system's Pub/Sub pattern for HL7 message
 * topic-based distribution. Messages are automatically routed to
 * appropriate topics based on message type and trigger event.
 *
 * @example Basic Usage
 * ```cpp
 * hl7_message_bus bus;
 * bus.start();
 *
 * // Subscribe to ADT messages
 * auto handle = bus.subscribe(topics::HL7_ADT_ALL,
 *     [](const hl7::hl7_message& msg) {
 *         std::cout << "Received ADT: " << msg.message_type() << std::endl;
 *         return subscription_result::ok();
 *     });
 *
 * // Publish message
 * bus.publish(adt_message);
 *
 * // Cleanup
 * bus.unsubscribe(handle);
 * bus.stop();
 * ```
 *
 * @example Filtered Subscription
 * ```cpp
 * // Subscribe to VIP patients only
 * bus.subscribe(topics::HL7_ADT_ALL,
 *     [](const hl7::hl7_message& msg) {
 *         process_vip_patient(msg);
 *         return subscription_result::ok();
 *     },
 *     [](const hl7::hl7_message& msg) {
 *         return msg.get_value("PV1.18") == "VIP";
 *     });
 * ```
 */
class hl7_message_bus {
public:
    /**
     * @brief Constructor with default configuration
     */
    hl7_message_bus();

    /**
     * @brief Constructor with custom configuration
     */
    explicit hl7_message_bus(const hl7_message_bus_config& config);

    /**
     * @brief Destructor - stops bus if running
     */
    ~hl7_message_bus();

    // Non-copyable, movable
    hl7_message_bus(const hl7_message_bus&) = delete;
    hl7_message_bus& operator=(const hl7_message_bus&) = delete;
    hl7_message_bus(hl7_message_bus&&) noexcept;
    hl7_message_bus& operator=(hl7_message_bus&&) noexcept;

    // =========================================================================
    // Lifecycle Management
    // =========================================================================

    /**
     * @brief Start the message bus
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, message_bus_error> start();

    /**
     * @brief Stop the message bus
     *
     * Gracefully stops all processing and delivers pending messages.
     */
    void stop();

    /**
     * @brief Check if message bus is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Publishing
    // =========================================================================

    /**
     * @brief Publish an HL7 message
     *
     * Topic is automatically determined from message type and trigger event.
     *
     * @param message HL7 message to publish
     * @param priority Message priority (default: normal)
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, message_bus_error> publish(
        const hl7::hl7_message& message,
        message_priority priority = message_priority::normal);

    /**
     * @brief Publish an HL7 message to a specific topic
     *
     * @param topic Target topic
     * @param message HL7 message to publish
     * @param priority Message priority
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, message_bus_error> publish(
        std::string_view topic,
        const hl7::hl7_message& message,
        message_priority priority = message_priority::normal);

    // =========================================================================
    // Subscribing
    // =========================================================================

    /**
     * @brief Subscribe to a topic pattern
     *
     * @param topic_pattern Topic pattern (supports wildcards: * and #)
     * @param callback Function to call for matching messages
     * @param filter Optional filter function
     * @param priority Subscription priority (default: 5)
     * @return Subscription handle or error
     *
     * @note Wildcard patterns:
     *       - '*' matches a single level (hl7.adt.* matches hl7.adt.a01)
     *       - '#' matches multiple levels (hl7.# matches all hl7 messages)
     */
    [[nodiscard]] std::expected<subscription_handle, message_bus_error> subscribe(
        std::string_view topic_pattern,
        message_callback callback,
        message_filter filter = nullptr,
        int priority = 5);

    /**
     * @brief Subscribe to a specific message type
     *
     * Convenience method for subscribing to all events of a message type.
     *
     * @param message_type Message type (ADT, ORM, etc.)
     * @param callback Function to call for matching messages
     * @return Subscription handle or error
     */
    [[nodiscard]] std::expected<subscription_handle, message_bus_error>
    subscribe_to_type(std::string_view message_type, message_callback callback);

    /**
     * @brief Subscribe to a specific message type and trigger
     *
     * @param message_type Message type (ADT, ORM, etc.)
     * @param trigger_event Trigger event (A01, O01, etc.)
     * @param callback Function to call for matching messages
     * @return Subscription handle or error
     */
    [[nodiscard]] std::expected<subscription_handle, message_bus_error>
    subscribe_to_event(std::string_view message_type,
                       std::string_view trigger_event,
                       message_callback callback);

    /**
     * @brief Unsubscribe from a topic
     *
     * @param handle Subscription handle
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, message_bus_error> unsubscribe(
        const subscription_handle& handle);

    /**
     * @brief Unsubscribe all subscriptions
     */
    void unsubscribe_all();

    /**
     * @brief Get number of active subscriptions
     */
    [[nodiscard]] size_t subscription_count() const noexcept;

    /**
     * @brief Check if topic has subscribers
     */
    [[nodiscard]] bool has_subscribers(std::string_view topic) const noexcept;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Message bus statistics
     */
    struct statistics {
        /** Total messages published */
        uint64_t messages_published = 0;

        /** Total messages delivered */
        uint64_t messages_delivered = 0;

        /** Messages failed to deliver */
        uint64_t messages_failed = 0;

        /** Messages in dead letter queue */
        uint64_t dead_letter_count = 0;

        /** Active subscriptions */
        size_t active_subscriptions = 0;

        /** Messages per topic */
        std::vector<std::pair<std::string, uint64_t>> topic_counts;

        /** Average delivery time in microseconds */
        double avg_delivery_time_us = 0.0;
    };

    /**
     * @brief Get current statistics
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
    [[nodiscard]] const hl7_message_bus_config& config() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// HL7 Publisher (Convenience Wrapper)
// =============================================================================

/**
 * @brief Convenience wrapper for publishing HL7 messages
 *
 * Provides a simplified interface for applications that only need
 * to publish messages without managing subscriptions.
 *
 * @example
 * ```cpp
 * hl7_publisher publisher(message_bus);
 * publisher.set_default_priority(message_priority::high);
 * publisher.publish(adt_message);
 * ```
 */
class hl7_publisher {
public:
    /**
     * @brief Constructor
     *
     * @param bus Message bus to publish to (must be started)
     */
    explicit hl7_publisher(std::shared_ptr<hl7_message_bus> bus);

    /**
     * @brief Publish an HL7 message
     *
     * @param message HL7 message to publish
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, message_bus_error> publish(
        const hl7::hl7_message& message);

    /**
     * @brief Publish to specific topic
     *
     * @param topic Target topic
     * @param message HL7 message to publish
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, message_bus_error> publish(
        std::string_view topic,
        const hl7::hl7_message& message);

    /**
     * @brief Set default priority for published messages
     */
    void set_default_priority(message_priority priority);

    /**
     * @brief Check if publisher is ready
     */
    [[nodiscard]] bool is_ready() const noexcept;

private:
    std::shared_ptr<hl7_message_bus> bus_;
    message_priority default_priority_ = message_priority::normal;
};

// =============================================================================
// HL7 Subscriber (Convenience Wrapper)
// =============================================================================

/**
 * @brief Convenience wrapper for subscribing to HL7 messages
 *
 * Manages subscription lifecycle and automatically unsubscribes on destruction.
 *
 * @example
 * ```cpp
 * hl7_subscriber subscriber(message_bus);
 *
 * subscriber.on_adt([](const hl7::hl7_message& msg) {
 *     handle_adt(msg);
 *     return subscription_result::ok();
 * });
 *
 * subscriber.on_orm([](const hl7::hl7_message& msg) {
 *     handle_order(msg);
 *     return subscription_result::ok();
 * });
 * ```
 */
class hl7_subscriber {
public:
    /**
     * @brief Constructor
     *
     * @param bus Message bus to subscribe to
     */
    explicit hl7_subscriber(std::shared_ptr<hl7_message_bus> bus);

    /**
     * @brief Destructor - automatically unsubscribes
     */
    ~hl7_subscriber();

    // Non-copyable, movable
    hl7_subscriber(const hl7_subscriber&) = delete;
    hl7_subscriber& operator=(const hl7_subscriber&) = delete;
    hl7_subscriber(hl7_subscriber&&) noexcept;
    hl7_subscriber& operator=(hl7_subscriber&&) noexcept;

    /**
     * @brief Subscribe to all ADT messages
     */
    [[nodiscard]] std::expected<void, message_bus_error> on_adt(
        message_callback callback);

    /**
     * @brief Subscribe to all ORM messages
     */
    [[nodiscard]] std::expected<void, message_bus_error> on_orm(
        message_callback callback);

    /**
     * @brief Subscribe to all ORU messages
     */
    [[nodiscard]] std::expected<void, message_bus_error> on_oru(
        message_callback callback);

    /**
     * @brief Subscribe to all SIU messages
     */
    [[nodiscard]] std::expected<void, message_bus_error> on_siu(
        message_callback callback);

    /**
     * @brief Subscribe to specific topic pattern
     */
    [[nodiscard]] std::expected<void, message_bus_error> on(
        std::string_view topic_pattern,
        message_callback callback,
        message_filter filter = nullptr);

    /**
     * @brief Unsubscribe from all topics
     */
    void unsubscribe_all();

    /**
     * @brief Get number of active subscriptions
     */
    [[nodiscard]] size_t subscription_count() const noexcept;

private:
    std::shared_ptr<hl7_message_bus> bus_;
    std::vector<subscription_handle> handles_;
};

}  // namespace pacs::bridge::messaging

#endif  // PACS_BRIDGE_MESSAGING_HL7_MESSAGE_BUS_H
