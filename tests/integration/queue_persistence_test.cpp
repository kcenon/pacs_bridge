/**
 * @file queue_persistence_test.cpp
 * @brief Integration tests for message queue persistence and recovery
 *
 * Tests the queue recovery scenarios when the RIS becomes temporarily
 * unavailable. Verifies that messages are persisted and redelivered
 * after system restart or RIS recovery.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/29
 * @see https://github.com/kcenon/pacs_bridge/issues/27 (Outbound Message Queue)
 */

#include "integration_test_base.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace pacs::bridge::integration::test {

// =============================================================================
// Queue Persistence with Persistence-specific Simulator
// =============================================================================

/**
 * @brief Outbound message queue simulator specialized for persistence testing
 *
 * This implementation uses yield-based polling and provides delivery attempt
 * tracking for testing queue recovery scenarios.
 */
class persistence_queue_simulator {
public:
    struct config {
        std::filesystem::path storage_path;
        uint16_t ris_port;
        std::chrono::milliseconds retry_interval{1000};
        int max_retries{5};
    };

    explicit persistence_queue_simulator(const config& cfg)
        : config_(cfg),
          queue_(cfg.storage_path),
          running_(false),
          delivery_attempts_(0),
          successful_deliveries_(0) {}

    ~persistence_queue_simulator() {
        stop();
    }

    void start() {
        running_ = true;
        queue_.start();
        delivery_thread_ = std::thread(&persistence_queue_simulator::delivery_loop,
                                       this);
    }

    void stop() {
        running_ = false;
        queue_.stop();
        if (delivery_thread_.joinable()) {
            delivery_thread_.join();
        }
    }

    bool is_running() const {
        return running_;
    }

    void enqueue(const std::string& message) {
        queue_.enqueue(message);
    }

    size_t queue_size() const {
        return queue_.size();
    }

    bool queue_empty() const {
        return queue_.empty();
    }

    /**
     * @brief Simulate system restart by reloading queue from disk
     */
    void simulate_restart() {
        stop();
        queue_.simulate_recovery();
        start();
    }

    uint32_t delivery_attempts() const {
        return delivery_attempts_;
    }

    uint32_t successful_deliveries() const {
        return successful_deliveries_;
    }

    void reset_counters() {
        delivery_attempts_ = 0;
        successful_deliveries_ = 0;
    }

private:
    void delivery_loop() {
        while (running_) {
            auto msg = queue_.peek();
            if (!msg.has_value()) {
                // Wait for messages using yield-based polling
                auto wait_start = std::chrono::steady_clock::now();
                while (running_ && !queue_.peek().has_value()) {
                    if (std::chrono::steady_clock::now() - wait_start >
                        std::chrono::milliseconds{100}) {
                        break;
                    }
                    std::this_thread::yield();
                }
                continue;
            }

            delivery_attempts_++;
            bool delivered = try_deliver(*msg);

            if (delivered) {
                queue_.dequeue();  // Remove from queue on success
                successful_deliveries_++;
            } else {
                // Wait before retry using yield-based polling
                auto retry_deadline = std::chrono::steady_clock::now() +
                                      config_.retry_interval;
                while (running_ &&
                       std::chrono::steady_clock::now() < retry_deadline) {
                    std::this_thread::yield();
                }
            }
        }
    }

    bool try_deliver(const std::string& message) {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = config_.ris_port;
        client_config.connect_timeout = std::chrono::milliseconds{500};

        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        if (!connect_result.has_value()) {
            return false;
        }

        auto msg = mllp::mllp_message::from_string(message);
        auto send_result = client.send(msg);

        client.disconnect();

        return send_result.has_value();
    }

    config config_;
    test_message_queue queue_;
    std::atomic<bool> running_;
    std::thread delivery_thread_;
    std::atomic<uint32_t> delivery_attempts_;
    std::atomic<uint32_t> successful_deliveries_;
};

// =============================================================================
// Basic Queue Persistence Tests
// =============================================================================

/**
 * @brief Test that messages are persisted to disk
 *
 * Verifies that enqueued messages survive process restart by
 * checking disk persistence.
 */
bool test_queue_persistence_basic() {
    auto temp_path = integration_test_fixture::generate_temp_path();

    {
        // Create queue and add messages
        test_message_queue queue(temp_path);
        queue.start();

        queue.enqueue("Message 1");
        queue.enqueue("Message 2");
        queue.enqueue("Message 3");

        INTEGRATION_TEST_ASSERT(queue.size() == 3, "Should have 3 messages");
        queue.stop();
    }

    // Queue goes out of scope, simulating process exit

    {
        // Create new queue instance (simulating process restart)
        test_message_queue queue(temp_path);

        INTEGRATION_TEST_ASSERT(queue.size() == 3,
                                "Should recover 3 messages from disk");

        auto msg1 = queue.dequeue();
        INTEGRATION_TEST_ASSERT(msg1.has_value(), "Should have message 1");
        INTEGRATION_TEST_ASSERT(*msg1 == "Message 1",
                                "Message 1 content should match");

        auto msg2 = queue.dequeue();
        INTEGRATION_TEST_ASSERT(msg2.has_value(), "Should have message 2");
        INTEGRATION_TEST_ASSERT(*msg2 == "Message 2",
                                "Message 2 content should match");

        auto msg3 = queue.dequeue();
        INTEGRATION_TEST_ASSERT(msg3.has_value(), "Should have message 3");
        INTEGRATION_TEST_ASSERT(*msg3 == "Message 3",
                                "Message 3 content should match");

        INTEGRATION_TEST_ASSERT(queue.empty(), "Queue should be empty");
    }

    // Cleanup
    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

/**
 * @brief Test queue FIFO ordering after recovery
 *
 * Verifies that message order is preserved across system restarts.
 */
bool test_queue_fifo_order_after_recovery() {
    auto temp_path = integration_test_fixture::generate_temp_path();

    {
        test_message_queue queue(temp_path);
        queue.start();

        // Add messages in specific order
        for (int i = 1; i <= 10; ++i) {
            queue.enqueue("Message_" + std::to_string(i));
        }

        queue.stop();
    }

    {
        // Recover and verify order
        test_message_queue queue(temp_path);

        for (int i = 1; i <= 10; ++i) {
            auto msg = queue.dequeue();
            INTEGRATION_TEST_ASSERT(msg.has_value(),
                                    "Should have message " + std::to_string(i));
            INTEGRATION_TEST_ASSERT(*msg == "Message_" + std::to_string(i),
                                    "Message order should be preserved");
        }
    }

    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

// =============================================================================
// Queue Recovery Scenario Tests
// =============================================================================

/**
 * @brief Test queue recovery when RIS becomes temporarily unavailable
 *
 * Scenario:
 * 1. RIS is available, messages are delivered
 * 2. RIS becomes unavailable, messages are queued
 * 3. RIS becomes available again, queued messages are delivered
 */
bool test_queue_recovery_ris_unavailable() {
    auto temp_path = integration_test_fixture::generate_temp_path();
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    // Setup mock RIS server
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    // Setup outbound queue
    persistence_queue_simulator::config queue_config;
    queue_config.storage_path = temp_path;
    queue_config.ris_port = ris_port;
    queue_config.retry_interval = std::chrono::milliseconds{200};

    persistence_queue_simulator queue(queue_config);
    queue.start();

    // Phase 1: RIS available - send first message
    queue.enqueue("MSH|^~\\&|PACS||RIS||20240101||ORM^O01|1|P|2.4\r");

    // Wait for delivery
    bool delivered1 = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 1; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(delivered1, "First message should be delivered");

    // Phase 2: RIS becomes unavailable
    ris.set_available(false);

    // Queue more messages while RIS is down
    queue.enqueue("MSH|^~\\&|PACS||RIS||20240101||ORM^O01|2|P|2.4\r");
    queue.enqueue("MSH|^~\\&|PACS||RIS||20240101||ORM^O01|3|P|2.4\r");

    // Wait a bit and verify messages are not delivered (RIS unavailable)
    // Use wait_for with a condition that should NOT become true
    integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() > 1; },
        std::chrono::milliseconds{500});  // Expected to timeout
    INTEGRATION_TEST_ASSERT(ris.messages_received() == 1,
                            "Only first message should be delivered");
    INTEGRATION_TEST_ASSERT(queue.queue_size() >= 1,
                            "Messages should be queued");

    // Phase 3: RIS becomes available again
    ris.set_available(true);

    // Wait for queued messages to be delivered (scaled for CI)
    auto timeout = integration_test_fixture::scale_timeout_for_ci(std::chrono::milliseconds{5000});
    bool delivered_all = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 3; },
        timeout);
    INTEGRATION_TEST_ASSERT(delivered_all,
                            "All messages should eventually be delivered");
    INTEGRATION_TEST_ASSERT(queue.queue_empty(),
                            "Queue should be empty after delivery");

    // Cleanup
    queue.stop();
    ris.stop();
    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

/**
 * @brief Test queue recovery after simulated system restart
 *
 * Scenario:
 * 1. Queue messages while RIS is down
 * 2. Simulate system restart (queue reloads from disk)
 * 3. RIS becomes available, queued messages are delivered
 */
bool test_queue_recovery_after_restart() {
    auto temp_path = integration_test_fixture::generate_temp_path();
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    // Setup mock RIS server (initially stopped)
    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    // Phase 1: Queue messages while RIS is unavailable
    {
        persistence_queue_simulator::config queue_config;
        queue_config.storage_path = temp_path;
        queue_config.ris_port = ris_port;
        queue_config.retry_interval = std::chrono::milliseconds{200};

        persistence_queue_simulator queue(queue_config);

        // Enqueue without starting (no delivery thread)
        queue.enqueue("MSH|^~\\&|PACS||RIS||20240101||ORM^O01|1|P|2.4\r");
        queue.enqueue("MSH|^~\\&|PACS||RIS||20240101||ORM^O01|2|P|2.4\r");

        INTEGRATION_TEST_ASSERT(queue.queue_size() == 2,
                                "Should have 2 queued messages");
    }

    // Phase 2: System "restarts" - verify messages persisted
    {
        test_message_queue recovery_check(temp_path);
        INTEGRATION_TEST_ASSERT(recovery_check.size() == 2,
                                "Messages should persist across restart");
    }

    // Phase 3: Start RIS and new queue instance
    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    {
        persistence_queue_simulator::config queue_config;
        queue_config.storage_path = temp_path;
        queue_config.ris_port = ris_port;
        queue_config.retry_interval = std::chrono::milliseconds{200};

        persistence_queue_simulator queue(queue_config);
        queue.start();

        // Wait for delivery of persisted messages (scaled for CI)
        auto timeout = integration_test_fixture::scale_timeout_for_ci(std::chrono::milliseconds{5000});
        bool delivered = integration_test_fixture::wait_for(
            [&ris]() { return ris.messages_received() >= 2; },
            timeout);

        INTEGRATION_TEST_ASSERT(delivered,
                                "Persisted messages should be delivered");
        INTEGRATION_TEST_ASSERT(queue.queue_empty(),
                                "Queue should be empty after delivery");

        queue.stop();
    }

    // Cleanup
    ris.stop();
    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

/**
 * @brief Test partial delivery recovery
 *
 * Scenario: Some messages delivered, then failure, then recovery.
 * Only undelivered messages should be redelivered.
 */
bool test_queue_partial_delivery_recovery() {
    auto temp_path = integration_test_fixture::generate_temp_path();
    uint16_t ris_port = integration_test_fixture::generate_test_port();

    mock_ris_server::config ris_config;
    ris_config.port = ris_port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    persistence_queue_simulator::config queue_config;
    queue_config.storage_path = temp_path;
    queue_config.ris_port = ris_port;
    queue_config.retry_interval = std::chrono::milliseconds{200};

    persistence_queue_simulator queue(queue_config);
    queue.start();

    // Queue 5 messages
    for (int i = 1; i <= 5; ++i) {
        queue.enqueue("MSH|^~\\&|PACS||RIS||20240101||ORM^O01|" +
                      std::to_string(i) + "|P|2.4\r");
    }

    // Wait for first 2 messages to be delivered
    bool partial = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 2; },
        std::chrono::milliseconds{3000});
    INTEGRATION_TEST_ASSERT(partial, "Some messages should be delivered");

    // Make RIS unavailable and wait for queue to detect it
    ris.set_available(false);
    // Allow time for any in-flight delivery to complete or fail
    integration_test_fixture::wait_for(
        [&ris]() { return !ris.is_available(); },
        std::chrono::milliseconds{100});
    // Brief wait for delivery attempts to fail
    integration_test_fixture::wait_for(
        [&queue]() { return queue.delivery_attempts() > 2; },
        std::chrono::milliseconds{500});

    uint32_t received_before = ris.messages_received();
    size_t remaining = queue.queue_size();

    // Make RIS available again
    ris.set_available(true);

    // Wait for remaining messages
    bool all_delivered = integration_test_fixture::wait_for(
        [&ris]() { return ris.messages_received() >= 5; },
        std::chrono::milliseconds{5000});

    INTEGRATION_TEST_ASSERT(all_delivered, "All messages should be delivered");
    INTEGRATION_TEST_ASSERT(ris.messages_received() == 5,
                            "Should receive exactly 5 messages");

    queue.stop();
    ris.stop();
    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

// =============================================================================
// Queue Edge Cases
// =============================================================================

/**
 * @brief Test empty queue recovery
 */
bool test_queue_empty_recovery() {
    auto temp_path = integration_test_fixture::generate_temp_path();

    {
        test_message_queue queue(temp_path);
        queue.start();
        // Don't enqueue anything
        queue.stop();
    }

    {
        test_message_queue queue(temp_path);
        INTEGRATION_TEST_ASSERT(queue.empty(),
                                "Recovered queue should be empty");
        INTEGRATION_TEST_ASSERT(queue.size() == 0, "Size should be 0");
    }

    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

/**
 * @brief Test queue with large messages
 */
bool test_queue_large_messages() {
    auto temp_path = integration_test_fixture::generate_temp_path();

    // Create a large message (100KB)
    std::string large_msg(100 * 1024, 'X');
    large_msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|1|P|2.4\r" +
                std::string(100 * 1024, 'X');

    {
        test_message_queue queue(temp_path);
        queue.start();
        queue.enqueue(large_msg);
        queue.enqueue("Small message");
        queue.stop();
    }

    {
        test_message_queue queue(temp_path);
        INTEGRATION_TEST_ASSERT(queue.size() == 2, "Should have 2 messages");

        auto msg1 = queue.dequeue();
        INTEGRATION_TEST_ASSERT(msg1.has_value(), "Should have first message");
        INTEGRATION_TEST_ASSERT(msg1->size() == large_msg.size(),
                                "Large message size should match");

        auto msg2 = queue.dequeue();
        INTEGRATION_TEST_ASSERT(msg2.has_value(), "Should have second message");
        INTEGRATION_TEST_ASSERT(*msg2 == "Small message",
                                "Small message should match");
    }

    integration_test_fixture::cleanup_temp_file(temp_path);
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_queue_persistence_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== Queue Persistence Integration Tests ===" << std::endl;
    std::cout << "Testing Phase 2: Message Queue Recovery\n" << std::endl;

    std::cout << "\n--- Basic Persistence Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_queue_persistence_basic);
    RUN_INTEGRATION_TEST(test_queue_fifo_order_after_recovery);

    std::cout << "\n--- Queue Recovery Scenario Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_queue_recovery_ris_unavailable);
    RUN_INTEGRATION_TEST(test_queue_recovery_after_restart);
    RUN_INTEGRATION_TEST(test_queue_partial_delivery_recovery);

    std::cout << "\n--- Queue Edge Cases ---" << std::endl;
    RUN_INTEGRATION_TEST(test_queue_empty_recovery);
    RUN_INTEGRATION_TEST(test_queue_large_messages);

    std::cout << "\n=== Queue Persistence Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    if (passed + failed > 0) {
        double pass_rate = (passed * 100.0) / (passed + failed);
        std::cout << "Pass Rate: " << pass_rate << "%" << std::endl;
    }

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::integration::test

int main() {
    return pacs::bridge::integration::test::run_all_queue_persistence_tests();
}
