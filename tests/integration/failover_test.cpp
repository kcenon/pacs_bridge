/**
 * @file failover_test.cpp
 * @brief Integration tests for message routing failover functionality
 *
 * Tests the failover routing scenarios when a primary RIS fails.
 * Verifies that messages are routed to backup systems and that
 * routing returns to primary when it recovers.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/29
 * @see https://github.com/kcenon/pacs_bridge/issues/28 (Outbound Message Router)
 */

#include "integration_test_base.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace pacs::bridge::integration::test {

// =============================================================================
// Failover Router Simulator
// =============================================================================

/**
 * @brief Multi-destination router with failover support
 *
 * Routes messages to primary RIS, automatically failing over to backup
 * RIS when primary is unavailable. Supports automatic failback to primary
 * when it recovers.
 */
class failover_router {
public:
    struct destination {
        std::string name;
        uint16_t port;
        bool is_primary;
    };

    struct config {
        std::vector<destination> destinations;
        std::chrono::milliseconds connect_timeout{500};
        std::chrono::milliseconds health_check_interval{1000};
        bool auto_failback{true};
    };

    explicit failover_router(const config& cfg)
        : config_(cfg),
          running_(false),
          primary_healthy_(true),
          messages_to_primary_(0),
          messages_to_backup_(0),
          failover_count_(0) {}

    ~failover_router() {
        stop();
    }

    void start() {
        running_ = true;
        if (config_.auto_failback) {
            health_check_thread_ =
                std::thread(&failover_router::health_check_loop, this);
        }
    }

    void stop() {
        running_ = false;
        if (health_check_thread_.joinable()) {
            health_check_thread_.join();
        }
    }

    /**
     * @brief Route a message using failover logic
     * @return true if message was delivered to any destination
     */
    bool route_message(const std::string& message) {
        // Try primary first if it's considered healthy
        if (primary_healthy_) {
            for (const auto& dest : config_.destinations) {
                if (dest.is_primary && try_send(dest.port, message)) {
                    messages_to_primary_++;
                    return true;
                }
            }
            // Primary failed
            primary_healthy_ = false;
            failover_count_++;
        }

        // Try backup destinations
        for (const auto& dest : config_.destinations) {
            if (!dest.is_primary && try_send(dest.port, message)) {
                messages_to_backup_++;
                return true;
            }
        }

        return false;
    }

    bool is_primary_healthy() const {
        return primary_healthy_;
    }

    void set_primary_healthy(bool healthy) {
        primary_healthy_ = healthy;
    }

    uint32_t messages_to_primary() const {
        return messages_to_primary_;
    }

    uint32_t messages_to_backup() const {
        return messages_to_backup_;
    }

    uint32_t failover_count() const {
        return failover_count_;
    }

    void reset_counters() {
        messages_to_primary_ = 0;
        messages_to_backup_ = 0;
        failover_count_ = 0;
    }

private:
    bool try_send(uint16_t port, const std::string& message) {
        mllp::mllp_client_config client_config;
        client_config.host = "localhost";
        client_config.port = port;
        client_config.connect_timeout = config_.connect_timeout;

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

    void health_check_loop() {
        while (running_) {
            std::this_thread::sleep_for(config_.health_check_interval);

            if (!primary_healthy_) {
                // Check if primary is back online
                for (const auto& dest : config_.destinations) {
                    if (dest.is_primary) {
                        mllp::mllp_client_config client_config;
                        client_config.host = "localhost";
                        client_config.port = dest.port;
                        client_config.connect_timeout =
                            config_.connect_timeout;

                        mllp::mllp_client client(client_config);
                        if (client.connect().has_value()) {
                            client.disconnect();
                            primary_healthy_ = true;
                        }
                        break;
                    }
                }
            }
        }
    }

    config config_;
    std::atomic<bool> running_;
    std::atomic<bool> primary_healthy_;
    std::atomic<uint32_t> messages_to_primary_;
    std::atomic<uint32_t> messages_to_backup_;
    std::atomic<uint32_t> failover_count_;
    std::thread health_check_thread_;
};

// =============================================================================
// Basic Failover Tests
// =============================================================================

/**
 * @brief Test routing to primary RIS when available
 *
 * Verifies that messages are routed to the primary RIS under normal conditions.
 */
bool test_failover_route_to_primary() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // Setup primary and backup RIS servers
    mock_ris_server::config primary_config;
    primary_config.port = primary_port;
    primary_config.auto_ack = true;

    mock_ris_server::config backup_config;
    backup_config.port = backup_port;
    backup_config.auto_ack = true;

    mock_ris_server primary_ris(primary_config);
    mock_ris_server backup_ris(backup_config);

    INTEGRATION_TEST_ASSERT(primary_ris.start(),
                            "Failed to start primary RIS");
    INTEGRATION_TEST_ASSERT(backup_ris.start(), "Failed to start backup RIS");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Setup failover router
    failover_router::config router_config;
    router_config.destinations = {
        {"PRIMARY", primary_port, true},
        {"BACKUP", backup_port, false}};
    router_config.auto_failback = false;

    failover_router router(router_config);
    router.start();

    // Send messages
    for (int i = 0; i < 5; ++i) {
        std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|" +
                          std::to_string(i) + "|P|2.4\r";
        bool result = router.route_message(msg);
        INTEGRATION_TEST_ASSERT(result, "Message should be delivered");
    }

    // Verify all messages went to primary
    bool received = integration_test_fixture::wait_for(
        [&primary_ris]() { return primary_ris.messages_received() >= 5; },
        std::chrono::milliseconds{2000});

    INTEGRATION_TEST_ASSERT(received, "Primary should receive all messages");
    INTEGRATION_TEST_ASSERT(router.messages_to_primary() == 5,
                            "Should send 5 to primary");
    INTEGRATION_TEST_ASSERT(router.messages_to_backup() == 0,
                            "Should send 0 to backup");
    INTEGRATION_TEST_ASSERT(backup_ris.messages_received() == 0,
                            "Backup should receive nothing");

    router.stop();
    primary_ris.stop();
    backup_ris.stop();
    return true;
}

/**
 * @brief Test failover to backup when primary is unavailable
 *
 * Scenario:
 * 1. Primary RIS fails
 * 2. Messages should be routed to backup RIS
 */
bool test_failover_to_backup() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // Only start backup RIS (primary is "failed")
    mock_ris_server::config backup_config;
    backup_config.port = backup_port;
    backup_config.auto_ack = true;

    mock_ris_server backup_ris(backup_config);
    INTEGRATION_TEST_ASSERT(backup_ris.start(), "Failed to start backup RIS");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Setup failover router
    failover_router::config router_config;
    router_config.destinations = {
        {"PRIMARY", primary_port, true},
        {"BACKUP", backup_port, false}};
    router_config.auto_failback = false;

    failover_router router(router_config);
    router.start();

    // Send messages - should fail over to backup
    for (int i = 0; i < 5; ++i) {
        std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|" +
                          std::to_string(i) + "|P|2.4\r";
        bool result = router.route_message(msg);
        INTEGRATION_TEST_ASSERT(result, "Message should be delivered to backup");
    }

    // Verify messages went to backup
    bool received = integration_test_fixture::wait_for(
        [&backup_ris]() { return backup_ris.messages_received() >= 5; },
        std::chrono::milliseconds{2000});

    INTEGRATION_TEST_ASSERT(received, "Backup should receive all messages");
    INTEGRATION_TEST_ASSERT(router.messages_to_backup() == 5,
                            "Should send 5 to backup");
    INTEGRATION_TEST_ASSERT(router.failover_count() >= 1,
                            "Should have at least 1 failover");

    router.stop();
    backup_ris.stop();
    return true;
}

/**
 * @brief Test failback to primary when it recovers
 *
 * Scenario:
 * 1. Primary fails, messages go to backup
 * 2. Primary recovers
 * 3. New messages should go to primary again
 */
bool test_failover_and_failback() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // Setup backup RIS (primary starts down)
    mock_ris_server::config backup_config;
    backup_config.port = backup_port;
    backup_config.auto_ack = true;

    mock_ris_server backup_ris(backup_config);
    INTEGRATION_TEST_ASSERT(backup_ris.start(), "Failed to start backup RIS");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Setup failover router with health checks
    failover_router::config router_config;
    router_config.destinations = {
        {"PRIMARY", primary_port, true},
        {"BACKUP", backup_port, false}};
    router_config.health_check_interval = std::chrono::milliseconds{200};
    router_config.auto_failback = true;

    failover_router router(router_config);
    router.start();

    // Phase 1: Send messages while primary is down
    for (int i = 0; i < 3; ++i) {
        std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|" +
                          std::to_string(i) + "|P|2.4\r";
        router.route_message(msg);
    }

    // Verify backup received messages
    bool backup_received = integration_test_fixture::wait_for(
        [&backup_ris]() { return backup_ris.messages_received() >= 3; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(backup_received, "Backup should receive messages");

    // Phase 2: Start primary RIS (recovery)
    mock_ris_server::config primary_config;
    primary_config.port = primary_port;
    primary_config.auto_ack = true;

    mock_ris_server primary_ris(primary_config);
    INTEGRATION_TEST_ASSERT(primary_ris.start(), "Failed to start primary RIS");

    // Wait for health check to detect primary recovery
    bool failback = integration_test_fixture::wait_for(
        [&router]() { return router.is_primary_healthy(); },
        std::chrono::milliseconds{3000});
    INTEGRATION_TEST_ASSERT(failback, "Primary should be detected as healthy");

    // Phase 3: Send more messages - should go to primary
    for (int i = 3; i < 6; ++i) {
        std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|" +
                          std::to_string(i) + "|P|2.4\r";
        bool result = router.route_message(msg);
        INTEGRATION_TEST_ASSERT(result, "Message should be delivered");
    }

    // Verify primary received new messages
    bool primary_received = integration_test_fixture::wait_for(
        [&primary_ris]() { return primary_ris.messages_received() >= 3; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(primary_received,
                            "Primary should receive messages after recovery");

    // Verify statistics
    INTEGRATION_TEST_ASSERT(backup_ris.messages_received() == 3,
                            "Backup should have 3 messages");
    INTEGRATION_TEST_ASSERT(primary_ris.messages_received() == 3,
                            "Primary should have 3 messages");

    router.stop();
    primary_ris.stop();
    backup_ris.stop();
    return true;
}

// =============================================================================
// Failover Edge Cases
// =============================================================================

/**
 * @brief Test failover when both primary and backup fail
 *
 * Verifies graceful failure when no destinations are available.
 */
bool test_failover_all_destinations_fail() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // No servers started - both destinations unavailable

    failover_router::config router_config;
    router_config.destinations = {
        {"PRIMARY", primary_port, true},
        {"BACKUP", backup_port, false}};
    router_config.auto_failback = false;

    failover_router router(router_config);
    router.start();

    // Try to send message
    std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|1|P|2.4\r";
    bool result = router.route_message(msg);

    INTEGRATION_TEST_ASSERT(!result,
                            "Should fail when no destinations available");
    INTEGRATION_TEST_ASSERT(router.messages_to_primary() == 0,
                            "No messages to primary");
    INTEGRATION_TEST_ASSERT(router.messages_to_backup() == 0,
                            "No messages to backup");
    INTEGRATION_TEST_ASSERT(router.failover_count() >= 1,
                            "Should record failover attempt");

    router.stop();
    return true;
}

/**
 * @brief Test failover with multiple backup destinations
 *
 * Scenario: Primary fails, first backup fails, second backup succeeds.
 */
bool test_failover_multiple_backups() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup1_port = integration_test_fixture::generate_test_port();
    uint16_t backup2_port = integration_test_fixture::generate_test_port();

    // Only start second backup (primary and first backup are "failed")
    mock_ris_server::config backup2_config;
    backup2_config.port = backup2_port;
    backup2_config.auto_ack = true;

    mock_ris_server backup2_ris(backup2_config);
    INTEGRATION_TEST_ASSERT(backup2_ris.start(),
                            "Failed to start second backup RIS");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Setup failover router with multiple backups
    failover_router::config router_config;
    router_config.destinations = {
        {"PRIMARY", primary_port, true},
        {"BACKUP1", backup1_port, false},
        {"BACKUP2", backup2_port, false}};
    router_config.auto_failback = false;

    failover_router router(router_config);
    router.start();

    // Send message - should eventually reach backup2
    std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|1|P|2.4\r";
    bool result = router.route_message(msg);

    INTEGRATION_TEST_ASSERT(result, "Message should reach second backup");

    bool received = integration_test_fixture::wait_for(
        [&backup2_ris]() { return backup2_ris.messages_received() >= 1; },
        std::chrono::milliseconds{2000});
    INTEGRATION_TEST_ASSERT(received, "Backup2 should receive message");

    router.stop();
    backup2_ris.stop();
    return true;
}

/**
 * @brief Test rapid failover/failback cycles
 *
 * Verifies system stability under repeated primary failures and recoveries.
 */
bool test_failover_rapid_cycles() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // Start backup RIS
    mock_ris_server::config backup_config;
    backup_config.port = backup_port;
    backup_config.auto_ack = true;

    mock_ris_server backup_ris(backup_config);
    INTEGRATION_TEST_ASSERT(backup_ris.start(), "Failed to start backup RIS");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Setup router
    failover_router::config router_config;
    router_config.destinations = {
        {"PRIMARY", primary_port, true},
        {"BACKUP", backup_port, false}};
    router_config.health_check_interval = std::chrono::milliseconds{100};
    router_config.auto_failback = true;

    failover_router router(router_config);
    router.start();

    int total_sent = 0;
    int total_received = 0;

    // Simulate 3 cycles of primary up/down
    for (int cycle = 0; cycle < 3; ++cycle) {
        // Primary down phase
        for (int i = 0; i < 2; ++i) {
            std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|" +
                              std::to_string(total_sent++) + "|P|2.4\r";
            router.route_message(msg);
        }

        // Start primary briefly
        mock_ris_server::config primary_config;
        primary_config.port = primary_port;
        primary_config.auto_ack = true;

        mock_ris_server primary_ris(primary_config);
        primary_ris.start();
        std::this_thread::sleep_for(std::chrono::milliseconds{200});

        // Send while primary is up
        for (int i = 0; i < 2; ++i) {
            std::string msg = "MSH|^~\\&|PACS||RIS||20240101||ORM^O01|" +
                              std::to_string(total_sent++) + "|P|2.4\r";
            router.route_message(msg);
        }

        total_received += primary_ris.messages_received();
        primary_ris.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    total_received += backup_ris.messages_received();

    // All messages should have been delivered somewhere
    INTEGRATION_TEST_ASSERT(
        total_received == total_sent,
        "All messages should be delivered during failover cycles");

    router.stop();
    backup_ris.stop();
    return true;
}

/**
 * @brief Test failover statistics tracking
 */
bool test_failover_statistics() {
    uint16_t primary_port = integration_test_fixture::generate_test_port();
    uint16_t backup_port = integration_test_fixture::generate_test_port();

    // Setup both servers
    mock_ris_server::config primary_config;
    primary_config.port = primary_port;
    primary_config.auto_ack = true;

    mock_ris_server::config backup_config;
    backup_config.port = backup_port;
    backup_config.auto_ack = true;

    mock_ris_server primary_ris(primary_config);
    mock_ris_server backup_ris(backup_config);

    INTEGRATION_TEST_ASSERT(primary_ris.start(), "Failed to start primary");
    INTEGRATION_TEST_ASSERT(backup_ris.start(), "Failed to start backup");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Setup router
    failover_router::config router_config;
    router_config.destinations = {
        {"PRIMARY", primary_port, true},
        {"BACKUP", backup_port, false}};
    router_config.auto_failback = false;

    failover_router router(router_config);
    router.start();

    // Phase 1: Send to primary
    for (int i = 0; i < 3; ++i) {
        router.route_message("MSH|MSG" + std::to_string(i) + "\r");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    INTEGRATION_TEST_ASSERT(router.messages_to_primary() == 3,
                            "Should track 3 to primary");
    INTEGRATION_TEST_ASSERT(router.messages_to_backup() == 0,
                            "Should track 0 to backup");
    INTEGRATION_TEST_ASSERT(router.failover_count() == 0,
                            "No failovers yet");

    // Phase 2: Make primary fail
    primary_ris.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // Send more messages
    for (int i = 3; i < 6; ++i) {
        router.route_message("MSH|MSG" + std::to_string(i) + "\r");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    INTEGRATION_TEST_ASSERT(router.messages_to_primary() == 3,
                            "Primary count unchanged");
    INTEGRATION_TEST_ASSERT(router.messages_to_backup() == 3,
                            "Should track 3 to backup");
    INTEGRATION_TEST_ASSERT(router.failover_count() >= 1,
                            "Should track failover");

    // Reset and verify
    router.reset_counters();
    INTEGRATION_TEST_ASSERT(router.messages_to_primary() == 0,
                            "Primary count reset");
    INTEGRATION_TEST_ASSERT(router.messages_to_backup() == 0,
                            "Backup count reset");

    router.stop();
    backup_ris.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_failover_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== Failover Routing Integration Tests ===" << std::endl;
    std::cout << "Testing Phase 2: Message Routing Failover\n" << std::endl;

    std::cout << "\n--- Basic Failover Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_failover_route_to_primary);
    RUN_INTEGRATION_TEST(test_failover_to_backup);
    RUN_INTEGRATION_TEST(test_failover_and_failback);

    std::cout << "\n--- Failover Edge Cases ---" << std::endl;
    RUN_INTEGRATION_TEST(test_failover_all_destinations_fail);
    RUN_INTEGRATION_TEST(test_failover_multiple_backups);
    RUN_INTEGRATION_TEST(test_failover_rapid_cycles);
    RUN_INTEGRATION_TEST(test_failover_statistics);

    std::cout << "\n=== Failover Test Summary ===" << std::endl;
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
    return pacs::bridge::integration::test::run_all_failover_tests();
}
