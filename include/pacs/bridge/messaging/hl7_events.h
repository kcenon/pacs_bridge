#ifndef PACS_BRIDGE_MESSAGING_HL7_EVENTS_H
#define PACS_BRIDGE_MESSAGING_HL7_EVENTS_H

/**
 * @file hl7_events.h
 * @brief HL7 event types for Event Bus integration
 *
 * Defines event types for HL7 message processing workflow:
 *   - Receive events: message received, ACK sent
 *   - Processing events: parsed, validated, routed
 *   - Transformation events: HL7→DICOM mapping, worklist updates
 *
 * These events integrate with common_system's Event Bus to enable
 * loosely-coupled, event-driven message processing.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/142
 * @see common_system/include/kcenon/common/patterns/event_bus.h
 */

#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::messaging {

// Forward declaration
namespace hl7 {
    class hl7_message;
}

// =============================================================================
// Event Base Types
// =============================================================================

/**
 * @brief Common base for all HL7 events
 *
 * Provides shared fields for event identification and timing.
 */
struct hl7_event_base {
    /** Unique event identifier */
    std::string event_id;

    /** Correlation ID for request tracking */
    std::string correlation_id;

    /** Event timestamp */
    std::chrono::steady_clock::time_point timestamp;

    /** Source module/component that generated the event */
    std::string source;

protected:
    /**
     * @brief Initialize base event fields
     */
    hl7_event_base();

    /**
     * @brief Initialize with correlation ID
     */
    explicit hl7_event_base(std::string_view correlation_id);
};

// =============================================================================
// Receive Events
// =============================================================================

/**
 * @brief Event published when an HL7 message is received
 *
 * Published immediately after receiving raw HL7 data from MLLP connection,
 * before any parsing or processing occurs.
 */
struct hl7_message_received_event : hl7_event_base {
    /** Message type from MSH-9 (e.g., "ADT^A01") */
    std::string message_type;

    /** Raw HL7 message data */
    std::string raw_message;

    /** Source connection identifier */
    std::string connection_id;

    /** Remote endpoint (IP:port) */
    std::string remote_endpoint;

    /** Message size in bytes */
    size_t message_size = 0;

    /**
     * @brief Default constructor
     */
    hl7_message_received_event() = default;

    /**
     * @brief Construct with message data
     */
    hl7_message_received_event(std::string_view msg_type,
                                std::string raw_data,
                                std::string_view conn_id = "",
                                std::string_view endpoint = "");
};

/**
 * @brief Event published when an ACK/NAK is sent
 *
 * Published after sending acknowledgment response to the sender.
 */
struct hl7_ack_sent_event : hl7_event_base {
    /** Original message control ID being acknowledged */
    std::string original_message_control_id;

    /** ACK code sent (AA, AE, AR, CA, CE, CR) */
    std::string ack_code;

    /** Text message in the ACK */
    std::string text_message;

    /** Destination connection identifier */
    std::string connection_id;

    /** Time taken to process and send ACK */
    std::chrono::microseconds processing_time{0};

    /** Was this a successful acknowledgment */
    bool success = true;

    /**
     * @brief Default constructor
     */
    hl7_ack_sent_event() = default;

    /**
     * @brief Construct with ACK details
     */
    hl7_ack_sent_event(std::string_view original_msg_id,
                       std::string_view code,
                       std::string_view correlation = "",
                       bool is_success = true);
};

// =============================================================================
// Processing Events
// =============================================================================

/**
 * @brief Event published when an HL7 message is successfully parsed
 *
 * Published after raw data is converted to structured HL7 message.
 */
struct hl7_message_parsed_event : hl7_event_base {
    /** Parsed message type (e.g., "ADT^A01") */
    std::string message_type;

    /** Message control ID from MSH-10 */
    std::string message_control_id;

    /** Sending application from MSH-3 */
    std::string sending_application;

    /** Sending facility from MSH-4 */
    std::string sending_facility;

    /** HL7 version from MSH-12 */
    std::string hl7_version;

    /** Number of segments in the message */
    size_t segment_count = 0;

    /** Segment names present in the message */
    std::vector<std::string> segment_names;

    /** Time taken to parse */
    std::chrono::microseconds parse_time{0};

    /**
     * @brief Default constructor
     */
    hl7_message_parsed_event() = default;

    /**
     * @brief Construct with parsed message details
     */
    hl7_message_parsed_event(std::string_view msg_type,
                              std::string_view control_id,
                              std::string_view correlation = "");
};

/**
 * @brief Event published when an HL7 message passes validation
 *
 * Published after message structure and content validation succeeds.
 */
struct hl7_message_validated_event : hl7_event_base {
    /** Message type that was validated */
    std::string message_type;

    /** Message control ID */
    std::string message_control_id;

    /** Validation profile used */
    std::string validation_profile;

    /** List of warnings (non-fatal issues) */
    std::vector<std::string> warnings;

    /** Time taken to validate */
    std::chrono::microseconds validation_time{0};

    /**
     * @brief Default constructor
     */
    hl7_message_validated_event() = default;

    /**
     * @brief Construct with validation details
     */
    hl7_message_validated_event(std::string_view msg_type,
                                 std::string_view control_id,
                                 std::string_view profile = "default",
                                 std::string_view correlation = "");
};

/**
 * @brief Event published when an HL7 message is routed to a destination
 *
 * Published after determining the message destination based on routing rules.
 */
struct hl7_message_routed_event : hl7_event_base {
    /** Message type being routed */
    std::string message_type;

    /** Message control ID */
    std::string message_control_id;

    /** Routing rule that matched */
    std::string routing_rule;

    /** Target destination(s) */
    std::vector<std::string> destinations;

    /** Priority assigned to the message */
    int priority = 0;

    /** Time taken to route */
    std::chrono::microseconds routing_time{0};

    /**
     * @brief Default constructor
     */
    hl7_message_routed_event() = default;

    /**
     * @brief Construct with routing details
     */
    hl7_message_routed_event(std::string_view msg_type,
                              std::string_view control_id,
                              std::string_view rule,
                              std::string_view correlation = "");
};

// =============================================================================
// Transformation Events
// =============================================================================

/**
 * @brief Event published when HL7 message is mapped to DICOM
 *
 * Published after successful transformation from HL7 to DICOM format.
 */
struct hl7_to_dicom_mapped_event : hl7_event_base {
    /** Original HL7 message type */
    std::string hl7_message_type;

    /** Original message control ID */
    std::string message_control_id;

    /** DICOM SOP Class UID */
    std::string sop_class_uid;

    /** Patient ID from mapping */
    std::string patient_id;

    /** Accession Number from mapping */
    std::string accession_number;

    /** Study Instance UID if generated */
    std::optional<std::string> study_instance_uid;

    /** Number of attributes mapped */
    size_t mapped_attributes = 0;

    /** Mapping profile used */
    std::string mapping_profile;

    /** Time taken to map */
    std::chrono::microseconds mapping_time{0};

    /**
     * @brief Default constructor
     */
    hl7_to_dicom_mapped_event() = default;

    /**
     * @brief Construct with mapping details
     */
    hl7_to_dicom_mapped_event(std::string_view msg_type,
                               std::string_view control_id,
                               std::string_view pat_id,
                               std::string_view correlation = "");
};

/**
 * @brief Event published when DICOM Modality Worklist is updated
 *
 * Published after worklist entry is created, updated, or removed.
 */
struct dicom_worklist_updated_event : hl7_event_base {
    /** Operation type */
    enum class operation_type {
        created,
        updated,
        deleted,
        completed
    };

    /** Type of worklist operation */
    operation_type operation = operation_type::created;

    /** Source HL7 message type that triggered update */
    std::string hl7_message_type;

    /** Source message control ID */
    std::string message_control_id;

    /** Patient ID */
    std::string patient_id;

    /** Patient name */
    std::string patient_name;

    /** Accession Number */
    std::string accession_number;

    /** Scheduled Procedure Step ID */
    std::string scheduled_procedure_step_id;

    /** Scheduled date/time */
    std::optional<std::string> scheduled_datetime;

    /** Modality (CT, MR, etc.) */
    std::string modality;

    /** Scheduled AE Title */
    std::string scheduled_ae_title;

    /** Time taken to update worklist */
    std::chrono::microseconds update_time{0};

    /**
     * @brief Default constructor
     */
    dicom_worklist_updated_event() = default;

    /**
     * @brief Construct with worklist update details
     */
    dicom_worklist_updated_event(operation_type op,
                                  std::string_view pat_id,
                                  std::string_view acc_num,
                                  std::string_view correlation = "");
};

/**
 * @brief Convert operation_type to string
 */
[[nodiscard]] constexpr const char* to_string(
    dicom_worklist_updated_event::operation_type op) noexcept {
    switch (op) {
        case dicom_worklist_updated_event::operation_type::created:
            return "created";
        case dicom_worklist_updated_event::operation_type::updated:
            return "updated";
        case dicom_worklist_updated_event::operation_type::deleted:
            return "deleted";
        case dicom_worklist_updated_event::operation_type::completed:
            return "completed";
        default:
            return "unknown";
    }
}

// =============================================================================
// Error Events
// =============================================================================

/**
 * @brief Event published when an error occurs during HL7 processing
 *
 * Published when any stage of HL7 processing fails.
 */
struct hl7_processing_error_event : hl7_event_base {
    /** Error code */
    int error_code = 0;

    /** Error message */
    std::string error_message;

    /** Stage where error occurred */
    std::string stage;

    /** Message type if known */
    std::optional<std::string> message_type;

    /** Message control ID if known */
    std::optional<std::string> message_control_id;

    /** Connection ID if applicable */
    std::optional<std::string> connection_id;

    /** Is error recoverable */
    bool recoverable = false;

    /** Retry count if retried */
    size_t retry_count = 0;

    /**
     * @brief Default constructor
     */
    hl7_processing_error_event() = default;

    /**
     * @brief Construct with error details
     */
    hl7_processing_error_event(int code,
                                std::string_view message,
                                std::string_view error_stage,
                                std::string_view correlation = "");
};

// =============================================================================
// Event Publisher Utilities
// =============================================================================

/**
 * @brief Convenience functions for publishing HL7 events
 *
 * These functions provide a simplified interface for publishing events
 * to the common_system event bus.
 */
namespace event_publisher {

/**
 * @brief Publish an HL7 message received event
 *
 * @param message_type Message type from MSH-9
 * @param raw_message Raw HL7 data
 * @param connection_id Source connection identifier
 * @param remote_endpoint Remote endpoint address
 */
void publish_message_received(std::string_view message_type,
                               std::string raw_message,
                               std::string_view connection_id = "",
                               std::string_view remote_endpoint = "");

/**
 * @brief Publish an ACK sent event
 *
 * @param original_message_id Original message control ID
 * @param ack_code ACK code (AA, AE, AR, etc.)
 * @param correlation_id Correlation ID for tracking
 * @param success Whether ACK indicates success
 */
void publish_ack_sent(std::string_view original_message_id,
                       std::string_view ack_code,
                       std::string_view correlation_id = "",
                       bool success = true);

/**
 * @brief Publish a message parsed event
 *
 * @param message_type Message type
 * @param control_id Message control ID
 * @param segment_count Number of segments
 * @param parse_time Time taken to parse
 * @param correlation_id Correlation ID for tracking
 */
void publish_message_parsed(std::string_view message_type,
                             std::string_view control_id,
                             size_t segment_count,
                             std::chrono::microseconds parse_time,
                             std::string_view correlation_id = "");

/**
 * @brief Publish a message validated event
 *
 * @param message_type Message type
 * @param control_id Message control ID
 * @param validation_profile Profile used
 * @param warnings List of warnings
 * @param validation_time Time taken to validate
 * @param correlation_id Correlation ID for tracking
 */
void publish_message_validated(std::string_view message_type,
                                std::string_view control_id,
                                std::string_view validation_profile,
                                const std::vector<std::string>& warnings,
                                std::chrono::microseconds validation_time,
                                std::string_view correlation_id = "");

/**
 * @brief Publish a message routed event
 *
 * @param message_type Message type
 * @param control_id Message control ID
 * @param routing_rule Rule that matched
 * @param destinations Target destinations
 * @param correlation_id Correlation ID for tracking
 */
void publish_message_routed(std::string_view message_type,
                             std::string_view control_id,
                             std::string_view routing_rule,
                             const std::vector<std::string>& destinations,
                             std::string_view correlation_id = "");

/**
 * @brief Publish an HL7 to DICOM mapping event
 *
 * @param message_type Original HL7 message type
 * @param control_id Message control ID
 * @param patient_id Patient ID
 * @param accession_number Accession Number
 * @param mapped_attributes Number of attributes mapped
 * @param correlation_id Correlation ID for tracking
 */
void publish_dicom_mapped(std::string_view message_type,
                           std::string_view control_id,
                           std::string_view patient_id,
                           std::string_view accession_number,
                           size_t mapped_attributes,
                           std::string_view correlation_id = "");

/**
 * @brief Publish a worklist update event
 *
 * @param operation Operation type
 * @param patient_id Patient ID
 * @param accession_number Accession Number
 * @param modality Modality type
 * @param correlation_id Correlation ID for tracking
 */
void publish_worklist_updated(
    dicom_worklist_updated_event::operation_type operation,
    std::string_view patient_id,
    std::string_view accession_number,
    std::string_view modality,
    std::string_view correlation_id = "");

/**
 * @brief Publish a processing error event
 *
 * @param error_code Error code
 * @param error_message Error description
 * @param stage Processing stage where error occurred
 * @param correlation_id Correlation ID for tracking
 * @param recoverable Whether error is recoverable
 */
void publish_processing_error(int error_code,
                               std::string_view error_message,
                               std::string_view stage,
                               std::string_view correlation_id = "",
                               bool recoverable = false);

}  // namespace event_publisher

// =============================================================================
// Event Subscriber Utilities
// =============================================================================

/**
 * @brief RAII-style event subscription manager
 *
 * Automatically unsubscribes from events when destroyed.
 */
class event_subscription {
public:
    /**
     * @brief Default constructor (no subscription)
     */
    event_subscription() = default;

    /**
     * @brief Construct with subscription ID
     */
    explicit event_subscription(uint64_t id);

    /**
     * @brief Destructor - unsubscribes if active
     */
    ~event_subscription();

    // Non-copyable
    event_subscription(const event_subscription&) = delete;
    event_subscription& operator=(const event_subscription&) = delete;

    // Movable
    event_subscription(event_subscription&& other) noexcept;
    event_subscription& operator=(event_subscription&& other) noexcept;

    /**
     * @brief Check if subscription is active
     */
    [[nodiscard]] explicit operator bool() const noexcept;

    /**
     * @brief Get subscription ID
     */
    [[nodiscard]] uint64_t id() const noexcept;

    /**
     * @brief Unsubscribe manually
     */
    void unsubscribe();

private:
    uint64_t subscription_id_ = 0;
};

/**
 * @brief Convenience functions for subscribing to HL7 events
 */
namespace event_subscriber {

/**
 * @brief Subscribe to message received events
 *
 * @param handler Callback function
 * @return Subscription handle
 */
[[nodiscard]] event_subscription on_message_received(
    std::function<void(const hl7_message_received_event&)> handler);

/**
 * @brief Subscribe to ACK sent events
 *
 * @param handler Callback function
 * @return Subscription handle
 */
[[nodiscard]] event_subscription on_ack_sent(
    std::function<void(const hl7_ack_sent_event&)> handler);

/**
 * @brief Subscribe to message parsed events
 *
 * @param handler Callback function
 * @return Subscription handle
 */
[[nodiscard]] event_subscription on_message_parsed(
    std::function<void(const hl7_message_parsed_event&)> handler);

/**
 * @brief Subscribe to message validated events
 *
 * @param handler Callback function
 * @return Subscription handle
 */
[[nodiscard]] event_subscription on_message_validated(
    std::function<void(const hl7_message_validated_event&)> handler);

/**
 * @brief Subscribe to message routed events
 *
 * @param handler Callback function
 * @return Subscription handle
 */
[[nodiscard]] event_subscription on_message_routed(
    std::function<void(const hl7_message_routed_event&)> handler);

/**
 * @brief Subscribe to DICOM mapping events
 *
 * @param handler Callback function
 * @return Subscription handle
 */
[[nodiscard]] event_subscription on_dicom_mapped(
    std::function<void(const hl7_to_dicom_mapped_event&)> handler);

/**
 * @brief Subscribe to worklist update events
 *
 * @param handler Callback function
 * @return Subscription handle
 */
[[nodiscard]] event_subscription on_worklist_updated(
    std::function<void(const dicom_worklist_updated_event&)> handler);

/**
 * @brief Subscribe to processing error events
 *
 * @param handler Callback function
 * @return Subscription handle
 */
[[nodiscard]] event_subscription on_processing_error(
    std::function<void(const hl7_processing_error_event&)> handler);

/**
 * @brief Subscribe to all HL7 events for logging/monitoring
 *
 * @param handler Callback function receiving event type name and event ID
 * @return Vector of subscription handles
 */
[[nodiscard]] std::vector<event_subscription> on_all_events(
    std::function<void(std::string_view event_type, std::string_view event_id)> handler);

}  // namespace event_subscriber

}  // namespace pacs::bridge::messaging

#endif  // PACS_BRIDGE_MESSAGING_HL7_EVENTS_H
