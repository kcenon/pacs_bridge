/**
 * @file oauth2_test.cpp
 * @brief Unit tests for OAuth2 authentication components
 *
 * Tests for OAuth2 client, token management, Smart-on-FHIR discovery,
 * and authentication providers.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/103
 */

#include "pacs/bridge/security/auth_provider.h"
#include "pacs/bridge/security/basic_auth_provider.h"
#include "pacs/bridge/security/oauth2_client.h"
#include "pacs/bridge/security/oauth2_types.h"
#include "pacs/bridge/security/smart_configuration.h"
#include "pacs/bridge/security/smart_discovery.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

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
// OAuth2 Error Code Tests
// =============================================================================

bool test_oauth2_error_codes() {
    // Test error code values
    TEST_ASSERT(to_error_code(oauth2_error::token_request_failed) == -1020,
                "token_request_failed should be -1020");
    TEST_ASSERT(to_error_code(oauth2_error::invalid_credentials) == -1021,
                "invalid_credentials should be -1021");
    TEST_ASSERT(to_error_code(oauth2_error::token_expired) == -1022,
                "token_expired should be -1022");
    TEST_ASSERT(to_error_code(oauth2_error::refresh_failed) == -1023,
                "refresh_failed should be -1023");
    TEST_ASSERT(to_error_code(oauth2_error::scope_denied) == -1024,
                "scope_denied should be -1024");
    TEST_ASSERT(to_error_code(oauth2_error::discovery_failed) == -1025,
                "discovery_failed should be -1025");

    // Test error code strings
    TEST_ASSERT(std::string(to_string(oauth2_error::token_request_failed)) ==
                    "Token request to authorization server failed",
                "token_request_failed string incorrect");
    TEST_ASSERT(std::string(to_string(oauth2_error::invalid_credentials)) ==
                    "Invalid client credentials",
                "invalid_credentials string incorrect");

    return true;
}

// =============================================================================
// OAuth2 Token Tests
// =============================================================================

bool test_oauth2_token_not_expired() {
    oauth2_token token;
    token.access_token = "test_token";
    token.token_type = "Bearer";
    token.expires_in = std::chrono::seconds{3600};  // 1 hour
    token.issued_at = std::chrono::system_clock::now();

    TEST_ASSERT(!token.is_expired(), "Fresh token should not be expired");
    TEST_ASSERT(token.is_valid(), "Token with access_token should be valid");

    return true;
}

bool test_oauth2_token_expired() {
    oauth2_token token;
    token.access_token = "test_token";
    token.token_type = "Bearer";
    token.expires_in = std::chrono::seconds{1};
    // Set issued_at to 2 seconds ago
    token.issued_at = std::chrono::system_clock::now() - std::chrono::seconds{2};

    TEST_ASSERT(token.is_expired(), "Old token should be expired");
    TEST_ASSERT(!token.is_valid(), "Expired token should not be valid");

    return true;
}

bool test_oauth2_token_needs_refresh() {
    oauth2_token token;
    token.access_token = "test_token";
    token.expires_in = std::chrono::seconds{30};  // 30 seconds
    token.issued_at = std::chrono::system_clock::now();

    // Token expires in 30 seconds, refresh margin is 60 seconds
    // So it should need refresh
    TEST_ASSERT(token.needs_refresh(std::chrono::seconds{60}),
                "Token expiring in 30s should need refresh with 60s margin");

    // Token expires in 30 seconds, refresh margin is 10 seconds
    // So it should NOT need refresh yet
    TEST_ASSERT(!token.needs_refresh(std::chrono::seconds{10}),
                "Token expiring in 30s should not need refresh with 10s margin");

    return true;
}

bool test_oauth2_token_authorization_header() {
    oauth2_token token;
    token.access_token = "abc123";
    token.token_type = "Bearer";

    TEST_ASSERT(token.authorization_header() == "Bearer abc123",
                "Authorization header should be 'Bearer abc123'");

    return true;
}

bool test_oauth2_token_remaining_validity() {
    oauth2_token token;
    token.access_token = "test_token";
    token.expires_in = std::chrono::seconds{3600};
    token.issued_at = std::chrono::system_clock::now();

    auto remaining = token.remaining_validity();
    TEST_ASSERT(remaining.count() > 3590 && remaining.count() <= 3600,
                "Remaining validity should be close to 3600 seconds");

    return true;
}

bool test_oauth2_token_no_expiration() {
    oauth2_token token;
    token.access_token = "test_token";
    token.expires_in = std::chrono::seconds{0};  // No expiration

    TEST_ASSERT(!token.is_expired(), "Token without expiration should not be expired");
    TEST_ASSERT(!token.needs_refresh(std::chrono::seconds{60}),
                "Token without expiration should not need refresh");

    return true;
}

// =============================================================================
// OAuth2 Config Tests
// =============================================================================

bool test_oauth2_config_validation() {
    oauth2_config config;
    TEST_ASSERT(!config.is_valid(), "Empty config should be invalid");

    config.token_url = "https://auth.example.com/token";
    TEST_ASSERT(!config.is_valid(), "Config without client_id should be invalid");

    config.client_id = "test_client";
    TEST_ASSERT(!config.is_valid(), "Config without client_secret should be invalid");

    config.client_secret = "test_secret";
    TEST_ASSERT(config.is_valid(), "Config with all required fields should be valid");

    return true;
}

bool test_oauth2_config_scopes_string() {
    oauth2_config config;
    config.scopes = {"patient/*.read", "patient/*.write", "openid"};

    TEST_ASSERT(config.scopes_string() == "patient/*.read patient/*.write openid",
                "Scopes should be joined with spaces");

    return true;
}

// =============================================================================
// Token Response Parsing Tests
// =============================================================================

bool test_parse_token_response_success() {
    std::string json = R"({
        "access_token": "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9",
        "token_type": "Bearer",
        "expires_in": 3600,
        "refresh_token": "refresh_abc123",
        "scope": "patient/*.read patient/*.write"
    })";

    auto result = parse_token_response(json);
    TEST_ASSERT(result.has_value(), "Should parse valid JSON successfully");

    auto& response = result.value();
    TEST_ASSERT(response.access_token == "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9",
                "Access token should be parsed");
    TEST_ASSERT(response.token_type == "Bearer", "Token type should be Bearer");
    TEST_ASSERT(response.expires_in == 3600, "Expires in should be 3600");
    TEST_ASSERT(response.refresh_token.has_value() &&
                    *response.refresh_token == "refresh_abc123",
                "Refresh token should be parsed");

    return true;
}

bool test_parse_token_response_error() {
    std::string json = R"({
        "error": "invalid_client",
        "error_description": "Client authentication failed"
    })";

    auto result = parse_token_response(json);
    TEST_ASSERT(!result.has_value(), "Should fail on error response");
    TEST_ASSERT(result.error() == oauth2_error::invalid_credentials,
                "Should map invalid_client to invalid_credentials");

    return true;
}

bool test_parse_token_response_empty() {
    auto result = parse_token_response("");
    TEST_ASSERT(!result.has_value(), "Should fail on empty JSON");
    TEST_ASSERT(result.error() == oauth2_error::invalid_response,
                "Should return invalid_response error");

    return true;
}

bool test_parse_token_response_missing_access_token() {
    std::string json = R"({
        "token_type": "Bearer",
        "expires_in": 3600
    })";

    auto result = parse_token_response(json);
    TEST_ASSERT(!result.has_value(), "Should fail when access_token is missing");
    TEST_ASSERT(result.error() == oauth2_error::invalid_response,
                "Should return invalid_response error");

    return true;
}

// =============================================================================
// Token Request Body Building Tests
// =============================================================================

bool test_build_token_request_body() {
    oauth2_config config;
    config.token_url = "https://auth.example.com/token";
    config.client_id = "my_client";
    config.client_secret = "my_secret";
    config.scopes = {"openid", "profile"};

    auto body = build_token_request_body(config);

    TEST_ASSERT(body.find("grant_type=client_credentials") != std::string::npos,
                "Should include grant_type");
    TEST_ASSERT(body.find("client_id=my_client") != std::string::npos,
                "Should include client_id");
    TEST_ASSERT(body.find("client_secret=my_secret") != std::string::npos,
                "Should include client_secret");
    TEST_ASSERT(body.find("scope=") != std::string::npos,
                "Should include scope");

    return true;
}

// =============================================================================
// Basic Auth Provider Tests
// =============================================================================

bool test_basic_auth_provider_header() {
    basic_auth_provider provider("username", "password");

    auto header = provider.get_authorization_header();
    TEST_ASSERT(header.has_value(), "Should return header");
    TEST_ASSERT(header->substr(0, 6) == "Basic ", "Should start with 'Basic '");

    return true;
}

bool test_basic_auth_provider_is_authenticated() {
    basic_auth_provider provider("username", "password");
    TEST_ASSERT(provider.is_authenticated(), "Should be authenticated with credentials");

    basic_auth_provider empty_provider("", "");
    TEST_ASSERT(!empty_provider.is_authenticated(),
                "Should not be authenticated without credentials");

    return true;
}

bool test_basic_auth_provider_auth_type() {
    basic_auth_provider provider("username", "password");
    TEST_ASSERT(provider.auth_type() == "basic", "Auth type should be 'basic'");

    return true;
}

bool test_basic_auth_provider_invalidate() {
    basic_auth_provider provider("username", "password");
    TEST_ASSERT(provider.is_authenticated(), "Should be authenticated initially");

    provider.invalidate();
    TEST_ASSERT(!provider.is_authenticated(),
                "Should not be authenticated after invalidate");

    return true;
}

// =============================================================================
// Base64 Encoding Tests
// =============================================================================

bool test_base64_encode() {
    TEST_ASSERT(base64_encode("") == "", "Empty string should encode to empty");
    TEST_ASSERT(base64_encode("f") == "Zg==", "'f' should encode to 'Zg=='");
    TEST_ASSERT(base64_encode("fo") == "Zm8=", "'fo' should encode to 'Zm8='");
    TEST_ASSERT(base64_encode("foo") == "Zm9v", "'foo' should encode to 'Zm9v'");
    TEST_ASSERT(base64_encode("foob") == "Zm9vYg==",
                "'foob' should encode to 'Zm9vYg=='");
    TEST_ASSERT(base64_encode("fooba") == "Zm9vYmE=",
                "'fooba' should encode to 'Zm9vYmE='");
    TEST_ASSERT(base64_encode("foobar") == "Zm9vYmFy",
                "'foobar' should encode to 'Zm9vYmFy'");

    return true;
}

bool test_base64_decode() {
    TEST_ASSERT(base64_decode("") == "", "Empty string should decode to empty");
    TEST_ASSERT(base64_decode("Zg==") == "f", "'Zg==' should decode to 'f'");
    TEST_ASSERT(base64_decode("Zm8=") == "fo", "'Zm8=' should decode to 'fo'");
    TEST_ASSERT(base64_decode("Zm9v") == "foo", "'Zm9v' should decode to 'foo'");
    TEST_ASSERT(base64_decode("Zm9vYmFy") == "foobar",
                "'Zm9vYmFy' should decode to 'foobar'");

    return true;
}

bool test_base64_roundtrip() {
    std::string original = "username:password123!@#";
    auto encoded = base64_encode(original);
    auto decoded = base64_decode(encoded);
    TEST_ASSERT(decoded == original, "Base64 roundtrip should preserve data");

    return true;
}

// =============================================================================
// No Auth Provider Tests
// =============================================================================

bool test_no_auth_provider() {
    no_auth_provider provider;

    TEST_ASSERT(provider.is_authenticated(), "No auth should always be authenticated");
    TEST_ASSERT(provider.auth_type() == "none", "Auth type should be 'none'");

    auto header = provider.get_authorization_header();
    TEST_ASSERT(header.has_value(), "Should return header");
    TEST_ASSERT(header->empty(), "Header should be empty");

    return true;
}

// =============================================================================
// Smart Configuration Tests
// =============================================================================

bool test_smart_configuration_supports_capability() {
    smart_configuration config;
    config.capabilities = {"launch-ehr", "client-confidential-symmetric", "sso-openid-connect"};

    TEST_ASSERT(config.supports_capability(smart_capability::launch_ehr),
                "Should support launch-ehr");
    TEST_ASSERT(config.supports_capability(smart_capability::client_confidential_symmetric),
                "Should support client-confidential-symmetric");
    TEST_ASSERT(!config.supports_capability(smart_capability::launch_standalone),
                "Should not support launch-standalone");

    return true;
}

bool test_smart_configuration_supports_scope() {
    smart_configuration config;
    config.scopes_supported = {"openid", "patient/*.read", "patient/*.write"};

    TEST_ASSERT(config.supports_scope("openid"), "Should support openid");
    TEST_ASSERT(config.supports_scope("patient/*.read"), "Should support patient/*.read");
    TEST_ASSERT(!config.supports_scope("admin/*"), "Should not support admin/*");

    return true;
}

bool test_smart_configuration_supports_client_credentials() {
    smart_configuration config;
    config.capabilities = {"client-confidential-symmetric"};

    TEST_ASSERT(config.supports_client_credentials(),
                "Should support client credentials with capability");

    smart_configuration config2;
    config2.grant_types_supported = {"client_credentials"};
    TEST_ASSERT(config2.supports_client_credentials(),
                "Should support client credentials with grant type");

    return true;
}

bool test_smart_configuration_validity() {
    smart_configuration config;
    TEST_ASSERT(!config.is_valid(), "Empty config should be invalid");

    config.token_endpoint = "https://auth.example.com/token";
    TEST_ASSERT(config.is_valid(), "Config with token_endpoint should be valid");

    return true;
}

// =============================================================================
// Smart Discovery URL Tests
// =============================================================================

bool test_smart_discovery_url() {
    TEST_ASSERT(smart_discovery::build_discovery_url("https://fhir.example.com") ==
                    "https://fhir.example.com/.well-known/smart-configuration",
                "Should build correct discovery URL");

    TEST_ASSERT(smart_discovery::build_discovery_url("https://fhir.example.com/") ==
                    "https://fhir.example.com/.well-known/smart-configuration",
                "Should handle trailing slash");

    return true;
}

// =============================================================================
// Smart Discovery Parsing Tests
// =============================================================================

bool test_smart_discovery_parse_configuration() {
    std::string json = R"({
        "issuer": "https://emr.hospital.local/fhir",
        "authorization_endpoint": "https://emr.hospital.local/oauth/authorize",
        "token_endpoint": "https://emr.hospital.local/oauth/token",
        "capabilities": ["launch-ehr", "client-confidential-symmetric"],
        "scopes_supported": ["openid", "patient/*.read"],
        "grant_types_supported": ["authorization_code", "client_credentials"]
    })";

    auto result = smart_discovery::parse_configuration(json);
    TEST_ASSERT(result.has_value(), "Should parse valid configuration");

    auto& config = result.value();
    TEST_ASSERT(config.issuer == "https://emr.hospital.local/fhir",
                "Issuer should be parsed");
    TEST_ASSERT(config.token_endpoint == "https://emr.hospital.local/oauth/token",
                "Token endpoint should be parsed");
    TEST_ASSERT(config.capabilities.size() == 2, "Should have 2 capabilities");
    TEST_ASSERT(config.scopes_supported.size() == 2, "Should have 2 scopes");

    return true;
}

bool test_smart_discovery_parse_empty() {
    auto result = smart_discovery::parse_configuration("");
    TEST_ASSERT(!result.has_value(), "Should fail on empty JSON");

    return true;
}

bool test_smart_discovery_parse_missing_token_endpoint() {
    std::string json = R"({
        "issuer": "https://emr.hospital.local/fhir",
        "authorization_endpoint": "https://emr.hospital.local/oauth/authorize"
    })";

    auto result = smart_discovery::parse_configuration(json);
    TEST_ASSERT(!result.has_value(), "Should fail without token_endpoint");

    return true;
}

// =============================================================================
// Grant Type Tests
// =============================================================================

bool test_grant_type_to_string() {
    TEST_ASSERT(std::string(to_string(oauth2_grant_type::client_credentials)) ==
                    "client_credentials",
                "client_credentials string incorrect");
    TEST_ASSERT(std::string(to_string(oauth2_grant_type::authorization_code)) ==
                    "authorization_code",
                "authorization_code string incorrect");
    TEST_ASSERT(std::string(to_string(oauth2_grant_type::refresh_token)) ==
                    "refresh_token",
                "refresh_token string incorrect");

    return true;
}

// =============================================================================
// Auth Type Parsing Tests
// =============================================================================

bool test_auth_type_parsing() {
    TEST_ASSERT(parse_auth_type("none") == auth_type::none, "Should parse 'none'");
    TEST_ASSERT(parse_auth_type("basic") == auth_type::basic, "Should parse 'basic'");
    TEST_ASSERT(parse_auth_type("oauth2") == auth_type::oauth2, "Should parse 'oauth2'");
    TEST_ASSERT(parse_auth_type("api_key") == auth_type::api_key, "Should parse 'api_key'");
    TEST_ASSERT(!parse_auth_type("invalid").has_value(),
                "Should return nullopt for invalid type");

    return true;
}

// =============================================================================
// OAuth2 Client Tests (with mock HTTP)
// =============================================================================

bool test_oauth2_client_no_http_client() {
    oauth2_config config;
    config.token_url = "https://auth.example.com/token";
    config.client_id = "test_client";
    config.client_secret = "test_secret";

    oauth2_client client(config);

    // Without HTTP client, should return network error
    auto result = client.get_access_token();
    TEST_ASSERT(!result.has_value(), "Should fail without HTTP client");
    TEST_ASSERT(result.error() == oauth2_error::network_error,
                "Should return network_error");

    return true;
}

bool test_oauth2_client_with_mock_http() {
    oauth2_config config;
    config.token_url = "https://auth.example.com/token";
    config.client_id = "test_client";
    config.client_secret = "test_secret";

    // Create mock HTTP client that returns a valid token
    http_post_callback mock_http = [](std::string_view, std::string_view,
                                      std::string_view, std::chrono::seconds)
        -> std::expected<std::string, oauth2_error> {
        return R"({
            "access_token": "mock_token_12345",
            "token_type": "Bearer",
            "expires_in": 3600
        })";
    };

    oauth2_client client(config, mock_http);

    auto result = client.get_access_token();
    TEST_ASSERT(result.has_value(), "Should return token with mock HTTP");
    TEST_ASSERT(*result == "mock_token_12345", "Token should be 'mock_token_12345'");

    // Second call should return cached token
    auto result2 = client.get_access_token();
    TEST_ASSERT(result2.has_value(), "Should return cached token");
    TEST_ASSERT(*result2 == "mock_token_12345", "Cached token should match");

    return true;
}

bool test_oauth2_client_invalidation() {
    oauth2_config config;
    config.token_url = "https://auth.example.com/token";
    config.client_id = "test_client";
    config.client_secret = "test_secret";

    int call_count = 0;
    http_post_callback mock_http = [&call_count](std::string_view, std::string_view,
                                                  std::string_view, std::chrono::seconds)
        -> std::expected<std::string, oauth2_error> {
        call_count++;
        return R"({
            "access_token": "token_" + std::to_string(call_count) + "",
            "token_type": "Bearer",
            "expires_in": 3600
        })";
    };

    oauth2_client client(config, mock_http);

    // First call
    (void)client.get_access_token();
    TEST_ASSERT(call_count == 1, "Should make first HTTP call");

    // Second call (cached)
    (void)client.get_access_token();
    TEST_ASSERT(call_count == 1, "Should use cached token");

    // Invalidate and call again
    client.invalidate();
    (void)client.get_access_token();
    TEST_ASSERT(call_count == 2, "Should make new HTTP call after invalidation");

    return true;
}

bool test_oauth2_client_is_authenticated() {
    oauth2_config config;
    config.token_url = "https://auth.example.com/token";
    config.client_id = "test_client";
    config.client_secret = "test_secret";

    http_post_callback mock_http = [](std::string_view, std::string_view,
                                      std::string_view, std::chrono::seconds)
        -> std::expected<std::string, oauth2_error> {
        return R"({
            "access_token": "mock_token",
            "token_type": "Bearer",
            "expires_in": 3600
        })";
    };

    oauth2_client client(config, mock_http);

    TEST_ASSERT(!client.is_authenticated(), "Should not be authenticated initially");

    (void)client.get_access_token();
    TEST_ASSERT(client.is_authenticated(), "Should be authenticated after getting token");

    client.invalidate();
    TEST_ASSERT(!client.is_authenticated(), "Should not be authenticated after invalidation");

    return true;
}

// =============================================================================
// OAuth2 Auth Provider Tests
// =============================================================================

bool test_oauth2_auth_provider() {
    oauth2_config config;
    config.token_url = "https://auth.example.com/token";
    config.client_id = "test_client";
    config.client_secret = "test_secret";

    oauth2_auth_provider provider(config);

    TEST_ASSERT(provider.auth_type() == "oauth2", "Auth type should be 'oauth2'");
    TEST_ASSERT(provider.can_refresh(), "OAuth2 should support refresh");

    return true;
}

}  // namespace pacs::bridge::security::test

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    using namespace pacs::bridge::security::test;

    std::cout << "============================================" << std::endl;
    std::cout << "OAuth2 Authentication Tests" << std::endl;
    std::cout << "============================================" << std::endl;

    int passed = 0;
    int failed = 0;

    // Error Code Tests
    RUN_TEST(test_oauth2_error_codes);

    // Token Tests
    RUN_TEST(test_oauth2_token_not_expired);
    RUN_TEST(test_oauth2_token_expired);
    RUN_TEST(test_oauth2_token_needs_refresh);
    RUN_TEST(test_oauth2_token_authorization_header);
    RUN_TEST(test_oauth2_token_remaining_validity);
    RUN_TEST(test_oauth2_token_no_expiration);

    // Config Tests
    RUN_TEST(test_oauth2_config_validation);
    RUN_TEST(test_oauth2_config_scopes_string);

    // Token Response Parsing Tests
    RUN_TEST(test_parse_token_response_success);
    RUN_TEST(test_parse_token_response_error);
    RUN_TEST(test_parse_token_response_empty);
    RUN_TEST(test_parse_token_response_missing_access_token);

    // Token Request Body Tests
    RUN_TEST(test_build_token_request_body);

    // Basic Auth Provider Tests
    RUN_TEST(test_basic_auth_provider_header);
    RUN_TEST(test_basic_auth_provider_is_authenticated);
    RUN_TEST(test_basic_auth_provider_auth_type);
    RUN_TEST(test_basic_auth_provider_invalidate);

    // Base64 Tests
    RUN_TEST(test_base64_encode);
    RUN_TEST(test_base64_decode);
    RUN_TEST(test_base64_roundtrip);

    // No Auth Provider Tests
    RUN_TEST(test_no_auth_provider);

    // Smart Configuration Tests
    RUN_TEST(test_smart_configuration_supports_capability);
    RUN_TEST(test_smart_configuration_supports_scope);
    RUN_TEST(test_smart_configuration_supports_client_credentials);
    RUN_TEST(test_smart_configuration_validity);

    // Smart Discovery URL Tests
    RUN_TEST(test_smart_discovery_url);

    // Smart Discovery Parsing Tests
    RUN_TEST(test_smart_discovery_parse_configuration);
    RUN_TEST(test_smart_discovery_parse_empty);
    RUN_TEST(test_smart_discovery_parse_missing_token_endpoint);

    // Grant Type Tests
    RUN_TEST(test_grant_type_to_string);

    // Auth Type Parsing Tests
    RUN_TEST(test_auth_type_parsing);

    // OAuth2 Client Tests
    RUN_TEST(test_oauth2_client_no_http_client);
    RUN_TEST(test_oauth2_client_with_mock_http);
    RUN_TEST(test_oauth2_client_invalidation);
    RUN_TEST(test_oauth2_client_is_authenticated);

    // OAuth2 Auth Provider Tests
    RUN_TEST(test_oauth2_auth_provider);

    std::cout << "============================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "============================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
