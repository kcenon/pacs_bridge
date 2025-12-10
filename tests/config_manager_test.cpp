/**
 * @file config_manager_test.cpp
 * @brief Unit tests for configuration hot-reload functionality
 *
 * Tests for config_manager and admin_server including:
 * - Configuration reload
 * - Callback notifications
 * - Change detection
 * - Admin endpoint handling
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/39
 */

#include "pacs/bridge/config/config_manager.h"
#include "pacs/bridge/config/admin_server.h"
#include "pacs/bridge/config/config_loader.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

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

// Helper to create a temporary config file
class temp_config_file {
public:
    explicit temp_config_file(const std::string& content) {
        path_ = std::filesystem::temp_directory_path() /
                ("pacs_bridge_test_config_" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                 ".yaml");

        std::ofstream file(path_);
        file << content;
        file.close();
    }

    ~temp_config_file() {
        try {
            std::filesystem::remove(path_);
        } catch (...) {
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    void update(const std::string& content) {
        // Wait a bit to ensure file modification time changes
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::ofstream file(path_);
        file << content;
        file.close();
    }

private:
    std::filesystem::path path_;
};

// =============================================================================
// Config Manager Tests
// =============================================================================

bool test_config_manager_creation() {
    std::string yaml = R"(
server:
  name: "TEST_BRIDGE"
hl7:
  listener:
    port: 2575
logging:
  level: "info"
)";

    temp_config_file config_file(yaml);

    // Create config manager from file
    try {
        config_manager manager(config_file.path());

        TEST_ASSERT(manager.get().name == "TEST_BRIDGE",
                    "Server name should match");
        TEST_ASSERT(manager.get().hl7.listener.port == 2575,
                    "HL7 port should match");
        TEST_ASSERT(manager.config_path() == config_file.path(),
                    "Config path should match");
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return false;
    }

    return true;
}

bool test_config_manager_get_copy() {
    std::string yaml = R"(
server:
  name: "COPY_TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    // Get a copy
    auto config_copy = manager.get_copy();

    TEST_ASSERT(config_copy.name == "COPY_TEST",
                "Copy should have correct name");

    // Modify the copy (should not affect original)
    config_copy.name = "MODIFIED";

    TEST_ASSERT(manager.get().name == "COPY_TEST",
                "Original should be unchanged");

    return true;
}

bool test_config_reload_success() {
    std::string yaml1 = R"(
server:
  name: "BEFORE"
hl7:
  listener:
    port: 2575
logging:
  level: "info"
)";

    std::string yaml2 = R"(
server:
  name: "AFTER"
hl7:
  listener:
    port: 2575
logging:
  level: "debug"
)";

    temp_config_file config_file(yaml1);
    config_manager manager(config_file.path());

    TEST_ASSERT(manager.get().name == "BEFORE", "Initial name should be BEFORE");

    // Update file and reload
    config_file.update(yaml2);
    auto result = manager.reload();

    TEST_ASSERT(result.success, "Reload should succeed");
    TEST_ASSERT(manager.get().name == "AFTER", "Name should be updated");
    TEST_ASSERT(manager.get().logging.level == log_level::debug,
                "Log level should be updated");

    return true;
}

bool test_config_reload_validation_failure() {
    std::string valid_yaml = R"(
server:
  name: "VALID"
hl7:
  listener:
    port: 2575
)";

    std::string invalid_yaml = R"(
server:
  name: "INVALID"
hl7:
  listener:
    port: 0
)";

    temp_config_file config_file(valid_yaml);
    config_manager manager(config_file.path());

    // Update with invalid config
    config_file.update(invalid_yaml);
    auto result = manager.reload();

    TEST_ASSERT(!result.success, "Reload should fail with invalid config");
    TEST_ASSERT(manager.get().name == "VALID",
                "Original config should be preserved");

    return true;
}

bool test_config_reload_callback() {
    std::string yaml1 = R"(
server:
  name: "BEFORE"
hl7:
  listener:
    port: 2575
)";

    std::string yaml2 = R"(
server:
  name: "AFTER"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml1);
    config_manager manager(config_file.path());

    // Register callback
    std::atomic<int> callback_count{0};
    std::string received_name;

    auto handle = manager.on_reload([&](const bridge_config& config) {
        callback_count++;
        received_name = config.name;
    });

    // Reload
    config_file.update(yaml2);
    auto result = manager.reload();

    TEST_ASSERT(result.success, "Reload should succeed");
    TEST_ASSERT(result.components_notified == 1,
                "One component should be notified");
    TEST_ASSERT(callback_count == 1, "Callback should be called once");
    TEST_ASSERT(received_name == "AFTER", "Callback should receive new config");

    return true;
}

bool test_config_reload_callback_with_diff() {
    std::string yaml1 = R"(
server:
  name: "BEFORE"
hl7:
  listener:
    port: 2575
logging:
  level: "info"
)";

    std::string yaml2 = R"(
server:
  name: "AFTER"
hl7:
  listener:
    port: 2575
logging:
  level: "debug"
)";

    temp_config_file config_file(yaml1);
    config_manager manager(config_file.path());

    // Register callback with diff
    std::vector<std::string> changed_fields;

    manager.on_reload(
        [&](const bridge_config& /*config*/, const config_diff& diff) {
            changed_fields = diff.changed_fields;
        });

    // Reload
    config_file.update(yaml2);
    manager.reload();

    TEST_ASSERT(!changed_fields.empty(), "Should have changed fields");

    return true;
}

bool test_config_remove_callback() {
    std::string yaml = R"(
server:
  name: "TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    std::atomic<int> callback_count{0};

    auto handle = manager.on_reload(
        [&](const bridge_config&) { callback_count++; });

    // Remove callback
    bool removed = manager.remove_callback(handle);
    TEST_ASSERT(removed, "Callback should be removed");

    // Reload
    manager.reload();
    TEST_ASSERT(callback_count == 0, "Callback should not be called");

    return true;
}

bool test_config_has_file_changed() {
    std::string yaml = R"(
server:
  name: "TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    // Initially not changed
    TEST_ASSERT(!manager.has_file_changed(),
                "File should not be marked as changed initially");

    // Update file
    config_file.update(yaml + "\n# comment");

    // Now changed
    TEST_ASSERT(manager.has_file_changed(),
                "File should be marked as changed after update");

    // Reload
    manager.reload();

    // Not changed after reload
    TEST_ASSERT(!manager.has_file_changed(),
                "File should not be marked as changed after reload");

    return true;
}

bool test_config_apply_direct() {
    std::string yaml = R"(
server:
  name: "ORIGINAL"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    // Create new config directly
    bridge_config new_config = manager.get_copy();
    new_config.name = "APPLIED";

    auto result = manager.apply(new_config);

    TEST_ASSERT(result.success, "Apply should succeed");
    TEST_ASSERT(manager.get().name == "APPLIED",
                "Config should be updated");

    return true;
}

bool test_config_statistics() {
    std::string yaml = R"(
server:
  name: "STATS_TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    manager.on_reload([](const bridge_config&) {});

    auto stats = manager.get_statistics();
    TEST_ASSERT(stats.reload_attempts == 0, "Initial attempts should be 0");
    TEST_ASSERT(stats.callback_count == 1, "Should have 1 callback");

    // Reload
    manager.reload();

    stats = manager.get_statistics();
    TEST_ASSERT(stats.reload_attempts == 1, "Attempts should be 1");
    TEST_ASSERT(stats.reload_successes == 1, "Successes should be 1");
    TEST_ASSERT(stats.last_reload_time.has_value(),
                "Last reload time should be set");

    return true;
}

bool test_config_is_reloadable() {
    // Reloadable fields
    TEST_ASSERT(config_manager::is_reloadable("routing_rules"),
                "routing_rules should be reloadable");
    TEST_ASSERT(config_manager::is_reloadable("logging.level"),
                "logging.level should be reloadable");
    TEST_ASSERT(config_manager::is_reloadable("hl7.outbound_destinations"),
                "outbound_destinations should be reloadable");

    // Non-reloadable fields
    TEST_ASSERT(!config_manager::is_reloadable("hl7.listener.port"),
                "listener port should not be reloadable");
    TEST_ASSERT(!config_manager::is_reloadable("fhir.server.port"),
                "fhir port should not be reloadable");

    return true;
}

bool test_config_compare() {
    bridge_config config1;
    config1.name = "CONFIG1";
    config1.logging.level = log_level::info;

    bridge_config config2;
    config2.name = "CONFIG2";
    config2.logging.level = log_level::debug;

    auto diff = config_manager::compare(config1, config2);

    TEST_ASSERT(!diff.changed_fields.empty(), "Should detect changes");
    TEST_ASSERT(!diff.requires_restart,
                "Name and log level changes should not require restart");

    return true;
}

bool test_config_compare_requires_restart() {
    bridge_config config1;
    config1.hl7.listener.port = 2575;

    bridge_config config2;
    config2.hl7.listener.port = 2576;

    auto diff = config_manager::compare(config1, config2);

    TEST_ASSERT(diff.requires_restart,
                "Port change should require restart");
    TEST_ASSERT(!diff.non_reloadable_changes.empty(),
                "Should have non-reloadable changes");

    return true;
}

// =============================================================================
// Admin Server Tests
// =============================================================================

bool test_admin_server_creation() {
    std::string yaml = R"(
server:
  name: "ADMIN_TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    admin_server::config admin_config;
    admin_config.port = 8082;

    admin_server server(manager, admin_config);

    TEST_ASSERT(server.port() == 8082, "Port should be 8082");
    TEST_ASSERT(server.base_path() == "/admin", "Base path should be /admin");
    TEST_ASSERT(!server.is_running(), "Server should not be running initially");

    return true;
}

bool test_admin_server_reload_endpoint() {
    std::string yaml1 = R"(
server:
  name: "BEFORE"
hl7:
  listener:
    port: 2575
)";

    std::string yaml2 = R"(
server:
  name: "AFTER"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml1);
    config_manager manager(config_file.path());

    admin_server server(manager);
    server.start();

    // Test POST /admin/reload
    config_file.update(yaml2);
    auto response = server.handle_request("POST", "/admin/reload");

    TEST_ASSERT(response.status_code == 200, "Should return 200 OK");
    TEST_ASSERT(response.body.find("\"success\": true") != std::string::npos,
                "Response should indicate success");

    TEST_ASSERT(manager.get().name == "AFTER",
                "Config should be reloaded");

    server.stop();
    return true;
}

bool test_admin_server_reload_method_not_allowed() {
    std::string yaml = R"(
server:
  name: "TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    admin_server server(manager);

    // Test GET /admin/reload (should be method not allowed)
    auto response = server.handle_request("GET", "/admin/reload");

    TEST_ASSERT(response.status_code == 405,
                "Should return 405 Method Not Allowed");

    return true;
}

bool test_admin_server_status_endpoint() {
    std::string yaml = R"(
server:
  name: "STATUS_TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    admin_server server(manager);

    // Test GET /admin/status
    auto response = server.handle_request("GET", "/admin/status");

    TEST_ASSERT(response.status_code == 200, "Should return 200 OK");
    TEST_ASSERT(response.body.find("\"success\": true") != std::string::npos,
                "Response should indicate success");
    TEST_ASSERT(response.body.find("\"reload_attempts\"") != std::string::npos,
                "Response should contain statistics");

    return true;
}

bool test_admin_server_config_endpoint_disabled() {
    std::string yaml = R"(
server:
  name: "CONFIG_TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    admin_server::config admin_config;
    admin_config.enable_config_view = false;  // Disabled by default

    admin_server server(manager, admin_config);

    // Test GET /admin/config (should be forbidden)
    auto response = server.handle_request("GET", "/admin/config");

    TEST_ASSERT(response.status_code == 403, "Should return 403 Forbidden");

    return true;
}

bool test_admin_server_config_endpoint_enabled() {
    std::string yaml = R"(
server:
  name: "CONFIG_VIEW_TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    admin_server::config admin_config;
    admin_config.enable_config_view = true;

    admin_server server(manager, admin_config);

    // Test GET /admin/config
    auto response = server.handle_request("GET", "/admin/config");

    TEST_ASSERT(response.status_code == 200, "Should return 200 OK");
    TEST_ASSERT(response.body.find("CONFIG_VIEW_TEST") != std::string::npos,
                "Response should contain server name");

    return true;
}

bool test_admin_server_not_found() {
    std::string yaml = R"(
server:
  name: "404_TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    admin_server server(manager);

    // Test unknown endpoint
    auto response = server.handle_request("GET", "/admin/unknown");

    TEST_ASSERT(response.status_code == 404, "Should return 404 Not Found");

    return true;
}

bool test_admin_server_statistics() {
    std::string yaml = R"(
server:
  name: "STATS_TEST"
hl7:
  listener:
    port: 2575
)";

    temp_config_file config_file(yaml);
    config_manager manager(config_file.path());

    admin_server server(manager);

    auto stats = server.get_statistics();
    TEST_ASSERT(stats.total_requests == 0, "Initial requests should be 0");

    // Make a request
    server.handle_request("POST", "/admin/reload");

    stats = server.get_statistics();
    TEST_ASSERT(stats.total_requests == 1, "Should have 1 request");
    TEST_ASSERT(stats.reload_requests == 1, "Should have 1 reload request");
    TEST_ASSERT(stats.successful_reloads == 1, "Should have 1 successful reload");

    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== Config Manager Tests ===" << std::endl;
    RUN_TEST(test_config_manager_creation);
    RUN_TEST(test_config_manager_get_copy);
    RUN_TEST(test_config_reload_success);
    RUN_TEST(test_config_reload_validation_failure);
    RUN_TEST(test_config_reload_callback);
    RUN_TEST(test_config_reload_callback_with_diff);
    RUN_TEST(test_config_remove_callback);
    RUN_TEST(test_config_has_file_changed);
    RUN_TEST(test_config_apply_direct);
    RUN_TEST(test_config_statistics);
    RUN_TEST(test_config_is_reloadable);
    RUN_TEST(test_config_compare);
    RUN_TEST(test_config_compare_requires_restart);

    std::cout << "\n=== Admin Server Tests ===" << std::endl;
    RUN_TEST(test_admin_server_creation);
    RUN_TEST(test_admin_server_reload_endpoint);
    RUN_TEST(test_admin_server_reload_method_not_allowed);
    RUN_TEST(test_admin_server_status_endpoint);
    RUN_TEST(test_admin_server_config_endpoint_disabled);
    RUN_TEST(test_admin_server_config_endpoint_enabled);
    RUN_TEST(test_admin_server_not_found);
    RUN_TEST(test_admin_server_statistics);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::config::test

int main() { return pacs::bridge::config::test::run_all_tests(); }
