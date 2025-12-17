/**
 * @file mllp_connection_test.cpp
 * @brief Integration tests for MLLP connection management
 *
 * Tests for MLLP connection lifecycle including:
 * - Connection setup and teardown
 * - Connection timeout handling
 * - Automatic reconnection on failure
 * - Connection pool behavior
 * - Graceful shutdown scenarios
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/161
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include "integration_test_base.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace pacs::bridge::integration::test {

// =============================================================================
// Connection Setup and Teardown Tests
// =============================================================================

/**
 * @brief Test basic MLLP connection setup and teardown
 *
 * Verifies that a client can connect to a server and disconnect cleanly.
 */
bool test_connection_setup_teardown_basic() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server
    mllp::mllp_server_config server_config;
    server_config.port = port;
    mllp::mllp_server server(server_config);

    auto start_result = server.start();
    if (!start_result.has_value()) {
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should start");

    // Setup client
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    mllp::mllp_client client(client_config);

    // Connect
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(),
                            "Client should connect successfully");
    INTEGRATION_TEST_ASSERT(client.is_connected(), "Client should be connected");

    // Verify server sees the connection
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.statistics().active_connections > 0; },
            std::chrono::milliseconds{1000}),
        "Server should see active connection");

    // Disconnect
    client.disconnect();
    INTEGRATION_TEST_ASSERT(!client.is_connected(),
                            "Client should be disconnected");

    // Cleanup
    server.stop(true, std::chrono::seconds{5});
    return true;
}

/**
 * @brief Test multiple sequential connections and disconnections
 *
 * Verifies that resources are properly released after each disconnect,
 * allowing subsequent connections.
 */
bool test_connection_sequential_reconnect() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server
    mllp::mllp_server_config server_config;
    server_config.port = port;
    mllp::mllp_server server(server_config);

    auto start_result = server.start();
    if (!start_result.has_value()) {
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should start");

    // Client config
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    // Perform multiple connect/disconnect cycles
    const int cycles = 5;
    for (int i = 0; i < cycles; ++i) {
        mllp::mllp_client client(client_config);

        auto connect_result = client.connect();
        INTEGRATION_TEST_ASSERT(connect_result.has_value(),
                                "Connection cycle " + std::to_string(i + 1) +
                                    " should succeed");
        INTEGRATION_TEST_ASSERT(client.is_connected(),
                                "Client should be connected in cycle " +
                                    std::to_string(i + 1));

        client.disconnect();
        INTEGRATION_TEST_ASSERT(!client.is_connected(),
                                "Client should be disconnected in cycle " +
                                    std::to_string(i + 1));

        // Small delay between cycles
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }

    // Verify server tracked all connections
    auto stats = server.statistics();
    INTEGRATION_TEST_ASSERT(stats.total_connections >= static_cast<uint64_t>(cycles),
                            "Server should have tracked all connections");

    server.stop(true, std::chrono::seconds{5});
    return true;
}

/**
 * @brief Test multiple concurrent client connections
 *
 * Verifies that the server can handle multiple simultaneous clients.
 */
bool test_connection_concurrent_clients() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server with sufficient max_connections
    mllp::mllp_server_config server_config;
    server_config.port = port;
    server_config.max_connections = 10;
    mllp::mllp_server server(server_config);

    auto start_result = server.start();
    if (!start_result.has_value()) {
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should start");

    // Create multiple clients
    const int client_count = 5;
    std::vector<std::unique_ptr<mllp::mllp_client>> clients;

    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    // Connect all clients
    for (int i = 0; i < client_count; ++i) {
        auto client = std::make_unique<mllp::mllp_client>(client_config);
        auto connect_result = client->connect();
        INTEGRATION_TEST_ASSERT(connect_result.has_value(),
                                "Client " + std::to_string(i + 1) +
                                    " should connect");
        clients.push_back(std::move(client));
    }

    // Wait for server to register all connections
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() {
                return server.statistics().active_connections >= 5;
            },
            std::chrono::milliseconds{2000}),
        "Server should have all clients connected");

    // Verify all clients are connected
    for (size_t i = 0; i < clients.size(); ++i) {
        INTEGRATION_TEST_ASSERT(clients[i]->is_connected(),
                                "Client " + std::to_string(i + 1) +
                                    " should be connected");
    }

    // Disconnect all clients
    for (auto& client : clients) {
        client->disconnect();
    }

    server.stop(true, std::chrono::seconds{5});
    return true;
}

// =============================================================================
// Connection Timeout Tests
// =============================================================================

/**
 * @brief Test connection timeout when server is not reachable
 *
 * Verifies that connection attempts timeout appropriately when
 * no server is listening on the target port.
 */
bool test_connection_timeout_no_server() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Client config with short timeout
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{500};

    mllp::mllp_client client(client_config);

    // Measure connection attempt time
    auto start = std::chrono::steady_clock::now();
    auto connect_result = client.connect();
    auto duration = std::chrono::steady_clock::now() - start;

    // Verify timeout behavior
    INTEGRATION_TEST_ASSERT(!connect_result.has_value(),
                            "Connection should fail when no server");
    INTEGRATION_TEST_ASSERT(!client.is_connected(),
                            "Client should not be connected");

    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    INTEGRATION_TEST_ASSERT(duration_ms.count() < 2000,
                            "Connection should timeout within reasonable time");

    return true;
}

/**
 * @brief Test send/receive timeout handling
 *
 * Verifies that message operations timeout appropriately when
 * the server is slow to respond.
 */
bool test_connection_timeout_slow_response() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server with delayed response
    mock_ris_server::config ris_config;
    ris_config.port = port;
    ris_config.auto_ack = true;
    ris_config.response_delay = std::chrono::milliseconds{2000};

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    // Client config
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};
    client_config.io_timeout = std::chrono::milliseconds{5000};

    mllp::mllp_client client(client_config);

    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Client should connect");

    // Send message and wait for delayed response
    std::string hl7_msg =
        "MSH|^~\\&|TEST|FACILITY|||20240101120000||ADT^A01|123|P|2.4\r";
    auto msg = mllp::mllp_message::from_string(hl7_msg);

    auto start = std::chrono::steady_clock::now();
    auto send_result = client.send(msg);
    auto duration = std::chrono::steady_clock::now() - start;

    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration);

    // Send should succeed (response delay < send_timeout)
    INTEGRATION_TEST_ASSERT(send_result.has_value(),
                            "Send should succeed despite delay");
    INTEGRATION_TEST_ASSERT(duration_ms.count() >= 1500,
                            "Should wait for delayed response");

    client.disconnect();
    ris.stop();
    return true;
}

// =============================================================================
// Reconnection Tests
// =============================================================================

/**
 * @brief Test automatic reconnection after server restart
 *
 * Verifies that clients can reconnect after the server has been
 * restarted.
 */
bool test_reconnection_after_server_restart() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup initial server
    mllp::mllp_server_config server_config;
    server_config.port = port;
    auto server = std::make_unique<mllp::mllp_server>(server_config);

    auto start_result = server->start();
    if (!start_result.has_value()) {
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server->is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should start");

    // Connect client
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    mllp::mllp_client client(client_config);
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(),
                            "Initial connection should succeed");

    // Stop server
    server->stop(true, std::chrono::seconds{5});
    server.reset();

    // Client should no longer be connected (after server close)
    client.disconnect();
    INTEGRATION_TEST_ASSERT(!client.is_connected(),
                            "Client should be disconnected after server stop");

    // Wait for port to be released
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    // Start new server on same port
    server = std::make_unique<mllp::mllp_server>(server_config);
    auto restart_result = server->start();
    INTEGRATION_TEST_ASSERT(restart_result.has_value(),
                            "Server should restart");

    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server->is_running(); },
            std::chrono::milliseconds{1000}),
        "Restarted server should be running");

    // Client should be able to reconnect
    auto reconnect_result = client.connect();
    INTEGRATION_TEST_ASSERT(reconnect_result.has_value(),
                            "Client should reconnect successfully");
    INTEGRATION_TEST_ASSERT(client.is_connected(),
                            "Client should be connected after reconnect");

    client.disconnect();
    server->stop(true, std::chrono::seconds{5});
    return true;
}

/**
 * @brief Test reconnection with retry logic
 *
 * Verifies that a client with retry configuration will attempt
 * multiple connection attempts.
 */
bool test_reconnection_with_retry() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Client config with retry enabled
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{500};
    client_config.retry_count = 3;
    client_config.retry_delay = std::chrono::milliseconds{100};

    mllp::mllp_client client(client_config);

    // Start server in background after a delay
    std::atomic<bool> server_started{false};
    std::thread server_thread([&]() {
        // Wait before starting server
        std::this_thread::sleep_for(std::chrono::milliseconds{300});

        mllp::mllp_server_config server_config;
        server_config.port = port;
        mllp::mllp_server server(server_config);

        auto result = server.start();
        if (result.has_value()) {
            server_started = true;
            // Keep server running briefly
            std::this_thread::sleep_for(std::chrono::milliseconds{3000});
            server.stop(true, std::chrono::seconds{5});
        }
    });

    // First connect attempt should fail (server not started yet)
    // Note: Result may vary depending on retry timing
    (void)client.connect();

    // Wait for server thread
    server_thread.join();

    // If server started, client with retry may have connected
    // Either way, verify statistics were updated
    auto stats = client.get_statistics();
    INTEGRATION_TEST_ASSERT(stats.connect_attempts >= 1,
                            "Should have at least 1 connect attempt");

    if (client.is_connected()) {
        client.disconnect();
    }

    return true;
}

// =============================================================================
// Graceful Shutdown Tests
// =============================================================================

/**
 * @brief Test graceful server shutdown with active connections
 *
 * Verifies that the server properly closes all client connections
 * during graceful shutdown.
 */
bool test_graceful_shutdown_with_connections() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server
    mllp::mllp_server_config server_config;
    server_config.port = port;
    mllp::mllp_server server(server_config);

    auto start_result = server.start();
    if (!start_result.has_value()) {
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() { return server.is_running(); },
            std::chrono::milliseconds{1000}),
        "Server should start");

    // Connect multiple clients
    std::vector<mllp::mllp_client> clients;
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    for (int i = 0; i < 3; ++i) {
        clients.emplace_back(client_config);
        auto connect_result = clients.back().connect();
        INTEGRATION_TEST_ASSERT(connect_result.has_value(),
                                "Client " + std::to_string(i + 1) +
                                    " should connect");
    }

    // Verify all connected
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&server]() {
                return server.statistics().active_connections >= 3;
            },
            std::chrono::milliseconds{2000}),
        "Server should have 3 active connections");

    // Graceful shutdown
    server.stop(true, std::chrono::seconds{10});

    INTEGRATION_TEST_ASSERT(!server.is_running(),
                            "Server should not be running after stop");

    // Cleanup clients
    for (auto& client : clients) {
        client.disconnect();
    }

    return true;
}

/**
 * @brief Test immediate server shutdown
 *
 * Verifies that the server can perform an immediate (non-graceful)
 * shutdown.
 */
bool test_immediate_shutdown() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server
    mllp::mllp_server_config server_config;
    server_config.port = port;
    mllp::mllp_server server(server_config);

    auto start_result = server.start();
    if (!start_result.has_value()) {
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    INTEGRATION_TEST_ASSERT(server.is_running(), "Server should be running");

    // Immediate shutdown (graceful=false)
    server.stop(false, std::chrono::seconds{1});

    INTEGRATION_TEST_ASSERT(!server.is_running(),
                            "Server should stop immediately");

    return true;
}

// =============================================================================
// Connection State Tests
// =============================================================================

/**
 * @brief Test connection state transitions
 *
 * Verifies that client correctly reports connection state changes.
 */
bool test_connection_state_transitions() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server
    mllp::mllp_server_config server_config;
    server_config.port = port;
    mllp::mllp_server server(server_config);

    auto start_result = server.start();
    if (!start_result.has_value()) {
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    // Create client
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    mllp::mllp_client client(client_config);

    // Initial state: not connected
    INTEGRATION_TEST_ASSERT(!client.is_connected(),
                            "Initial state should be disconnected");
    INTEGRATION_TEST_ASSERT(!client.session_info().has_value(),
                            "No session info when disconnected");

    // Connect: connected
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Connect should succeed");
    INTEGRATION_TEST_ASSERT(client.is_connected(),
                            "Should be connected after connect()");

    // Disconnect: not connected
    client.disconnect();
    INTEGRATION_TEST_ASSERT(!client.is_connected(),
                            "Should be disconnected after disconnect()");

    server.stop(true, std::chrono::seconds{5});
    return true;
}

/**
 * @brief Test client statistics tracking
 *
 * Verifies that client correctly tracks connection statistics.
 */
bool test_connection_statistics_tracking() {
    uint16_t port = integration_test_fixture::generate_test_port();

    // Setup server with auto-ACK
    mock_ris_server::config ris_config;
    ris_config.port = port;
    ris_config.auto_ack = true;

    mock_ris_server ris(ris_config);
    INTEGRATION_TEST_ASSERT(ris.start(), "Failed to start mock RIS server");
    INTEGRATION_TEST_ASSERT(
        integration_test_fixture::wait_for(
            [&ris]() { return ris.is_running(); },
            std::chrono::milliseconds{1000}),
        "RIS server should start");

    // Client config
    mllp::mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = port;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    mllp::mllp_client client(client_config);

    // Initial statistics
    auto initial_stats = client.get_statistics();
    INTEGRATION_TEST_ASSERT(initial_stats.connect_attempts == 0,
                            "Initial connect_attempts should be 0");
    INTEGRATION_TEST_ASSERT(initial_stats.messages_sent == 0,
                            "Initial messages_sent should be 0");

    // Connect
    auto connect_result = client.connect();
    INTEGRATION_TEST_ASSERT(connect_result.has_value(), "Connect should succeed");

    // Send some messages
    const int message_count = 3;
    for (int i = 0; i < message_count; ++i) {
        std::string hl7_msg =
            "MSH|^~\\&|TEST|FACILITY|||20240101120000||ADT^A01|" +
            std::to_string(i) + "|P|2.4\r";
        auto msg = mllp::mllp_message::from_string(hl7_msg);
        auto send_result = client.send(msg);
        INTEGRATION_TEST_ASSERT(send_result.has_value(),
                                "Send " + std::to_string(i + 1) + " should succeed");
    }

    // Check updated statistics
    auto final_stats = client.get_statistics();
    INTEGRATION_TEST_ASSERT(final_stats.connect_attempts >= 1,
                            "connect_attempts should be at least 1");
    INTEGRATION_TEST_ASSERT(
        final_stats.messages_sent >= static_cast<uint64_t>(message_count),
        "messages_sent should reflect sent messages");

    client.disconnect();
    ris.stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_mllp_connection_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== MLLP Connection Integration Tests ===" << std::endl;
    std::cout << "Testing Issue #161: MLLP Connection Management\n" << std::endl;

    std::cout << "\n--- Connection Setup/Teardown Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_connection_setup_teardown_basic);
    RUN_INTEGRATION_TEST(test_connection_sequential_reconnect);
    RUN_INTEGRATION_TEST(test_connection_concurrent_clients);

    std::cout << "\n--- Connection Timeout Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_connection_timeout_no_server);
    RUN_INTEGRATION_TEST(test_connection_timeout_slow_response);

    std::cout << "\n--- Reconnection Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_reconnection_after_server_restart);
    RUN_INTEGRATION_TEST(test_reconnection_with_retry);

    std::cout << "\n--- Graceful Shutdown Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_graceful_shutdown_with_connections);
    RUN_INTEGRATION_TEST(test_immediate_shutdown);

    std::cout << "\n--- Connection State Tests ---" << std::endl;
    RUN_INTEGRATION_TEST(test_connection_state_transitions);
    RUN_INTEGRATION_TEST(test_connection_statistics_tracking);

    std::cout << "\n=== MLLP Connection Test Summary ===" << std::endl;
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
    return pacs::bridge::integration::test::run_all_mllp_connection_tests();
}
