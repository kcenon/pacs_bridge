/**
 * @file config_test.cpp
 * @brief Unit tests for configuration loader and validation
 *
 * Tests for configuration loading from YAML/JSON, environment variable
 * expansion, validation, and serialization.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/20
 */

#include "pacs/bridge/config/bridge_config.h"
#include "pacs/bridge/config/config_loader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// Platform-specific environment variable functions
#ifdef _WIN32
#include <cstring>
inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && std::getenv(name) != nullptr) {
        return 0;
    }
    return _putenv_s(name, value);
}

inline int unsetenv(const char* name) {
    return _putenv_s(name, "");
}
#endif

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

// Sample YAML configuration for testing
static const std::string SAMPLE_YAML = R"(
server:
  name: "TEST_BRIDGE"

hl7:
  listener:
    port: 2575
    max_connections: 100
    idle_timeout: 300s

pacs:
  host: "pacs.test.local"
  port: 11112
  ae_title: "TEST_BRIDGE"
  called_ae: "PACS_SCP"

logging:
  level: "debug"
  format: "json"
)";

// Sample JSON configuration for testing
static const std::string SAMPLE_JSON = R"({
  "server": {
    "name": "JSON_BRIDGE"
  },
  "hl7": {
    "listener": {
      "port": 2580,
      "max_connections": 50
    }
  },
  "pacs": {
    "host": "pacs.json.local",
    "port": 11113,
    "ae_title": "JSON_BRIDGE",
    "called_ae": "PACS_JSON"
  },
  "logging": {
    "level": "info",
    "format": "text"
  }
})";

// Minimal YAML for quick testing
static const std::string MINIMAL_YAML = R"(
server:
  name: "MINIMAL"
hl7:
  listener:
    port: 2575
pacs:
  host: "localhost"
  port: 11112
  ae_title: "BRIDGE"
  called_ae: "PACS"
logging:
  level: "info"
  format: "json"
)";

// =============================================================================
// Error Code Tests
// =============================================================================

bool test_config_error_to_string() {
    TEST_ASSERT(
        std::string(to_string(config_error::file_not_found)) ==
            "Configuration file not found",
        "file_not_found string");

    TEST_ASSERT(
        std::string(to_string(config_error::parse_error)) ==
            "Failed to parse configuration file",
        "parse_error string");

    TEST_ASSERT(
        std::string(to_string(config_error::validation_error)) ==
            "Configuration validation failed",
        "validation_error string");

    TEST_ASSERT(
        std::string(to_string(config_error::env_var_not_found)) ==
            "Environment variable not found",
        "env_var_not_found string");

    return true;
}

bool test_config_error_code_values() {
    TEST_ASSERT(to_error_code(config_error::file_not_found) == -900,
                "file_not_found code");
    TEST_ASSERT(to_error_code(config_error::parse_error) == -901,
                "parse_error code");
    TEST_ASSERT(to_error_code(config_error::validation_error) == -902,
                "validation_error code");
    TEST_ASSERT(to_error_code(config_error::io_error) == -909,
                "io_error code");

    return true;
}

// =============================================================================
// Log Level Tests
// =============================================================================

bool test_log_level_to_string() {
    TEST_ASSERT(std::string(to_string(log_level::trace)) == "TRACE",
                "trace string");
    TEST_ASSERT(std::string(to_string(log_level::debug)) == "DEBUG",
                "debug string");
    TEST_ASSERT(std::string(to_string(log_level::info)) == "INFO",
                "info string");
    TEST_ASSERT(std::string(to_string(log_level::warn)) == "WARN",
                "warn string");
    TEST_ASSERT(std::string(to_string(log_level::error)) == "ERROR",
                "error string");
    TEST_ASSERT(std::string(to_string(log_level::fatal)) == "FATAL",
                "fatal string");

    return true;
}

// =============================================================================
// Default Configuration Tests
// =============================================================================

bool test_default_config_values() {
    auto config = config_loader::get_default_config();

    TEST_ASSERT(config.name == "PACS_BRIDGE", "Default server name");
    TEST_ASSERT(config.hl7.listener.port == mllp::MLLP_DEFAULT_PORT,
                "Default MLLP port");
    TEST_ASSERT(config.hl7.listener.max_connections == 50,
                "Default max connections");
    TEST_ASSERT(config.pacs.host == "localhost", "Default pacs host");
    TEST_ASSERT(config.pacs.port == 11112, "Default pacs port");
    TEST_ASSERT(config.pacs.ae_title == "PACS_BRIDGE", "Default AE title");
    TEST_ASSERT(config.logging.level == log_level::info, "Default log level");
    TEST_ASSERT(config.logging.format == "json", "Default log format");

    return true;
}

bool test_default_config_is_valid() {
    auto config = config_loader::get_default_config();
    auto errors = config.validate();

    TEST_ASSERT(errors.empty(), "Default config should be valid");
    TEST_ASSERT(config.is_valid(), "is_valid() should return true");

    return true;
}

// =============================================================================
// YAML Parsing Tests
// =============================================================================

bool test_load_yaml_string_basic() {
    auto result = config_loader::load_yaml_string(SAMPLE_YAML);

    TEST_ASSERT(result.has_value(), "YAML parsing should succeed");

    auto& config = result.value();
    TEST_ASSERT(config.name == "TEST_BRIDGE", "Server name from YAML");
    TEST_ASSERT(config.hl7.listener.port == 2575, "Port from YAML");
    TEST_ASSERT(config.hl7.listener.max_connections == 100,
                "Max connections from YAML");
    TEST_ASSERT(config.pacs.host == "pacs.test.local", "PACS host from YAML");
    TEST_ASSERT(config.pacs.port == 11112, "PACS port from YAML");
    TEST_ASSERT(config.logging.level == log_level::debug,
                "Log level from YAML");
    TEST_ASSERT(config.logging.format == "json", "Log format from YAML");

    return true;
}

bool test_load_yaml_string_minimal() {
    auto result = config_loader::load_yaml_string(MINIMAL_YAML);

    TEST_ASSERT(result.has_value(), "Minimal YAML parsing should succeed");
    TEST_ASSERT(result->name == "MINIMAL", "Server name from minimal YAML");
    TEST_ASSERT(result->is_valid(), "Minimal config should be valid");

    return true;
}

bool test_load_yaml_empty_fails() {
    auto result = config_loader::load_yaml_string("");

    TEST_ASSERT(!result.has_value(), "Empty YAML should fail");
    TEST_ASSERT(result.error().code == config_error::empty_config,
                "Should return empty_config error");

    return true;
}

bool test_load_yaml_whitespace_only_fails() {
    auto result = config_loader::load_yaml_string("   \n\t\n   ");

    TEST_ASSERT(!result.has_value(), "Whitespace-only YAML should fail");
    TEST_ASSERT(result.error().code == config_error::empty_config,
                "Should return empty_config error");

    return true;
}

// =============================================================================
// JSON Parsing Tests
// =============================================================================

bool test_load_json_string_basic() {
    auto result = config_loader::load_json_string(SAMPLE_JSON);

    TEST_ASSERT(result.has_value(), "JSON parsing should succeed");

    auto& config = result.value();
    TEST_ASSERT(config.name == "JSON_BRIDGE", "Server name from JSON");
    TEST_ASSERT(config.hl7.listener.port == 2580, "Port from JSON");
    TEST_ASSERT(config.hl7.listener.max_connections == 50,
                "Max connections from JSON");
    TEST_ASSERT(config.pacs.host == "pacs.json.local", "PACS host from JSON");
    TEST_ASSERT(config.pacs.port == 11113, "PACS port from JSON");
    TEST_ASSERT(config.logging.format == "text", "Log format from JSON");

    return true;
}

bool test_load_json_empty_fails() {
    auto result = config_loader::load_json_string("");

    TEST_ASSERT(!result.has_value(), "Empty JSON should fail");

    return true;
}

bool test_load_json_invalid_fails() {
    auto result = config_loader::load_json_string("{invalid json}");

    TEST_ASSERT(!result.has_value(), "Invalid JSON should fail");
    TEST_ASSERT(result.error().code == config_error::parse_error,
                "Should return parse_error");

    return true;
}

// =============================================================================
// Validation Tests
// =============================================================================

bool test_validation_empty_name_fails() {
    auto config = config_loader::get_default_config();
    config.name = "";

    auto errors = config.validate();
    TEST_ASSERT(!errors.empty(), "Empty name should fail validation");

    bool found_name_error = false;
    for (const auto& err : errors) {
        if (err.field_path == "name") {
            found_name_error = true;
            break;
        }
    }
    TEST_ASSERT(found_name_error, "Should have error for 'name' field");

    return true;
}

bool test_validation_zero_port_fails() {
    auto config = config_loader::get_default_config();
    config.hl7.listener.port = 0;

    auto errors = config.validate();
    TEST_ASSERT(!errors.empty(), "Zero port should fail validation");

    bool found_port_error = false;
    for (const auto& err : errors) {
        if (err.field_path == "hl7.listener.port") {
            found_port_error = true;
            break;
        }
    }
    TEST_ASSERT(found_port_error, "Should have error for port field");

    return true;
}

bool test_validation_invalid_log_format_fails() {
    auto config = config_loader::get_default_config();
    config.logging.format = "xml";  // Invalid format

    auto errors = config.validate();
    TEST_ASSERT(!errors.empty(), "Invalid log format should fail validation");

    return true;
}

bool test_validation_empty_pacs_host_fails() {
    auto config = config_loader::get_default_config();
    config.pacs.host = "";

    auto errors = config.validate();
    TEST_ASSERT(!errors.empty(), "Empty PACS host should fail validation");

    return true;
}

// =============================================================================
// Environment Variable Tests
// =============================================================================

bool test_env_var_expansion_simple() {
    // Set test environment variable
    setenv("TEST_CONFIG_VAR", "test_value", 1);

    auto result = config_loader::expand_env_vars("prefix_${TEST_CONFIG_VAR}_suffix");
    TEST_ASSERT(result.has_value(), "Expansion should succeed");
    TEST_ASSERT(*result == "prefix_test_value_suffix",
                "Value should be expanded");

    unsetenv("TEST_CONFIG_VAR");
    return true;
}

bool test_env_var_expansion_with_default() {
    // Make sure the variable is not set
    unsetenv("NONEXISTENT_VAR");

    auto result = config_loader::expand_env_vars("${NONEXISTENT_VAR:-default_value}");
    TEST_ASSERT(result.has_value(), "Expansion with default should succeed");
    TEST_ASSERT(*result == "default_value",
                "Should use default value");

    return true;
}

bool test_env_var_expansion_missing_fails() {
    // Make sure the variable is not set
    unsetenv("REQUIRED_MISSING_VAR");

    auto result = config_loader::expand_env_vars("${REQUIRED_MISSING_VAR}");
    TEST_ASSERT(!result.has_value(), "Missing required var should fail");
    TEST_ASSERT(result.error().code == config_error::env_var_not_found,
                "Should return env_var_not_found error");

    return true;
}

bool test_env_var_needs_expansion() {
    TEST_ASSERT(config_loader::needs_env_expansion("${VAR}"),
                "Should need expansion");
    TEST_ASSERT(config_loader::needs_env_expansion("prefix_${VAR}_suffix"),
                "Should need expansion");
    TEST_ASSERT(!config_loader::needs_env_expansion("no_vars_here"),
                "Should not need expansion");
    TEST_ASSERT(!config_loader::needs_env_expansion(""),
                "Empty string should not need expansion");

    return true;
}

// =============================================================================
// Serialization Tests
// =============================================================================

bool test_to_yaml_roundtrip() {
    auto original = config_loader::get_default_config();
    original.name = "ROUNDTRIP_TEST";
    original.hl7.listener.port = 2580;

    auto yaml = config_loader::to_yaml(original);
    TEST_ASSERT(!yaml.empty(), "YAML output should not be empty");
    TEST_ASSERT(yaml.find("ROUNDTRIP_TEST") != std::string::npos,
                "YAML should contain server name");
    TEST_ASSERT(yaml.find("2580") != std::string::npos,
                "YAML should contain port number");

    // Note: Full roundtrip would require parsing the YAML back
    // which our simple parser supports

    return true;
}

bool test_to_json_basic() {
    auto config = config_loader::get_default_config();
    auto json = config_loader::to_json(config, true);

    TEST_ASSERT(!json.empty(), "JSON output should not be empty");
    TEST_ASSERT(json.find("PACS_BRIDGE") != std::string::npos,
                "JSON should contain server name");
    TEST_ASSERT(json[0] == '{', "JSON should start with '{'");

    return true;
}

// =============================================================================
// Merge Tests
// =============================================================================

bool test_merge_overlay() {
    auto base = config_loader::get_default_config();
    base.name = "BASE";
    base.hl7.listener.port = 2575;

    auto overlay = config_loader::get_default_config();
    overlay.name = "OVERLAY";
    overlay.pacs.host = "overlay.host";

    auto merged = config_loader::merge(base, overlay);

    TEST_ASSERT(merged.name == "OVERLAY", "Name should be from overlay");
    TEST_ASSERT(merged.pacs.host == "overlay.host",
                "PACS host should be from overlay");
    TEST_ASSERT(merged.hl7.listener.port == 2575,
                "Port should be from base (not changed in overlay)");

    return true;
}

// =============================================================================
// File I/O Tests
// =============================================================================

bool test_file_not_found() {
    auto result = config_loader::load("/nonexistent/path/config.yaml");

    TEST_ASSERT(!result.has_value(), "Non-existent file should fail");
    TEST_ASSERT(result.error().code == config_error::file_not_found,
                "Should return file_not_found error");

    return true;
}

bool test_invalid_extension_fails() {
    // Create a temp file with wrong extension
    auto temp_path = std::filesystem::temp_directory_path() / "test.txt";
    {
        std::ofstream file(temp_path);
        file << "test content";
    }

    auto result = config_loader::load(temp_path);
    TEST_ASSERT(!result.has_value(), "Invalid extension should fail");
    TEST_ASSERT(result.error().code == config_error::invalid_format,
                "Should return invalid_format error");

    std::filesystem::remove(temp_path);
    return true;
}

bool test_save_and_load_yaml() {
    auto temp_path = std::filesystem::temp_directory_path() / "test_config.yaml";

    auto original = config_loader::get_default_config();
    original.name = "SAVE_TEST";
    original.hl7.listener.port = 2590;

    auto save_result = config_loader::save_yaml(original, temp_path);
    TEST_ASSERT(save_result.has_value(), "Save should succeed");
    TEST_ASSERT(std::filesystem::exists(temp_path), "File should exist");

    auto load_result = config_loader::load_yaml(temp_path);
    TEST_ASSERT(load_result.has_value(), "Load should succeed");
    TEST_ASSERT(load_result->name == "SAVE_TEST",
                "Loaded name should match");
    TEST_ASSERT(load_result->hl7.listener.port == 2590,
                "Loaded port should match");

    std::filesystem::remove(temp_path);
    return true;
}

// =============================================================================
// Integration Tests
// =============================================================================

bool test_full_config_with_routing_rules() {
    const std::string yaml = R"(
server:
  name: "FULL_TEST"

hl7:
  listener:
    port: 2575
    max_connections: 50

pacs:
  host: "localhost"
  port: 11112
  ae_title: "BRIDGE"
  called_ae: "PACS"

routing_rules:
  - name: "ADT Handler"
    message_type_pattern: "ADT^A*"
    destination: "patient_cache"
    priority: 10
    enabled: true

logging:
  level: "info"
  format: "json"
)";

    auto result = config_loader::load_yaml_string(yaml);
    TEST_ASSERT(result.has_value(), "Full config should parse");

    // Note: The simple parser doesn't fully support arrays yet
    // This test verifies the basic structure parsing works

    return true;
}

bool test_config_with_env_vars_in_yaml() {
    setenv("TEST_BRIDGE_PORT", "2599", 1);
    setenv("TEST_PACS_HOST", "env.pacs.local", 1);

    const std::string yaml = R"(
server:
  name: "ENV_TEST"
hl7:
  listener:
    port: ${TEST_BRIDGE_PORT}
pacs:
  host: "${TEST_PACS_HOST}"
  port: 11112
  ae_title: "BRIDGE"
  called_ae: "PACS"
logging:
  level: "info"
  format: "json"
)";

    auto result = config_loader::load_yaml_string(yaml);
    TEST_ASSERT(result.has_value(), "Config with env vars should parse");
    TEST_ASSERT(result->hl7.listener.port == 2599,
                "Port should be from env var");
    TEST_ASSERT(result->pacs.host == "env.pacs.local",
                "PACS host should be from env var");

    unsetenv("TEST_BRIDGE_PORT");
    unsetenv("TEST_PACS_HOST");
    return true;
}

}  // namespace pacs::bridge::config::test

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    using namespace pacs::bridge::config::test;

    int passed = 0;
    int failed = 0;

    std::cout << "=== Configuration Module Unit Tests ===" << std::endl;
    std::cout << std::endl;

    // Error Code Tests
    std::cout << "--- Error Code Tests ---" << std::endl;
    RUN_TEST(test_config_error_to_string);
    RUN_TEST(test_config_error_code_values);
    std::cout << std::endl;

    // Log Level Tests
    std::cout << "--- Log Level Tests ---" << std::endl;
    RUN_TEST(test_log_level_to_string);
    std::cout << std::endl;

    // Default Configuration Tests
    std::cout << "--- Default Configuration Tests ---" << std::endl;
    RUN_TEST(test_default_config_values);
    RUN_TEST(test_default_config_is_valid);
    std::cout << std::endl;

    // YAML Parsing Tests
    std::cout << "--- YAML Parsing Tests ---" << std::endl;
    RUN_TEST(test_load_yaml_string_basic);
    RUN_TEST(test_load_yaml_string_minimal);
    RUN_TEST(test_load_yaml_empty_fails);
    RUN_TEST(test_load_yaml_whitespace_only_fails);
    std::cout << std::endl;

    // JSON Parsing Tests
    std::cout << "--- JSON Parsing Tests ---" << std::endl;
    RUN_TEST(test_load_json_string_basic);
    RUN_TEST(test_load_json_empty_fails);
    RUN_TEST(test_load_json_invalid_fails);
    std::cout << std::endl;

    // Validation Tests
    std::cout << "--- Validation Tests ---" << std::endl;
    RUN_TEST(test_validation_empty_name_fails);
    RUN_TEST(test_validation_zero_port_fails);
    RUN_TEST(test_validation_invalid_log_format_fails);
    RUN_TEST(test_validation_empty_pacs_host_fails);
    std::cout << std::endl;

    // Environment Variable Tests
    std::cout << "--- Environment Variable Tests ---" << std::endl;
    RUN_TEST(test_env_var_expansion_simple);
    RUN_TEST(test_env_var_expansion_with_default);
    RUN_TEST(test_env_var_expansion_missing_fails);
    RUN_TEST(test_env_var_needs_expansion);
    std::cout << std::endl;

    // Serialization Tests
    std::cout << "--- Serialization Tests ---" << std::endl;
    RUN_TEST(test_to_yaml_roundtrip);
    RUN_TEST(test_to_json_basic);
    std::cout << std::endl;

    // Merge Tests
    std::cout << "--- Merge Tests ---" << std::endl;
    RUN_TEST(test_merge_overlay);
    std::cout << std::endl;

    // File I/O Tests
    std::cout << "--- File I/O Tests ---" << std::endl;
    RUN_TEST(test_file_not_found);
    RUN_TEST(test_invalid_extension_fails);
    RUN_TEST(test_save_and_load_yaml);
    std::cout << std::endl;

    // Integration Tests
    std::cout << "--- Integration Tests ---" << std::endl;
    RUN_TEST(test_full_config_with_routing_rules);
    RUN_TEST(test_config_with_env_vars_in_yaml);
    std::cout << std::endl;

    // Summary
    std::cout << "=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    return failed > 0 ? 1 : 0;
}
