/**
 * @file queue_manager_test.cpp
 * @brief Unit tests for persistent message queue manager
 *
 * Tests for queue operations, retry logic, dead letter handling,
 * crash recovery, and persistence.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/27
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/router/queue_manager.h"

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

class QueueErrorTest : public pacs_bridge_test {};

TEST_F(QueueErrorTest, ErrorCodeValues) {
    EXPECT_EQ(to_error_code(queue_error::database_error), -910);
    EXPECT_EQ(to_error_code(queue_error::message_not_found), -911);
    EXPECT_EQ(to_error_code(queue_error::queue_full), -912);
    EXPECT_EQ(to_error_code(queue_error::invalid_message), -913);
    EXPECT_EQ(to_error_code(queue_error::message_expired), -914);
    EXPECT_EQ(to_error_code(queue_error::serialization_error), -915);
    EXPECT_EQ(to_error_code(queue_error::not_running), -916);
    EXPECT_EQ(to_error_code(queue_error::already_running), -917);
    EXPECT_EQ(to_error_code(queue_error::transaction_error), -918);
    EXPECT_EQ(to_error_code(queue_error::worker_error), -919);
}

TEST_F(QueueErrorTest, ErrorCodeStrings) {
    EXPECT_STREQ(to_string(queue_error::database_error), "Database operation failed");
    EXPECT_STREQ(to_string(queue_error::message_not_found), "Message not found in queue");
    EXPECT_STREQ(to_string(queue_error::queue_full), "Queue has reached maximum capacity");
    EXPECT_STREQ(to_string(queue_error::invalid_message), "Invalid message data");
    EXPECT_STREQ(to_string(queue_error::not_running), "Queue manager is not running");
}

// =============================================================================
// Message State Tests
// =============================================================================

class MessageStateTest : public pacs_bridge_test {};

TEST_F(MessageStateTest, StateStrings) {
    EXPECT_STREQ(to_string(message_state::pending), "pending");
    EXPECT_STREQ(to_string(message_state::processing), "processing");
    EXPECT_STREQ(to_string(message_state::retry_scheduled), "retry_scheduled");
    EXPECT_STREQ(to_string(message_state::delivered), "delivered");
    EXPECT_STREQ(to_string(message_state::dead_letter), "dead_letter");
}

// =============================================================================
// Queue Configuration Tests
// =============================================================================

class QueueConfigTest : public pacs_bridge_test {};

TEST_F(QueueConfigTest, DefaultValues) {
    queue_config config;

    EXPECT_EQ(config.database_path, "queue.db");
    EXPECT_EQ(config.max_queue_size, 50000u);
    EXPECT_EQ(config.max_retry_count, 5u);
    EXPECT_EQ(config.initial_retry_delay, std::chrono::seconds{5});
    EXPECT_DOUBLE_EQ(config.retry_backoff_multiplier, 2.0);
    EXPECT_EQ(config.max_retry_delay, std::chrono::seconds{600});
    EXPECT_EQ(config.message_ttl, std::chrono::hours{24});
    EXPECT_EQ(config.worker_count, 4u);
    EXPECT_TRUE(config.enable_wal_mode);
}

TEST_F(QueueConfigTest, ValidationValid) {
    queue_config config;
    config.database_path = "/tmp/test_queue.db";
    config.max_queue_size = 1000;
    config.max_retry_count = 3;
    config.worker_count = 2;
    config.retry_backoff_multiplier = 1.5;

    EXPECT_TRUE(config.is_valid());
}

TEST_F(QueueConfigTest, ValidationEmptyPath) {
    queue_config config;
    config.database_path = "";

    EXPECT_FALSE(config.is_valid());
}

TEST_F(QueueConfigTest, ValidationZeroQueueSize) {
    queue_config config;
    config.max_queue_size = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST_F(QueueConfigTest, ValidationZeroWorkers) {
    queue_config config;
    config.worker_count = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST_F(QueueConfigTest, ValidationInvalidBackoff) {
    queue_config config;
    config.retry_backoff_multiplier = 0.5;  // Less than 1.0

    EXPECT_FALSE(config.is_valid());
}

// =============================================================================
// Queue Config Builder Tests
// =============================================================================

class QueueConfigBuilderTest : public pacs_bridge_test {};

TEST_F(QueueConfigBuilderTest, BasicBuild) {
    auto config = queue_config_builder::create()
                      .database("/tmp/queue.db")
                      .max_size(10000)
                      .workers(8)
                      .build();

    EXPECT_EQ(config.database_path, "/tmp/queue.db");
    EXPECT_EQ(config.max_queue_size, 10000u);
    EXPECT_EQ(config.worker_count, 8u);
    EXPECT_TRUE(config.is_valid());
}

TEST_F(QueueConfigBuilderTest, FullConfiguration) {
    auto config = queue_config_builder::create()
                      .database("/var/lib/pacs/queue.db")
                      .max_size(100000)
                      .workers(16)
                      .retry_policy(10, std::chrono::seconds{10}, 1.5)
                      .max_retry_delay(std::chrono::seconds{3600})
                      .ttl(std::chrono::hours{48})
                      .batch_size(50)
                      .cleanup_interval(std::chrono::minutes{10})
                      .wal_mode(true)
                      .build();

    EXPECT_EQ(config.database_path, "/var/lib/pacs/queue.db");
    EXPECT_EQ(config.max_queue_size, 100000u);
    EXPECT_EQ(config.worker_count, 16u);
    EXPECT_EQ(config.max_retry_count, 10u);
    EXPECT_EQ(config.initial_retry_delay, std::chrono::seconds{10});
    EXPECT_DOUBLE_EQ(config.retry_backoff_multiplier, 1.5);
    EXPECT_EQ(config.max_retry_delay, std::chrono::seconds{3600});
    EXPECT_EQ(config.message_ttl, std::chrono::hours{48});
    EXPECT_EQ(config.batch_size, 50u);
    EXPECT_EQ(config.cleanup_interval, std::chrono::minutes{10});
    EXPECT_TRUE(config.enable_wal_mode);
}

// =============================================================================
// Queue Manager Lifecycle Tests
// =============================================================================

class QueueManagerLifecycleTest : public pacs_bridge_test {
protected:
    std::string test_db_path_;

    void SetUp() override {
        pacs_bridge_test::SetUp();
        test_db_path_ = "/tmp/test_queue_" + std::to_string(std::time(nullptr)) + ".db";
    }

    void TearDown() override {
        // Clean up test database
        std::filesystem::remove(test_db_path_);
        std::filesystem::remove(test_db_path_ + "-wal");
        std::filesystem::remove(test_db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }
};

TEST_F(QueueManagerLifecycleTest, DefaultConstruction) {
    queue_manager queue;
    EXPECT_FALSE(queue.is_running());
}

TEST_F(QueueManagerLifecycleTest, StartAndStop) {
    auto config = queue_config_builder::create()
                      .database(test_db_path_)
                      .build();

    queue_manager queue(config);

    EXPECT_FALSE(queue.is_running());

    auto result = queue.start();
    ASSERT_EXPECTED_OK(result);
    EXPECT_TRUE(queue.is_running());

    queue.stop();
    EXPECT_FALSE(queue.is_running());
}

TEST_F(QueueManagerLifecycleTest, DoubleStart) {
    auto config = queue_config_builder::create()
                      .database(test_db_path_)
                      .build();

    queue_manager queue(config);

    ASSERT_EXPECTED_OK(queue.start());
    EXPECT_TRUE(queue.is_running());

    auto result = queue.start();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), queue_error::already_running);
}

TEST_F(QueueManagerLifecycleTest, OperationsWhenNotRunning) {
    queue_manager queue;

    auto enqueue_result = queue.enqueue("DEST", "payload");
    EXPECT_FALSE(enqueue_result.has_value());
    EXPECT_EQ(enqueue_result.error(), queue_error::not_running);

    auto dequeue_result = queue.dequeue();
    EXPECT_FALSE(dequeue_result.has_value());

    auto ack_result = queue.ack("msg_id");
    EXPECT_FALSE(ack_result.has_value());
    EXPECT_EQ(ack_result.error(), queue_error::not_running);
}

// =============================================================================
// Basic Queue Operation Tests
// =============================================================================

class QueueOperationsTest : public QueueManagerLifecycleTest {
protected:
    std::unique_ptr<queue_manager> queue_;

    void SetUp() override {
        QueueManagerLifecycleTest::SetUp();

        auto config = queue_config_builder::create()
                          .database(test_db_path_)
                          .max_size(1000)
                          .workers(2)
                          .retry_policy(3, std::chrono::seconds{1}, 2.0)
                          .build();

        queue_ = std::make_unique<queue_manager>(config);
        ASSERT_TRUE(queue_->start().has_value());
    }

    void TearDown() override {
        if (queue_ && queue_->is_running()) {
            queue_->stop();
        }
        queue_.reset();
        QueueManagerLifecycleTest::TearDown();
    }
};

TEST_F(QueueOperationsTest, EnqueueBasic) {
    auto result = queue_->enqueue("RIS", "HL7|MESSAGE|CONTENT");

    ASSERT_EXPECTED_OK(result);
    EXPECT_FALSE(result->empty());
    EXPECT_EQ(queue_->queue_depth(), 1u);
}

TEST_F(QueueOperationsTest, EnqueueWithMetadata) {
    auto result = queue_->enqueue("RIS", "HL7|MESSAGE|CONTENT", 0, "CORR123", "ORM^O01");

    ASSERT_EXPECTED_OK(result);

    auto msg = queue_->get_message(*result);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->destination, "RIS");
    EXPECT_EQ(msg->correlation_id, "CORR123");
    EXPECT_EQ(msg->message_type, "ORM^O01");
}

TEST_F(QueueOperationsTest, EnqueueWithPriority) {
    (void)queue_->enqueue("RIS", "LOW", 100);
    (void)queue_->enqueue("RIS", "HIGH", -10);
    (void)queue_->enqueue("RIS", "NORMAL", 0);

    EXPECT_EQ(queue_->queue_depth(), 3u);

    // Dequeue should return highest priority (lowest number) first
    auto msg = queue_->dequeue();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->payload, "HIGH");
    EXPECT_EQ(msg->priority, -10);
}

TEST_F(QueueOperationsTest, EnqueueEmptyDestination) {
    auto result = queue_->enqueue("", "payload");

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), queue_error::invalid_message);
}

TEST_F(QueueOperationsTest, EnqueueEmptyPayload) {
    auto result = queue_->enqueue("DEST", "");

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), queue_error::invalid_message);
}

TEST_F(QueueOperationsTest, DequeueEmpty) {
    auto result = queue_->dequeue();
    EXPECT_FALSE(result.has_value());
}

TEST_F(QueueOperationsTest, DequeueBasic) {
    (void)queue_->enqueue("RIS", "TEST_PAYLOAD");

    auto msg = queue_->dequeue();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->destination, "RIS");
    EXPECT_EQ(msg->payload, "TEST_PAYLOAD");
    EXPECT_EQ(msg->state, message_state::processing);
    EXPECT_EQ(msg->attempt_count, 1);
}

TEST_F(QueueOperationsTest, DequeueByDestination) {
    (void)queue_->enqueue("RIS", "RIS_MESSAGE");
    (void)queue_->enqueue("PACS", "PACS_MESSAGE");
    (void)queue_->enqueue("RIS", "RIS_MESSAGE_2");

    auto msg = queue_->dequeue("PACS");
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->destination, "PACS");
    EXPECT_EQ(msg->payload, "PACS_MESSAGE");

    // RIS messages should still be there
    EXPECT_EQ(queue_->queue_depth("RIS"), 2u);
}

TEST_F(QueueOperationsTest, DequeueBatch) {
    for (int i = 0; i < 5; ++i) {
        (void)queue_->enqueue("RIS", "MSG_" + std::to_string(i));
    }

    auto batch = queue_->dequeue_batch(3);
    EXPECT_EQ(batch.size(), 3u);

    // Remaining messages
    EXPECT_EQ(queue_->queue_depth(), 2u);
}

TEST_F(QueueOperationsTest, AckMessage) {
    auto enqueue_result = queue_->enqueue("RIS", "PAYLOAD");
    ASSERT_EXPECTED_OK(enqueue_result);

    auto msg = queue_->dequeue();
    ASSERT_TRUE(msg.has_value());

    auto ack_result = queue_->ack(msg->id);
    ASSERT_EXPECTED_OK(ack_result);

    // Message should be removed
    EXPECT_EQ(queue_->queue_depth(), 0u);
    EXPECT_FALSE(queue_->get_message(msg->id).has_value());
}

TEST_F(QueueOperationsTest, AckNonExistent) {
    auto result = queue_->ack("nonexistent_id");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), queue_error::message_not_found);
}

TEST_F(QueueOperationsTest, NackMessage) {
    auto enqueue_result = queue_->enqueue("RIS", "PAYLOAD");
    ASSERT_EXPECTED_OK(enqueue_result);

    auto msg = queue_->dequeue();
    ASSERT_TRUE(msg.has_value());

    auto nack_result = queue_->nack(msg->id, "Delivery failed");
    ASSERT_EXPECTED_OK(nack_result);

    // Message should be rescheduled for retry
    auto updated_msg = queue_->get_message(msg->id);
    ASSERT_TRUE(updated_msg.has_value());
    EXPECT_EQ(updated_msg->state, message_state::retry_scheduled);
    EXPECT_EQ(updated_msg->last_error, "Delivery failed");
}

TEST_F(QueueOperationsTest, NackMaxRetries) {
    auto config = queue_config_builder::create()
                      .database(test_db_path_ + "_retry")
                      .retry_policy(2, std::chrono::seconds{1}, 1.0)
                      .build();

    queue_manager queue(config);
    ASSERT_EXPECTED_OK(queue.start());

    auto enqueue_result = queue.enqueue("RIS", "PAYLOAD");
    ASSERT_EXPECTED_OK(enqueue_result);
    std::string msg_id = *enqueue_result;

    // Dequeue and nack twice
    for (int i = 0; i < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        auto msg = queue.dequeue();
        if (msg) {
            (void)queue.nack(msg->id, "Failed attempt " + std::to_string(i + 1));
        }
    }

    // Third dequeue and nack should move to dead letter
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    auto msg = queue.dequeue();
    if (msg) {
        (void)queue.nack(msg->id, "Final failure");
    }

    // Check dead letter queue
    EXPECT_EQ(queue.dead_letter_count(), 1u);

    queue.stop();
    std::filesystem::remove(test_db_path_ + "_retry");
    std::filesystem::remove(test_db_path_ + "_retry-wal");
    std::filesystem::remove(test_db_path_ + "_retry-shm");
}

// =============================================================================
// Dead Letter Queue Tests
// =============================================================================

class DeadLetterTest : public QueueOperationsTest {};

TEST_F(DeadLetterTest, ManualDeadLetter) {
    auto enqueue_result = queue_->enqueue("RIS", "PAYLOAD");
    ASSERT_EXPECTED_OK(enqueue_result);

    auto msg = queue_->dequeue();
    ASSERT_TRUE(msg.has_value());

    auto dl_result = queue_->dead_letter(msg->id, "Manual dead letter");
    ASSERT_EXPECTED_OK(dl_result);

    EXPECT_EQ(queue_->queue_depth(), 0u);
    EXPECT_EQ(queue_->dead_letter_count(), 1u);
}

TEST_F(DeadLetterTest, GetDeadLetters) {
    auto enqueue_result = queue_->enqueue("RIS", "PAYLOAD");
    ASSERT_EXPECTED_OK(enqueue_result);

    auto msg = queue_->dequeue();
    ASSERT_TRUE(msg.has_value());

    (void)queue_->dead_letter(msg->id, "Test reason");

    auto dead_letters = queue_->get_dead_letters();
    ASSERT_EQ(dead_letters.size(), 1u);
    EXPECT_EQ(dead_letters[0].message.id, msg->id);
    EXPECT_EQ(dead_letters[0].reason, "Test reason");
    EXPECT_EQ(dead_letters[0].message.state, message_state::dead_letter);
}

TEST_F(DeadLetterTest, RetryDeadLetter) {
    auto enqueue_result = queue_->enqueue("RIS", "PAYLOAD");
    ASSERT_EXPECTED_OK(enqueue_result);
    std::string msg_id = *enqueue_result;

    auto msg = queue_->dequeue();
    (void)queue_->dead_letter(msg->id, "Test reason");

    EXPECT_EQ(queue_->dead_letter_count(), 1u);
    EXPECT_EQ(queue_->queue_depth(), 0u);

    // Retry the dead letter
    auto retry_result = queue_->retry_dead_letter(msg_id);
    ASSERT_EXPECTED_OK(retry_result);

    EXPECT_EQ(queue_->dead_letter_count(), 0u);
    EXPECT_EQ(queue_->queue_depth(), 1u);

    // Message should be pending again
    auto retrieved = queue_->get_message(msg_id);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->state, message_state::pending);
}

TEST_F(DeadLetterTest, DeleteDeadLetter) {
    auto enqueue_result = queue_->enqueue("RIS", "PAYLOAD");
    ASSERT_EXPECTED_OK(enqueue_result);

    auto msg = queue_->dequeue();
    (void)queue_->dead_letter(msg->id, "Test reason");

    EXPECT_EQ(queue_->dead_letter_count(), 1u);

    auto delete_result = queue_->delete_dead_letter(msg->id);
    ASSERT_EXPECTED_OK(delete_result);

    EXPECT_EQ(queue_->dead_letter_count(), 0u);
}

TEST_F(DeadLetterTest, PurgeDeadLetters) {
    // Create multiple dead letters
    for (int i = 0; i < 5; ++i) {
        auto result = queue_->enqueue("RIS", "PAYLOAD_" + std::to_string(i));
        auto msg = queue_->dequeue();
        (void)queue_->dead_letter(msg->id, "Reason " + std::to_string(i));
    }

    EXPECT_EQ(queue_->dead_letter_count(), 5u);

    size_t purged = queue_->purge_dead_letters();
    EXPECT_EQ(purged, 5u);
    EXPECT_EQ(queue_->dead_letter_count(), 0u);
}

TEST_F(DeadLetterTest, DeadLetterCallback) {
    bool callback_called = false;
    dead_letter_entry received_entry;

    queue_->set_dead_letter_callback([&](const dead_letter_entry& entry) {
        callback_called = true;
        received_entry = entry;
    });

    auto enqueue_result = queue_->enqueue("RIS", "TEST_PAYLOAD");
    auto msg = queue_->dequeue();
    (void)queue_->dead_letter(msg->id, "Callback test");

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(received_entry.message.payload, "TEST_PAYLOAD");
    EXPECT_EQ(received_entry.reason, "Callback test");
}

// =============================================================================
// Statistics Tests
// =============================================================================

class StatisticsTest : public QueueOperationsTest {};

TEST_F(StatisticsTest, InitialStatistics) {
    auto stats = queue_->get_statistics();

    EXPECT_EQ(stats.total_enqueued, 0u);
    EXPECT_EQ(stats.total_delivered, 0u);
    EXPECT_EQ(stats.total_dead_lettered, 0u);
    EXPECT_EQ(stats.pending_count, 0u);
}

TEST_F(StatisticsTest, EnqueueUpdatesStats) {
    (void)queue_->enqueue("RIS", "PAYLOAD1");
    (void)queue_->enqueue("RIS", "PAYLOAD2");

    auto stats = queue_->get_statistics();
    EXPECT_EQ(stats.total_enqueued, 2u);
    EXPECT_EQ(stats.pending_count, 2u);
}

TEST_F(StatisticsTest, AckUpdatesStats) {
    (void)queue_->enqueue("RIS", "PAYLOAD");
    auto msg = queue_->dequeue();
    (void)queue_->ack(msg->id);

    auto stats = queue_->get_statistics();
    EXPECT_EQ(stats.total_delivered, 1u);
    EXPECT_EQ(stats.pending_count, 0u);
}

TEST_F(StatisticsTest, DeadLetterUpdatesStats) {
    (void)queue_->enqueue("RIS", "PAYLOAD");
    auto msg = queue_->dequeue();
    (void)queue_->dead_letter(msg->id, "Test");

    auto stats = queue_->get_statistics();
    EXPECT_EQ(stats.total_dead_lettered, 1u);
    EXPECT_EQ(stats.dead_letter_count, 1u);
}

TEST_F(StatisticsTest, DepthByDestination) {
    (void)queue_->enqueue("RIS", "RIS_1");
    (void)queue_->enqueue("RIS", "RIS_2");
    (void)queue_->enqueue("PACS", "PACS_1");

    auto stats = queue_->get_statistics();

    bool found_ris = false;
    bool found_pacs = false;

    for (const auto& [dest, count] : stats.depth_by_destination) {
        if (dest == "RIS") {
            found_ris = true;
            EXPECT_EQ(count, 2u);
        } else if (dest == "PACS") {
            found_pacs = true;
            EXPECT_EQ(count, 1u);
        }
    }

    EXPECT_TRUE(found_ris);
    EXPECT_TRUE(found_pacs);
}

TEST_F(StatisticsTest, ResetStatistics) {
    (void)queue_->enqueue("RIS", "PAYLOAD");
    queue_->reset_statistics();

    auto stats = queue_->get_statistics();
    EXPECT_EQ(stats.total_enqueued, 0u);
}

// =============================================================================
// Queue Depth and Inspection Tests
// =============================================================================

class QueueInspectionTest : public QueueOperationsTest {};

TEST_F(QueueInspectionTest, QueueDepth) {
    EXPECT_EQ(queue_->queue_depth(), 0u);

    (void)queue_->enqueue("RIS", "MSG1");
    (void)queue_->enqueue("RIS", "MSG2");
    (void)queue_->enqueue("PACS", "MSG3");

    EXPECT_EQ(queue_->queue_depth(), 3u);
    EXPECT_EQ(queue_->queue_depth("RIS"), 2u);
    EXPECT_EQ(queue_->queue_depth("PACS"), 1u);
    EXPECT_EQ(queue_->queue_depth("UNKNOWN"), 0u);
}

TEST_F(QueueInspectionTest, GetDestinations) {
    (void)queue_->enqueue("RIS", "MSG1");
    (void)queue_->enqueue("PACS", "MSG2");
    (void)queue_->enqueue("EMR", "MSG3");

    auto destinations = queue_->destinations();
    EXPECT_EQ(destinations.size(), 3u);
    EXPECT_THAT(destinations, Contains("RIS"));
    EXPECT_THAT(destinations, Contains("PACS"));
    EXPECT_THAT(destinations, Contains("EMR"));
}

TEST_F(QueueInspectionTest, GetPending) {
    (void)queue_->enqueue("RIS", "MSG1");
    (void)queue_->enqueue("RIS", "MSG2");
    (void)queue_->enqueue("PACS", "MSG3");

    auto pending = queue_->get_pending("RIS", 10);
    EXPECT_EQ(pending.size(), 2u);
    for (const auto& msg : pending) {
        EXPECT_EQ(msg.destination, "RIS");
    }
}

TEST_F(QueueInspectionTest, GetMessage) {
    auto result = queue_->enqueue("RIS", "TEST_PAYLOAD", 5, "CORR", "ADT^A01");
    ASSERT_EXPECTED_OK(result);

    auto msg = queue_->get_message(*result);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->id, *result);
    EXPECT_EQ(msg->destination, "RIS");
    EXPECT_EQ(msg->payload, "TEST_PAYLOAD");
    EXPECT_EQ(msg->priority, 5);
    EXPECT_EQ(msg->correlation_id, "CORR");
    EXPECT_EQ(msg->message_type, "ADT^A01");
    EXPECT_EQ(msg->state, message_state::pending);
}

TEST_F(QueueInspectionTest, GetNonExistentMessage) {
    auto msg = queue_->get_message("nonexistent_id");
    EXPECT_FALSE(msg.has_value());
}

// =============================================================================
// Persistence and Recovery Tests
// =============================================================================

class PersistenceTest : public QueueManagerLifecycleTest {};

TEST_F(PersistenceTest, MessagesSurviveRestart) {
    {
        auto config = queue_config_builder::create()
                          .database(test_db_path_)
                          .build();

        queue_manager queue(config);
        ASSERT_EXPECTED_OK(queue.start());

        (void)queue.enqueue("RIS", "PERSISTENT_MSG_1");
        (void)queue.enqueue("RIS", "PERSISTENT_MSG_2");

        EXPECT_EQ(queue.queue_depth(), 2u);
        queue.stop();
    }

    // Reopen database
    {
        auto config = queue_config_builder::create()
                          .database(test_db_path_)
                          .build();

        queue_manager queue(config);
        ASSERT_EXPECTED_OK(queue.start());

        EXPECT_EQ(queue.queue_depth(), 2u);

        auto msg = queue.dequeue();
        ASSERT_TRUE(msg.has_value());
        EXPECT_EQ(msg->payload, "PERSISTENT_MSG_1");

        queue.stop();
    }
}

TEST_F(PersistenceTest, RecoverProcessingMessages) {
    std::string msg_id;

    {
        auto config = queue_config_builder::create()
                          .database(test_db_path_)
                          .build();

        queue_manager queue(config);
        ASSERT_EXPECTED_OK(queue.start());

        auto result = queue.enqueue("RIS", "PROCESSING_MSG");
        ASSERT_EXPECTED_OK(result);
        msg_id = *result;

        // Dequeue but don't ack - simulates crash during processing
        auto msg = queue.dequeue();
        ASSERT_TRUE(msg.has_value());
        EXPECT_EQ(msg->state, message_state::processing);

        // Simulate crash by not calling stop() properly
        // Just let it destruct
    }

    // Reopen - recovery should reset processing to pending
    {
        auto config = queue_config_builder::create()
                          .database(test_db_path_)
                          .build();

        queue_manager queue(config);
        ASSERT_EXPECTED_OK(queue.start());

        // Recovery should have run
        auto msg = queue.get_message(msg_id);
        ASSERT_TRUE(msg.has_value());
        EXPECT_EQ(msg->state, message_state::pending);

        queue.stop();
    }
}

TEST_F(PersistenceTest, DeadLettersSurviveRestart) {
    std::string msg_id;

    {
        auto config = queue_config_builder::create()
                          .database(test_db_path_)
                          .build();

        queue_manager queue(config);
        ASSERT_EXPECTED_OK(queue.start());

        auto result = queue.enqueue("RIS", "DL_MSG");
        msg_id = *result;

        auto msg = queue.dequeue();
        (void)queue.dead_letter(msg->id, "Test persistence");

        EXPECT_EQ(queue.dead_letter_count(), 1u);
        queue.stop();
    }

    {
        auto config = queue_config_builder::create()
                          .database(test_db_path_)
                          .build();

        queue_manager queue(config);
        ASSERT_EXPECTED_OK(queue.start());

        EXPECT_EQ(queue.dead_letter_count(), 1u);

        auto dead_letters = queue.get_dead_letters();
        ASSERT_EQ(dead_letters.size(), 1u);
        EXPECT_EQ(dead_letters[0].message.id, msg_id);

        queue.stop();
    }
}

// =============================================================================
// Maintenance Tests
// =============================================================================

class MaintenanceTest : public QueueOperationsTest {};

TEST_F(MaintenanceTest, Compact) {
    // Enqueue and ack some messages to create fragmentation
    for (int i = 0; i < 10; ++i) {
        (void)queue_->enqueue("RIS", "MSG_" + std::to_string(i));
        auto msg = queue_->dequeue();
        (void)queue_->ack(msg->id);
    }

    // Compact should not throw
    EXPECT_NO_THROW(queue_->compact());
}

TEST_F(MaintenanceTest, CleanupExpired) {
    // This would require waiting for TTL which is impractical in unit tests
    // Just verify the function doesn't crash
    size_t cleaned = queue_->cleanup_expired();
    EXPECT_EQ(cleaned, 0u);  // No expired messages yet
}

// =============================================================================
// Queue Full Tests
// =============================================================================

class QueueCapacityTest : public QueueManagerLifecycleTest {};

TEST_F(QueueCapacityTest, QueueFull) {
    auto config = queue_config_builder::create()
                      .database(test_db_path_)
                      .max_size(3)  // Small queue for testing
                      .build();

    queue_manager queue(config);
    ASSERT_EXPECTED_OK(queue.start());

    EXPECT_EXPECTED_OK(queue.enqueue("RIS", "MSG1"));
    EXPECT_EXPECTED_OK(queue.enqueue("RIS", "MSG2"));
    EXPECT_EXPECTED_OK(queue.enqueue("RIS", "MSG3"));

    // Fourth message should fail
    auto result = queue.enqueue("RIS", "MSG4");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), queue_error::queue_full);

    queue.stop();
}

}  // namespace
}  // namespace pacs::bridge::router
