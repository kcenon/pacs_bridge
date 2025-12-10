/**
 * @file emr_config_test.cpp
 * @brief Unit tests for EMR configuration module
 *
 * Tests for EMR configuration validation, defaults, and environment variable
 * substitution.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/109
 */

#include "pacs/bridge/config/emr_config.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace pacs::bridge::config::test {

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
// EMR Vendor Tests
// =============================================================================

bool test_emr_vendor_to_string() {
    TEST_ASSERT(std::string(to_string(emr_vendor::generic)) == "generic",
                "generic vendor string incorrect");
    TEST_ASSERT(std::string(to_string(emr_vendor::epic)) == "epic",
                "epic vendor string incorrect");
    TEST_ASSERT(std::string(to_string(emr_vendor::cerner)) == "cerner",
                "cerner vendor string incorrect");

    return true;
}

bool test_emr_vendor_parsing() {
    TEST_ASSERT(parse_emr_vendor("generic") == emr_vendor::generic,
                "Should parse 'generic'");
    TEST_ASSERT(parse_emr_vendor("Generic") == emr_vendor::generic,
                "Should parse 'Generic'");
    TEST_ASSERT(parse_emr_vendor("epic") == emr_vendor::epic,
                "Should parse 'epic'");
    TEST_ASSERT(parse_emr_vendor("Epic") == emr_vendor::epic,
                "Should parse 'Epic'");
    TEST_ASSERT(parse_emr_vendor("cerner") == emr_vendor::cerner,
                "Should parse 'cerner'");
    TEST_ASSERT(parse_emr_vendor("Cerner") == emr_vendor::cerner,
                "Should parse 'Cerner'");
    TEST_ASSERT(!parse_emr_vendor("invalid").has_value(),
                "Should return nullopt for invalid vendor");

    return true;
}

// =============================================================================
// Error Code Tests
// =============================================================================

bool test_emr_config_error_codes() {
    TEST_ASSERT(to_error_code(emr_config_error::config_invalid) == -1100,
                "config_invalid should be -1100");
    TEST_ASSERT(to_error_code(emr_config_error::missing_url) == -1101,
                "missing_url should be -1101");
    TEST_ASSERT(to_error_code(emr_config_error::invalid_auth) == -1102,
                "invalid_auth should be -1102");
    TEST_ASSERT(to_error_code(emr_config_error::missing_credentials) == -1103,
                "missing_credentials should be -1103");
    TEST_ASSERT(to_error_code(emr_config_error::invalid_timeout) == -1104,
                "invalid_timeout should be -1104");

    return true;
}

bool test_emr_config_error_strings() {
    TEST_ASSERT(std::string(to_string(emr_config_error::config_invalid)) ==
                    "EMR configuration is invalid",
                "config_invalid string incorrect");
    TEST_ASSERT(std::string(to_string(emr_config_error::missing_url)) ==
                    "Missing required EMR base URL",
                "missing_url string incorrect");

    return true;
}

// =============================================================================
// Connection Config Tests
// =============================================================================

bool test_connection_config_validation() {
    emr_connection_config config;
    TEST_ASSERT(!config.is_valid(), "Empty connection config should be invalid");

    config.base_url = "https://emr.hospital.local/fhir/r4";
    TEST_ASSERT(config.is_valid(), "Connection config with URL should be valid");

    config.timeout = std::chrono::seconds{0};
    TEST_ASSERT(!config.is_valid(), "Zero timeout should be invalid");

    config.timeout = std::chrono::seconds{30};
    config.max_connections = 0;
    TEST_ASSERT(!config.is_valid(), "Zero max_connections should be invalid");

    return true;
}

bool test_connection_config_defaults() {
    emr_connection_config config;

    TEST_ASSERT(config.timeout == std::chrono::seconds{30},
                "Default timeout should be 30s");
    TEST_ASSERT(config.max_connections == 10,
                "Default max_connections should be 10");
    TEST_ASSERT(config.verify_ssl == true,
                "Default verify_ssl should be true");
    TEST_ASSERT(config.keepalive_timeout == std::chrono::seconds{60},
                "Default keepalive_timeout should be 60s");

    return true;
}

// =============================================================================
// Authentication Config Tests
// =============================================================================

bool test_oauth2_config_validation() {
    emr_oauth2_config config;
    TEST_ASSERT(!config.is_valid(), "Empty OAuth2 config should be invalid");

    config.token_url = "https://auth.example.com/token";
    TEST_ASSERT(!config.is_valid(), "OAuth2 without client_id should be invalid");

    config.client_id = "test_client";
    TEST_ASSERT(!config.is_valid(), "OAuth2 without client_secret should be invalid");

    config.client_secret = "test_secret";
    TEST_ASSERT(config.is_valid(), "Complete OAuth2 config should be valid");

    return true;
}

bool test_basic_auth_config_validation() {
    emr_basic_auth_config config;
    TEST_ASSERT(!config.is_valid(), "Empty basic auth config should be invalid");

    config.username = "user";
    TEST_ASSERT(!config.is_valid(), "Basic auth without password should be invalid");

    config.password = "pass";
    TEST_ASSERT(config.is_valid(), "Complete basic auth config should be valid");

    return true;
}

bool test_api_key_config_validation() {
    emr_api_key_config config;
    TEST_ASSERT(!config.is_valid(), "Empty API key config should be invalid");

    config.key = "my_api_key";
    TEST_ASSERT(config.is_valid(), "API key config with key should be valid");

    return true;
}

bool test_auth_config_validation() {
    emr_auth_config config;

    // Test OAuth2
    config.type = security::auth_type::oauth2;
    TEST_ASSERT(!config.is_valid(), "OAuth2 without credentials should be invalid");

    config.oauth2.token_url = "https://auth.example.com/token";
    config.oauth2.client_id = "client";
    config.oauth2.client_secret = "secret";
    TEST_ASSERT(config.is_valid(), "OAuth2 with credentials should be valid");

    // Test none
    config.type = security::auth_type::none;
    TEST_ASSERT(config.is_valid(), "None auth type should always be valid");

    // Test basic
    config.type = security::auth_type::basic;
    TEST_ASSERT(!config.is_valid(), "Basic without credentials should be invalid");

    config.basic.username = "user";
    config.basic.password = "pass";
    TEST_ASSERT(config.is_valid(), "Basic with credentials should be valid");

    return true;
}

bool test_oauth2_to_security_config() {
    emr_oauth2_config emr_config;
    emr_config.token_url = "https://auth.example.com/token";
    emr_config.client_id = "my_client";
    emr_config.client_secret = "my_secret";
    emr_config.scopes = {"scope1", "scope2"};
    emr_config.token_refresh_margin = std::chrono::seconds{120};

    auto oauth_config = emr_config.to_oauth2_config();

    TEST_ASSERT(oauth_config.token_url == "https://auth.example.com/token",
                "token_url should be preserved");
    TEST_ASSERT(oauth_config.client_id == "my_client",
                "client_id should be preserved");
    TEST_ASSERT(oauth_config.client_secret == "my_secret",
                "client_secret should be preserved");
    TEST_ASSERT(oauth_config.scopes.size() == 2,
                "scopes should be preserved");
    TEST_ASSERT(oauth_config.token_refresh_margin == std::chrono::seconds{120},
                "token_refresh_margin should be preserved");

    return true;
}

// =============================================================================
// Retry Config Tests
// =============================================================================

bool test_retry_config_validation() {
    emr_retry_config config;

    // Default should be valid
    TEST_ASSERT(config.is_valid(), "Default retry config should be valid");

    config.max_attempts = 0;
    TEST_ASSERT(!config.is_valid(), "Zero max_attempts should be invalid");
    config.max_attempts = 3;

    config.initial_backoff = std::chrono::milliseconds{0};
    TEST_ASSERT(!config.is_valid(), "Zero initial_backoff should be invalid");
    config.initial_backoff = std::chrono::milliseconds{1000};

    config.max_backoff = std::chrono::milliseconds{500};  // Less than initial
    TEST_ASSERT(!config.is_valid(), "max_backoff < initial_backoff should be invalid");
    config.max_backoff = std::chrono::milliseconds{30000};

    config.backoff_multiplier = 0.0;
    TEST_ASSERT(!config.is_valid(), "Zero backoff_multiplier should be invalid");

    return true;
}

bool test_retry_config_backoff_calculation() {
    emr_retry_config config;
    config.initial_backoff = std::chrono::milliseconds{1000};
    config.max_backoff = std::chrono::milliseconds{30000};
    config.backoff_multiplier = 2.0;

    TEST_ASSERT(config.calculate_backoff(0) == std::chrono::milliseconds{1000},
                "Attempt 0 should return initial backoff");
    TEST_ASSERT(config.calculate_backoff(1) == std::chrono::milliseconds{2000},
                "Attempt 1 should return 2x initial");
    TEST_ASSERT(config.calculate_backoff(2) == std::chrono::milliseconds{4000},
                "Attempt 2 should return 4x initial");
    TEST_ASSERT(config.calculate_backoff(3) == std::chrono::milliseconds{8000},
                "Attempt 3 should return 8x initial");

    // Test capping at max_backoff
    TEST_ASSERT(config.calculate_backoff(10) == std::chrono::milliseconds{30000},
                "High attempt should cap at max_backoff");

    return true;
}

// =============================================================================
// Cache Config Tests
// =============================================================================

bool test_cache_config_validation() {
    emr_cache_config config;

    // Default should be valid
    TEST_ASSERT(config.is_valid(), "Default cache config should be valid");

    config.max_entries = 0;
    TEST_ASSERT(!config.is_valid(), "Zero max_entries should be invalid");

    return true;
}

bool test_cache_config_defaults() {
    emr_cache_config config;

    TEST_ASSERT(config.patient_ttl == std::chrono::seconds{300},
                "Default patient_ttl should be 300s");
    TEST_ASSERT(config.encounter_ttl == std::chrono::seconds{60},
                "Default encounter_ttl should be 60s");
    TEST_ASSERT(config.max_entries == 10000,
                "Default max_entries should be 10000");
    TEST_ASSERT(config.evict_on_full == true,
                "Default evict_on_full should be true");

    return true;
}

// =============================================================================
// Complete EMR Config Tests
// =============================================================================

bool test_emr_config_disabled_is_valid() {
    emr_config config;
    config.enabled = false;

    TEST_ASSERT(config.is_valid(), "Disabled config should always be valid");

    auto errors = config.validate();
    TEST_ASSERT(errors.empty(), "Disabled config should have no validation errors");

    return true;
}

bool test_emr_config_enabled_requires_connection() {
    emr_config config;
    config.enabled = true;

    TEST_ASSERT(!config.is_valid(),
                "Enabled config without connection URL should be invalid");

    auto errors = config.validate();
    TEST_ASSERT(!errors.empty(),
                "Should have validation errors for missing URL");

    return true;
}

bool test_emr_config_complete_validation() {
    emr_config config;
    config.enabled = true;

    // Set up valid connection
    config.connection.base_url = "https://emr.hospital.local/fhir/r4";

    // Set up valid OAuth2 auth
    config.auth.type = security::auth_type::oauth2;
    config.auth.oauth2.token_url = "https://auth.example.com/token";
    config.auth.oauth2.client_id = "client";
    config.auth.oauth2.client_secret = "secret";

    TEST_ASSERT(config.is_valid(), "Complete config should be valid");

    auto errors = config.validate();
    TEST_ASSERT(errors.empty(), "Complete config should have no errors");

    return true;
}

bool test_default_emr_config() {
    auto config = default_emr_config();

    TEST_ASSERT(config.enabled == false, "Default should be disabled");
    TEST_ASSERT(config.vendor == emr_vendor::generic, "Default vendor should be generic");
    TEST_ASSERT(config.is_valid(), "Default config should be valid");

    return true;
}

// =============================================================================
// Environment Variable Substitution Tests
// =============================================================================

bool test_env_var_substitution_no_vars() {
    std::string input = "https://example.com/path";
    auto result = substitute_env_vars(input);

    TEST_ASSERT(result == input, "String without vars should be unchanged");

    return true;
}

bool test_env_var_substitution_with_vars() {
    // Set test environment variable
#ifdef _WIN32
    _putenv_s("TEST_EMR_HOST", "test.hospital.local");
#else
    setenv("TEST_EMR_HOST", "test.hospital.local", 1);
#endif

    std::string input = "https://${TEST_EMR_HOST}/fhir/r4";
    auto result = substitute_env_vars(input);

    TEST_ASSERT(result == "https://test.hospital.local/fhir/r4",
                "Env var should be substituted");

    // Clean up
#ifdef _WIN32
    _putenv_s("TEST_EMR_HOST", "");
#else
    unsetenv("TEST_EMR_HOST");
#endif

    return true;
}

bool test_env_var_substitution_missing_var() {
    // Ensure the var doesn't exist
#ifdef _WIN32
    _putenv_s("TEST_MISSING_VAR", "");
#else
    unsetenv("TEST_MISSING_VAR");
#endif

    std::string input = "https://${TEST_MISSING_VAR}/fhir/r4";
    auto result = substitute_env_vars(input);

    // Should keep original when env var not found
    TEST_ASSERT(result == input, "Missing env var should keep original");

    return true;
}

bool test_apply_env_substitution() {
    // Set test environment variables
#ifdef _WIN32
    _putenv_s("TEST_CLIENT_ID", "my_client_id");
    _putenv_s("TEST_CLIENT_SECRET", "my_secret");
#else
    setenv("TEST_CLIENT_ID", "my_client_id", 1);
    setenv("TEST_CLIENT_SECRET", "my_secret", 1);
#endif

    emr_config config;
    config.auth.oauth2.client_id = "${TEST_CLIENT_ID}";
    config.auth.oauth2.client_secret = "${TEST_CLIENT_SECRET}";

    auto result = apply_env_substitution(config);

    TEST_ASSERT(result.auth.oauth2.client_id == "my_client_id",
                "client_id should be substituted");
    TEST_ASSERT(result.auth.oauth2.client_secret == "my_secret",
                "client_secret should be substituted");

    // Clean up
#ifdef _WIN32
    _putenv_s("TEST_CLIENT_ID", "");
    _putenv_s("TEST_CLIENT_SECRET", "");
#else
    unsetenv("TEST_CLIENT_ID");
    unsetenv("TEST_CLIENT_SECRET");
#endif

    return true;
}

// =============================================================================
// Feature Flags Tests
// =============================================================================

bool test_feature_flags_defaults() {
    emr_features_config features;

    TEST_ASSERT(features.patient_lookup == true,
                "Default patient_lookup should be true");
    TEST_ASSERT(features.result_posting == true,
                "Default result_posting should be true");
    TEST_ASSERT(features.encounter_context == true,
                "Default encounter_context should be true");
    TEST_ASSERT(features.auto_retry == true,
                "Default auto_retry should be true");
    TEST_ASSERT(features.caching == true,
                "Default caching should be true");

    return true;
}

}  // namespace pacs::bridge::config::test

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    using namespace pacs::bridge::config::test;

    std::cout << "============================================" << std::endl;
    std::cout << "EMR Configuration Tests" << std::endl;
    std::cout << "============================================" << std::endl;

    int passed = 0;
    int failed = 0;

    // Vendor Tests
    RUN_TEST(test_emr_vendor_to_string);
    RUN_TEST(test_emr_vendor_parsing);

    // Error Code Tests
    RUN_TEST(test_emr_config_error_codes);
    RUN_TEST(test_emr_config_error_strings);

    // Connection Config Tests
    RUN_TEST(test_connection_config_validation);
    RUN_TEST(test_connection_config_defaults);

    // Authentication Config Tests
    RUN_TEST(test_oauth2_config_validation);
    RUN_TEST(test_basic_auth_config_validation);
    RUN_TEST(test_api_key_config_validation);
    RUN_TEST(test_auth_config_validation);
    RUN_TEST(test_oauth2_to_security_config);

    // Retry Config Tests
    RUN_TEST(test_retry_config_validation);
    RUN_TEST(test_retry_config_backoff_calculation);

    // Cache Config Tests
    RUN_TEST(test_cache_config_validation);
    RUN_TEST(test_cache_config_defaults);

    // Complete EMR Config Tests
    RUN_TEST(test_emr_config_disabled_is_valid);
    RUN_TEST(test_emr_config_enabled_requires_connection);
    RUN_TEST(test_emr_config_complete_validation);
    RUN_TEST(test_default_emr_config);

    // Environment Variable Tests
    RUN_TEST(test_env_var_substitution_no_vars);
    RUN_TEST(test_env_var_substitution_with_vars);
    RUN_TEST(test_env_var_substitution_missing_var);
    RUN_TEST(test_apply_env_substitution);

    // Feature Flags Tests
    RUN_TEST(test_feature_flags_defaults);

    std::cout << "============================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "============================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
