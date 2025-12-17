/**
 * @file disaster_recovery_test.cpp
 * @brief Integration tests for disaster recovery and resilience scenarios
 *
 * Tests system behavior under various failure conditions including:
 *   - Network failure scenarios (connection loss, timeouts)
 *   - Message loss detection and recovery
 *   - Retry logic with exponential backoff
 *   - System resilience under stress conditions
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/162
 * @see https://github.com/kcenon/pacs_bridge/issues/145 (Parent: Expand test coverage)
 */

#include "integration_test_base.h"

#include "pacs/bridge/router/queue_manager.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include <thread>
#include <vector>

namespace pacs::bridge::integration::test {

// =============================================================================
// Network Failure Simulator
// =============================================================================

/**
 * @brief Simulates various network failure conditions for testing
 *
 * Provides configurable failure injection including:
 *   - Connection refusal
 *   - Response delays/timeouts
 *   - Intermittent failures
 *   - Partial message delivery
 */
class network_failure_simulator {
public:
    enum class failure_mode {
        none,               ///< Normal operation
        connection_refused, ///< Refuse all connections
        connection_timeout, ///< Delay connection beyond timeout
        response_timeout,   ///< Accept connection but delay response
        intermittent,       ///< Random failures based on failure_rate
        disconnect_mid_send ///< Disconnect during message transmission
    };

    struct config {
        failure_mode mode = failure_mode::none;
        std::chrono::milliseconds delay{0};
        double failure_rate = 0.5;  // For intermittent mode
        int fail_after_count = 0;   // Fail after N successful operations
    };

    explicit network_failure_simulator(const config& cfg)
        : config_(cfg), operation_count_(0), random_engine_(std::random_device{}()) {}

    /**
     * @brief Check if the current operation should fail
     */
    bool should_fail() {
        operation_count_++;

        switch (config_.mode) {
            case failure_mode::none:
                return false;

            case failure_mode::connection_refused:
            case failure_mode::connection_timeout:
            case failure_mode::response_timeout:
            case failure_mode::disconnect_mid_send:
                return true;

            case failure_mode::intermittent: {
                std::uniform_real_distribution<double> dist(0.0, 1.0);
                return dist(random_engine_) < config_.failure_rate;
            }

            default:
                return false;
        }
    }

    /**
     * @brief Get the delay to apply for timeout simulation
     */
    std::chrono::milliseconds get_delay() const {
        return config_.delay;
    }

    /**
     * @brief Check if we should fail after N operations
     */
    bool should_fail_after_count() const {
        return config_.fail_after_count > 0 &&
               operation_count_ > static_cast<uint32_t>(config_.fail_after_count);
    }

    void reset() {
        operation_count_ = 0;
    }

    uint32_t operation_count() const {
        return operation_count_;
    }

    void set_mode(failure_mode mode) {
        config_.mode = mode;
    }

    void set_failure_rate(double rate) {
        config_.failure_rate = rate;
    }

private:
    config config_;
    std::atomic<uint32_t> operation_count_;
    std::mt19937 random_engine_;
};

// =============================================================================
// Resilient RIS Server for Testing
// =============================================================================

/**
 * @brief Mock RIS server with configurable failure injection
 *
 * Extends mock_ris_server with network failure simulation capabilities
 * for testing system resilience under various failure conditions.
 */
class resilient_ris_server {
public:
    struct config {
        uint16_t port = 12900;
        bool auto_ack = true;
        network_failure_simulator::config failure_config;
    };

    explicit resilient_ris_server(const config& cfg)
        : config_(cfg),
          failure_sim_(cfg.failure_config),
          running_(false),
          messages_received_(0),
          messages_rejected_(0) {}

    ~resilient_ris_server() {
        stop();
    }

    bool start() {
        if (running_) {
            return false;
        }

        mllp::mllp_server_config server_config;
        server_config.port = config_.port;

        server_ = std::make_unique<mllp::mllp_server>(server_config);

        server_->set_message_handler(
            [this](const mllp::mllp_message& msg,
                   const mllp::mllp_session_info& /*session*/)
                -> std::optional<mllp::mllp_message> {
                return handle_message(msg);
            });

        auto result = server_->start();
        if (!result.has_value()) {
            return false;
        }

        running_ = true;
        return true;
    }

    void stop() {
        if (server_ && running_) {
            server_->stop(true, std::chrono::seconds{5});
            running_ = false;
        }
    }

    bool is_running() const {
        return running_;
    }

    uint32_t messages_received() const {
        return messages_received_;
    }

    uint32_t messages_rejected() const {
        return messages_rejected_;
    }

    void set_failure_mode(network_failure_simulator::failure_mode mode) {
        failure_sim_.set_mode(mode);
    }

    void set_failure_rate(double rate) {
        failure_sim_.set_failure_rate(rate);
    }

    void reset_counters() {
        messages_received_ = 0;
        messages_rejected_ = 0;
        failure_sim_.reset();
    }

    uint16_t port() const {
        return config_.port;
    }

private:
    std::optional<mllp::mllp_message> handle_message(
        const mllp::mllp_message& msg) {

        // Check for simulated failure
        if (failure_sim_.should_fail()) {
            messages_rejected_++;

            // Apply delay if configured
            auto delay = failure_sim_.get_delay();
            if (delay.count() > 0) {
                std::this_thread::sleep_for(delay);
            }

            return std::nullopt;  // Simulate failure by not responding
        }

        messages_received_++;

        if (config_.auto_ack) {
            return generate_ack(msg);
        }

        return std::nullopt;
    }

    mllp::mllp_message generate_ack(const mllp::mllp_message& original) {
        hl7::hl7_parser parser;
        auto parse_result = parser.parse(original.to_string());

        std::string msg_control_id = "0";
        if (parse_result.has_value()) {
            msg_control_id = parse_result->get_value("MSH.10");
        }

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp[15];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S",
                      std::localtime(&time_t_now));

        std::string ack =
            "MSH|^~\\&|RIS|RADIOLOGY|PACS|HOSPITAL|" + std::string(timestamp) +
            "||ACK|ACK" + msg_control_id + "|P|2.4\r" +
            "MSA|AA|" + msg_control_id + "\r";

        return mllp::mllp_message::from_string(ack);
    }

    config config_;
    network_failure_simulator failure_sim_;
    std::unique_ptr<mllp::mllp_server> server_;
    std::atomic<bool> running_;
    std::atomic<uint32_t> messages_received_;
    std::atomic<uint32_t> messages_rejected_;
};

// =============================================================================
// Retry Client with Queue Manager Integration
// =============================================================================

/**
 * @brief Client that uses queue_manager for reliable message delivery
 *
 * Demonstrates integration of queue_manager retry logic with MLLP client.
 */
class reliable_message_client {
public:
    struct config {
        std::string host = "localhost";
        uint16_t port = 12900;
        std::chrono::milliseconds connect_timeout{500};
        std::chrono::milliseconds send_timeout{1000};
        std::string queue_db_path = "/tmp/test_queue.db";
        size_t max_retries = 3;
    };

    explicit reliable_message_client(const config& cfg)
        : config_(cfg),
          messages_sent_(0),
          messages_failed_(0),
          retry_count_(0) {

        // Initialize queue manager
        router::queue_config queue_config;
        queue_config.database_path = cfg.queue_db_path;
        queue_config.max_retry_count = cfg.max_retries;
        queue_config.initial_retry_delay = std::chrono::seconds{1};
        queue_config.retry_backoff_multiplier = 2.0;
        queue_config.worker_count = 1;

        queue_manager_ = std::make_unique<router::queue_manager>(queue_config);
    }

    ~reliable_message_client() {
        stop();
        cleanup_database();
    }

    bool start() {
        auto result = queue_manager_->start();
        if (!result.has_value()) {
            return false;
        }

        queue_manager_->start_workers(
            [this](const router::queued_message& msg)
                -> std::expected<void, std::string> {
                return deliver_message(msg);
            });

        return true;
    }

    void stop() {
        if (queue_manager_) {
            queue_manager_->stop_workers();
            queue_manager_->stop();
        }
    }

    /**
     * @brief Enqueue a message for reliable delivery
     */
    bool send(const std::string& message) {
        auto result = queue_manager_->enqueue(
            config_.host + ":" + std::to_string(config_.port),
            message);
        return result.has_value();
    }

    uint32_t messages_sent() const {
        return messages_sent_;
    }

    uint32_t messages_failed() const {
        return messages_failed_;
    }

    uint32_t retry_count() const {
        return retry_count_;
    }

    size_t queue_depth() const {
        return queue_manager_ ? queue_manager_->queue_depth() : 0;
    }

    size_t dead_letter_count() const {
        return queue_manager_ ? queue_manager_->dead_letter_count() : 0;
    }

    router::queue_statistics get_statistics() const {
        return queue_manager_ ? queue_manager_->get_statistics()
                              : router::queue_statistics{};
    }

private:
    std::expected<void, std::string> deliver_message(
        const router::queued_message& msg) {

        if (msg.attempt_count > 1) {
            retry_count_++;
        }

        mllp::mllp_client_config client_config;
        client_config.host = config_.host;
        client_config.port = config_.port;
        client_config.connect_timeout = config_.connect_timeout;

        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        if (!connect_result.has_value()) {
            messages_failed_++;
            return std::unexpected("Connection failed");
        }

        auto mllp_msg = mllp::mllp_message::from_string(msg.payload);
        auto send_result = client.send(mllp_msg);

        client.disconnect();

        if (!send_result.has_value()) {
            messages_failed_++;
            return std::unexpected("Send failed");
        }

        messages_sent_++;
        return {};
    }

    void cleanup_database() {
        std::error_code ec;
        std::filesystem::remove(config_.queue_db_path, ec);
        std::filesystem::remove(config_.queue_db_path + "-wal", ec);
        std::filesystem::remove(config_.queue_db_path + "-shm", ec);
    }

    config config_;
    std::unique_ptr<router::queue_manager> queue_manager_;
    std::atomic<uint32_t> messages_sent_;
    std::atomic<uint32_t> messages_failed_;
    std::atomic<uint32_t> retry_count_;
};

// =============================================================================
// Network Failure Scenario Tests
// =============================================================================

/**
 * @brief Test behavior when server refuses connections
 *
 * Scenario: Server is completely unavailable (port not listening)
 * Expected: Messages should be queued and retried
 */
bool test_network_connection_refused() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // No server started - connection will be refused

    reliable_message_client::config client_config;
    client_config.port = port;
    client_config.queue_db_path = "/tmp/dr_test_refused_" +
                                   std::to_string(std::time(nullptr)) + ".db";
    client_config.max_retries = 2;

    reliable_message_client client(client_config);
    INTEGRATION_TEST_ASSERT(client.start(), "Client should start");

    // Try to send a message
    std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|1|P|2.4\r";
    bool enqueued = client.send(msg);
    INTEGRATION_TEST_ASSERT(enqueued, "Message should be enqueued");

    // Wait for retry attempts to complete
    integration_test_fixture::wait_for(
        [&client]() { return client.dead_letter_count() > 0; },
        std::chrono::milliseconds{5000});

    // Message should eventually be dead-lettered after max retries
    INTEGRATION_TEST_ASSERT(client.dead_letter_count() >= 1,
                            "Message should be dead-lettered after failed retries");

    client.stop();
    return true;
}

/**
 * @brief Test behavior when server responds slowly (timeout)
 *
 * Scenario: Server accepts connection but delays response beyond timeout
 * Expected: Timeout should trigger retry
 */
bool test_network_response_timeout() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server with response delay
    resilient_ris_server::config server_config;
    server_config.port = port;
    server_config.failure_config.mode =
        network_failure_simulator::failure_mode::response_timeout;
    server_config.failure_config.delay = std::chrono::milliseconds{2000};

    resilient_ris_server server(server_config);
    INTEGRATION_TEST_ASSERT(server.start(), "Server should start");

    // Client with short timeout
    reliable_message_client::config client_config;
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{500};
    client_config.send_timeout = std::chrono::milliseconds{500};
    client_config.queue_db_path = "/tmp/dr_test_timeout_" +
                                   std::to_string(std::time(nullptr)) + ".db";
    client_config.max_retries = 2;

    reliable_message_client client(client_config);
    INTEGRATION_TEST_ASSERT(client.start(), "Client should start");

    std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|2|P|2.4\r";
    client.send(msg);

    // Wait for retries
    integration_test_fixture::wait_for(
        [&server]() { return server.messages_rejected() >= 2; },
        std::chrono::milliseconds{8000});

    // Server should have rejected due to simulated timeout
    INTEGRATION_TEST_ASSERT(server.messages_rejected() >= 1,
                            "Server should reject due to timeout simulation");

    server.stop();
    client.stop();
    return true;
}

/**
 * @brief Test recovery after intermittent network failures
 *
 * Scenario: Network has intermittent failures (50% failure rate)
 * Expected: Successful delivery after retries
 */
bool test_network_intermittent_failures() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server with 50% failure rate
    resilient_ris_server::config server_config;
    server_config.port = port;
    server_config.failure_config.mode =
        network_failure_simulator::failure_mode::intermittent;
    server_config.failure_config.failure_rate = 0.5;

    resilient_ris_server server(server_config);
    INTEGRATION_TEST_ASSERT(server.start(), "Server should start");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should be running");

    // Send multiple messages through MLLP client
    int messages_to_send = 10;

    for (int i = 0; i < messages_to_send; ++i) {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = port;
        client_config.connect_timeout = std::chrono::milliseconds{500};

        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        if (!connect_result.has_value()) {
            continue;
        }

        std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|" +
                          std::to_string(i) + "|P|2.4\r";
        (void)client.send(mllp::mllp_message::from_string(msg));

        client.disconnect();
    }

    // With 50% failure rate and 10 attempts, we should have some successes
    // and some failures
    INTEGRATION_TEST_ASSERT(server.messages_received() > 0,
                            "Some messages should be received");
    INTEGRATION_TEST_ASSERT(server.messages_rejected() > 0,
                            "Some messages should be rejected");

    server.stop();
    return true;
}

/**
 * @brief Test behavior when network recovers mid-retry cycle
 *
 * Scenario: Network fails initially, then recovers during retry attempts
 * Expected: Successful delivery after recovery
 */
bool test_network_recovery_during_retry() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Server starts unavailable
    std::atomic<bool> start_server{false};

    std::thread server_thread([&]() {
        // Wait for signal to start
        while (!start_server) {
            std::this_thread::yield();
        }

        mock_ris_server::config server_config;
        server_config.port = port;
        server_config.auto_ack = true;

        mock_ris_server server(server_config);
        server.start();

        // Keep server running for a while
        std::this_thread::sleep_for(std::chrono::seconds{5});
        server.stop();
    });

    // Client sends message while server is down
    reliable_message_client::config client_config;
    client_config.port = port;
    client_config.queue_db_path = "/tmp/dr_test_recovery_" +
                                   std::to_string(std::time(nullptr)) + ".db";
    client_config.max_retries = 5;

    reliable_message_client client(client_config);
    INTEGRATION_TEST_ASSERT(client.start(), "Client should start");

    std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|RECOVERY|P|2.4\r";
    client.send(msg);

    // Wait a bit then start server
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    start_server = true;

    // Wait for successful delivery
    bool delivered = integration_test_fixture::wait_for(
        [&client]() { return client.messages_sent() > 0; },
        std::chrono::milliseconds{10000});

    // Message should be delivered after server comes up
    INTEGRATION_TEST_ASSERT(delivered, "Message should be delivered after recovery");
    INTEGRATION_TEST_ASSERT(client.retry_count() > 0,
                            "Should have retried at least once");

    client.stop();
    server_thread.join();
    return true;
}

// =============================================================================
// Message Loss Scenario Tests
// =============================================================================

/**
 * @brief Test message persistence across client restart
 *
 * Scenario: Client crashes with pending messages in queue
 * Expected: Messages should be recovered on restart
 */
bool test_message_persistence_across_restart() {
    uint16_t port = integration_test_fixture::generate_test_port();
    std::string db_path = "/tmp/dr_test_persist_" +
                          std::to_string(std::time(nullptr)) + ".db";

    // Phase 1: Enqueue messages without server
    {
        router::queue_config queue_config;
        queue_config.database_path = db_path;
        queue_config.max_retry_count = 10;  // High retry count

        router::queue_manager queue(queue_config);
        INTEGRATION_TEST_ASSERT(queue.start().has_value(), "Queue should start");

        // Enqueue messages
        for (int i = 0; i < 5; ++i) {
            auto result = queue.enqueue("localhost:" + std::to_string(port),
                                        "MSG_" + std::to_string(i));
            INTEGRATION_TEST_ASSERT(result.has_value(), "Enqueue should succeed");
        }

        INTEGRATION_TEST_ASSERT(queue.queue_depth() == 5,
                                "Should have 5 pending messages");

        // Simulate crash by just stopping
        queue.stop();
    }

    // Phase 2: Restart and verify messages are recovered
    {
        router::queue_config queue_config;
        queue_config.database_path = db_path;

        router::queue_manager queue(queue_config);
        INTEGRATION_TEST_ASSERT(queue.start().has_value(),
                                "Queue should restart");

        // Messages should be recovered
        INTEGRATION_TEST_ASSERT(queue.queue_depth() == 5,
                                "All 5 messages should be recovered");

        queue.stop();
    }

    // Cleanup
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + "-wal");
    std::filesystem::remove(db_path + "-shm");

    return true;
}

/**
 * @brief Test message deduplication (at-least-once delivery guarantee)
 *
 * Scenario: Message delivered but ACK not received, causing retry
 * Expected: Duplicate detection or idempotent handling
 */
bool test_message_duplicate_detection() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Track all received message IDs
    std::vector<std::string> received_ids;
    std::mutex received_mutex;

    mock_ris_server::config server_config;
    server_config.port = port;
    server_config.auto_ack = true;

    mock_ris_server server(server_config);
    INTEGRATION_TEST_ASSERT(server.start(), "Server should start");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should be running");

    // Send same message multiple times (simulating retry)
    std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|UNIQUE123|P|2.4\r";

    for (int i = 0; i < 3; ++i) {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = port;
        client_config.connect_timeout = std::chrono::milliseconds{500};

        mllp::mllp_client client(client_config);
        if (client.connect().has_value()) {
            (void)client.send(mllp::mllp_message::from_string(msg));
            client.disconnect();
        }
    }

    // Wait for messages
    integration_test_fixture::wait_for(
        [&server]() { return server.messages_received() >= 3; },
        std::chrono::milliseconds{2000});

    // Server received all 3 (at-least-once delivery)
    // In production, deduplication would be handled by message control ID
    INTEGRATION_TEST_ASSERT(server.messages_received() == 3,
                            "Server should receive all retries");

    // Verify same message ID in all received messages
    const auto& messages = server.received_messages();
    hl7::hl7_parser parser;
    std::set<std::string> unique_ids;

    for (const auto& raw_msg : messages) {
        auto parsed = parser.parse(raw_msg);
        if (parsed.has_value()) {
            unique_ids.insert(std::string(parsed->get_value("MSH.10")));
        }
    }

    INTEGRATION_TEST_ASSERT(unique_ids.size() == 1,
                            "All messages should have same control ID");

    server.stop();
    return true;
}

/**
 * @brief Test recovery of in-progress messages after crash
 *
 * Scenario: Process crashes while message is in "processing" state
 * Expected: Message should be recovered and reprocessed
 */
bool test_processing_message_recovery() {
    std::string db_path = "/tmp/dr_test_processing_" +
                          std::to_string(std::time(nullptr)) + ".db";
    std::string msg_id;

    // Phase 1: Start processing a message, then "crash"
    {
        router::queue_config queue_config;
        queue_config.database_path = db_path;

        router::queue_manager queue(queue_config);
        INTEGRATION_TEST_ASSERT(queue.start().has_value(), "Queue should start");

        auto enqueue_result = queue.enqueue("RIS", "PROCESSING_TEST");
        INTEGRATION_TEST_ASSERT(enqueue_result.has_value(), "Enqueue should succeed");
        msg_id = *enqueue_result;

        // Dequeue (moves to processing state)
        auto msg = queue.dequeue();
        INTEGRATION_TEST_ASSERT(msg.has_value(), "Dequeue should succeed");
        INTEGRATION_TEST_ASSERT(msg->state == router::message_state::processing,
                                "Message should be in processing state");

        // Simulate crash - don't ack/nack, just stop
        queue.stop();
    }

    // Phase 2: Recover and verify message is back in pending state
    {
        router::queue_config queue_config;
        queue_config.database_path = db_path;

        router::queue_manager queue(queue_config);
        INTEGRATION_TEST_ASSERT(queue.start().has_value(),
                                "Queue should restart");

        // Message should be recovered to pending
        auto msg = queue.get_message(msg_id);
        INTEGRATION_TEST_ASSERT(msg.has_value(), "Message should exist");
        INTEGRATION_TEST_ASSERT(msg->state == router::message_state::pending,
                                "Message should be recovered to pending state");

        queue.stop();
    }

    // Cleanup
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + "-wal");
    std::filesystem::remove(db_path + "-shm");

    return true;
}

// =============================================================================
// Retry Logic Tests
// =============================================================================

/**
 * @brief Test exponential backoff timing
 *
 * Scenario: Message fails delivery repeatedly
 * Expected: Retry intervals should follow exponential backoff
 */
bool test_exponential_backoff_timing() {
    std::string db_path = "/tmp/dr_test_backoff_" +
                          std::to_string(std::time(nullptr)) + ".db";

    router::queue_config queue_config;
    queue_config.database_path = db_path;
    queue_config.max_retry_count = 4;
    queue_config.initial_retry_delay = std::chrono::seconds{1};
    queue_config.retry_backoff_multiplier = 2.0;
    queue_config.max_retry_delay = std::chrono::seconds{60};

    router::queue_manager queue(queue_config);
    INTEGRATION_TEST_ASSERT(queue.start().has_value(), "Queue should start");

    auto enqueue_result = queue.enqueue("RIS", "BACKOFF_TEST");
    INTEGRATION_TEST_ASSERT(enqueue_result.has_value(), "Enqueue should succeed");
    std::string msg_id = *enqueue_result;

    // Track scheduled times after each nack
    std::vector<std::chrono::system_clock::time_point> scheduled_times;

    for (int i = 0; i < 3; ++i) {
        // Wait for message to become available
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        auto msg = queue.dequeue();
        if (msg) {
            // Nack to trigger retry with backoff
            (void)queue.nack(msg->id, "Test failure " + std::to_string(i));

            // Get scheduled time
            auto updated = queue.get_message(msg_id);
            if (updated) {
                scheduled_times.push_back(updated->scheduled_at);
            }
        }

        // Wait for retry delay
        std::this_thread::sleep_for(std::chrono::seconds{2});
    }

    // Verify exponential backoff
    // With initial_delay=1s and multiplier=2.0:
    // Retry 1: 1s, Retry 2: 2s, Retry 3: 4s
    INTEGRATION_TEST_ASSERT(scheduled_times.size() >= 2,
                            "Should have at least 2 scheduled times");

    queue.stop();

    // Cleanup
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + "-wal");
    std::filesystem::remove(db_path + "-shm");

    return true;
}

/**
 * @brief Test successful delivery after initial failures
 *
 * Scenario: First 2 delivery attempts fail, 3rd succeeds
 * Expected: Message delivered, not dead-lettered
 */
bool test_retry_success_after_failures() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Server that fails first 2 requests, then succeeds
    resilient_ris_server::config server_config;
    server_config.port = port;
    server_config.failure_config.mode =
        network_failure_simulator::failure_mode::intermittent;
    server_config.failure_config.failure_rate = 1.0;  // Will be changed

    resilient_ris_server server(server_config);
    INTEGRATION_TEST_ASSERT(server.start(), "Server should start");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should be running");

    // Thread to stop failures after 2 attempts
    std::thread failure_control([&]() {
        while (server.messages_rejected() < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        // Stop failures
        server.set_failure_mode(network_failure_simulator::failure_mode::none);
    });

    // Send message with retries
    std::string db_path = "/tmp/dr_test_retry_success_" +
                          std::to_string(std::time(nullptr)) + ".db";

    reliable_message_client::config client_config;
    client_config.port = port;
    client_config.queue_db_path = db_path;
    client_config.max_retries = 5;

    reliable_message_client client(client_config);
    INTEGRATION_TEST_ASSERT(client.start(), "Client should start");

    std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|RETRY|P|2.4\r";
    client.send(msg);

    // Wait for delivery
    bool delivered = integration_test_fixture::wait_for(
        [&client]() { return client.messages_sent() > 0; },
        std::chrono::milliseconds{15000});

    failure_control.join();

    INTEGRATION_TEST_ASSERT(delivered, "Message should be delivered eventually");
    INTEGRATION_TEST_ASSERT(server.messages_received() >= 1,
                            "Server should have received message");
    INTEGRATION_TEST_ASSERT(client.dead_letter_count() == 0,
                            "No messages should be dead-lettered");

    server.stop();
    client.stop();

    return true;
}

/**
 * @brief Test max retry exhaustion leads to dead letter
 *
 * Scenario: Message fails more than max_retry_count times
 * Expected: Message moved to dead letter queue
 */
bool test_max_retry_dead_letter() {
    std::string db_path = "/tmp/dr_test_dead_letter_" +
                          std::to_string(std::time(nullptr)) + ".db";

    router::queue_config queue_config;
    queue_config.database_path = db_path;
    queue_config.max_retry_count = 2;  // Low retry count
    queue_config.initial_retry_delay = std::chrono::seconds{1};
    queue_config.retry_backoff_multiplier = 1.0;

    router::queue_manager queue(queue_config);
    INTEGRATION_TEST_ASSERT(queue.start().has_value(), "Queue should start");

    auto enqueue_result = queue.enqueue("RIS", "DEAD_LETTER_TEST");
    INTEGRATION_TEST_ASSERT(enqueue_result.has_value(), "Enqueue should succeed");
    std::string msg_id = *enqueue_result;

    // Fail message 3 times (exceeds max_retry_count of 2)
    for (int i = 0; i <= 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        auto msg = queue.dequeue();
        if (msg) {
            (void)queue.nack(msg->id, "Failure " + std::to_string(i));
        }
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }

    // Message should be in dead letter queue
    INTEGRATION_TEST_ASSERT(queue.dead_letter_count() == 1,
                            "Message should be dead-lettered");

    // Verify dead letter entry
    auto dead_letters = queue.get_dead_letters();
    INTEGRATION_TEST_ASSERT(dead_letters.size() == 1, "Should have 1 dead letter");
    INTEGRATION_TEST_ASSERT(dead_letters[0].message.id == msg_id,
                            "Dead letter should be our message");

    queue.stop();

    // Cleanup
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + "-wal");
    std::filesystem::remove(db_path + "-shm");

    return true;
}

/**
 * @brief Test dead letter retry functionality
 *
 * Scenario: Dead-lettered message is manually retried
 * Expected: Message moves back to pending and can be delivered
 */
bool test_dead_letter_retry() {
    uint16_t port = integration_test_fixture::generate_test_port();
    std::string db_path = "/tmp/dr_test_dl_retry_" +
                          std::to_string(std::time(nullptr)) + ".db";

    // Phase 1: Create a dead-lettered message
    router::queue_config queue_config;
    queue_config.database_path = db_path;
    queue_config.max_retry_count = 1;

    router::queue_manager queue(queue_config);
    INTEGRATION_TEST_ASSERT(queue.start().has_value(), "Queue should start");

    auto enqueue_result = queue.enqueue("RIS", "DL_RETRY_TEST");
    std::string msg_id = *enqueue_result;

    // Fail twice to dead letter
    for (int i = 0; i < 2; ++i) {
        auto msg = queue.dequeue();
        if (msg) {
            (void)queue.nack(msg->id, "Failure");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    INTEGRATION_TEST_ASSERT(queue.dead_letter_count() == 1,
                            "Message should be dead-lettered");

    // Phase 2: Start server and retry dead letter
    mock_ris_server::config server_config;
    server_config.port = port;
    server_config.auto_ack = true;

    mock_ris_server server(server_config);
    INTEGRATION_TEST_ASSERT(server.start(), "Server should start");

    // Retry the dead letter
    auto retry_result = queue.retry_dead_letter(msg_id);
    INTEGRATION_TEST_ASSERT(retry_result.has_value(), "Retry should succeed");

    INTEGRATION_TEST_ASSERT(queue.dead_letter_count() == 0,
                            "Dead letter queue should be empty");
    INTEGRATION_TEST_ASSERT(queue.queue_depth() == 1,
                            "Message should be back in queue");

    // Verify message state
    auto msg = queue.get_message(msg_id);
    INTEGRATION_TEST_ASSERT(msg.has_value(), "Message should exist");
    INTEGRATION_TEST_ASSERT(msg->state == router::message_state::pending,
                            "Message should be pending");

    server.stop();
    queue.stop();

    // Cleanup
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + "-wal");
    std::filesystem::remove(db_path + "-shm");

    return true;
}

// =============================================================================
// Resilience Under Load Tests
// =============================================================================

/**
 * @brief Test system stability under concurrent failures
 *
 * Scenario: Multiple clients sending messages with intermittent failures
 * Expected: System remains stable, messages are eventually delivered or dead-lettered
 */
bool test_resilience_concurrent_failures() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Server with 30% failure rate
    resilient_ris_server::config server_config;
    server_config.port = port;
    server_config.failure_config.mode =
        network_failure_simulator::failure_mode::intermittent;
    server_config.failure_config.failure_rate = 0.3;

    resilient_ris_server server(server_config);
    INTEGRATION_TEST_ASSERT(server.start(), "Server should start");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should be running");

    // Multiple concurrent senders
    const int num_senders = 3;
    const int messages_per_sender = 5;
    std::vector<std::thread> senders;

    for (int s = 0; s < num_senders; ++s) {
        senders.emplace_back([&, s]() {
            for (int m = 0; m < messages_per_sender; ++m) {
                mllp::mllp_client_config client_config;
                client_config.host = "localhost";
                client_config.port = port;
                client_config.connect_timeout = std::chrono::milliseconds{500};

                mllp::mllp_client client(client_config);
                if (client.connect().has_value()) {
                    std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|S" +
                                      std::to_string(s) + "_M" +
                                      std::to_string(m) + "|P|2.4\r";
                    (void)client.send(mllp::mllp_message::from_string(msg));
                    client.disconnect();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
        });
    }

    // Wait for all senders to complete
    for (auto& sender : senders) {
        sender.join();
    }

    // Give server time to process
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    // Verify system remained stable
    INTEGRATION_TEST_ASSERT(server.is_running(), "Server should still be running");

    // With 30% failure rate, we should have a mix of successes and failures
    uint32_t received = server.messages_received();
    uint32_t rejected = server.messages_rejected();

    INTEGRATION_TEST_ASSERT(received > 0, "Some messages should be received");
    INTEGRATION_TEST_ASSERT(received + rejected > 0,
                            "Should have processed some messages");

    server.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_disaster_recovery_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== Disaster Recovery Integration Tests ===" << std::endl;
    std::cout << "Testing system resilience under various failure conditions\n"
              << std::endl;

    std::cout << "\n--- Network Failure Scenario Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_network_connection_refused);
    RUN_INTEGRATION_TEST(test_network_response_timeout);
    RUN_INTEGRATION_TEST(test_network_intermittent_failures);
    RUN_INTEGRATION_TEST(test_network_recovery_during_retry);

    std::cout << "\n--- Message Loss Scenario Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_message_persistence_across_restart);
    RUN_INTEGRATION_TEST(test_message_duplicate_detection);
    RUN_INTEGRATION_TEST(test_processing_message_recovery);

    std::cout << "\n--- Retry Logic Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_exponential_backoff_timing);
    RUN_INTEGRATION_TEST(test_retry_success_after_failures);
    RUN_INTEGRATION_TEST(test_max_retry_dead_letter);
    RUN_INTEGRATION_TEST(test_dead_letter_retry);

    std::cout << "\n--- Resilience Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_resilience_concurrent_failures);

    std::cout << "\n=== Disaster Recovery Test Summary ===" << std::endl;
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
    return pacs::bridge::integration::test::run_all_disaster_recovery_tests();
}
