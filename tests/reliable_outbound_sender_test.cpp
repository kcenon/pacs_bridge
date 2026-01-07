/**
 * @file reliable_outbound_sender_test.cpp
 * @brief Unit tests for reliable outbound message sender
 *
 * Tests for integrated queue_manager + outbound_router delivery,
 * persistence, retry logic, recovery, and dead letter handling.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/174
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/router/reliable_outbound_sender.h"

#include "utils/test_helpers.h"

#include <chrono>
#include <filesystem>
#include <thread>

namespace pacs::bridge::router {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Error Code Tests
// =============================================================================

class ReliableSenderErrorTest : public pacs_bridge_test {};

TEST_F(ReliableSenderErrorTest, ErrorCodeValues) {
    EXPECT_EQ(to_error_code(reliable_sender_error::not_running), -930);
    EXPECT_EQ(to_error_code(reliable_sender_error::already_running), -931);
    EXPECT_EQ(to_error_code(reliable_sender_error::queue_init_failed), -932);
    EXPECT_EQ(to_error_code(reliable_sender_error::router_init_failed), -933);
    EXPECT_EQ(to_error_code(reliable_sender_error::enqueue_failed), -934);
    EXPECT_EQ(to_error_code(reliable_sender_error::invalid_configuration), -935);
    EXPECT_EQ(to_error_code(reliable_sender_error::destination_not_found), -936);
    EXPECT_EQ(to_error_code(reliable_sender_error::internal_error), -937);
}

TEST_F(ReliableSenderErrorTest, ErrorCodeStrings) {
    EXPECT_STREQ(to_string(reliable_sender_error::not_running),
                 "Reliable sender is not running");
    EXPECT_STREQ(to_string(reliable_sender_error::already_running),
                 "Reliable sender is already running");
    EXPECT_STREQ(to_string(reliable_sender_error::queue_init_failed),
                 "Failed to initialize queue");
    EXPECT_STREQ(to_string(reliable_sender_error::router_init_failed),
                 "Failed to initialize router");
    EXPECT_STREQ(to_string(reliable_sender_error::enqueue_failed),
                 "Failed to enqueue message");
    EXPECT_STREQ(to_string(reliable_sender_error::destination_not_found),
                 "Destination not found");
}

// =============================================================================
// Configuration Tests
// =============================================================================

class ReliableSenderConfigTest : public pacs_bridge_test {};

TEST_F(ReliableSenderConfigTest, DefaultValues) {
    reliable_sender_config config;

    EXPECT_EQ(config.queue.database_path, "queue.db");
    EXPECT_EQ(config.queue.max_queue_size, 50000u);
    EXPECT_EQ(config.queue.max_retry_count, 5u);
    EXPECT_EQ(config.queue.worker_count, 4u);
    EXPECT_TRUE(config.auto_start_workers);
}

TEST_F(ReliableSenderConfigTest, ValidationValid) {
    reliable_sender_config config;
    config.queue.database_path = "/tmp/test_reliable.db";
    config.queue.max_queue_size = 1000;
    config.queue.max_retry_count = 3;
    config.queue.worker_count = 2;

    EXPECT_TRUE(config.is_valid());
}

TEST_F(ReliableSenderConfigTest, ValidationEmptyPath) {
    reliable_sender_config config;
    config.queue.database_path = "";

    EXPECT_FALSE(config.is_valid());
}

// =============================================================================
// Enqueue Request Tests
// =============================================================================

class EnqueueRequestTest : public pacs_bridge_test {};

TEST_F(EnqueueRequestTest, ValidRequest) {
    enqueue_request request;
    request.destination = "RIS_PRIMARY";
    request.payload = "MSH|^~\\&|...";
    request.correlation_id = "ORDER-12345";
    request.message_type = "ORM^O01";
    request.priority = 0;

    EXPECT_TRUE(request.is_valid());
}

TEST_F(EnqueueRequestTest, InvalidEmptyDestination) {
    enqueue_request request;
    request.destination = "";
    request.payload = "MSH|^~\\&|...";

    EXPECT_FALSE(request.is_valid());
}

TEST_F(EnqueueRequestTest, InvalidEmptyPayload) {
    enqueue_request request;
    request.destination = "RIS";
    request.payload = "";

    EXPECT_FALSE(request.is_valid());
}

// =============================================================================
// Config Builder Tests
// =============================================================================

class ReliableSenderConfigBuilderTest : public pacs_bridge_test {};

TEST_F(ReliableSenderConfigBuilderTest, FluentBuilder) {
    auto dest = destination_builder::create("RIS")
                    .host("ris.hospital.local")
                    .port(2576)
                    .message_types({"ORM^O01", "ORU^R01"})
                    .build();

    auto config = reliable_sender_config_builder::create()
                      .database("/tmp/reliable_test.db")
                      .workers(4)
                      .max_queue_size(10000)
                      .retry_policy(3, std::chrono::seconds{5}, 2.0)
                      .ttl(std::chrono::hours{12})
                      .add_destination(dest)
                      .auto_start_workers(true)
                      .build();

    EXPECT_EQ(config.queue.database_path, "/tmp/reliable_test.db");
    EXPECT_EQ(config.queue.worker_count, 4u);
    EXPECT_EQ(config.queue.max_queue_size, 10000u);
    EXPECT_EQ(config.queue.max_retry_count, 3u);
    EXPECT_EQ(config.queue.initial_retry_delay, std::chrono::seconds{5});
    EXPECT_DOUBLE_EQ(config.queue.retry_backoff_multiplier, 2.0);
    EXPECT_EQ(config.queue.message_ttl, std::chrono::hours{12});
    EXPECT_EQ(config.router.destinations.size(), 1u);
    EXPECT_EQ(config.router.destinations[0].name, "RIS");
    EXPECT_TRUE(config.auto_start_workers);
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

class ReliableSenderLifecycleTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();
        db_path_ = test_data_path("reliable_lifecycle_test.db");

        // Clean up any existing test database
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override {
        // Clean up test database
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        // Also remove WAL and SHM files
        std::filesystem::remove(db_path_ + "-wal");
        std::filesystem::remove(db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }

    std::string db_path_;
};

TEST_F(ReliableSenderLifecycleTest, StartStop) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.queue.worker_count = 2;
    config.auto_start_workers = false;  // Don't auto-start workers for this test

    reliable_outbound_sender sender(config);

    EXPECT_FALSE(sender.is_running());

    auto result = sender.start();
    ASSERT_TRUE(result.has_value()) << "Start failed: " << to_string(result.error());
    EXPECT_TRUE(sender.is_running());

    sender.stop();
    EXPECT_FALSE(sender.is_running());
}

TEST_F(ReliableSenderLifecycleTest, DoubleStartFails) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);

    auto result1 = sender.start();
    ASSERT_TRUE(result1.has_value());

    auto result2 = sender.start();
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), reliable_sender_error::already_running);

    sender.stop();
}

TEST_F(ReliableSenderLifecycleTest, EnqueueBeforeStartFails) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;

    reliable_outbound_sender sender(config);

    auto result = sender.enqueue("RIS", "MSH|...", 0);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), reliable_sender_error::not_running);
}

// =============================================================================
// Enqueue Tests
// =============================================================================

class ReliableSenderEnqueueTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();
        db_path_ = test_data_path("reliable_enqueue_test.db");

        // Clean up any existing test database
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }

        config_.queue.database_path = db_path_;
        config_.queue.worker_count = 1;
        config_.auto_start_workers = false;  // Manual control for testing
    }

    void TearDown() override {
        sender_.reset();
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        std::filesystem::remove(db_path_ + "-wal");
        std::filesystem::remove(db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }

    std::string db_path_;
    reliable_sender_config config_;
    std::unique_ptr<reliable_outbound_sender> sender_;
};

TEST_F(ReliableSenderEnqueueTest, EnqueueWithRequest) {
    sender_ = std::make_unique<reliable_outbound_sender>(config_);
    ASSERT_TRUE(sender_->start().has_value());

    enqueue_request request;
    request.destination = "RIS";
    request.payload = "MSH|^~\\&|PACS|HOSP|RIS|HOSP|...";
    request.correlation_id = "ORDER-12345";
    request.message_type = "ORM^O01";
    request.priority = 0;

    auto result = sender_->enqueue(request);
    ASSERT_TRUE(result.has_value()) << "Enqueue failed";
    EXPECT_FALSE(result->empty());

    EXPECT_EQ(sender_->queue_depth(), 1u);

    sender_->stop();
}

TEST_F(ReliableSenderEnqueueTest, EnqueueWithParameters) {
    sender_ = std::make_unique<reliable_outbound_sender>(config_);
    ASSERT_TRUE(sender_->start().has_value());

    auto result = sender_->enqueue(
        "RIS",                    // destination
        "MSH|^~\\&|...",          // payload
        -10,                      // priority (high)
        "CORR-001",               // correlation_id
        "ORU^R01"                 // message_type
    );

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());

    EXPECT_EQ(sender_->queue_depth(), 1u);

    sender_->stop();
}

TEST_F(ReliableSenderEnqueueTest, EnqueueMultipleMessages) {
    sender_ = std::make_unique<reliable_outbound_sender>(config_);
    ASSERT_TRUE(sender_->start().has_value());

    for (int i = 0; i < 10; ++i) {
        auto result = sender_->enqueue(
            "RIS",
            "MSH|^~\\&|TEST|" + std::to_string(i),
            i % 3 - 1,  // Varying priorities
            "CORR-" + std::to_string(i),
            "ORM^O01"
        );
        EXPECT_TRUE(result.has_value());
    }

    EXPECT_EQ(sender_->queue_depth(), 10u);

    sender_->stop();
}

TEST_F(ReliableSenderEnqueueTest, EnqueueInvalidRequest) {
    sender_ = std::make_unique<reliable_outbound_sender>(config_);
    ASSERT_TRUE(sender_->start().has_value());

    // Empty destination
    auto result1 = sender_->enqueue("", "MSH|...", 0);
    EXPECT_FALSE(result1.has_value());
    EXPECT_EQ(result1.error(), reliable_sender_error::enqueue_failed);

    // Empty payload
    auto result2 = sender_->enqueue("RIS", "", 0);
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), reliable_sender_error::enqueue_failed);

    EXPECT_EQ(sender_->queue_depth(), 0u);

    sender_->stop();
}

// =============================================================================
// Statistics Tests
// =============================================================================

class ReliableSenderStatisticsTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();
        db_path_ = test_data_path("reliable_stats_test.db");
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        std::filesystem::remove(db_path_ + "-wal");
        std::filesystem::remove(db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }

    std::string db_path_;
};

TEST_F(ReliableSenderStatisticsTest, InitialStatistics) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    auto stats = sender.get_statistics();
    EXPECT_EQ(stats.total_enqueued, 0u);
    EXPECT_EQ(stats.total_delivered, 0u);
    EXPECT_EQ(stats.total_failed, 0u);
    EXPECT_EQ(stats.queue_depth, 0u);
    EXPECT_EQ(stats.dlq_depth, 0u);

    sender.stop();
}

TEST_F(ReliableSenderStatisticsTest, StatisticsAfterEnqueue) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    for (int i = 0; i < 5; ++i) {
        (void)sender.enqueue("RIS", "MSH|...|" + std::to_string(i), 0);
    }

    auto stats = sender.get_statistics();
    EXPECT_EQ(stats.total_enqueued, 5u);
    EXPECT_EQ(stats.queue_depth, 5u);

    sender.stop();
}

TEST_F(ReliableSenderStatisticsTest, ResetStatistics) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    (void)sender.enqueue("RIS", "MSH|...", 0);

    auto stats1 = sender.get_statistics();
    EXPECT_GT(stats1.total_enqueued, 0u);

    sender.reset_statistics();

    auto stats2 = sender.get_statistics();
    EXPECT_EQ(stats2.total_enqueued, 0u);
    // Queue depth should still be non-zero (messages still in queue)
    EXPECT_EQ(stats2.queue_depth, 1u);

    sender.stop();
}

// =============================================================================
// Destination Management Tests
// =============================================================================

class ReliableSenderDestinationTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();
        db_path_ = test_data_path("reliable_dest_test.db");
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        std::filesystem::remove(db_path_ + "-wal");
        std::filesystem::remove(db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }

    std::string db_path_;
};

TEST_F(ReliableSenderDestinationTest, AddDestination) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;
    config.router.enable_health_check = false;  // Disable health check for testing

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    EXPECT_TRUE(sender.destinations().empty());

    auto dest = destination_builder::create("RIS_PRIMARY")
                    .host("ris.hospital.local")
                    .port(2576)
                    .build();

    auto result = sender.add_destination(dest);
    EXPECT_TRUE(result.has_value());

    EXPECT_EQ(sender.destinations().size(), 1u);
    EXPECT_TRUE(sender.has_destination("RIS_PRIMARY"));

    sender.stop();
}

TEST_F(ReliableSenderDestinationTest, RemoveDestination) {
    auto dest = destination_builder::create("RIS")
                    .host("ris.local")
                    .port(2576)
                    .build();

    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.router.destinations.push_back(dest);
    config.router.enable_health_check = false;  // Disable health check for testing
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    EXPECT_TRUE(sender.has_destination("RIS"));

    bool removed = sender.remove_destination("RIS");
    EXPECT_TRUE(removed);
    EXPECT_FALSE(sender.has_destination("RIS"));

    sender.stop();
}

// =============================================================================
// Recovery Tests (Issue #174 - Key Requirement)
// =============================================================================

class ReliableSenderRecoveryTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();
        db_path_ = test_data_path("reliable_recovery_test.db");
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        std::filesystem::remove(db_path_ + "-wal");
        std::filesystem::remove(db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }

    std::string db_path_;
};

TEST_F(ReliableSenderRecoveryTest, MessagesPersistedAcrossRestart) {
    // Phase 1: Create sender, enqueue messages, stop
    {
        reliable_sender_config config;
        config.queue.database_path = db_path_;
        config.auto_start_workers = false;

        reliable_outbound_sender sender(config);
        ASSERT_TRUE(sender.start().has_value());

        // Enqueue some messages
        for (int i = 0; i < 5; ++i) {
            auto result = sender.enqueue(
                "RIS",
                "MSH|^~\\&|PACS|HOSP|...|" + std::to_string(i),
                0,
                "CORR-" + std::to_string(i),
                "ORM^O01"
            );
            EXPECT_TRUE(result.has_value());
        }

        EXPECT_EQ(sender.queue_depth(), 5u);

        // Stop without processing
        sender.stop();
    }

    // Phase 2: Create new sender, verify messages recovered
    {
        reliable_sender_config config;
        config.queue.database_path = db_path_;  // Same database!
        config.auto_start_workers = false;

        reliable_outbound_sender sender(config);
        ASSERT_TRUE(sender.start().has_value());

        // Messages should be recovered
        EXPECT_EQ(sender.queue_depth(), 5u);

        // Verify we can retrieve pending messages
        auto pending = sender.get_pending("RIS", 10);
        EXPECT_EQ(pending.size(), 5u);

        sender.stop();
    }
}

// =============================================================================
// Dead Letter Queue Tests
// =============================================================================

class ReliableSenderDLQTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();
        db_path_ = test_data_path("reliable_dlq_test.db");
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        std::filesystem::remove(db_path_ + "-wal");
        std::filesystem::remove(db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }

    std::string db_path_;
};

TEST_F(ReliableSenderDLQTest, InitialDLQEmpty) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    EXPECT_EQ(sender.dead_letter_count(), 0u);
    EXPECT_TRUE(sender.get_dead_letters().empty());

    sender.stop();
}

TEST_F(ReliableSenderDLQTest, PurgeDeadLetters) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    // Purge should work even with empty DLQ
    size_t purged = sender.purge_dead_letters();
    EXPECT_EQ(purged, 0u);

    sender.stop();
}

// =============================================================================
// Callback Tests
// =============================================================================

class ReliableSenderCallbackTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();
        db_path_ = test_data_path("reliable_callback_test.db");
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        std::filesystem::remove(db_path_ + "-wal");
        std::filesystem::remove(db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }

    std::string db_path_;
};

TEST_F(ReliableSenderCallbackTest, SetDeliveryCallback) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    std::vector<delivery_event> events;
    sender.set_delivery_callback([&events](const delivery_event& event) {
        events.push_back(event);
    });

    // Callback should be set but not invoked yet (no deliveries)
    EXPECT_TRUE(events.empty());

    sender.clear_delivery_callback();
    sender.stop();
}

TEST_F(ReliableSenderCallbackTest, SetDeadLetterCallback) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    std::vector<dead_letter_entry> entries;
    sender.set_dead_letter_callback([&entries](const dead_letter_entry& entry) {
        entries.push_back(entry);
    });

    // Callback should be set but not invoked yet (no dead letters)
    EXPECT_TRUE(entries.empty());

    sender.clear_dead_letter_callback();
    sender.stop();
}

// =============================================================================
// Component Access Tests
// =============================================================================

class ReliableSenderComponentAccessTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();
        db_path_ = test_data_path("reliable_component_test.db");
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
    }

    void TearDown() override {
        if (std::filesystem::exists(db_path_)) {
            std::filesystem::remove(db_path_);
        }
        std::filesystem::remove(db_path_ + "-wal");
        std::filesystem::remove(db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }

    std::string db_path_;
};

TEST_F(ReliableSenderComponentAccessTest, AccessQueueManager) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    auto& queue = sender.get_queue_manager();
    EXPECT_TRUE(queue.is_running());

    sender.stop();
}

TEST_F(ReliableSenderComponentAccessTest, AccessOutboundRouter) {
    reliable_sender_config config;
    config.queue.database_path = db_path_;
    config.auto_start_workers = false;

    reliable_outbound_sender sender(config);
    ASSERT_TRUE(sender.start().has_value());

    auto& router = sender.get_outbound_router();
    EXPECT_TRUE(router.is_running());

    sender.stop();
}

}  // namespace
}  // namespace pacs::bridge::router
