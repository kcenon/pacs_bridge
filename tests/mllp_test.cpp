/**
 * @file mllp_test.cpp
 * @brief Unit tests for MLLP types and configuration
 *
 * Tests for MLLP protocol constants, error handling, and configuration.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/38
 */

#include "pacs/bridge/mllp/mllp_types.h"

#include <cassert>
#include <iostream>
#include <string>

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

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::mllp::test

int main() {
    return pacs::bridge::mllp::test::run_all_tests();
}
