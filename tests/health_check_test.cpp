/**
 * @file health_check_test.cpp
 * @brief Unit tests for health check functionality
 *
 * Tests cover:
 * - Health types and status conversions
 * - Component health checks (MLLP, PACS, Queue, FHIR, Memory)
 * - Health checker integration
 * - Health server request handling
 * - JSON serialization
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/41
 */

#include "pacs/bridge/monitoring/health_checker.h"
#include "pacs/bridge/monitoring/health_server.h"
#include "pacs/bridge/monitoring/health_types.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

namespace pacs::bridge::monitoring::test {

// ═══════════════════════════════════════════════════════════════════════════
// Test Utilities
// ═══════════════════════════════════════════════════════════════════════════

#define TEST_ASSERT(condition, message)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_TEST(test_fn)                                                      \
    do {                                                                       \
        std::cout << "Running " << #test_fn << "... ";                         \
        if (test_fn()) {                                                       \
            std::cout << "PASSED" << std::endl;                                \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "FAILED" << std::endl;                                \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════
// Health Types Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_health_status_to_string() {
    TEST_ASSERT(std::string(to_string(health_status::healthy)) == "UP",
                "healthy should be UP");
    TEST_ASSERT(std::string(to_string(health_status::degraded)) == "DEGRADED",
                "degraded should be DEGRADED");
    TEST_ASSERT(std::string(to_string(health_status::unhealthy)) == "DOWN",
                "unhealthy should be DOWN");
    return true;
}

bool test_health_status_parsing() {
    auto up = parse_health_status("UP");
    TEST_ASSERT(up.has_value() && *up == health_status::healthy,
                "UP should parse to healthy");

    auto healthy = parse_health_status("healthy");
    TEST_ASSERT(healthy.has_value() && *healthy == health_status::healthy,
                "healthy should parse to healthy");

    auto degraded = parse_health_status("DEGRADED");
    TEST_ASSERT(degraded.has_value() && *degraded == health_status::degraded,
                "DEGRADED should parse to degraded");

    auto down = parse_health_status("DOWN");
    TEST_ASSERT(down.has_value() && *down == health_status::unhealthy,
                "DOWN should parse to unhealthy");

    auto invalid = parse_health_status("INVALID");
    TEST_ASSERT(!invalid.has_value(), "INVALID should return nullopt");

    return true;
}

bool test_health_error_codes() {
    TEST_ASSERT(to_error_code(health_error::timeout) == -980,
                "timeout should be -980");
    TEST_ASSERT(to_error_code(health_error::component_unavailable) == -981,
                "component_unavailable should be -981");
    TEST_ASSERT(to_error_code(health_error::threshold_exceeded) == -982,
                "threshold_exceeded should be -982");
    return true;
}

bool test_component_health_is_healthy() {
    component_health healthy_comp;
    healthy_comp.status = health_status::healthy;
    TEST_ASSERT(healthy_comp.is_healthy(), "healthy should be healthy");
    TEST_ASSERT(healthy_comp.is_operational(), "healthy should be operational");

    component_health degraded_comp;
    degraded_comp.status = health_status::degraded;
    TEST_ASSERT(!degraded_comp.is_healthy(), "degraded should not be healthy");
    TEST_ASSERT(degraded_comp.is_operational(),
                "degraded should be operational");

    component_health unhealthy_comp;
    unhealthy_comp.status = health_status::unhealthy;
    TEST_ASSERT(!unhealthy_comp.is_healthy(),
                "unhealthy should not be healthy");
    TEST_ASSERT(!unhealthy_comp.is_operational(),
                "unhealthy should not be operational");

    return true;
}

bool test_liveness_result() {
    auto ok = liveness_result::ok();
    TEST_ASSERT(ok.status == health_status::healthy,
                "ok should be healthy");

    auto fail = liveness_result::fail();
    TEST_ASSERT(fail.status == health_status::unhealthy,
                "fail should be unhealthy");

    return true;
}

bool test_readiness_result_all_healthy() {
    readiness_result result;
    result.components["comp1"] = health_status::healthy;
    result.components["comp2"] = health_status::healthy;

    TEST_ASSERT(result.all_healthy(), "all components healthy should be true");
    TEST_ASSERT(!result.any_unhealthy(),
                "no unhealthy components should be false");

    return true;
}

bool test_readiness_result_some_unhealthy() {
    readiness_result result;
    result.components["comp1"] = health_status::healthy;
    result.components["comp2"] = health_status::unhealthy;

    TEST_ASSERT(!result.all_healthy(),
                "not all components healthy should be false");
    TEST_ASSERT(result.any_unhealthy(),
                "some unhealthy components should be true");

    return true;
}

bool test_deep_health_calculate_status() {
    deep_health_result result;

    // Empty should be unhealthy
    result.calculate_overall_status();
    TEST_ASSERT(result.status == health_status::unhealthy,
                "empty should be unhealthy");

    // All healthy
    result.components.push_back({.name = "comp1", .status = health_status::healthy});
    result.components.push_back({.name = "comp2", .status = health_status::healthy});
    result.calculate_overall_status();
    TEST_ASSERT(result.status == health_status::healthy,
                "all healthy should be healthy");

    // One degraded
    result.components[1].status = health_status::degraded;
    result.calculate_overall_status();
    TEST_ASSERT(result.status == health_status::degraded,
                "one degraded should be degraded");

    // One unhealthy
    result.components[1].status = health_status::unhealthy;
    result.calculate_overall_status();
    TEST_ASSERT(result.status == health_status::unhealthy,
                "one unhealthy should be unhealthy");

    return true;
}

bool test_deep_health_find_component() {
    deep_health_result result;
    result.components.push_back({.name = "mllp_server", .status = health_status::healthy});
    result.components.push_back({.name = "pacs_system", .status = health_status::degraded});

    auto found = result.find_component("mllp_server");
    TEST_ASSERT(found != nullptr, "should find mllp_server");
    TEST_ASSERT(found->status == health_status::healthy,
                "mllp_server should be healthy");

    auto not_found = result.find_component("nonexistent");
    TEST_ASSERT(not_found == nullptr, "should not find nonexistent");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Component Check Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_mllp_server_check_healthy() {
    mllp_server_check check(
        []() { return true; },  // is_running
        []() { return std::make_tuple(size_t(5), size_t(100), size_t(2)); }
    );

    auto health = check.check(std::chrono::milliseconds(1000));
    TEST_ASSERT(health.name == "mllp_server", "name should be mllp_server");
    TEST_ASSERT(health.status == health_status::healthy,
                "running server should be healthy");
    TEST_ASSERT(health.metrics["active_connections"] == "5",
                "should have 5 active connections");

    return true;
}

bool test_mllp_server_check_unhealthy() {
    mllp_server_check check(
        []() { return false; },  // not running
        nullptr
    );

    auto health = check.check(std::chrono::milliseconds(1000));
    TEST_ASSERT(health.status == health_status::unhealthy,
                "stopped server should be unhealthy");
    TEST_ASSERT(health.details.has_value(), "should have details");

    return true;
}

bool test_pacs_connection_check_healthy() {
    pacs_connection_check check(
        [](std::chrono::milliseconds) { return true; }
    );

    auto health = check.check(std::chrono::milliseconds(1000));
    TEST_ASSERT(health.name == "pacs_system", "name should be pacs_system");
    TEST_ASSERT(health.status == health_status::healthy,
                "successful echo should be healthy");

    return true;
}

bool test_pacs_connection_check_unhealthy() {
    pacs_connection_check check(
        [](std::chrono::milliseconds) { return false; }
    );

    auto health = check.check(std::chrono::milliseconds(1000));
    TEST_ASSERT(health.status == health_status::unhealthy,
                "failed echo should be unhealthy");

    return true;
}

bool test_queue_health_check_healthy() {
    health_thresholds thresholds;
    thresholds.queue_depth = 1000;
    thresholds.queue_dead_letters = 10;

    queue_health_check check(
        []() {
            return queue_health_check::queue_metrics{
                .pending_messages = 50,
                .dead_letters = 0,
                .database_connected = true
            };
        },
        thresholds
    );

    auto health = check.check(std::chrono::milliseconds(1000));
    TEST_ASSERT(health.name == "message_queue", "name should be message_queue");
    TEST_ASSERT(health.status == health_status::healthy,
                "healthy queue should be healthy");

    return true;
}

bool test_queue_health_check_degraded() {
    health_thresholds thresholds;
    thresholds.queue_depth = 1000;
    thresholds.queue_dead_letters = 10;

    queue_health_check check(
        []() {
            return queue_health_check::queue_metrics{
                .pending_messages = 50,
                .dead_letters = 15,  // Exceeds threshold
                .database_connected = true
            };
        },
        thresholds
    );

    auto health = check.check(std::chrono::milliseconds(1000));
    TEST_ASSERT(health.status == health_status::degraded,
                "too many dead letters should be degraded");

    return true;
}

bool test_fhir_server_check_optional() {
    fhir_server_check check(nullptr, nullptr);

    TEST_ASSERT(!check.is_critical(), "FHIR should not be critical");

    auto health = check.check(std::chrono::milliseconds(1000));
    TEST_ASSERT(health.status == health_status::healthy,
                "disabled FHIR should be healthy");

    return true;
}

bool test_memory_health_check() {
    health_thresholds thresholds;
    thresholds.memory_mb = 10000;  // 10GB - should be under

    memory_health_check check(thresholds);

    TEST_ASSERT(!check.is_critical(), "memory should not be critical");

    auto health = check.check(std::chrono::milliseconds(1000));
    TEST_ASSERT(health.name == "memory", "name should be memory");
    // Memory check should pass with high threshold
    TEST_ASSERT(health.is_operational(), "memory should be operational");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Health Checker Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_health_checker_liveness() {
    health_config config;
    health_checker checker(config);

    auto result = checker.check_liveness();
    TEST_ASSERT(result.status == health_status::healthy,
                "liveness should be healthy");

    return true;
}

bool test_health_checker_register_component() {
    health_config config;
    health_checker checker(config);

    checker.register_check(
        std::make_unique<mllp_server_check>(
            []() { return true; },
            nullptr
        )
    );

    auto components = checker.registered_components();
    TEST_ASSERT(components.size() == 1, "should have 1 component");
    TEST_ASSERT(components[0] == "mllp_server",
                "component should be mllp_server");

    return true;
}

bool test_health_checker_register_lambda() {
    health_config config;
    health_checker checker(config);

    checker.register_check(
        "custom_check",
        [](std::chrono::milliseconds) {
            return component_health{
                .name = "custom_check",
                .status = health_status::healthy
            };
        },
        true  // critical
    );

    auto result = checker.check_readiness();
    TEST_ASSERT(result.components.count("custom_check") == 1,
                "should have custom_check component");

    return true;
}

bool test_health_checker_unregister() {
    health_config config;
    health_checker checker(config);

    checker.register_check(
        "test_component",
        [](std::chrono::milliseconds) {
            return component_health{
                .name = "test_component",
                .status = health_status::healthy
            };
        },
        true
    );

    TEST_ASSERT(checker.registered_components().size() == 1,
                "should have 1 component");

    bool removed = checker.unregister_check("test_component");
    TEST_ASSERT(removed, "should remove component");
    TEST_ASSERT(checker.registered_components().empty(),
                "should have 0 components");

    removed = checker.unregister_check("nonexistent");
    TEST_ASSERT(!removed, "should not remove nonexistent");

    return true;
}

bool test_health_checker_readiness_all_healthy() {
    health_config config;
    health_checker checker(config);

    checker.register_check(
        "comp1",
        [](std::chrono::milliseconds) {
            return component_health{.name = "comp1", .status = health_status::healthy};
        },
        true
    );

    checker.register_check(
        "comp2",
        [](std::chrono::milliseconds) {
            return component_health{.name = "comp2", .status = health_status::healthy};
        },
        true
    );

    auto result = checker.check_readiness();
    TEST_ASSERT(result.status == health_status::healthy,
                "all healthy should be healthy");

    return true;
}

bool test_health_checker_readiness_critical_unhealthy() {
    health_config config;
    health_checker checker(config);

    checker.register_check(
        "critical",
        [](std::chrono::milliseconds) {
            return component_health{.name = "critical", .status = health_status::unhealthy};
        },
        true  // critical
    );

    checker.register_check(
        "optional",
        [](std::chrono::milliseconds) {
            return component_health{.name = "optional", .status = health_status::healthy};
        },
        false  // not critical
    );

    auto result = checker.check_readiness();
    TEST_ASSERT(result.status == health_status::unhealthy,
                "critical unhealthy should be unhealthy");

    return true;
}

bool test_health_checker_deep() {
    health_config config;
    health_checker checker(config);

    checker.register_check(
        "comp1",
        [](std::chrono::milliseconds) {
            component_health h;
            h.name = "comp1";
            h.status = health_status::healthy;
            h.response_time_ms = 5;
            h.metrics["key"] = "value";
            return h;
        },
        true
    );

    auto result = checker.check_deep();
    TEST_ASSERT(result.components.size() == 1, "should have 1 component");
    TEST_ASSERT(result.components[0].name == "comp1", "name should be comp1");
    TEST_ASSERT(result.components[0].response_time_ms.value_or(0) == 5,
                "response time should be 5");

    return true;
}

bool test_health_checker_check_specific_component() {
    health_config config;
    health_checker checker(config);

    checker.register_check(
        "target",
        [](std::chrono::milliseconds) {
            return component_health{.name = "target", .status = health_status::degraded};
        },
        true
    );

    auto result = checker.check_component("target");
    TEST_ASSERT(result.has_value(), "should find target");
    TEST_ASSERT(result->status == health_status::degraded,
                "should be degraded");

    auto not_found = checker.check_component("nonexistent");
    TEST_ASSERT(!not_found.has_value(), "should not find nonexistent");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Health Server Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_health_server_start_stop() {
    health_config hc_config;
    health_checker checker(hc_config);

    health_server::config server_config;
    server_config.port = 8081;
    health_server server(checker, server_config);

    TEST_ASSERT(!server.is_running(), "should not be running initially");

    bool started = server.start();
    TEST_ASSERT(started, "should start successfully");
    TEST_ASSERT(server.is_running(), "should be running after start");

    server.stop();
    TEST_ASSERT(!server.is_running(), "should not be running after stop");

    return true;
}

bool test_health_server_handle_liveness() {
    health_config hc_config;
    health_checker checker(hc_config);

    health_server::config server_config;
    health_server server(checker, server_config);
    (void)server.start();

    auto response = server.handle_request("/health/live");
    TEST_ASSERT(response.status_code == 200, "liveness should return 200");
    TEST_ASSERT(response.content_type == "application/json",
                "content type should be json");
    TEST_ASSERT(response.body.find("\"status\": \"UP\"") != std::string::npos,
                "body should contain UP status");

    return true;
}

bool test_health_server_handle_readiness() {
    health_config hc_config;
    health_checker checker(hc_config);

    checker.register_check(
        "test",
        [](std::chrono::milliseconds) {
            return component_health{.name = "test", .status = health_status::healthy};
        },
        true
    );

    health_server::config server_config;
    health_server server(checker, server_config);
    (void)server.start();

    auto response = server.handle_request("/health/ready");
    TEST_ASSERT(response.status_code == 200, "readiness should return 200");

    return true;
}

bool test_health_server_handle_readiness_unhealthy() {
    health_config hc_config;
    health_checker checker(hc_config);

    checker.register_check(
        "failing",
        [](std::chrono::milliseconds) {
            return component_health{.name = "failing", .status = health_status::unhealthy};
        },
        true
    );

    health_server::config server_config;
    health_server server(checker, server_config);
    (void)server.start();

    auto response = server.handle_request("/health/ready");
    TEST_ASSERT(response.status_code == 503,
                "unhealthy readiness should return 503");

    return true;
}

bool test_health_server_handle_deep() {
    health_config hc_config;
    health_checker checker(hc_config);

    checker.register_check(
        "comp",
        [](std::chrono::milliseconds) {
            component_health h;
            h.name = "comp";
            h.status = health_status::healthy;
            h.metrics["metric1"] = "100";
            return h;
        },
        true
    );

    health_server::config server_config;
    health_server server(checker, server_config);
    (void)server.start();

    auto response = server.handle_request("/health/deep");
    TEST_ASSERT(response.status_code == 200, "deep should return 200");
    TEST_ASSERT(response.body.find("\"components\"") != std::string::npos,
                "body should contain components");

    return true;
}

bool test_health_server_handle_not_found() {
    health_config hc_config;
    health_checker checker(hc_config);

    health_server::config server_config;
    health_server server(checker, server_config);
    (void)server.start();

    auto response = server.handle_request("/invalid/path");
    TEST_ASSERT(response.status_code == 404, "invalid path should return 404");

    return true;
}

bool test_health_server_statistics() {
    health_config hc_config;
    health_checker checker(hc_config);

    health_server::config server_config;
    health_server server(checker, server_config);
    (void)server.start();

    (void)server.handle_request("/health/live");
    (void)server.handle_request("/health/ready");
    (void)server.handle_request("/health/deep");
    (void)server.handle_request("/invalid");

    auto stats = server.get_statistics();
    TEST_ASSERT(stats.total_requests == 4, "should have 4 total requests");
    TEST_ASSERT(stats.liveness_requests == 1, "should have 1 liveness request");
    TEST_ASSERT(stats.readiness_requests == 1,
                "should have 1 readiness request");
    TEST_ASSERT(stats.deep_health_requests == 1,
                "should have 1 deep health request");
    TEST_ASSERT(stats.errors == 1, "should have 1 error");

    return true;
}

bool test_health_server_urls() {
    health_config hc_config;
    health_checker checker(hc_config);

    health_server::config server_config;
    server_config.port = 9090;
    server_config.base_path = "/api/health";
    server_config.bind_address = "127.0.0.1";
    health_server server(checker, server_config);

    TEST_ASSERT(server.port() == 9090, "port should be 9090");
    TEST_ASSERT(server.base_path() == "/api/health",
                "base_path should be /api/health");
    TEST_ASSERT(server.liveness_url() == "http://127.0.0.1:9090/api/health/live",
                "liveness_url should be correct");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// JSON Serialization Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_json_liveness() {
    auto result = liveness_result::ok();
    auto json = to_json(result);

    TEST_ASSERT(json.find("\"status\": \"UP\"") != std::string::npos,
                "should contain UP status");
    TEST_ASSERT(json.find("\"timestamp\"") != std::string::npos,
                "should contain timestamp");

    return true;
}

bool test_json_readiness() {
    readiness_result result;
    result.status = health_status::healthy;
    result.timestamp = std::chrono::system_clock::now();
    result.components["comp1"] = health_status::healthy;
    result.components["comp2"] = health_status::degraded;

    auto json = to_json(result);

    TEST_ASSERT(json.find("\"checks\"") != std::string::npos,
                "should contain checks");
    TEST_ASSERT(json.find("\"comp1\"") != std::string::npos,
                "should contain comp1");
    TEST_ASSERT(json.find("\"comp2\"") != std::string::npos,
                "should contain comp2");

    return true;
}

bool test_json_deep_health() {
    deep_health_result result;
    result.status = health_status::healthy;
    result.timestamp = std::chrono::system_clock::now();
    result.message = "All systems operational";

    component_health comp;
    comp.name = "test_comp";
    comp.status = health_status::healthy;
    comp.response_time_ms = 42;
    comp.details = "Test details";
    comp.metrics["count"] = "100";
    comp.metrics["rate"] = "0.95";
    result.components.push_back(comp);

    auto json = to_json(result);

    TEST_ASSERT(json.find("\"message\"") != std::string::npos,
                "should contain message");
    TEST_ASSERT(json.find("\"components\"") != std::string::npos,
                "should contain components");
    TEST_ASSERT(json.find("\"test_comp\"") != std::string::npos,
                "should contain test_comp");
    TEST_ASSERT(json.find("\"response_time_ms\": 42") != std::string::npos,
                "should contain response_time_ms");
    TEST_ASSERT(json.find("\"metrics\"") != std::string::npos,
                "should contain metrics");

    return true;
}

bool test_timestamp_format() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = format_timestamp(now);

    // Should be ISO 8601 format: YYYY-MM-DDTHH:MM:SS.mmmZ
    TEST_ASSERT(timestamp.length() >= 20, "timestamp should be at least 20 chars");
    TEST_ASSERT(timestamp.find('T') != std::string::npos,
                "timestamp should contain T separator");
    TEST_ASSERT(timestamp.back() == 'Z', "timestamp should end with Z");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Configuration Helper Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_k8s_probe_config() {
    auto yaml = generate_k8s_probe_config(8081, "/health");

    TEST_ASSERT(yaml.find("livenessProbe:") != std::string::npos,
                "should contain livenessProbe");
    TEST_ASSERT(yaml.find("readinessProbe:") != std::string::npos,
                "should contain readinessProbe");
    TEST_ASSERT(yaml.find("path: /health/live") != std::string::npos,
                "should contain liveness path");
    TEST_ASSERT(yaml.find("path: /health/ready") != std::string::npos,
                "should contain readiness path");
    TEST_ASSERT(yaml.find("port: 8081") != std::string::npos,
                "should contain port");

    return true;
}

bool test_docker_healthcheck() {
    auto cmd = generate_docker_healthcheck(8080, "/health");

    TEST_ASSERT(cmd.find("HEALTHCHECK") != std::string::npos,
                "should contain HEALTHCHECK");
    TEST_ASSERT(cmd.find("--interval=30s") != std::string::npos,
                "should contain interval");
    TEST_ASSERT(cmd.find("http://localhost:8080/health/live") != std::string::npos,
                "should contain health URL");

    return true;
}

}  // namespace pacs::bridge::monitoring::test

// ═══════════════════════════════════════════════════════════════════════════
// Test Runner
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    using namespace pacs::bridge::monitoring::test;

    int passed = 0;
    int failed = 0;

    std::cout << "═══════════════════════════════════════════════════════════"
              << std::endl;
    std::cout << "PACS Bridge Health Check Unit Tests" << std::endl;
    std::cout << "═══════════════════════════════════════════════════════════"
              << std::endl;

    // Health Types Tests
    std::cout << "\n--- Health Types Tests ---" << std::endl;
    RUN_TEST(test_health_status_to_string);
    RUN_TEST(test_health_status_parsing);
    RUN_TEST(test_health_error_codes);
    RUN_TEST(test_component_health_is_healthy);
    RUN_TEST(test_liveness_result);
    RUN_TEST(test_readiness_result_all_healthy);
    RUN_TEST(test_readiness_result_some_unhealthy);
    RUN_TEST(test_deep_health_calculate_status);
    RUN_TEST(test_deep_health_find_component);

    // Component Check Tests
    std::cout << "\n--- Component Check Tests ---" << std::endl;
    RUN_TEST(test_mllp_server_check_healthy);
    RUN_TEST(test_mllp_server_check_unhealthy);
    RUN_TEST(test_pacs_connection_check_healthy);
    RUN_TEST(test_pacs_connection_check_unhealthy);
    RUN_TEST(test_queue_health_check_healthy);
    RUN_TEST(test_queue_health_check_degraded);
    RUN_TEST(test_fhir_server_check_optional);
    RUN_TEST(test_memory_health_check);

    // Health Checker Tests
    std::cout << "\n--- Health Checker Tests ---" << std::endl;
    RUN_TEST(test_health_checker_liveness);
    RUN_TEST(test_health_checker_register_component);
    RUN_TEST(test_health_checker_register_lambda);
    RUN_TEST(test_health_checker_unregister);
    RUN_TEST(test_health_checker_readiness_all_healthy);
    RUN_TEST(test_health_checker_readiness_critical_unhealthy);
    RUN_TEST(test_health_checker_deep);
    RUN_TEST(test_health_checker_check_specific_component);

    // Health Server Tests
    std::cout << "\n--- Health Server Tests ---" << std::endl;
    RUN_TEST(test_health_server_start_stop);
    RUN_TEST(test_health_server_handle_liveness);
    RUN_TEST(test_health_server_handle_readiness);
    RUN_TEST(test_health_server_handle_readiness_unhealthy);
    RUN_TEST(test_health_server_handle_deep);
    RUN_TEST(test_health_server_handle_not_found);
    RUN_TEST(test_health_server_statistics);
    RUN_TEST(test_health_server_urls);

    // JSON Serialization Tests
    std::cout << "\n--- JSON Serialization Tests ---" << std::endl;
    RUN_TEST(test_json_liveness);
    RUN_TEST(test_json_readiness);
    RUN_TEST(test_json_deep_health);
    RUN_TEST(test_timestamp_format);

    // Configuration Helper Tests
    std::cout << "\n--- Configuration Helper Tests ---" << std::endl;
    RUN_TEST(test_k8s_probe_config);
    RUN_TEST(test_docker_healthcheck);

    // Summary
    std::cout << "\n═══════════════════════════════════════════════════════════"
              << std::endl;
    std::cout << "Test Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "═══════════════════════════════════════════════════════════"
              << std::endl;

    return failed == 0 ? 0 : 1;
}
