/**
 * @file hl7_events_test.cpp
 * @brief Unit tests for HL7 event types and Event Bus integration
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/142
 */

#include <gtest/gtest.h>

#include "pacs/bridge/messaging/hl7_events.h"

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>
#include <vector>

using namespace pacs::bridge::messaging;

// =============================================================================
// Test Fixtures
// =============================================================================

class HL7EventsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any existing subscriptions
    }

    void TearDown() override {
        // Clean up
    }
};

// =============================================================================
// Event Base Tests
// =============================================================================

TEST_F(HL7EventsTest, MessageReceivedEventHasUniqueId) {
    hl7_message_received_event event1{"ADT^A01", "MSH|...", "conn1"};
    hl7_message_received_event event2{"ADT^A01", "MSH|...", "conn2"};

    EXPECT_FALSE(event1.event_id.empty());
    EXPECT_FALSE(event2.event_id.empty());
    EXPECT_NE(event1.event_id, event2.event_id);
}

TEST_F(HL7EventsTest, MessageReceivedEventHasTimestamp) {
    auto before = std::chrono::steady_clock::now();
    hl7_message_received_event event{"ADT^A01", "MSH|..."};
    auto after = std::chrono::steady_clock::now();

    EXPECT_GE(event.timestamp, before);
    EXPECT_LE(event.timestamp, after);
}

TEST_F(HL7EventsTest, MessageReceivedEventStoresData) {
    std::string raw_data = "MSH|^~\\&|APP|FAC|PACS|RAD|...";
    hl7_message_received_event event{"ADT^A01", raw_data, "conn123", "192.168.1.100:5000"};

    EXPECT_EQ(event.message_type, "ADT^A01");
    EXPECT_EQ(event.raw_message, raw_data);
    EXPECT_EQ(event.connection_id, "conn123");
    EXPECT_EQ(event.remote_endpoint, "192.168.1.100:5000");
    EXPECT_EQ(event.message_size, raw_data.size());
}

// =============================================================================
// ACK Sent Event Tests
// =============================================================================

TEST_F(HL7EventsTest, AckSentEventStoresData) {
    hl7_ack_sent_event event{"MSG001", "AA", "corr123", true};

    EXPECT_EQ(event.original_message_control_id, "MSG001");
    EXPECT_EQ(event.ack_code, "AA");
    EXPECT_EQ(event.correlation_id, "corr123");
    EXPECT_TRUE(event.success);
}

TEST_F(HL7EventsTest, AckSentEventFailure) {
    hl7_ack_sent_event event{"MSG002", "AE", "corr456", false};

    EXPECT_EQ(event.ack_code, "AE");
    EXPECT_FALSE(event.success);
}

// =============================================================================
// Processing Event Tests
// =============================================================================

TEST_F(HL7EventsTest, MessageParsedEventStoresData) {
    hl7_message_parsed_event event{"ADT^A01", "MSG003", "corr789"};
    event.segment_count = 5;
    event.segment_names = {"MSH", "EVN", "PID", "PV1", "OBX"};
    event.parse_time = std::chrono::microseconds{150};

    EXPECT_EQ(event.message_type, "ADT^A01");
    EXPECT_EQ(event.message_control_id, "MSG003");
    EXPECT_EQ(event.correlation_id, "corr789");
    EXPECT_EQ(event.segment_count, 5);
    EXPECT_EQ(event.segment_names.size(), 5);
    EXPECT_EQ(event.parse_time.count(), 150);
}

TEST_F(HL7EventsTest, MessageValidatedEventStoresWarnings) {
    hl7_message_validated_event event{"ORM^O01", "MSG004", "strict", "corr001"};
    event.warnings = {"Field PID.5 truncated", "Optional segment OBX missing"};
    event.validation_time = std::chrono::microseconds{75};

    EXPECT_EQ(event.validation_profile, "strict");
    EXPECT_EQ(event.warnings.size(), 2);
    EXPECT_EQ(event.warnings[0], "Field PID.5 truncated");
}

TEST_F(HL7EventsTest, MessageRoutedEventStoresDestinations) {
    hl7_message_routed_event event{"ADT^A01", "MSG005", "rule_adt_all", "corr002"};
    event.destinations = {"PACS", "RIS", "ARCHIVE"};
    event.priority = 10;

    EXPECT_EQ(event.routing_rule, "rule_adt_all");
    EXPECT_EQ(event.destinations.size(), 3);
    EXPECT_EQ(event.priority, 10);
}

// =============================================================================
// Transformation Event Tests
// =============================================================================

TEST_F(HL7EventsTest, DicomMappedEventStoresData) {
    hl7_to_dicom_mapped_event event{"ORM^O01", "MSG006", "PAT001", "corr003"};
    event.accession_number = "ACC123456";
    event.sop_class_uid = "1.2.840.10008.5.1.4.32.1";
    event.study_instance_uid = "1.2.3.4.5.6.7.8.9";
    event.mapped_attributes = 42;
    event.mapping_profile = "default_orm";
    event.mapping_time = std::chrono::microseconds{500};

    EXPECT_EQ(event.hl7_message_type, "ORM^O01");
    EXPECT_EQ(event.patient_id, "PAT001");
    EXPECT_EQ(event.accession_number, "ACC123456");
    EXPECT_TRUE(event.study_instance_uid.has_value());
    EXPECT_EQ(*event.study_instance_uid, "1.2.3.4.5.6.7.8.9");
    EXPECT_EQ(event.mapped_attributes, 42);
}

TEST_F(HL7EventsTest, WorklistUpdatedEventOperations) {
    using op = dicom_worklist_updated_event::operation_type;

    dicom_worklist_updated_event created_event{op::created, "PAT001", "ACC001", "corr001"};
    dicom_worklist_updated_event updated_event{op::updated, "PAT001", "ACC001", "corr002"};
    dicom_worklist_updated_event deleted_event{op::deleted, "PAT001", "ACC001", "corr003"};
    dicom_worklist_updated_event completed_event{op::completed, "PAT001", "ACC001", "corr004"};

    EXPECT_EQ(created_event.operation, op::created);
    EXPECT_EQ(updated_event.operation, op::updated);
    EXPECT_EQ(deleted_event.operation, op::deleted);
    EXPECT_EQ(completed_event.operation, op::completed);

    EXPECT_STREQ(to_string(op::created), "created");
    EXPECT_STREQ(to_string(op::updated), "updated");
    EXPECT_STREQ(to_string(op::deleted), "deleted");
    EXPECT_STREQ(to_string(op::completed), "completed");
}

TEST_F(HL7EventsTest, WorklistUpdatedEventStoresScheduling) {
    using op = dicom_worklist_updated_event::operation_type;

    dicom_worklist_updated_event event{op::created, "PAT002", "ACC002", "corr005"};
    event.patient_name = "DOE^JOHN";
    event.scheduled_procedure_step_id = "SPS001";
    event.scheduled_datetime = "20250101120000";
    event.modality = "CT";
    event.scheduled_ae_title = "CT_SCANNER_1";

    EXPECT_EQ(event.patient_name, "DOE^JOHN");
    EXPECT_EQ(event.modality, "CT");
    EXPECT_TRUE(event.scheduled_datetime.has_value());
    EXPECT_EQ(*event.scheduled_datetime, "20250101120000");
}

// =============================================================================
// Error Event Tests
// =============================================================================

TEST_F(HL7EventsTest, ProcessingErrorEventStoresData) {
    hl7_processing_error_event event{-100, "Parse failed: Invalid segment", "parse", "corr006"};
    event.message_type = "ADT^A01";
    event.message_control_id = "MSG007";
    event.connection_id = "conn789";
    event.recoverable = true;
    event.retry_count = 2;

    EXPECT_EQ(event.error_code, -100);
    EXPECT_EQ(event.error_message, "Parse failed: Invalid segment");
    EXPECT_EQ(event.stage, "parse");
    EXPECT_TRUE(event.message_type.has_value());
    EXPECT_EQ(*event.message_type, "ADT^A01");
    EXPECT_TRUE(event.recoverable);
    EXPECT_EQ(event.retry_count, 2);
}

// =============================================================================
// Event Subscription Tests
// =============================================================================

TEST_F(HL7EventsTest, EventSubscriptionRAII) {
    // Subscription should auto-unsubscribe on destruction
    {
        auto sub = event_subscriber::on_message_received(
            [](const hl7_message_received_event&) {
                // Handler
            });
        EXPECT_TRUE(static_cast<bool>(sub));
        EXPECT_NE(sub.id(), 0);
    }
    // Subscription automatically unsubscribed here
}

TEST_F(HL7EventsTest, EventSubscriptionMove) {
    auto sub1 = event_subscriber::on_ack_sent(
        [](const hl7_ack_sent_event&) {});

    auto id = sub1.id();
    EXPECT_NE(id, 0);

    auto sub2 = std::move(sub1);
    EXPECT_EQ(sub2.id(), id);
    EXPECT_EQ(sub1.id(), 0);  // NOLINT: testing moved-from state
    EXPECT_FALSE(static_cast<bool>(sub1));  // NOLINT
    EXPECT_TRUE(static_cast<bool>(sub2));
}

TEST_F(HL7EventsTest, EventSubscriptionManualUnsubscribe) {
    auto sub = event_subscriber::on_message_parsed(
        [](const hl7_message_parsed_event&) {});

    EXPECT_TRUE(static_cast<bool>(sub));
    sub.unsubscribe();
    EXPECT_FALSE(static_cast<bool>(sub));
    EXPECT_EQ(sub.id(), 0);
}

// =============================================================================
// Event Publishing Tests
// =============================================================================

TEST_F(HL7EventsTest, PublishAndReceiveMessageReceived) {
    std::atomic<int> received_count{0};
    std::string received_type;

    auto sub = event_subscriber::on_message_received(
        [&](const hl7_message_received_event& event) {
            received_count++;
            received_type = event.message_type;
        });

    event_publisher::publish_message_received("ADT^A08", "MSH|...", "conn1", "127.0.0.1:5000");

    EXPECT_EQ(received_count, 1);
    EXPECT_EQ(received_type, "ADT^A08");
}

TEST_F(HL7EventsTest, PublishAndReceiveAckSent) {
    std::atomic<int> received_count{0};
    std::string received_code;

    auto sub = event_subscriber::on_ack_sent(
        [&](const hl7_ack_sent_event& event) {
            received_count++;
            received_code = event.ack_code;
        });

    event_publisher::publish_ack_sent("MSG001", "AA", "corr001", true);

    EXPECT_EQ(received_count, 1);
    EXPECT_EQ(received_code, "AA");
}

TEST_F(HL7EventsTest, PublishAndReceiveMessageParsed) {
    std::atomic<int> received_count{0};
    size_t received_segment_count{0};

    auto sub = event_subscriber::on_message_parsed(
        [&](const hl7_message_parsed_event& event) {
            received_count++;
            received_segment_count = event.segment_count;
        });

    event_publisher::publish_message_parsed("ADT^A01", "MSG002", 7,
        std::chrono::microseconds{200}, "corr002");

    EXPECT_EQ(received_count, 1);
    EXPECT_EQ(received_segment_count, 7);
}

TEST_F(HL7EventsTest, PublishAndReceiveMessageValidated) {
    std::atomic<int> received_count{0};
    std::vector<std::string> received_warnings;

    auto sub = event_subscriber::on_message_validated(
        [&](const hl7_message_validated_event& event) {
            received_count++;
            received_warnings = event.warnings;
        });

    std::vector<std::string> warnings = {"Warning 1", "Warning 2"};
    event_publisher::publish_message_validated("ORM^O01", "MSG003", "strict",
        warnings, std::chrono::microseconds{100}, "corr003");

    EXPECT_EQ(received_count, 1);
    EXPECT_EQ(received_warnings.size(), 2);
}

TEST_F(HL7EventsTest, PublishAndReceiveMessageRouted) {
    std::atomic<int> received_count{0};
    std::vector<std::string> received_destinations;

    auto sub = event_subscriber::on_message_routed(
        [&](const hl7_message_routed_event& event) {
            received_count++;
            received_destinations = event.destinations;
        });

    std::vector<std::string> destinations = {"PACS", "RIS"};
    event_publisher::publish_message_routed("ADT^A01", "MSG004", "rule1",
        destinations, "corr004");

    EXPECT_EQ(received_count, 1);
    EXPECT_EQ(received_destinations.size(), 2);
}

TEST_F(HL7EventsTest, PublishAndReceiveDicomMapped) {
    std::atomic<int> received_count{0};
    size_t received_attr_count{0};

    auto sub = event_subscriber::on_dicom_mapped(
        [&](const hl7_to_dicom_mapped_event& event) {
            received_count++;
            received_attr_count = event.mapped_attributes;
        });

    event_publisher::publish_dicom_mapped("ORM^O01", "MSG005", "PAT001",
        "ACC001", 35, "corr005");

    EXPECT_EQ(received_count, 1);
    EXPECT_EQ(received_attr_count, 35);
}

TEST_F(HL7EventsTest, PublishAndReceiveWorklistUpdated) {
    std::atomic<int> received_count{0};
    dicom_worklist_updated_event::operation_type received_op{};

    auto sub = event_subscriber::on_worklist_updated(
        [&](const dicom_worklist_updated_event& event) {
            received_count++;
            received_op = event.operation;
        });

    event_publisher::publish_worklist_updated(
        dicom_worklist_updated_event::operation_type::created,
        "PAT001", "ACC001", "CT", "corr006");

    EXPECT_EQ(received_count, 1);
    EXPECT_EQ(received_op, dicom_worklist_updated_event::operation_type::created);
}

TEST_F(HL7EventsTest, PublishAndReceiveProcessingError) {
    std::atomic<int> received_count{0};
    int received_error_code{0};

    auto sub = event_subscriber::on_processing_error(
        [&](const hl7_processing_error_event& event) {
            received_count++;
            received_error_code = event.error_code;
        });

    event_publisher::publish_processing_error(-500, "Connection timeout",
        "send", "corr007", true);

    EXPECT_EQ(received_count, 1);
    EXPECT_EQ(received_error_code, -500);
}

// =============================================================================
// Multiple Subscriber Tests
// =============================================================================

TEST_F(HL7EventsTest, MultipleSubscribersReceiveEvent) {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};
    std::atomic<int> count3{0};

    auto sub1 = event_subscriber::on_message_received(
        [&](const hl7_message_received_event&) { count1++; });
    auto sub2 = event_subscriber::on_message_received(
        [&](const hl7_message_received_event&) { count2++; });
    auto sub3 = event_subscriber::on_message_received(
        [&](const hl7_message_received_event&) { count3++; });

    event_publisher::publish_message_received("ADT^A01", "MSH|...");

    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);
    EXPECT_EQ(count3, 1);
}

TEST_F(HL7EventsTest, OnAllEventsReceivesMultipleTypes) {
    std::atomic<int> event_count{0};
    std::vector<std::string> event_types;
    std::mutex types_mutex;

    auto subscriptions = event_subscriber::on_all_events(
        [&](std::string_view event_type, std::string_view) {
            std::lock_guard<std::mutex> lock(types_mutex);
            event_count++;
            event_types.emplace_back(event_type);
        });

    // Should have subscriptions for all event types
    EXPECT_EQ(subscriptions.size(), 8);

    // Publish different event types
    event_publisher::publish_message_received("ADT^A01", "MSH|...");
    event_publisher::publish_ack_sent("MSG001", "AA");
    event_publisher::publish_message_parsed("ADT^A01", "MSG002", 5,
        std::chrono::microseconds{100});

    EXPECT_EQ(event_count, 3);
    EXPECT_EQ(event_types.size(), 3);
}

// =============================================================================
// Correlation ID Propagation Tests
// =============================================================================

TEST_F(HL7EventsTest, CorrelationIdPropagation) {
    const std::string correlation_id = "test-correlation-12345";
    std::string received_correlation;

    auto sub = event_subscriber::on_message_parsed(
        [&](const hl7_message_parsed_event& event) {
            received_correlation = event.correlation_id;
        });

    event_publisher::publish_message_parsed("ADT^A01", "MSG001", 3,
        std::chrono::microseconds{50}, correlation_id);

    EXPECT_EQ(received_correlation, correlation_id);
}

// =============================================================================
// Performance Tests
// =============================================================================

TEST_F(HL7EventsTest, HighVolumeEventPublishing) {
    const int event_count = 1000;
    std::atomic<int> received_count{0};

    auto sub = event_subscriber::on_message_received(
        [&](const hl7_message_received_event&) {
            received_count++;
        });

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < event_count; ++i) {
        event_publisher::publish_message_received("ADT^A01", "MSH|...");
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(received_count, event_count);

    // Should complete in reasonable time (< 1 second for 1000 events)
    EXPECT_LT(duration.count(), 1000);
}
