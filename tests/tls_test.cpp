/**
 * @file tls_test.cpp
 * @brief Unit tests for TLS functionality
 *
 * Tests for TLS configuration, context creation, and error handling.
 * Note: Full integration tests require actual certificates.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/38
 */

#include "pacs/bridge/security/tls_context.h"
#include "pacs/bridge/security/tls_socket.h"
#include "pacs/bridge/security/tls_types.h"

#include <cassert>
#include <iostream>
#include <string>

namespace pacs::bridge::security::test {

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
// TLS Types Tests
// =============================================================================

bool test_tls_version_conversion() {
    // Test to_string
    TEST_ASSERT(std::string(to_string(tls_version::tls_1_2)) == "TLS1.2",
                "tls_version::tls_1_2 should convert to TLS1.2");
    TEST_ASSERT(std::string(to_string(tls_version::tls_1_3)) == "TLS1.3",
                "tls_version::tls_1_3 should convert to TLS1.3");

    // Test parse_tls_version
    auto v12 = parse_tls_version("TLS1.2");
    TEST_ASSERT(v12.has_value() && *v12 == tls_version::tls_1_2,
                "Should parse TLS1.2");

    auto v13 = parse_tls_version("1.3");
    TEST_ASSERT(v13.has_value() && *v13 == tls_version::tls_1_3,
                "Should parse 1.3 as TLS1.3");

    auto invalid = parse_tls_version("TLS1.0");
    TEST_ASSERT(!invalid.has_value(), "Should not parse TLS1.0");

    return true;
}

bool test_client_auth_mode_conversion() {
    // Test to_string
    TEST_ASSERT(std::string(to_string(client_auth_mode::none)) == "none",
                "client_auth_mode::none should convert to none");
    TEST_ASSERT(std::string(to_string(client_auth_mode::optional)) == "optional",
                "client_auth_mode::optional should convert to optional");
    TEST_ASSERT(std::string(to_string(client_auth_mode::required)) == "required",
                "client_auth_mode::required should convert to required");

    // Test parse_client_auth_mode
    auto none = parse_client_auth_mode("none");
    TEST_ASSERT(none.has_value() && *none == client_auth_mode::none,
                "Should parse none");

    auto optional = parse_client_auth_mode("optional");
    TEST_ASSERT(optional.has_value() && *optional == client_auth_mode::optional,
                "Should parse optional");

    auto required = parse_client_auth_mode("required");
    TEST_ASSERT(required.has_value() && *required == client_auth_mode::required,
                "Should parse required");

    auto true_val = parse_client_auth_mode("true");
    TEST_ASSERT(true_val.has_value() && *true_val == client_auth_mode::required,
                "Should parse true as required");

    return true;
}

bool test_tls_error_conversion() {
    TEST_ASSERT(to_error_code(tls_error::initialization_failed) == -990,
                "initialization_failed should be -990");
    TEST_ASSERT(to_error_code(tls_error::certificate_invalid) == -991,
                "certificate_invalid should be -991");
    TEST_ASSERT(to_error_code(tls_error::handshake_failed) == -995,
                "handshake_failed should be -995");

    // Test to_string
    TEST_ASSERT(std::string(to_string(tls_error::certificate_invalid)) ==
                    "Certificate file not found or invalid",
                "Should have proper error description");

    return true;
}

bool test_tls_config_validation() {
    tls_config config;

    // Disabled config should always be valid
    config.enabled = false;
    TEST_ASSERT(config.is_valid_for_server(),
                "Disabled config should be valid for server");
    TEST_ASSERT(config.is_valid_for_client(),
                "Disabled config should be valid for client");

    // Enabled server config requires cert and key
    config.enabled = true;
    TEST_ASSERT(!config.is_valid_for_server(),
                "Enabled server config without cert should be invalid");

    config.cert_path = "/path/to/cert.pem";
    TEST_ASSERT(!config.is_valid_for_server(),
                "Enabled server config without key should be invalid");

    config.key_path = "/path/to/key.pem";
    TEST_ASSERT(config.is_valid_for_server(),
                "Enabled server config with cert and key should be valid");

    // Client config is more lenient
    tls_config client_config;
    client_config.enabled = true;
    TEST_ASSERT(client_config.is_valid_for_client(),
                "Enabled client config should be valid (CA optional)");

    return true;
}

bool test_tls_config_mutual_tls() {
    tls_config config;
    config.enabled = true;
    config.client_auth = client_auth_mode::none;
    TEST_ASSERT(!config.is_mutual_tls(),
                "client_auth=none should not be mutual TLS");

    config.client_auth = client_auth_mode::optional;
    TEST_ASSERT(config.is_mutual_tls(),
                "client_auth=optional should be mutual TLS");

    config.client_auth = client_auth_mode::required;
    TEST_ASSERT(config.is_mutual_tls(),
                "client_auth=required should be mutual TLS");

    config.enabled = false;
    TEST_ASSERT(!config.is_mutual_tls(),
                "Disabled TLS should not be mutual TLS");

    return true;
}

bool test_tls_statistics() {
    tls_statistics stats;

    // Default values
    TEST_ASSERT(stats.handshakes_attempted == 0,
                "Default handshakes_attempted should be 0");
    TEST_ASSERT(stats.success_rate() == 100.0,
                "Empty stats should have 100% success rate");
    TEST_ASSERT(stats.resumption_rate() == 0.0,
                "Empty stats should have 0% resumption rate");

    // Calculate success rate
    stats.handshakes_attempted = 100;
    stats.handshakes_succeeded = 95;
    TEST_ASSERT(stats.success_rate() == 95.0,
                "Success rate should be 95%");

    // Calculate resumption rate
    stats.sessions_resumed = 50;
    double expected_resumption = (50.0 / 95.0) * 100.0;
    TEST_ASSERT(std::abs(stats.resumption_rate() - expected_resumption) < 0.01,
                "Resumption rate calculation should be correct");

    return true;
}

bool test_certificate_info_validity() {
    certificate_info cert;
    auto now = std::chrono::system_clock::now();

    // Set valid date range
    cert.not_before = now - std::chrono::hours(24);
    cert.not_after = now + std::chrono::hours(24 * 365);

    TEST_ASSERT(cert.is_valid(), "Certificate should be valid");
    TEST_ASSERT(!cert.expires_within(std::chrono::hours(24)),
                "Certificate should not expire within 24 hours");
    TEST_ASSERT(cert.expires_within(std::chrono::hours(24 * 400)),
                "Certificate should expire within 400 days");

    // Expired certificate
    cert.not_after = now - std::chrono::hours(1);
    TEST_ASSERT(!cert.is_valid(), "Expired certificate should be invalid");
    TEST_ASSERT(cert.remaining_validity().count() < 0,
                "Expired cert should have negative remaining validity");

    // Not yet valid
    cert.not_before = now + std::chrono::hours(24);
    cert.not_after = now + std::chrono::hours(48);
    TEST_ASSERT(!cert.is_valid(),
                "Future certificate should be invalid");

    return true;
}

// =============================================================================
// TLS Context Tests
// =============================================================================

bool test_tls_initialization() {
    // Test initialization
    auto result = initialize_tls();
#ifdef PACS_BRIDGE_HAS_OPENSSL
    TEST_ASSERT(result.has_value(), "TLS initialization should succeed");

    // Cleanup
    cleanup_tls();
#else
    TEST_ASSERT(!result.has_value(),
                "TLS initialization should fail without OpenSSL");
#endif

    return true;
}

bool test_tls_library_guard() {
    {
        tls_library_guard guard;
#ifdef PACS_BRIDGE_HAS_OPENSSL
        TEST_ASSERT(guard.is_initialized(),
                    "Library guard should be initialized");
#else
        TEST_ASSERT(!guard.is_initialized(),
                    "Library guard should fail without OpenSSL");
#endif
    }
    // Guard destructor should have been called

    return true;
}

bool test_openssl_version() {
    std::string version = openssl_version();
    TEST_ASSERT(!version.empty(), "OpenSSL version should not be empty");
#ifdef PACS_BRIDGE_HAS_OPENSSL
    TEST_ASSERT(version.find("OpenSSL") != std::string::npos,
                "Version should contain OpenSSL");
#else
    TEST_ASSERT(version == "OpenSSL not available",
                "Should indicate OpenSSL not available");
#endif

    return true;
}

bool test_server_context_creation_without_certs() {
    tls_library_guard guard;

    tls_config config;
    config.enabled = true;
    // No cert/key paths - should fail

    auto result = tls_context::create_server_context(config);
    TEST_ASSERT(!result.has_value(),
                "Server context without certs should fail");

    return true;
}

bool test_client_context_creation() {
    tls_library_guard guard;

    tls_config config;
    config.enabled = true;
    // CA path is optional for client

    auto result = tls_context::create_client_context(config);
#ifdef PACS_BRIDGE_HAS_OPENSSL
    // May succeed or fail depending on system CA store
    // Just check it doesn't crash
#endif

    return true;
}

// =============================================================================
// TLS Socket Tests
// =============================================================================

bool test_handshake_status_conversion() {
    TEST_ASSERT(std::string(to_string(tls_socket::handshake_status::not_started)) ==
                    "not_started",
                "not_started conversion");
    TEST_ASSERT(std::string(to_string(tls_socket::handshake_status::want_read)) ==
                    "want_read",
                "want_read conversion");
    TEST_ASSERT(std::string(to_string(tls_socket::handshake_status::complete)) ==
                    "complete",
                "complete conversion");

    return true;
}

bool test_io_status_conversion() {
    TEST_ASSERT(std::string(to_string(tls_socket::io_status::success)) ==
                    "success",
                "success conversion");
    TEST_ASSERT(std::string(to_string(tls_socket::io_status::want_read)) ==
                    "want_read",
                "want_read conversion");
    TEST_ASSERT(std::string(to_string(tls_socket::io_status::closed)) ==
                    "closed",
                "closed conversion");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== TLS Types Tests ===" << std::endl;
    RUN_TEST(test_tls_version_conversion);
    RUN_TEST(test_client_auth_mode_conversion);
    RUN_TEST(test_tls_error_conversion);
    RUN_TEST(test_tls_config_validation);
    RUN_TEST(test_tls_config_mutual_tls);
    RUN_TEST(test_tls_statistics);
    RUN_TEST(test_certificate_info_validity);

    std::cout << "\n=== TLS Context Tests ===" << std::endl;
    RUN_TEST(test_tls_initialization);
    RUN_TEST(test_tls_library_guard);
    RUN_TEST(test_openssl_version);
    RUN_TEST(test_server_context_creation_without_certs);
    RUN_TEST(test_client_context_creation);

    std::cout << "\n=== TLS Socket Tests ===" << std::endl;
    RUN_TEST(test_handshake_status_conversion);
    RUN_TEST(test_io_status_conversion);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::security::test

int main() {
    return pacs::bridge::security::test::run_all_tests();
}
