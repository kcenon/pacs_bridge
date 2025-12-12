/**
 * @file prometheus_endpoint_test.cpp
 * @brief Integration tests for Prometheus metrics HTTP endpoint
 *
 * Tests cover:
 * - HTTP server startup and shutdown
 * - Prometheus metrics endpoint response
 * - Health check endpoints via HTTP
 * - Concurrent connections handling
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/88
 */

#include "pacs/bridge/monitoring/bridge_metrics.h"
#include "pacs/bridge/monitoring/health_checker.h"
#include "pacs/bridge/monitoring/health_server.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Platform-specific socket headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t INVALID_SOCKET_VALUE = -1;
#define CLOSE_SOCKET ::close
#endif

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

// Test HTTP port - use high port to avoid permission issues
constexpr uint16_t TEST_PORT = 19191;

/**
 * @brief Send HTTP GET request and receive response
 *
 * @param port Server port
 * @param path Request path
 * @return Response string (status + body), empty on error
 */
std::string http_get(uint16_t port, const std::string& path) {
    // Create socket
    socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET_VALUE) {
        return "";
    }

    // Connect to server
    struct sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&server_addr),
                  sizeof(server_addr)) < 0) {
        CLOSE_SOCKET(sock);
        return "";
    }

    // Send HTTP request
    std::string request = "GET " + path + " HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Connection: close\r\n"
                          "\r\n";

    ::send(sock, request.data(), static_cast<int>(request.size()), 0);

    // Receive response
    std::string response;
    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = ::recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        response += buffer;
    }

    CLOSE_SOCKET(sock);
    return response;
}

// ═══════════════════════════════════════════════════════════════════════════
// Server Lifecycle Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_server_start_stop() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT;
    cfg.enable_metrics_endpoint = true;

    health_server server(checker, cfg);

    TEST_ASSERT(!server.is_running(), "Server should not be running initially");

    bool started = server.start();
    TEST_ASSERT(started, "Server should start successfully");
    TEST_ASSERT(server.is_running(), "Server should be running after start");

    // Wait a bit for server to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.stop(true);
    TEST_ASSERT(!server.is_running(), "Server should not be running after stop");

    return true;
}

bool test_server_double_start() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 1;

    health_server server(checker, cfg);

    bool first_start = server.start();
    TEST_ASSERT(first_start, "First start should succeed");

    // Wait for server to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool second_start = server.start();
    TEST_ASSERT(!second_start, "Second start should fail");

    server.stop(true);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Metrics Endpoint Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_metrics_endpoint() {
    // Initialize metrics collector
    auto& metrics = bridge_metrics_collector::instance();
    metrics.shutdown();
    metrics.initialize("test_service", 0);
    metrics.set_enabled(true);

    // Record some test metrics
    metrics.record_hl7_message_received("ADT");
    metrics.record_hl7_message_sent("ACK");
    metrics.record_mwl_entry_created();

    // Start server
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 2;
    cfg.enable_metrics_endpoint = true;
    cfg.metrics_path = "/metrics";

    health_server server(checker, cfg);
    TEST_ASSERT(server.start(), "Server should start");

    // Wait for server to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Send HTTP request to metrics endpoint
    std::string response = http_get(cfg.port, "/metrics");

    server.stop(true);

    TEST_ASSERT(!response.empty(), "Response should not be empty");
    TEST_ASSERT(response.find("HTTP/1.1 200") != std::string::npos,
                "Response should be 200 OK");
    TEST_ASSERT(response.find("text/plain") != std::string::npos,
                "Content-Type should be text/plain");
    TEST_ASSERT(response.find("hl7_messages_received_total") != std::string::npos,
                "Response should contain HL7 metrics");
    TEST_ASSERT(response.find("# HELP") != std::string::npos,
                "Response should contain HELP comments");
    TEST_ASSERT(response.find("# TYPE") != std::string::npos,
                "Response should contain TYPE comments");

    return true;
}

bool test_metrics_endpoint_custom_path() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 3;
    cfg.enable_metrics_endpoint = true;
    cfg.metrics_path = "/prometheus/metrics";

    health_server server(checker, cfg);
    TEST_ASSERT(server.start(), "Server should start");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string response = http_get(cfg.port, "/prometheus/metrics");

    server.stop(true);

    TEST_ASSERT(!response.empty(), "Response should not be empty");
    TEST_ASSERT(response.find("HTTP/1.1 200") != std::string::npos,
                "Custom metrics path should return 200");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Health Endpoint Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_liveness_endpoint() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 4;
    cfg.base_path = "/health";

    health_server server(checker, cfg);
    TEST_ASSERT(server.start(), "Server should start");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string response = http_get(cfg.port, "/health/live");

    server.stop(true);

    TEST_ASSERT(!response.empty(), "Response should not be empty");
    TEST_ASSERT(response.find("HTTP/1.1 200") != std::string::npos,
                "Liveness should return 200 OK");
    TEST_ASSERT(response.find("application/json") != std::string::npos,
                "Content-Type should be application/json");
    TEST_ASSERT(response.find("\"status\"") != std::string::npos,
                "Response should contain status field");

    return true;
}

bool test_readiness_endpoint() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 5;
    cfg.base_path = "/health";

    health_server server(checker, cfg);
    TEST_ASSERT(server.start(), "Server should start");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string response = http_get(cfg.port, "/health/ready");

    server.stop(true);

    TEST_ASSERT(!response.empty(), "Response should not be empty");
    TEST_ASSERT(response.find("HTTP/1.1") != std::string::npos,
                "Response should contain HTTP status");
    TEST_ASSERT(response.find("application/json") != std::string::npos,
                "Content-Type should be application/json");

    return true;
}

bool test_deep_health_endpoint() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 6;
    cfg.base_path = "/health";

    health_server server(checker, cfg);
    TEST_ASSERT(server.start(), "Server should start");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string response = http_get(cfg.port, "/health/deep");

    server.stop(true);

    TEST_ASSERT(!response.empty(), "Response should not be empty");
    TEST_ASSERT(response.find("application/json") != std::string::npos,
                "Content-Type should be application/json");
    TEST_ASSERT(response.find("\"components\"") != std::string::npos,
                "Deep health should contain components");

    return true;
}

bool test_not_found_endpoint() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 7;

    health_server server(checker, cfg);
    TEST_ASSERT(server.start(), "Server should start");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string response = http_get(cfg.port, "/nonexistent");

    server.stop(true);

    TEST_ASSERT(!response.empty(), "Response should not be empty");
    TEST_ASSERT(response.find("HTTP/1.1 404") != std::string::npos,
                "Unknown path should return 404");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Concurrent Connection Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_concurrent_requests() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 8;
    cfg.max_connections = 10;
    cfg.enable_metrics_endpoint = true;

    health_server server(checker, cfg);
    TEST_ASSERT(server.start(), "Server should start");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    constexpr int num_threads = 5;
    constexpr int requests_per_thread = 3;

    std::atomic<int> successful_requests{0};
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&cfg, &successful_requests]() {
            for (int j = 0; j < requests_per_thread; j++) {
                std::string response = http_get(cfg.port, "/metrics");
                if (response.find("HTTP/1.1 200") != std::string::npos) {
                    successful_requests++;
                }
                // Small delay between requests
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    server.stop(true);

    int expected = num_threads * requests_per_thread;
    TEST_ASSERT(successful_requests >= expected / 2,
                "At least half of concurrent requests should succeed");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Statistics Tests
// ═══════════════════════════════════════════════════════════════

bool test_server_statistics() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = TEST_PORT + 9;
    cfg.enable_metrics_endpoint = true;

    health_server server(checker, cfg);
    TEST_ASSERT(server.start(), "Server should start");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Make some requests
    http_get(cfg.port, "/health/live");
    http_get(cfg.port, "/health/ready");
    http_get(cfg.port, "/metrics");
    http_get(cfg.port, "/nonexistent");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto stats = server.get_statistics();

    server.stop(true);

    TEST_ASSERT(stats.liveness_requests >= 1, "Liveness requests should be counted");
    TEST_ASSERT(stats.readiness_requests >= 1, "Readiness requests should be counted");
    TEST_ASSERT(stats.metrics_requests >= 1, "Metrics requests should be counted");
    TEST_ASSERT(stats.errors >= 1, "Errors should be counted for 404");
    TEST_ASSERT(stats.total_requests >= 4, "Total requests should be at least 4");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// URL Generation Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_url_generation() {
    health_checker checker(health_config{});
    health_server::config cfg;
    cfg.port = 8080;
    cfg.bind_address = "0.0.0.0";
    cfg.base_path = "/health";
    cfg.metrics_path = "/metrics";

    health_server server(checker, cfg);

    TEST_ASSERT(server.liveness_url() == "http://0.0.0.0:8080/health/live",
                "Liveness URL should be correct");
    TEST_ASSERT(server.readiness_url() == "http://0.0.0.0:8080/health/ready",
                "Readiness URL should be correct");
    TEST_ASSERT(server.deep_health_url() == "http://0.0.0.0:8080/health/deep",
                "Deep health URL should be correct");
    TEST_ASSERT(server.metrics_url() == "http://0.0.0.0:8080/metrics",
                "Metrics URL should be correct");

    return true;
}

}  // namespace pacs::bridge::monitoring::test

// ═══════════════════════════════════════════════════════════════════════════
// Main Test Runner
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    using namespace pacs::bridge::monitoring::test;

#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    int passed = 0;
    int failed = 0;

    std::cout << "\n===== Prometheus Endpoint Tests =====" << std::endl;

    // Server lifecycle tests
    std::cout << "\n--- Server Lifecycle Tests ---" << std::endl;
    RUN_TEST(test_server_start_stop);
    RUN_TEST(test_server_double_start);

    // Metrics endpoint tests
    std::cout << "\n--- Metrics Endpoint Tests ---" << std::endl;
    RUN_TEST(test_metrics_endpoint);
    RUN_TEST(test_metrics_endpoint_custom_path);

    // Health endpoint tests
    std::cout << "\n--- Health Endpoint Tests ---" << std::endl;
    RUN_TEST(test_liveness_endpoint);
    RUN_TEST(test_readiness_endpoint);
    RUN_TEST(test_deep_health_endpoint);
    RUN_TEST(test_not_found_endpoint);

    // Concurrent tests
    std::cout << "\n--- Concurrent Request Tests ---" << std::endl;
    RUN_TEST(test_concurrent_requests);

    // Statistics tests
    std::cout << "\n--- Statistics Tests ---" << std::endl;
    RUN_TEST(test_server_statistics);

    // URL generation tests
    std::cout << "\n--- URL Generation Tests ---" << std::endl;
    RUN_TEST(test_url_generation);

    // Summary
    std::cout << "\n===== Summary =====" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "===================" << std::endl;

#ifdef _WIN32
    WSACleanup();
#endif

    return failed > 0 ? 1 : 0;
}
