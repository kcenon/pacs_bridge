/**
 * @file security_test.cpp
 * @brief Unit tests for security hardening components
 *
 * Tests for input validation, PHI sanitization, audit logging,
 * access control, and rate limiting.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include "pacs/bridge/security/access_control.h"
#include "pacs/bridge/security/audit_logger.h"
#include "pacs/bridge/security/input_validator.h"
#include "pacs/bridge/security/log_sanitizer.h"
#include "pacs/bridge/security/rate_limiter.h"

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
// Input Validator Tests
// =============================================================================

bool test_input_validator_empty_message() {
    input_validator validator;

    auto result = validator.validate("");
    TEST_ASSERT(!result.valid, "Empty message should be invalid");
    TEST_ASSERT(result.error == validation_error::empty_message,
                "Error should be empty_message");

    return true;
}

bool test_input_validator_valid_hl7() {
    input_validator validator;

    std::string valid_hl7 =
        "MSH|^~\\&|SENDING_APP|SENDING_FACILITY|RECEIVING_APP|RECEIVING_FACILITY|"
        "20240101120000||ADT^A01|MSG001|P|2.4";

    auto result = validator.validate(valid_hl7);
    TEST_ASSERT(result.valid, "Valid HL7 message should pass validation");
    TEST_ASSERT(result.message_type.has_value() && *result.message_type == "ADT^A01",
                "Message type should be extracted");
    TEST_ASSERT(result.message_control_id.has_value() && *result.message_control_id == "MSG001",
                "Message control ID should be extracted");

    return true;
}

bool test_input_validator_missing_msh() {
    input_validator validator;

    std::string invalid_hl7 = "PID|1|12345||Doe^John||19800101|M";

    auto result = validator.validate(invalid_hl7);
    TEST_ASSERT(!result.valid, "Message without MSH should be invalid");
    TEST_ASSERT(result.error == validation_error::missing_msh_segment,
                "Error should be missing_msh_segment");

    return true;
}

bool test_input_validator_size_limit() {
    validation_config config;
    config.max_message_size = 100;
    input_validator validator(config);

    std::string large_message(200, 'X');
    auto result = validator.validate(large_message);
    TEST_ASSERT(!result.valid, "Oversized message should be invalid");
    TEST_ASSERT(result.error == validation_error::message_too_large,
                "Error should be message_too_large");

    return true;
}

bool test_input_validator_sql_injection_detection() {
    validation_config config;
    config.detect_sql_injection = true;
    input_validator validator(config);

    std::string sql_injection =
        "MSH|^~\\&|SENDER|FAC|RECV|FAC|20240101||ADT^A01|1|P|2.4\rPID|1|SELECT * FROM users--|";

    auto result = validator.validate(sql_injection);
    TEST_ASSERT(!result.valid, "SQL injection should be detected");
    TEST_ASSERT(result.error == validation_error::injection_detected,
                "Error should be injection_detected");

    return true;
}

bool test_input_validator_command_injection_detection() {
    validation_config config;
    config.detect_command_injection = true;
    input_validator validator(config);

    std::string cmd_injection =
        "MSH|^~\\&|SENDER|FAC|RECV|FAC|20240101||ADT^A01|1|P|2.4\rPID|1|; rm -rf /|";

    auto result = validator.validate(cmd_injection);
    TEST_ASSERT(!result.valid, "Command injection should be detected");
    TEST_ASSERT(result.error == validation_error::injection_detected,
                "Error should be injection_detected");

    return true;
}

bool test_input_validator_application_whitelist() {
    validation_config config;
    config.allowed_sending_apps.insert("APPROVED_APP");
    input_validator validator(config);

    std::string approved =
        "MSH|^~\\&|APPROVED_APP|FAC|RECV|FAC|20240101||ADT^A01|1|P|2.4";

    auto result_approved = validator.validate(approved);
    TEST_ASSERT(result_approved.valid, "Approved app should pass");

    std::string unapproved =
        "MSH|^~\\&|UNKNOWN_APP|FAC|RECV|FAC|20240101||ADT^A01|1|P|2.4";

    auto result_unapproved = validator.validate(unapproved);
    TEST_ASSERT(!result_unapproved.valid, "Unapproved app should fail");
    TEST_ASSERT(result_unapproved.error == validation_error::invalid_application_id,
                "Error should be invalid_application_id");

    return true;
}

bool test_input_validator_sanitization() {
    input_validator validator;

    std::string with_nulls = std::string("MSH|^~\\&|") + '\0' + std::string("TEST");
    auto sanitized = validator.sanitize(with_nulls);

    TEST_ASSERT(sanitized.find('\0') == std::string::npos,
                "Null bytes should be stripped");

    return true;
}

// =============================================================================
// Log Sanitizer Tests
// =============================================================================

bool test_log_sanitizer_disabled() {
    healthcare_sanitization_config config;
    config.enabled = false;
    healthcare_log_sanitizer sanitizer(config);

    std::string content = "Patient: John Doe, SSN: 123-45-6789";
    auto result = sanitizer.sanitize(content);

    TEST_ASSERT(result == content, "Disabled sanitizer should not modify content");

    return true;
}

bool test_log_sanitizer_ssn_detection() {
    healthcare_sanitization_config config;
    config.enabled = true;
    healthcare_log_sanitizer sanitizer(config);

    std::string content = "SSN: 123-45-6789";
    auto result = sanitizer.sanitize(content);

    TEST_ASSERT(result.find("123-45-6789") == std::string::npos,
                "SSN should be sanitized");
    TEST_ASSERT(result.find("[SSN]") != std::string::npos,
                "SSN should be replaced with marker");

    return true;
}

bool test_log_sanitizer_phone_detection() {
    healthcare_sanitization_config config;
    config.enabled = true;
    healthcare_log_sanitizer sanitizer(config);

    std::string content = "Phone: (555) 123-4567";
    auto result = sanitizer.sanitize(content);

    TEST_ASSERT(result.find("123-4567") == std::string::npos,
                "Phone number should be sanitized");
    TEST_ASSERT(result.find("[PHONE]") != std::string::npos,
                "Phone should be replaced with marker");

    return true;
}

bool test_log_sanitizer_email_detection() {
    healthcare_sanitization_config config;
    config.enabled = true;
    healthcare_log_sanitizer sanitizer(config);

    std::string content = "Email: patient@example.com";
    auto result = sanitizer.sanitize(content);

    TEST_ASSERT(result.find("patient@example.com") == std::string::npos,
                "Email should be sanitized");
    TEST_ASSERT(result.find("[EMAIL]") != std::string::npos,
                "Email should be replaced with marker");

    return true;
}

bool test_log_sanitizer_mrn_detection() {
    healthcare_sanitization_config config;
    config.enabled = true;
    healthcare_log_sanitizer sanitizer(config);

    std::string content = "MRN: ABC12345";
    auto result = sanitizer.sanitize(content);

    TEST_ASSERT(result.find("ABC12345") == std::string::npos,
                "MRN should be sanitized");
    TEST_ASSERT(result.find("[PATIENT_ID]") != std::string::npos,
                "MRN should be replaced with marker");

    return true;
}

bool test_log_sanitizer_hl7_message() {
    healthcare_sanitization_config config;
    config.enabled = true;
    config.phi_segments = {"PID"};
    healthcare_log_sanitizer sanitizer(config);

    std::string hl7 =
        "MSH|^~\\&|SENDER|FAC|RECV|FAC|20240101||ADT^A01|1|P|2.4\r"
        "PID|1|MRN123||Doe^John||19800101|M|||123 Main St";

    auto result = sanitizer.sanitize_hl7(hl7);

    TEST_ASSERT(result.find("MSH|") != std::string::npos,
                "MSH segment should be preserved");
    TEST_ASSERT(result.find("Doe^John") == std::string::npos,
                "Patient name should be sanitized from PID");

    return true;
}

bool test_log_sanitizer_contains_phi() {
    healthcare_log_sanitizer sanitizer;

    TEST_ASSERT(sanitizer.contains_phi("SSN: 123-45-6789"),
                "Should detect SSN as PHI");
    TEST_ASSERT(sanitizer.contains_phi("Email: test@example.com"),
                "Should detect email as PHI");
    TEST_ASSERT(!sanitizer.contains_phi("Normal text without PHI"),
                "Should not detect PHI in clean text");

    return true;
}

bool test_log_sanitizer_custom_pattern() {
    healthcare_log_sanitizer sanitizer;

    // Add custom pattern for internal ID
    sanitizer.add_custom_pattern(R"(\bINT-\d{6}\b)", "[INTERNAL_ID]");

    std::string content = "Internal ID: INT-123456";
    auto result = sanitizer.sanitize(content);

    TEST_ASSERT(result.find("INT-123456") == std::string::npos,
                "Custom pattern should be sanitized");
    TEST_ASSERT(result.find("[INTERNAL_ID]") != std::string::npos,
                "Custom pattern should be replaced");

    return true;
}

bool test_log_sanitizer_masking_styles() {
    // Test asterisks style
    {
        healthcare_sanitization_config config;
        config.enabled = true;
        config.style = masking_style::asterisks;
        healthcare_log_sanitizer sanitizer(config);

        auto masked = sanitizer.mask("SECRET", phi_field_type::patient_id);
        TEST_ASSERT(masked == "******", "Asterisks masking should replace with *");
    }

    // Test x_characters style
    {
        healthcare_sanitization_config config;
        config.enabled = true;
        config.style = masking_style::x_characters;
        healthcare_log_sanitizer sanitizer(config);

        auto masked = sanitizer.mask("SECRET", phi_field_type::patient_id);
        TEST_ASSERT(masked == "XXXXXX", "X-character masking should replace with X");
    }

    // Test remove style
    {
        healthcare_sanitization_config config;
        config.enabled = true;
        config.style = masking_style::remove;
        healthcare_log_sanitizer sanitizer(config);

        auto masked = sanitizer.mask("SECRET", phi_field_type::patient_id);
        TEST_ASSERT(masked.empty(), "Remove style should return empty string");
    }

    return true;
}

bool test_safe_hl7_summary() {
    std::string hl7 =
        "MSH|^~\\&|SENDER|FACILITY|RECEIVER|FAC|20240101||ADT^A01|MSG001|P|2.4";

    auto summary = make_safe_hl7_summary(hl7);

    TEST_ASSERT(summary.find("ADT^A01") != std::string::npos,
                "Summary should contain message type");
    TEST_ASSERT(summary.find("MSG001") != std::string::npos,
                "Summary should contain control ID");
    TEST_ASSERT(summary.find("SENDER") != std::string::npos,
                "Summary should contain sending app");

    return true;
}

bool test_safe_session_desc() {
    auto desc = make_safe_session_desc("192.168.1.100", 2575, 12345, true);

    TEST_ASSERT(desc.find("session=12345") != std::string::npos,
                "Session ID should be included");
    TEST_ASSERT(desc.find("192.168.x.x") != std::string::npos,
                "IP should be partially masked");
    TEST_ASSERT(desc.find("2575") != std::string::npos,
                "Port should be included");

    return true;
}

// =============================================================================
// Audit Logger Tests
// =============================================================================

bool test_audit_event_record_structure() {
    healthcare_audit_event_record event;
    event.timestamp = std::chrono::system_clock::now();
    event.event_id = "EVT001";
    event.category = healthcare_audit_category::hl7_transaction;
    event.type = healthcare_audit_event::hl7_message_received;
    event.severity = audit_severity::info;
    event.description = "Test event";
    event.source_component = "test";
    event.message_control_id = "MSG001";
    event.message_type = "ADT^A01";
    event.outcome = "success";

    TEST_ASSERT(!event.event_id.empty(), "Event should have an ID");
    TEST_ASSERT(event.category == healthcare_audit_category::hl7_transaction,
                "Category should match");
    TEST_ASSERT(event.type == healthcare_audit_event::hl7_message_received,
                "Event type should match");

    return true;
}

bool test_audit_event_builder() {
    healthcare_audit_config config;
    config.enabled = false;  // Disable actual logging for test
    healthcare_audit_logger logger(config);

    // Use the fluent API via log_event
    auto builder = logger.log_event(
        healthcare_audit_category::hl7_transaction,
        healthcare_audit_event::hl7_message_received);

    builder.description("Test message received")
           .message("MSG001", "ADT^A01", "SENDER", "FACILITY")
           .outcome("success")
           .processing_time(15.5);
    // Note: commit() would actually log the event

    // Just verify builder doesn't throw
    return true;
}

bool test_audit_event_serialization() {
    healthcare_audit_event_record event;
    event.timestamp = std::chrono::system_clock::now();
    event.event_id = "EVT001";
    event.category = healthcare_audit_category::phi_access;
    event.type = healthcare_audit_event::phi_accessed;
    event.severity = audit_severity::info;
    event.description = "Patient data accessed";
    event.outcome = "success";
    event.properties["user"] = "nurse@hospital.org";

    // Get JSON representation
    std::string json = event.to_json();

    TEST_ASSERT(json.find("event_id") != std::string::npos,
                "JSON should contain event_id");
    TEST_ASSERT(json.find("category") != std::string::npos,
                "JSON should contain category");

    return true;
}

bool test_audit_logger_hl7_transaction() {
    healthcare_audit_config config;
    config.enabled = false;  // Disable actual file logging for test
    healthcare_audit_logger logger(config);

    // This should not throw
    logger.log_hl7_received("ADT^A01", "MSG001", "SENDER", 1024, 12345);
    logger.log_hl7_processed("MSG001", true, 15.5);
    logger.log_hl7_response("MSG001", true, "AA");

    return true;
}

bool test_audit_logger_security_event() {
    healthcare_audit_config config;
    config.enabled = false;
    healthcare_audit_logger logger(config);

    // Log various security events
    logger.log_auth_attempt("192.168.1.100", true, "TLS", "Certificate valid");
    logger.log_access_denied("10.0.0.1", "Not whitelisted", 12345);
    logger.log_rate_limited("192.168.1.100", "requests_per_second", 12345);
    logger.log_security_violation(audit_severity::warning, "Suspicious activity detected");

    return true;
}

bool test_audit_logger_system_events() {
    healthcare_audit_config config;
    config.enabled = false;
    healthcare_audit_logger logger(config);

    logger.log_system_start("1.0.0", "/etc/pacs_bridge/config.json");
    logger.log_config_change("mllp", "max_connections", "100", "200");
    logger.log_system_stop("shutdown");

    return true;
}

bool test_audit_logger_network_events() {
    healthcare_audit_config config;
    config.enabled = false;
    healthcare_audit_logger logger(config);

    logger.log_connection_opened("192.168.1.100", 2575, 12345, true);
    logger.log_connection_closed(12345, "normal");
    logger.log_connection_rejected("10.0.0.1", "not whitelisted");

    return true;
}

bool test_audit_category_to_string() {
    TEST_ASSERT(std::string(to_string(healthcare_audit_category::system)).find("system") != std::string::npos ||
                std::string(to_string(healthcare_audit_category::system)).length() > 0,
                "Category string conversion should work");

    TEST_ASSERT(std::string(to_string(healthcare_audit_category::hl7_transaction)).length() > 0,
                "HL7 transaction category should convert to string");

    return true;
}

bool test_audit_severity_to_string() {
    TEST_ASSERT(std::string(to_string(audit_severity::info)).length() > 0,
                "Info severity should convert to string");
    TEST_ASSERT(std::string(to_string(audit_severity::error)).length() > 0,
                "Error severity should convert to string");
    TEST_ASSERT(std::string(to_string(audit_severity::critical)).length() > 0,
                "Critical severity should convert to string");

    return true;
}

// =============================================================================
// Access Control Tests
// =============================================================================

bool test_ip_range_from_cidr() {
    // Single IP
    auto single = ip_range::from_cidr("192.168.1.100");
    TEST_ASSERT(single.has_value(), "Single IP should parse");
    TEST_ASSERT(single->matches("192.168.1.100"), "Should match exact IP");
    TEST_ASSERT(!single->matches("192.168.1.101"), "Should not match different IP");

    // /24 subnet
    auto subnet = ip_range::from_cidr("10.0.0.0/24");
    TEST_ASSERT(subnet.has_value(), "/24 CIDR should parse");
    TEST_ASSERT(subnet->matches("10.0.0.1"), "Should match IP in subnet");
    TEST_ASSERT(subnet->matches("10.0.0.255"), "Should match broadcast");
    TEST_ASSERT(!subnet->matches("10.0.1.1"), "Should not match IP outside subnet");

    // /16 subnet
    auto large = ip_range::from_cidr("172.16.0.0/16");
    TEST_ASSERT(large.has_value(), "/16 CIDR should parse");
    TEST_ASSERT(large->matches("172.16.100.50"), "Should match IP in /16");
    TEST_ASSERT(!large->matches("172.17.0.1"), "Should not match outside /16");

    return true;
}

bool test_ip_range_invalid() {
    TEST_ASSERT(!ip_range::from_cidr("invalid").has_value(),
                "Invalid IP should fail");
    TEST_ASSERT(!ip_range::from_cidr("256.1.1.1").has_value(),
                "Out of range octet should fail");
    TEST_ASSERT(!ip_range::from_cidr("192.168.1.0/33").has_value(),
                "Invalid prefix length should fail");

    return true;
}

bool test_access_controller_whitelist() {
    access_control_config config;
    config.enabled = true;
    config.mode = access_control_mode::whitelist_only;
    config.whitelisted_ranges.push_back(*ip_range::from_cidr("192.168.1.0/24"));

    access_controller controller(config);

    auto allowed = controller.check_access("192.168.1.50");
    TEST_ASSERT(allowed.allowed, "Whitelisted IP should be allowed");

    auto denied = controller.check_access("10.0.0.1");
    TEST_ASSERT(!denied.allowed, "Non-whitelisted IP should be denied");
    TEST_ASSERT(denied.error == access_error::ip_not_whitelisted,
                "Error should be ip_not_whitelisted");

    return true;
}

bool test_access_controller_blacklist() {
    access_control_config config;
    config.enabled = true;
    config.mode = access_control_mode::blacklist_only;
    config.blacklisted_ranges.push_back(*ip_range::from_cidr("10.0.0.0/8"));

    access_controller controller(config);

    auto allowed = controller.check_access("192.168.1.50");
    TEST_ASSERT(allowed.allowed, "Non-blacklisted IP should be allowed");

    auto denied = controller.check_access("10.1.2.3");
    TEST_ASSERT(!denied.allowed, "Blacklisted IP should be denied");
    TEST_ASSERT(denied.error == access_error::ip_blacklisted,
                "Error should be ip_blacklisted");

    return true;
}

bool test_access_controller_temporary_block() {
    access_control_config config;
    config.enabled = true;
    config.mode = access_control_mode::whitelist_only;
    config.whitelisted_ranges.push_back(*ip_range::from_cidr("192.168.1.0/24"));

    access_controller controller(config);

    // Temporarily block an otherwise allowed IP
    controller.temporarily_block("192.168.1.50", std::chrono::seconds(60));

    auto result = controller.check_access("192.168.1.50");
    TEST_ASSERT(!result.allowed, "Temporarily blocked IP should be denied");
    TEST_ASSERT(result.error == access_error::temporarily_blocked,
                "Error should be temporarily_blocked");

    // Unblock and check again
    controller.unblock("192.168.1.50");

    auto unblocked = controller.check_access("192.168.1.50");
    TEST_ASSERT(unblocked.allowed, "Unblocked IP should be allowed again");

    return true;
}

bool test_access_controller_disabled() {
    access_control_config config;
    config.enabled = false;

    access_controller controller(config);

    auto result = controller.check_access("any.ip.address");
    TEST_ASSERT(result.allowed, "Disabled controller should allow all");

    return true;
}

bool test_access_controller_application_id() {
    access_control_config config;
    config.enabled = true;
    config.allowed_sending_apps = {"APPROVED_APP", "TRUSTED_SYSTEM"};

    access_controller controller(config);

    auto approved = controller.check_application("APPROVED_APP");
    TEST_ASSERT(approved.allowed, "Approved app should be allowed");

    auto unknown = controller.check_application("UNKNOWN_APP");
    TEST_ASSERT(!unknown.allowed, "Unknown app should be denied");
    TEST_ASSERT(unknown.error == access_error::application_not_allowed,
                "Error should be application_not_allowed");

    return true;
}

bool test_access_controller_localhost() {
    access_control_config config;
    config.enabled = true;
    config.mode = access_control_mode::whitelist_only;
    config.allow_localhost = true;
    // No IPs in whitelist

    access_controller controller(config);

    TEST_ASSERT(controller.check_access("127.0.0.1").allowed,
                "Localhost should be allowed when allow_localhost is true");
    TEST_ASSERT(controller.check_access("::1").allowed,
                "IPv6 localhost should be allowed");

    return true;
}

bool test_is_private_ip() {
    TEST_ASSERT(is_private_ip("192.168.1.100"), "192.168.x.x should be private");
    TEST_ASSERT(is_private_ip("10.0.0.1"), "10.x.x.x should be private");
    TEST_ASSERT(is_private_ip("172.16.0.1"), "172.16.x.x should be private");
    TEST_ASSERT(!is_private_ip("8.8.8.8"), "8.8.8.8 should not be private");
    TEST_ASSERT(!is_private_ip("1.2.3.4"), "1.2.3.4 should not be private");

    return true;
}

// =============================================================================
// Rate Limiter Tests
// =============================================================================

bool test_rate_limiter_within_limit() {
    rate_limit_config config;
    config.enabled = true;
    config.requests_per_second = 10;
    config.requests_per_minute = 100;

    rate_limiter limiter(config);

    // First request should always succeed
    auto result = limiter.check_limit("192.168.1.100");
    TEST_ASSERT(result.allowed, "First request should be allowed");
    TEST_ASSERT(result.remaining > 0, "Should have remaining requests");

    return true;
}

bool test_rate_limiter_exceeded() {
    rate_limit_config config;
    config.enabled = true;
    config.requests_per_second = 2;  // Very low limit for testing
    config.burst_size = 2;

    rate_limiter limiter(config);

    std::string client_ip = "192.168.1.100";

    // Exhaust the limit
    limiter.check_limit(client_ip);
    limiter.check_limit(client_ip);

    // Third request should be denied
    auto result = limiter.check_limit(client_ip);
    TEST_ASSERT(!result.allowed, "Request after limit should be denied");
    TEST_ASSERT(result.retry_after > std::chrono::seconds(0),
                "Should have retry_after value");

    return true;
}

bool test_rate_limiter_per_application() {
    rate_limit_config config;
    config.enabled = true;
    config.requests_per_second = 5;

    rate_limiter limiter(config);

    // Set specific limit for an application
    limiter.set_application_limit("HIGH_VOLUME_APP", 100, 1000);

    auto normal = limiter.check_limit("192.168.1.1", "NORMAL_APP");
    TEST_ASSERT(normal.allowed, "Normal app should be allowed");

    auto high_volume = limiter.check_limit("192.168.1.2", "HIGH_VOLUME_APP");
    TEST_ASSERT(high_volume.allowed, "High volume app should be allowed");

    return true;
}

bool test_rate_limiter_disabled() {
    rate_limit_config config;
    config.enabled = false;

    rate_limiter limiter(config);

    // Should always allow when disabled
    for (int i = 0; i < 1000; ++i) {
        auto result = limiter.check_limit("192.168.1.100");
        if (!result.allowed) {
            TEST_ASSERT(false, "Disabled limiter should always allow");
        }
    }

    return true;
}

bool test_rate_limiter_size_based() {
    rate_limit_config config;
    config.enabled = true;
    config.max_bytes_per_second = 1000;  // 1KB/sec limit

    rate_limiter limiter(config);

    std::string client = "192.168.1.100";

    // First request with 500 bytes should succeed
    auto result1 = limiter.check_limit(client, std::nullopt, 500);
    TEST_ASSERT(result1.allowed, "First 500 bytes should be allowed");

    // Second request with 500 bytes should succeed
    auto result2 = limiter.check_limit(client, std::nullopt, 500);
    TEST_ASSERT(result2.allowed, "Next 500 bytes should be allowed");

    // Third request with 500 bytes should fail (would exceed 1KB)
    auto result3 = limiter.check_limit(client, std::nullopt, 500);
    TEST_ASSERT(!result3.allowed, "Additional 500 bytes should be denied");

    return true;
}

bool test_rate_limiter_http_headers() {
    rate_limit_config config;
    config.enabled = true;
    config.requests_per_second = 10;

    rate_limiter limiter(config);

    auto result = limiter.check_limit("192.168.1.100");
    auto headers = make_rate_limit_headers(result);

    TEST_ASSERT(headers.find("X-RateLimit-Limit") != headers.end(),
                "Should have X-RateLimit-Limit header");
    TEST_ASSERT(headers.find("X-RateLimit-Remaining") != headers.end(),
                "Should have X-RateLimit-Remaining header");

    return true;
}

bool test_rate_limiter_statistics() {
    rate_limit_config config;
    config.enabled = true;
    config.requests_per_second = 1;  // Low limit to trigger denials

    rate_limiter limiter(config);

    std::string client = "192.168.1.100";

    // Make several requests
    limiter.check_limit(client);
    limiter.check_limit(client);  // This should be denied
    limiter.check_limit(client);  // This should be denied

    auto stats = limiter.statistics();
    TEST_ASSERT(stats.total_requests >= 3, "Should track total requests");
    TEST_ASSERT(stats.denied_requests >= 1, "Should track denied requests");

    return true;
}

bool test_rate_limiter_reset() {
    rate_limit_config config;
    config.enabled = true;
    config.requests_per_second = 1;
    config.burst_size = 1;

    rate_limiter limiter(config);

    std::string client = "192.168.1.100";

    // Exhaust limit
    limiter.check_limit(client);
    auto denied = limiter.check_limit(client);
    TEST_ASSERT(!denied.allowed, "Should be rate limited");

    // Reset the client's limit
    limiter.reset(client);

    auto after_reset = limiter.check_limit(client);
    TEST_ASSERT(after_reset.allowed, "Should be allowed after reset");

    return true;
}

// =============================================================================
// Integration Tests
// =============================================================================

bool test_security_pipeline_integration() {
    // Test the full security pipeline
    input_validator validator;
    healthcare_log_sanitizer sanitizer;

    healthcare_audit_config audit_config;
    audit_config.enabled = false;  // Disable file logging for test
    healthcare_audit_logger logger(audit_config);

    access_control_config ac_config;
    ac_config.enabled = true;
    ac_config.mode = access_control_mode::whitelist_only;
    ac_config.whitelisted_ranges.push_back(*ip_range::from_cidr("192.168.0.0/16"));
    access_controller access(ac_config);

    rate_limit_config rl_config;
    rl_config.enabled = true;
    rate_limiter limiter(rl_config);

    std::string client_ip = "192.168.1.100";
    std::string hl7_message =
        "MSH|^~\\&|SENDER|FAC|RECV|FAC|20240101||ADT^A01|MSG001|P|2.4\r"
        "PID|1|MRN123||Doe^John||19800101|M";

    // Step 1: Check access control
    auto access_result = access.check_access(client_ip);
    TEST_ASSERT(access_result.allowed, "Access should be allowed");

    // Step 2: Check rate limit
    auto rate_result = limiter.check_limit(client_ip);
    TEST_ASSERT(rate_result.allowed, "Rate limit should allow");

    // Step 3: Validate input
    auto validation = validator.validate(hl7_message);
    TEST_ASSERT(validation.valid, "HL7 should be valid");

    // Step 4: Sanitize for logging
    auto sanitized = sanitizer.sanitize_hl7(hl7_message);
    TEST_ASSERT(sanitized.find("Doe^John") == std::string::npos,
                "PHI should be sanitized");

    // Step 5: Log the transaction
    logger.log_hl7_received(*validation.message_type,
                            *validation.message_control_id,
                            "SENDER",
                            hl7_message.size(),
                            12345);

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== Input Validator Tests ===" << std::endl;
    RUN_TEST(test_input_validator_empty_message);
    RUN_TEST(test_input_validator_valid_hl7);
    RUN_TEST(test_input_validator_missing_msh);
    RUN_TEST(test_input_validator_size_limit);
    RUN_TEST(test_input_validator_sql_injection_detection);
    RUN_TEST(test_input_validator_command_injection_detection);
    RUN_TEST(test_input_validator_application_whitelist);
    RUN_TEST(test_input_validator_sanitization);

    std::cout << "\n=== Log Sanitizer Tests ===" << std::endl;
    RUN_TEST(test_log_sanitizer_disabled);
    RUN_TEST(test_log_sanitizer_ssn_detection);
    RUN_TEST(test_log_sanitizer_phone_detection);
    RUN_TEST(test_log_sanitizer_email_detection);
    RUN_TEST(test_log_sanitizer_mrn_detection);
    RUN_TEST(test_log_sanitizer_hl7_message);
    RUN_TEST(test_log_sanitizer_contains_phi);
    RUN_TEST(test_log_sanitizer_custom_pattern);
    RUN_TEST(test_log_sanitizer_masking_styles);
    RUN_TEST(test_safe_hl7_summary);
    RUN_TEST(test_safe_session_desc);

    std::cout << "\n=== Audit Logger Tests ===" << std::endl;
    RUN_TEST(test_audit_event_record_structure);
    RUN_TEST(test_audit_event_builder);
    RUN_TEST(test_audit_event_serialization);
    RUN_TEST(test_audit_logger_hl7_transaction);
    RUN_TEST(test_audit_logger_security_event);
    RUN_TEST(test_audit_logger_system_events);
    RUN_TEST(test_audit_logger_network_events);
    RUN_TEST(test_audit_category_to_string);
    RUN_TEST(test_audit_severity_to_string);

    std::cout << "\n=== Access Control Tests ===" << std::endl;
    RUN_TEST(test_ip_range_from_cidr);
    RUN_TEST(test_ip_range_invalid);
    RUN_TEST(test_access_controller_whitelist);
    RUN_TEST(test_access_controller_blacklist);
    RUN_TEST(test_access_controller_temporary_block);
    RUN_TEST(test_access_controller_disabled);
    RUN_TEST(test_access_controller_application_id);
    RUN_TEST(test_access_controller_localhost);
    RUN_TEST(test_is_private_ip);

    std::cout << "\n=== Rate Limiter Tests ===" << std::endl;
    RUN_TEST(test_rate_limiter_within_limit);
    RUN_TEST(test_rate_limiter_exceeded);
    RUN_TEST(test_rate_limiter_per_application);
    RUN_TEST(test_rate_limiter_disabled);
    RUN_TEST(test_rate_limiter_size_based);
    RUN_TEST(test_rate_limiter_http_headers);
    RUN_TEST(test_rate_limiter_statistics);
    RUN_TEST(test_rate_limiter_reset);

    std::cout << "\n=== Integration Tests ===" << std::endl;
    RUN_TEST(test_security_pipeline_integration);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::security::test

int main() {
    return pacs::bridge::security::test::run_all_tests();
}
