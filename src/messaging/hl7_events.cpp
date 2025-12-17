/**
 * @file hl7_events.cpp
 * @brief Implementation of HL7 event types and utilities
 *
 * @see include/pacs/bridge/messaging/hl7_events.h
 * @see https://github.com/kcenon/pacs_bridge/issues/142
 */

#include "pacs/bridge/messaging/hl7_events.h"

#include <kcenon/common/patterns/event_bus.h>

#include <atomic>
#include <random>
#include <sstream>
#include <iomanip>

namespace pacs::bridge::messaging {

// =============================================================================
// UUID Generation
// =============================================================================

namespace {

/**
 * @brief Generate a simple UUID-like identifier
 *
 * Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 */
[[nodiscard]] std::string generate_event_id() {
    static std::atomic<uint64_t> counter{0};
    static const auto start_time = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - start_time).count();

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << (elapsed & 0xFFFFFFFF) << "-";
    oss << std::setw(4) << ((elapsed >> 32) & 0xFFFF) << "-";
    oss << "4" << std::setw(3) << (counter.fetch_add(1) & 0xFFF) << "-";
    oss << std::setw(4) << (0x8000 | (elapsed & 0x3FFF)) << "-";
    oss << std::setw(12) << (counter.load() ^ elapsed);

    return oss.str();
}

}  // namespace

// =============================================================================
// Event Base Implementation
// =============================================================================

hl7_event_base::hl7_event_base()
    : event_id(generate_event_id())
    , timestamp(std::chrono::steady_clock::now())
    , source("pacs_bridge") {
}

hl7_event_base::hl7_event_base(std::string_view corr_id)
    : event_id(generate_event_id())
    , correlation_id(corr_id)
    , timestamp(std::chrono::steady_clock::now())
    , source("pacs_bridge") {
}

// =============================================================================
// Receive Events Implementation
// =============================================================================

hl7_message_received_event::hl7_message_received_event(
    std::string_view msg_type,
    std::string raw_data,
    std::string_view conn_id,
    std::string_view endpoint)
    : hl7_event_base()
    , message_type(msg_type)
    , raw_message(std::move(raw_data))
    , connection_id(conn_id)
    , remote_endpoint(endpoint)
    , message_size(raw_message.size()) {
}

hl7_ack_sent_event::hl7_ack_sent_event(
    std::string_view original_msg_id,
    std::string_view code,
    std::string_view correlation,
    bool is_success)
    : hl7_event_base(correlation)
    , original_message_control_id(original_msg_id)
    , ack_code(code)
    , success(is_success) {
}

// =============================================================================
// Processing Events Implementation
// =============================================================================

hl7_message_parsed_event::hl7_message_parsed_event(
    std::string_view msg_type,
    std::string_view control_id,
    std::string_view correlation)
    : hl7_event_base(correlation)
    , message_type(msg_type)
    , message_control_id(control_id) {
}

hl7_message_validated_event::hl7_message_validated_event(
    std::string_view msg_type,
    std::string_view control_id,
    std::string_view profile,
    std::string_view correlation)
    : hl7_event_base(correlation)
    , message_type(msg_type)
    , message_control_id(control_id)
    , validation_profile(profile) {
}

hl7_message_routed_event::hl7_message_routed_event(
    std::string_view msg_type,
    std::string_view control_id,
    std::string_view rule,
    std::string_view correlation)
    : hl7_event_base(correlation)
    , message_type(msg_type)
    , message_control_id(control_id)
    , routing_rule(rule) {
}

// =============================================================================
// Transformation Events Implementation
// =============================================================================

hl7_to_dicom_mapped_event::hl7_to_dicom_mapped_event(
    std::string_view msg_type,
    std::string_view control_id,
    std::string_view pat_id,
    std::string_view correlation)
    : hl7_event_base(correlation)
    , hl7_message_type(msg_type)
    , message_control_id(control_id)
    , patient_id(pat_id) {
}

dicom_worklist_updated_event::dicom_worklist_updated_event(
    operation_type op,
    std::string_view pat_id,
    std::string_view acc_num,
    std::string_view correlation)
    : hl7_event_base(correlation)
    , operation(op)
    , patient_id(pat_id)
    , accession_number(acc_num) {
}

// =============================================================================
// Error Events Implementation
// =============================================================================

hl7_processing_error_event::hl7_processing_error_event(
    int code,
    std::string_view message,
    std::string_view error_stage,
    std::string_view correlation)
    : hl7_event_base(correlation)
    , error_code(code)
    , error_message(message)
    , stage(error_stage) {
}

// =============================================================================
// Event Publisher Implementation
// =============================================================================

namespace event_publisher {

void publish_message_received(std::string_view message_type,
                               std::string raw_message,
                               std::string_view connection_id,
                               std::string_view remote_endpoint) {
    auto& bus = kcenon::common::get_event_bus();
    bus.publish(hl7_message_received_event{
        message_type,
        std::move(raw_message),
        connection_id,
        remote_endpoint
    });
}

void publish_ack_sent(std::string_view original_message_id,
                       std::string_view ack_code,
                       std::string_view correlation_id,
                       bool success) {
    auto& bus = kcenon::common::get_event_bus();
    bus.publish(hl7_ack_sent_event{
        original_message_id,
        ack_code,
        correlation_id,
        success
    });
}

void publish_message_parsed(std::string_view message_type,
                             std::string_view control_id,
                             size_t segment_count,
                             std::chrono::microseconds parse_time,
                             std::string_view correlation_id) {
    auto event = hl7_message_parsed_event{
        message_type,
        control_id,
        correlation_id
    };
    event.segment_count = segment_count;
    event.parse_time = parse_time;

    auto& bus = kcenon::common::get_event_bus();
    bus.publish(std::move(event));
}

void publish_message_validated(std::string_view message_type,
                                std::string_view control_id,
                                std::string_view validation_profile,
                                const std::vector<std::string>& warnings,
                                std::chrono::microseconds validation_time,
                                std::string_view correlation_id) {
    auto event = hl7_message_validated_event{
        message_type,
        control_id,
        validation_profile,
        correlation_id
    };
    event.warnings = warnings;
    event.validation_time = validation_time;

    auto& bus = kcenon::common::get_event_bus();
    bus.publish(std::move(event));
}

void publish_message_routed(std::string_view message_type,
                             std::string_view control_id,
                             std::string_view routing_rule,
                             const std::vector<std::string>& destinations,
                             std::string_view correlation_id) {
    auto event = hl7_message_routed_event{
        message_type,
        control_id,
        routing_rule,
        correlation_id
    };
    event.destinations = destinations;

    auto& bus = kcenon::common::get_event_bus();
    bus.publish(std::move(event));
}

void publish_dicom_mapped(std::string_view message_type,
                           std::string_view control_id,
                           std::string_view patient_id,
                           std::string_view accession_number,
                           size_t mapped_attributes,
                           std::string_view correlation_id) {
    auto event = hl7_to_dicom_mapped_event{
        message_type,
        control_id,
        patient_id,
        correlation_id
    };
    event.accession_number = accession_number;
    event.mapped_attributes = mapped_attributes;

    auto& bus = kcenon::common::get_event_bus();
    bus.publish(std::move(event));
}

void publish_worklist_updated(
    dicom_worklist_updated_event::operation_type operation,
    std::string_view patient_id,
    std::string_view accession_number,
    std::string_view modality,
    std::string_view correlation_id) {
    auto event = dicom_worklist_updated_event{
        operation,
        patient_id,
        accession_number,
        correlation_id
    };
    event.modality = modality;

    auto& bus = kcenon::common::get_event_bus();
    bus.publish(std::move(event));
}

void publish_processing_error(int error_code,
                               std::string_view error_message,
                               std::string_view stage,
                               std::string_view correlation_id,
                               bool recoverable) {
    auto event = hl7_processing_error_event{
        error_code,
        error_message,
        stage,
        correlation_id
    };
    event.recoverable = recoverable;

    auto& bus = kcenon::common::get_event_bus();
    bus.publish(std::move(event));
}

}  // namespace event_publisher

// =============================================================================
// Event Subscription Implementation
// =============================================================================

event_subscription::event_subscription(uint64_t id)
    : subscription_id_(id) {
}

event_subscription::~event_subscription() {
    unsubscribe();
}

event_subscription::event_subscription(event_subscription&& other) noexcept
    : subscription_id_(other.subscription_id_) {
    other.subscription_id_ = 0;
}

event_subscription& event_subscription::operator=(event_subscription&& other) noexcept {
    if (this != &other) {
        unsubscribe();
        subscription_id_ = other.subscription_id_;
        other.subscription_id_ = 0;
    }
    return *this;
}

event_subscription::operator bool() const noexcept {
    return subscription_id_ != 0;
}

uint64_t event_subscription::id() const noexcept {
    return subscription_id_;
}

void event_subscription::unsubscribe() {
    if (subscription_id_ != 0) {
        auto& bus = kcenon::common::get_event_bus();
        bus.unsubscribe(subscription_id_);
        subscription_id_ = 0;
    }
}

// =============================================================================
// Event Subscriber Implementation
// =============================================================================

namespace event_subscriber {

event_subscription on_message_received(
    std::function<void(const hl7_message_received_event&)> handler) {
    auto& bus = kcenon::common::get_event_bus();
    auto id = bus.subscribe<hl7_message_received_event>(std::move(handler));
    return event_subscription{id};
}

event_subscription on_ack_sent(
    std::function<void(const hl7_ack_sent_event&)> handler) {
    auto& bus = kcenon::common::get_event_bus();
    auto id = bus.subscribe<hl7_ack_sent_event>(std::move(handler));
    return event_subscription{id};
}

event_subscription on_message_parsed(
    std::function<void(const hl7_message_parsed_event&)> handler) {
    auto& bus = kcenon::common::get_event_bus();
    auto id = bus.subscribe<hl7_message_parsed_event>(std::move(handler));
    return event_subscription{id};
}

event_subscription on_message_validated(
    std::function<void(const hl7_message_validated_event&)> handler) {
    auto& bus = kcenon::common::get_event_bus();
    auto id = bus.subscribe<hl7_message_validated_event>(std::move(handler));
    return event_subscription{id};
}

event_subscription on_message_routed(
    std::function<void(const hl7_message_routed_event&)> handler) {
    auto& bus = kcenon::common::get_event_bus();
    auto id = bus.subscribe<hl7_message_routed_event>(std::move(handler));
    return event_subscription{id};
}

event_subscription on_dicom_mapped(
    std::function<void(const hl7_to_dicom_mapped_event&)> handler) {
    auto& bus = kcenon::common::get_event_bus();
    auto id = bus.subscribe<hl7_to_dicom_mapped_event>(std::move(handler));
    return event_subscription{id};
}

event_subscription on_worklist_updated(
    std::function<void(const dicom_worklist_updated_event&)> handler) {
    auto& bus = kcenon::common::get_event_bus();
    auto id = bus.subscribe<dicom_worklist_updated_event>(std::move(handler));
    return event_subscription{id};
}

event_subscription on_processing_error(
    std::function<void(const hl7_processing_error_event&)> handler) {
    auto& bus = kcenon::common::get_event_bus();
    auto id = bus.subscribe<hl7_processing_error_event>(std::move(handler));
    return event_subscription{id};
}

std::vector<event_subscription> on_all_events(
    std::function<void(std::string_view event_type, std::string_view event_id)> handler) {
    std::vector<event_subscription> subscriptions;
    subscriptions.reserve(8);

    // Subscribe to all event types
    subscriptions.push_back(on_message_received(
        [h = handler](const hl7_message_received_event& e) {
            h("hl7_message_received", e.event_id);
        }));

    subscriptions.push_back(on_ack_sent(
        [h = handler](const hl7_ack_sent_event& e) {
            h("hl7_ack_sent", e.event_id);
        }));

    subscriptions.push_back(on_message_parsed(
        [h = handler](const hl7_message_parsed_event& e) {
            h("hl7_message_parsed", e.event_id);
        }));

    subscriptions.push_back(on_message_validated(
        [h = handler](const hl7_message_validated_event& e) {
            h("hl7_message_validated", e.event_id);
        }));

    subscriptions.push_back(on_message_routed(
        [h = handler](const hl7_message_routed_event& e) {
            h("hl7_message_routed", e.event_id);
        }));

    subscriptions.push_back(on_dicom_mapped(
        [h = handler](const hl7_to_dicom_mapped_event& e) {
            h("hl7_to_dicom_mapped", e.event_id);
        }));

    subscriptions.push_back(on_worklist_updated(
        [h = handler](const dicom_worklist_updated_event& e) {
            h("dicom_worklist_updated", e.event_id);
        }));

    subscriptions.push_back(on_processing_error(
        [h = handler](const hl7_processing_error_event& e) {
            h("hl7_processing_error", e.event_id);
        }));

    return subscriptions;
}

}  // namespace event_subscriber

}  // namespace pacs::bridge::messaging
