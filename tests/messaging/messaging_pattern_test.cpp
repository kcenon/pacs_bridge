/**
 * @file messaging_pattern_test.cpp
 * @brief Unit tests for messaging pattern integration
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 */

#include <gtest/gtest.h>

#include "pacs/bridge/messaging/hl7_message_bus.h"
#include "pacs/bridge/messaging/hl7_pipeline.h"
#include "pacs/bridge/messaging/hl7_request_handler.h"
#include "pacs/bridge/messaging/messaging_backend.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>

using namespace pacs::bridge::messaging;
using namespace pacs::bridge::hl7;

// Alias for messaging::ack_code to avoid ambiguity with hl7::ack_code
using messaging_ack_code = pacs::bridge::messaging::ack_code;

// =============================================================================
// Test Fixtures
// =============================================================================

class MessagingPatternTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a test HL7 message
        auto result = hl7_builder::create()
            .message_type("ADT", "A01")
            .sending_app("TEST_APP")
            .sending_facility("TEST_FAC")
            .receiving_app("PACS")
            .receiving_facility("RAD")
            .patient_id("12345")
            .patient_name("DOE", "JOHN")
            .build();
        ASSERT_TRUE(result.has_value()) << "Failed to create test message";
        test_message_ = std::move(*result);
    }

    hl7_message test_message_;
};

// =============================================================================
// Topic Utility Tests
// =============================================================================

TEST_F(MessagingPatternTest, BuildTopicFromTypeAndTrigger) {
    auto topic = topics::build_topic("ADT", "A01");
    EXPECT_EQ(topic, "hl7.adt.a01");
}

TEST_F(MessagingPatternTest, BuildTopicFromMessage) {
    auto topic = topics::build_topic(test_message_);
    EXPECT_EQ(topic, "hl7.adt.a01");
}

TEST_F(MessagingPatternTest, BuildTopicLowercase) {
    auto topic = topics::build_topic("ORM", "O01");
    EXPECT_EQ(topic, "hl7.orm.o01");
}

// =============================================================================
// Message Bus Configuration Tests
// =============================================================================

TEST_F(MessagingPatternTest, DefaultConfig) {
    auto config = hl7_message_bus_config::defaults();
    EXPECT_EQ(config.worker_threads, 0);
    EXPECT_EQ(config.queue_capacity, 10000);
    EXPECT_FALSE(config.enable_persistence);
    EXPECT_TRUE(config.enable_dead_letter_queue);
}

TEST_F(MessagingPatternTest, HighThroughputConfig) {
    auto config = hl7_message_bus_config::high_throughput();
    EXPECT_EQ(config.worker_threads, 4);
    EXPECT_EQ(config.queue_capacity, 50000);
    EXPECT_FALSE(config.enable_statistics);
}

// =============================================================================
// Message Bus Lifecycle Tests
// =============================================================================

TEST_F(MessagingPatternTest, MessageBusStartStop) {
    hl7_message_bus bus;

    EXPECT_FALSE(bus.is_running());

    auto start_result = bus.start();
    EXPECT_TRUE(start_result.has_value());
    EXPECT_TRUE(bus.is_running());

    bus.stop();
    EXPECT_FALSE(bus.is_running());
}

TEST_F(MessagingPatternTest, MessageBusDoubleStart) {
    hl7_message_bus bus;

    auto first_start = bus.start();
    EXPECT_TRUE(first_start.has_value());

    auto second_start = bus.start();
    EXPECT_FALSE(second_start.has_value());
    EXPECT_EQ(second_start.error(), message_bus_error::already_started);

    bus.stop();
}

// =============================================================================
// Pub/Sub Pattern Tests
// =============================================================================

TEST_F(MessagingPatternTest, PublishWithoutStart) {
    hl7_message_bus bus;

    auto result = bus.publish(test_message_);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), message_bus_error::not_started);
}

TEST_F(MessagingPatternTest, SubscribeAndPublish) {
    hl7_message_bus bus;
    bus.start();

    std::atomic<int> received_count{0};
    std::latch message_received{1};

    auto sub_result = bus.subscribe(topics::HL7_ADT_ALL,
        [&](const hl7_message& msg) {
            received_count++;
            message_received.count_down();
            return subscription_result::ok();
        });

    EXPECT_TRUE(sub_result.has_value());
    EXPECT_EQ(bus.subscription_count(), 1);

    auto pub_result = bus.publish(test_message_);
    EXPECT_TRUE(pub_result.has_value());

    // Wait for message delivery
    message_received.wait();

    EXPECT_EQ(received_count.load(), 1);

    bus.stop();
}

TEST_F(MessagingPatternTest, SubscribeToSpecificEvent) {
    hl7_message_bus bus;
    bus.start();

    std::atomic<int> a01_count{0};
    std::atomic<int> a04_count{0};

    bus.subscribe_to_event("ADT", "A01",
        [&](const hl7_message&) {
            a01_count++;
            return subscription_result::ok();
        });

    bus.subscribe_to_event("ADT", "A04",
        [&](const hl7_message&) {
            a04_count++;
            return subscription_result::ok();
        });

    // Publish A01 message
    bus.publish(test_message_);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(a01_count.load(), 1);
    EXPECT_EQ(a04_count.load(), 0);

    bus.stop();
}

TEST_F(MessagingPatternTest, FilteredSubscription) {
    hl7_message_bus bus;
    bus.start();

    std::atomic<int> filtered_count{0};

    bus.subscribe(topics::HL7_ADT_ALL,
        [&](const hl7_message&) {
            filtered_count++;
            return subscription_result::ok();
        },
        [](const hl7_message& msg) {
            // Only accept messages with patient ID starting with "1"
            auto pid = msg.get_value("PID.3");
            return !pid.empty() && pid.starts_with("1");
        });

    // Should be received (patient ID is "12345")
    bus.publish(test_message_);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(filtered_count.load(), 1);

    bus.stop();
}

TEST_F(MessagingPatternTest, Unsubscribe) {
    hl7_message_bus bus;
    bus.start();

    std::atomic<int> count{0};

    auto sub_result = bus.subscribe(topics::HL7_ADT_ALL,
        [&](const hl7_message&) {
            count++;
            return subscription_result::ok();
        });

    EXPECT_EQ(bus.subscription_count(), 1);

    auto unsub_result = bus.unsubscribe(*sub_result);
    EXPECT_TRUE(unsub_result.has_value());
    EXPECT_EQ(bus.subscription_count(), 0);

    bus.stop();
}

// =============================================================================
// HL7 Publisher/Subscriber Wrapper Tests
// =============================================================================

TEST_F(MessagingPatternTest, HL7PublisherWrapper) {
    auto bus = std::make_shared<hl7_message_bus>();
    bus->start();

    hl7_publisher publisher(bus);
    EXPECT_TRUE(publisher.is_ready());

    publisher.set_default_priority(message_priority::high);

    auto result = publisher.publish(test_message_);
    EXPECT_TRUE(result.has_value());

    bus->stop();
}

TEST_F(MessagingPatternTest, HL7SubscriberWrapper) {
    auto bus = std::make_shared<hl7_message_bus>();
    bus->start();

    hl7_subscriber subscriber(bus);
    EXPECT_EQ(subscriber.subscription_count(), 0);

    auto adt_result = subscriber.on_adt(
        [](const hl7_message&) { return subscription_result::ok(); });
    EXPECT_TRUE(adt_result.has_value());

    auto orm_result = subscriber.on_orm(
        [](const hl7_message&) { return subscription_result::ok(); });
    EXPECT_TRUE(orm_result.has_value());

    EXPECT_EQ(subscriber.subscription_count(), 2);

    subscriber.unsubscribe_all();
    EXPECT_EQ(subscriber.subscription_count(), 0);

    bus->stop();
}

// =============================================================================
// Statistics Tests
// =============================================================================

TEST_F(MessagingPatternTest, MessageBusStatistics) {
    hl7_message_bus bus;
    bus.start();

    bus.subscribe(topics::HL7_ALL,
        [](const hl7_message&) { return subscription_result::ok(); });

    bus.publish(test_message_);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto stats = bus.get_statistics();
    EXPECT_GE(stats.messages_published, 1);
    EXPECT_EQ(stats.active_subscriptions, 1);

    bus.reset_statistics();
    stats = bus.get_statistics();
    EXPECT_EQ(stats.messages_published, 0);

    bus.stop();
}

// =============================================================================
// Pipeline Tests
// =============================================================================

TEST_F(MessagingPatternTest, PipelineAddStage) {
    hl7_pipeline pipeline;

    auto result = pipeline.add_stage("validate", "Validate",
        [](const hl7_message&) { return stage_result::ok(); });

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(pipeline.stage_count(), 1);
}

TEST_F(MessagingPatternTest, PipelineProcess) {
    hl7_pipeline pipeline;

    std::atomic<int> stage1_count{0};
    std::atomic<int> stage2_count{0};

    pipeline.add_stage("stage1", "Stage 1",
        [&](const hl7_message&) {
            stage1_count++;
            return stage_result::ok();
        });

    pipeline.add_stage("stage2", "Stage 2",
        [&](const hl7_message&) {
            stage2_count++;
            return stage_result::ok();
        });

    auto result = pipeline.process(test_message_);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(stage1_count.load(), 1);
    EXPECT_EQ(stage2_count.load(), 1);
}

TEST_F(MessagingPatternTest, PipelineStageFailure) {
    hl7_pipeline pipeline;

    pipeline.add_stage("fail", "Failing Stage",
        [](const hl7_message&) {
            return stage_result::error("Intentional failure");
        });

    pipeline.add_stage("after", "After Stage",
        [](const hl7_message&) {
            return stage_result::ok();
        });

    auto result = pipeline.process(test_message_);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pipeline_error::stage_failed);
}

TEST_F(MessagingPatternTest, PipelineOptionalStage) {
    hl7_pipeline pipeline;

    pipeline_stage optional_stage;
    optional_stage.id = "optional";
    optional_stage.name = "Optional Stage";
    optional_stage.processor = [](const hl7_message&) {
        return stage_result::error("This failure should be ignored");
    };
    optional_stage.optional = true;

    pipeline.add_stage(optional_stage);

    std::atomic<bool> next_called{false};
    pipeline.add_stage("next", "Next Stage",
        [&](const hl7_message&) {
            next_called = true;
            return stage_result::ok();
        });

    auto result = pipeline.process(test_message_);
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(next_called.load());
}

TEST_F(MessagingPatternTest, PipelineTransformation) {
    hl7_pipeline pipeline;

    pipeline.add_stage("transform", "Transform",
        [](const hl7_message& msg) {
            auto transformed = msg;
            transformed.set_value("ZPI.1", "TRANSFORMED");
            return stage_result::ok(std::move(transformed));
        });

    auto result = pipeline.process(test_message_);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->get_value("ZPI.1"), "TRANSFORMED");
}

// =============================================================================
// Pipeline Builder Tests
// =============================================================================

TEST_F(MessagingPatternTest, PipelineBuilderBasic) {
    auto pipeline = hl7_pipeline_builder::create("test_pipeline")
        .add_validator([](const hl7_message& msg) {
            return msg.has_segment("MSH");
        })
        .add_processor("log", [](const hl7_message&) {
            return stage_result::ok();
        })
        .build();

    EXPECT_EQ(pipeline.stage_count(), 2);
}

TEST_F(MessagingPatternTest, PipelineBuilderWithTransformer) {
    auto pipeline = hl7_pipeline_builder::create("transform_pipeline")
        .add_transformer("enrich", [](const hl7_message& msg) {
            auto enriched = msg;
            enriched.set_value("ZPI.1", "ENRICHED");
            return enriched;
        })
        .build();

    auto result = pipeline.process(test_message_);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->get_value("ZPI.1"), "ENRICHED");
}

// =============================================================================
// ACK Builder Tests
// =============================================================================

TEST_F(MessagingPatternTest, GenerateAck) {
    auto ack = ack_builder::generate_ack(test_message_, messaging_ack_code::AA);

    EXPECT_EQ(ack.get_value("MSH.9.1"), "ACK");
    EXPECT_EQ(ack.get_value("MSA.1"), "AA");
    EXPECT_EQ(ack.get_value("MSA.2"), test_message_.control_id());
}

TEST_F(MessagingPatternTest, GenerateNak) {
    auto nak = ack_builder::generate_nak(test_message_,
                                          "Test error",
                                          "AE");

    EXPECT_EQ(nak.get_value("MSH.9.1"), "ACK");
    EXPECT_EQ(nak.get_value("MSA.1"), "AE");
}

TEST_F(MessagingPatternTest, IsAckSuccess) {
    auto success_ack = ack_builder::generate_ack(test_message_, messaging_ack_code::AA);
    EXPECT_TRUE(ack_builder::is_ack_success(success_ack));

    auto error_ack = ack_builder::generate_ack(test_message_, messaging_ack_code::AE);
    EXPECT_FALSE(ack_builder::is_ack_success(error_ack));
}

// =============================================================================
// Backend Factory Tests
// =============================================================================

TEST_F(MessagingPatternTest, BackendFactoryCreateDefault) {
    auto result = messaging_backend_factory::create_message_bus();
    EXPECT_TRUE(result.has_value());
    EXPECT_NE(*result, nullptr);
}

TEST_F(MessagingPatternTest, BackendFactoryStandalone) {
    auto config = backend_config::standalone(2);
    auto result = messaging_backend_factory::create_message_bus(config);
    EXPECT_TRUE(result.has_value());
}

TEST_F(MessagingPatternTest, BackendRecommended) {
    auto recommended = messaging_backend_factory::recommended_backend();
    // Without external executor, should recommend standalone
    EXPECT_EQ(recommended, backend_type::standalone);
}

TEST_F(MessagingPatternTest, DefaultWorkerThreads) {
    auto threads = messaging_backend_factory::default_worker_threads();
    EXPECT_GT(threads, 0);
}

// =============================================================================
// Error Code Tests
// =============================================================================

TEST_F(MessagingPatternTest, MessageBusErrorCodes) {
    EXPECT_EQ(to_error_code(message_bus_error::not_started), -800);
    EXPECT_EQ(to_error_code(message_bus_error::already_started), -801);
    EXPECT_STREQ(to_string(message_bus_error::not_started),
                 "Message bus not started");
}

TEST_F(MessagingPatternTest, PipelineErrorCodes) {
    EXPECT_EQ(to_error_code(pipeline_error::not_started), -820);
    EXPECT_EQ(to_error_code(pipeline_error::stage_failed), -821);
    EXPECT_STREQ(to_string(pipeline_error::stage_failed),
                 "Stage processing failed");
}

TEST_F(MessagingPatternTest, RequestErrorCodes) {
    EXPECT_EQ(to_error_code(request_error::timeout), -810);
    EXPECT_EQ(to_error_code(request_error::no_handler), -811);
    EXPECT_STREQ(to_string(request_error::timeout),
                 "Request timed out waiting for response");
}

TEST_F(MessagingPatternTest, BackendErrorCodes) {
    EXPECT_EQ(to_error_code(backend_error::not_initialized), -830);
    EXPECT_STREQ(to_string(backend_error::creation_failed),
                 "Backend creation failed");
}

// =============================================================================
// Pipeline Stage Utilities Tests
// =============================================================================

TEST_F(MessagingPatternTest, CreateLoggingStage) {
    std::string logged_message;
    auto stage = pipeline_stages::create_logging_stage("test",
        [&](std::string_view msg) { logged_message = std::string(msg); });

    auto result = stage(test_message_);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(logged_message.empty());
}

TEST_F(MessagingPatternTest, CreateValidationStage) {
    auto stage = pipeline_stages::create_validation_stage(
        [](const hl7_message& msg) {
            return msg.has_segment("MSH");
        },
        "Missing MSH segment");

    auto result = stage(test_message_);
    EXPECT_TRUE(result.success);
}

TEST_F(MessagingPatternTest, CreateEnrichmentStage) {
    auto stage = pipeline_stages::create_enrichment_stage(
        [](hl7_message& msg) {
            msg.set_value("ZPI.1", "ENRICHED");
        });

    auto result = stage(test_message_);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.message.has_value());
    EXPECT_EQ(result.message->get_value("ZPI.1"), "ENRICHED");
}

TEST_F(MessagingPatternTest, CreateConditionalStage) {
    std::atomic<bool> processor_called{false};

    auto stage = pipeline_stages::create_conditional_stage(
        [](const hl7_message& msg) {
            return msg.get_value("MSH.9.1") == "ADT";
        },
        [&](const hl7_message&) {
            processor_called = true;
            return stage_result::ok();
        });

    auto result = stage(test_message_);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(processor_called.load());
}
