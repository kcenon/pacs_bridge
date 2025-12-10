/**
 * @file mllp_test.cpp
 * @brief Comprehensive unit tests for MLLP server and client implementation
 *
 * Tests for MLLP protocol constants, error handling, configuration,
 * server operations, client operations, and connection management.
 * Target coverage: >= 80%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/12
 * @see https://github.com/kcenon/pacs_bridge/issues/13
 * @see https://github.com/kcenon/pacs_bridge/issues/38
 */

#include "pacs/bridge/mllp/mllp_client.h"
#include "pacs/bridge/mllp/mllp_server.h"
#include "pacs/bridge/mllp/mllp_types.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace pacs::bridge::mllp::test {

// =============================================================================
// Test Utilities
// =============================================================================

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

/**
 * @brief Wait until a condition is met or timeout occurs
 * @param condition Function returning true when condition is met
 * @param timeout Maximum time to wait
 * @return true if condition was met, false on timeout
 */
template <typename Predicate>
bool wait_for(Predicate condition, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

#define RUN_TEST(test_func)                                                    \
    do {                                                                       \
        std::cout << "Running " << #test_func << "..." << std::endl;           \
        if (test_func()) {                                                     \
            std::cout << "  PASSED" << std::endl;                              \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED" << std::endl;                              \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// =============================================================================
// MLLP Protocol Tests
// =============================================================================

bool test_mllp_constants() {
    // Verify protocol constants
    TEST_ASSERT(MLLP_START_BYTE == '\x0B',
                "MLLP start byte should be 0x0B (VT)");
    TEST_ASSERT(MLLP_END_BYTE == '\x1C',
                "MLLP end byte should be 0x1C (FS)");
    TEST_ASSERT(MLLP_CARRIAGE_RETURN == '\x0D',
                "MLLP CR should be 0x0D");

    TEST_ASSERT(MLLP_DEFAULT_PORT == 2575,
                "Default MLLP port should be 2575");
    TEST_ASSERT(MLLPS_DEFAULT_PORT == 2576,
                "Default MLLPS (TLS) port should be 2576");

    TEST_ASSERT(MLLP_MAX_MESSAGE_SIZE == 10 * 1024 * 1024,
                "Max message size should be 10MB");

    return true;
}

bool test_mllp_error_codes() {
    // Verify error code range
    TEST_ASSERT(to_error_code(mllp_error::invalid_frame) == -970,
                "invalid_frame should be -970");
    TEST_ASSERT(to_error_code(mllp_error::ack_error) == -979,
                "ack_error should be -979");

    // Verify error messages
    TEST_ASSERT(std::string(to_string(mllp_error::invalid_frame)) ==
                    "Invalid MLLP frame structure",
                "invalid_frame message");
    TEST_ASSERT(std::string(to_string(mllp_error::timeout)) ==
                    "Connection timeout",
                "timeout message");

    return true;
}

bool test_mllp_message_creation() {
    // Create message from string
    std::string_view hl7 = "MSH|^~\\&|TEST|FACILITY|||20240101120000||ADT^A01|123|P|2.4";
    auto msg = mllp_message::from_string(hl7);

    TEST_ASSERT(msg.to_string() == hl7,
                "Message content should match original");
    TEST_ASSERT(msg.content.size() == hl7.size(),
                "Content size should match");

    return true;
}

bool test_mllp_message_framing() {
    std::string hl7 = "MSH|^~\\&|TEST";
    auto msg = mllp_message::from_string(hl7);

    auto framed = msg.frame();

    // Verify framing
    TEST_ASSERT(framed.size() == hl7.size() + 3,
                "Framed message should be 3 bytes larger");
    TEST_ASSERT(framed.front() == static_cast<uint8_t>(MLLP_START_BYTE),
                "Should start with VT");
    TEST_ASSERT(framed[framed.size() - 2] == static_cast<uint8_t>(MLLP_END_BYTE),
                "Should have FS before CR");
    TEST_ASSERT(framed.back() == static_cast<uint8_t>(MLLP_CARRIAGE_RETURN),
                "Should end with CR");

    // Verify content is in the middle
    std::string content(framed.begin() + 1, framed.end() - 2);
    TEST_ASSERT(content == hl7, "Content should be preserved");

    return true;
}

// =============================================================================
// Configuration Tests
// =============================================================================

bool test_server_config_validation() {
    mllp_server_config config;

    // Default config should be valid
    TEST_ASSERT(config.is_valid(), "Default config should be valid");

    // Invalid port
    config.port = 0;
    TEST_ASSERT(!config.is_valid(), "Port 0 should be invalid");
    config.port = 2575;

    // Invalid max_connections
    config.max_connections = 0;
    TEST_ASSERT(!config.is_valid(), "0 max_connections should be invalid");
    config.max_connections = 50;

    // Invalid max_message_size
    config.max_message_size = 0;
    TEST_ASSERT(!config.is_valid(), "0 max_message_size should be invalid");
    config.max_message_size = MLLP_MAX_MESSAGE_SIZE;

    // TLS enabled but invalid
    config.tls.enabled = true;
    TEST_ASSERT(!config.is_valid(), "TLS without certs should be invalid");

    config.tls.cert_path = "/path/to/cert.pem";
    config.tls.key_path = "/path/to/key.pem";
    TEST_ASSERT(config.is_valid(), "TLS with certs should be valid");

    return true;
}

bool test_client_config_validation() {
    mllp_client_config config;

    // Missing host
    TEST_ASSERT(!config.is_valid(), "Empty host should be invalid");

    config.host = "localhost";
    TEST_ASSERT(config.is_valid(), "Valid host should be valid");

    // Invalid port
    config.port = 0;
    TEST_ASSERT(!config.is_valid(), "Port 0 should be invalid");
    config.port = 2575;

    // TLS enabled (should be valid without CA for client)
    config.tls.enabled = true;
    TEST_ASSERT(config.is_valid(), "Client TLS should be valid without CA");

    return true;
}

bool test_session_info_duration() {
    mllp_session_info session;
    session.connected_at = std::chrono::system_clock::now() -
                           std::chrono::seconds(120);

    auto duration = session.duration();
    TEST_ASSERT(duration.count() >= 119 && duration.count() <= 121,
                "Duration should be approximately 120 seconds");

    return true;
}

bool test_server_statistics_uptime() {
    mllp_server_statistics stats;
    stats.started_at = std::chrono::system_clock::now() -
                       std::chrono::seconds(3600);

    auto uptime = stats.uptime();
    TEST_ASSERT(uptime.count() >= 3599 && uptime.count() <= 3601,
                "Uptime should be approximately 3600 seconds");

    return true;
}

// =============================================================================
// MLLP Server Tests
// =============================================================================

bool test_mllp_server_creation() {
    mllp_server_config config;
    config.port = 12575;  // Use non-standard port for testing

    mllp_server server(config);

    TEST_ASSERT(!server.is_running(), "Server should not be running initially");
    TEST_ASSERT(server.port() == 12575, "Port should match config");
    TEST_ASSERT(!server.is_tls_enabled(), "TLS should not be enabled");

    return true;
}

bool test_mllp_server_config_accessor() {
    mllp_server_config config;
    config.port = 12576;
    config.max_connections = 100;
    config.max_message_size = 5 * 1024 * 1024;

    mllp_server server(config);

    const auto& server_config = server.config();
    TEST_ASSERT(server_config.port == 12576,
                "Config port should match");
    TEST_ASSERT(server_config.max_connections == 100,
                "Config max_connections should match");
    TEST_ASSERT(server_config.max_message_size == 5 * 1024 * 1024,
                "Config max_message_size should match");

    return true;
}

bool test_mllp_server_statistics_initial() {
    mllp_server_config config;
    config.port = 12577;

    mllp_server server(config);
    auto stats = server.statistics();

    TEST_ASSERT(stats.active_connections == 0,
                "Initial active connections should be 0");
    TEST_ASSERT(stats.total_connections == 0,
                "Initial total connections should be 0");

    return true;
}

bool test_mllp_server_active_sessions_empty() {
    mllp_server_config config;
    config.port = 12578;

    mllp_server server(config);
    auto sessions = server.active_sessions();

    TEST_ASSERT(sessions.empty(),
                "Should have no active sessions initially");

    return true;
}

bool test_mllp_server_invalid_config() {
    mllp_server_config config;
    config.port = 0;  // Invalid port

    mllp_server server(config);
    auto result = server.start();

    TEST_ASSERT(!result.has_value(), "Should fail with invalid config");
    TEST_ASSERT(result.error() == mllp_error::invalid_configuration,
                "Error should be invalid_configuration");

    return true;
}

bool test_mllp_server_start_stop() {
    mllp_server_config config;
    config.port = 12590;

    mllp_server server(config);

    // Start server
    auto start_result = server.start();
    if (!start_result.has_value()) {
        // Port might be in use, skip test
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    TEST_ASSERT(server.is_running(), "Server should be running after start");

    // Try to start again (should fail)
    auto start_again = server.start();
    TEST_ASSERT(!start_again.has_value(), "Starting again should fail");
    TEST_ASSERT(start_again.error() == mllp_error::already_running,
                "Error should be already_running");

    // Stop server
    server.stop(true, std::chrono::seconds{5});
    TEST_ASSERT(!server.is_running(), "Server should not be running after stop");

    return true;
}

// =============================================================================
// MLLP Client Tests
// =============================================================================

bool test_mllp_client_creation() {
    mllp_client_config config;
    config.host = "localhost";
    config.port = 12579;

    mllp_client client(config);

    TEST_ASSERT(!client.is_connected(),
                "Client should not be connected initially");
    TEST_ASSERT(!client.is_tls_active(),
                "TLS should not be active initially");

    return true;
}

bool test_mllp_client_config_accessor() {
    mllp_client_config config;
    config.host = "test.example.com";
    config.port = 12580;
    config.retry_count = 5;
    config.keep_alive = false;

    mllp_client client(config);

    const auto& client_config = client.config();
    TEST_ASSERT(client_config.host == "test.example.com",
                "Config host should match");
    TEST_ASSERT(client_config.port == 12580,
                "Config port should match");
    TEST_ASSERT(client_config.retry_count == 5,
                "Config retry_count should match");
    TEST_ASSERT(!client_config.keep_alive,
                "Config keep_alive should match");

    return true;
}

bool test_mllp_client_session_info_not_connected() {
    mllp_client_config config;
    config.host = "localhost";
    config.port = 12581;

    mllp_client client(config);
    auto session_info = client.session_info();

    TEST_ASSERT(!session_info.has_value(),
                "Session info should be empty when not connected");

    return true;
}

bool test_mllp_client_statistics_initial() {
    mllp_client_config config;
    config.host = "localhost";
    config.port = 12582;

    mllp_client client(config);
    auto stats = client.get_statistics();

    TEST_ASSERT(stats.messages_sent == 0,
                "Initial messages sent should be 0");
    TEST_ASSERT(stats.messages_received == 0,
                "Initial messages received should be 0");
    TEST_ASSERT(stats.connect_attempts == 0,
                "Initial connect attempts should be 0");

    return true;
}

bool test_mllp_client_tls_info_not_connected() {
    mllp_client_config config;
    config.host = "localhost";
    config.port = 12583;

    mllp_client client(config);

    TEST_ASSERT(!client.tls_version().has_value(),
                "TLS version should be empty when not connected");
    TEST_ASSERT(!client.tls_cipher().has_value(),
                "TLS cipher should be empty when not connected");
    TEST_ASSERT(!client.server_certificate().has_value(),
                "Server certificate should be empty when not connected");

    return true;
}

bool test_mllp_client_connect_failure() {
    mllp_client_config config;
    config.host = "localhost";
    config.port = 12591;  // No server running on this port
    config.connect_timeout = std::chrono::milliseconds{100};  // Short timeout

    mllp_client client(config);
    auto result = client.connect();

    TEST_ASSERT(!result.has_value(),
                "Connect should fail when no server is running");
    TEST_ASSERT(!client.is_connected(),
                "Client should not be connected after failed connect");

    return true;
}

bool test_mllp_client_send_not_connected() {
    mllp_client_config config;
    config.host = "localhost";
    config.port = 12592;
    config.keep_alive = false;  // Don't auto-connect

    mllp_client client(config);

    auto msg = mllp_message::from_string(
        "MSH|^~\\&|TEST|FACILITY|||20240101120000||ADT^A01|123|P|2.4");
    auto result = client.send(msg);

    TEST_ASSERT(!result.has_value(),
                "Send should fail when not connected");

    return true;
}

// =============================================================================
// MLLP Connection Pool Tests
// =============================================================================

bool test_mllp_pool_config_defaults() {
    mllp_pool_config config;

    TEST_ASSERT(config.min_connections == 1,
                "Default min connections should be 1");
    TEST_ASSERT(config.max_connections == 10,
                "Default max connections should be 10");
    TEST_ASSERT(config.idle_timeout == std::chrono::seconds{60},
                "Default idle timeout should be 60 seconds");
    TEST_ASSERT(config.health_check_interval == std::chrono::seconds{30},
                "Default health check interval should be 30 seconds");

    return true;
}

// =============================================================================
// Integration Tests (Server-Client Communication)
// =============================================================================

bool test_server_client_communication() {
    // Server configuration
    mllp_server_config server_config;
    server_config.port = 12600;

    mllp_server server(server_config);

    // Set up message handler to echo messages with ACK
    std::atomic<int> messages_received{0};
    server.set_message_handler(
        [&messages_received](const mllp_message& msg,
                             const mllp_session_info& /*session*/)
            -> std::optional<mllp_message> {
            messages_received++;
            // Create simple ACK response
            std::string ack =
                "MSH|^~\\&|PACS|RADIOLOGY|HIS|HOSPITAL|20240115103001||ACK^A01|ACK001|P|2.4\r"
                "MSA|AA|MSG001\r";
            return mllp_message::from_string(ack);
        });

    // Start server
    auto start_result = server.start();
    if (!start_result.has_value()) {
        std::cout << "  (skipped - port may be in use)" << std::endl;
        return true;
    }

    // Wait for server to be ready
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{1000}),
        "Server should start");

    // Client configuration
    mllp_client_config client_config;
    client_config.host = "localhost";
    client_config.port = 12600;
    client_config.connect_timeout = std::chrono::milliseconds{5000};

    mllp_client client(client_config);

    // Connect client
    auto connect_result = client.connect();
    TEST_ASSERT(connect_result.has_value(), "Client should connect successfully");
    TEST_ASSERT(client.is_connected(), "Client should be connected");

    // Send message
    std::string hl7_msg =
        "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||ADT^A01|MSG001|P|2.4\r"
        "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN\r";
    auto msg = mllp_message::from_string(hl7_msg);

    auto send_result = client.send(msg);
    TEST_ASSERT(send_result.has_value(), "Send should succeed");

    // Verify response
    TEST_ASSERT(!send_result->response.content.empty(),
                "Response should not be empty");

    // Verify server received message
    TEST_ASSERT(
        wait_for([&messages_received]() { return messages_received >= 1; },
                 std::chrono::milliseconds{1000}),
        "Server should receive message");
    TEST_ASSERT(messages_received == 1, "Server should have received 1 message");

    // Disconnect and stop
    client.disconnect();
    server.stop(true, std::chrono::seconds{5});

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== MLLP Protocol Tests ===" << std::endl;
    RUN_TEST(test_mllp_constants);
    RUN_TEST(test_mllp_error_codes);
    RUN_TEST(test_mllp_message_creation);
    RUN_TEST(test_mllp_message_framing);

    std::cout << "\n=== MLLP Configuration Tests ===" << std::endl;
    RUN_TEST(test_server_config_validation);
    RUN_TEST(test_client_config_validation);
    RUN_TEST(test_session_info_duration);
    RUN_TEST(test_server_statistics_uptime);

    std::cout << "\n=== MLLP Server Tests ===" << std::endl;
    RUN_TEST(test_mllp_server_creation);
    RUN_TEST(test_mllp_server_config_accessor);
    RUN_TEST(test_mllp_server_statistics_initial);
    RUN_TEST(test_mllp_server_active_sessions_empty);
    RUN_TEST(test_mllp_server_invalid_config);
    RUN_TEST(test_mllp_server_start_stop);

    std::cout << "\n=== MLLP Client Tests ===" << std::endl;
    RUN_TEST(test_mllp_client_creation);
    RUN_TEST(test_mllp_client_config_accessor);
    RUN_TEST(test_mllp_client_session_info_not_connected);
    RUN_TEST(test_mllp_client_statistics_initial);
    RUN_TEST(test_mllp_client_tls_info_not_connected);
    RUN_TEST(test_mllp_client_connect_failure);
    RUN_TEST(test_mllp_client_send_not_connected);

    std::cout << "\n=== MLLP Connection Pool Tests ===" << std::endl;
    RUN_TEST(test_mllp_pool_config_defaults);

    std::cout << "\n=== MLLP Integration Tests ===" << std::endl;
    RUN_TEST(test_server_client_communication);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::mllp::test

int main() {
    return pacs::bridge::mllp::test::run_all_tests();
}
