/**
 * @file performance_test.cpp
 * @brief Performance benchmarks for MLLP network adapter
 *
 * Tests performance characteristics:
 * - Throughput measurement (messages/second)
 * - Latency measurement (p50, p95, p99)
 * - Concurrent connection performance
 * - Large message throughput
 * - Connection churn performance
 *
 * Performance targets (from #307, #277):
 * - Throughput: >1000 HL7 messages/second
 * - Latency p95: <10ms per message
 * - Memory: <100MB for 100 concurrent connections
 * - CPU: <50% single core at max throughput
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/317
 */

#include "src/mllp/bsd_mllp_server.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

// Platform-specific socket headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using ssize_t = std::ptrdiff_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pacs::bridge::mllp::test {

// =============================================================================
// Performance Test Utilities
// =============================================================================

/**
 * @brief Scale iteration count for CI environment
 * CI builds run with heavily reduced iterations to avoid timeout
 * Uses compile-time detection for reliability
 *
 * CI environments are significantly slower (10-100x) due to:
 * - Shared CPU resources with other CI jobs
 * - Slower I/O (network loopback, disk)
 * - Lower memory bandwidth
 * - Potential CPU throttling
 *
 * Apply aggressive 10000x reduction to keep tests under 2 minutes
 */
static int scale_for_ci(int normal_count) {
#ifdef PACS_BRIDGE_CI_BUILD
    // Compile-time CI detection: 10000x reduction for CI builds
    return std::max(1, normal_count / 10000);
#else
    // Runtime detection as fallback: 10000x reduction for CI builds
    static const bool is_ci = (std::getenv("CI") != nullptr ||
                               std::getenv("GITHUB_ACTIONS") != nullptr ||
                               std::getenv("GITLAB_CI") != nullptr);
    return is_ci ? std::max(1, normal_count / 10000) : normal_count;
#endif
}

/**
 * @brief Generate unique port number for test isolation
 */
static uint16_t generate_test_port() {
    static std::atomic<uint16_t> port_counter{17000};
    return port_counter.fetch_add(1);
}

/**
 * @brief Calculate percentile from sorted latency data
 */
static double calculate_percentile(const std::vector<double>& sorted_data, double percentile) {
    if (sorted_data.empty()) {
        return 0.0;
    }

    size_t index = static_cast<size_t>(sorted_data.size() * percentile);
    if (index >= sorted_data.size()) {
        index = sorted_data.size() - 1;
    }

    return sorted_data[index];
}

/**
 * @brief Latency statistics
 */
struct latency_stats {
    double min_ms = 0.0;
    double max_ms = 0.0;
    double mean_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;

    void print() const {
        printf("  Min:  %.2f ms\n", min_ms);
        printf("  Mean: %.2f ms\n", mean_ms);
        printf("  p50:  %.2f ms\n", p50_ms);
        printf("  p95:  %.2f ms\n", p95_ms);
        printf("  p99:  %.2f ms\n", p99_ms);
        printf("  Max:  %.2f ms\n", max_ms);
    }
};

/**
 * @brief Calculate latency statistics from measurements
 */
static latency_stats calculate_latency_stats(std::vector<double> latencies_us) {
    if (latencies_us.empty()) {
        return {};
    }

    std::sort(latencies_us.begin(), latencies_us.end());

    latency_stats stats;
    stats.min_ms = latencies_us.front() / 1000.0;
    stats.max_ms = latencies_us.back() / 1000.0;
    stats.p50_ms = calculate_percentile(latencies_us, 0.50) / 1000.0;
    stats.p95_ms = calculate_percentile(latencies_us, 0.95) / 1000.0;
    stats.p99_ms = calculate_percentile(latencies_us, 0.99) / 1000.0;

    double sum = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);
    stats.mean_ms = (sum / latencies_us.size()) / 1000.0;

    return stats;
}

/**
 * @brief Test fixture for performance tests
 */
class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port_ = generate_test_port();
    }

    void TearDown() override {
        if (server_) {
            server_->stop(true);
            server_.reset();
        }
        // Allow time for socket cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    /**
     * @brief Create and start test server
     */
    std::unique_ptr<bsd_mllp_server> create_server(uint16_t port) {
        server_config config;
        config.port = port;
        config.backlog = 256;  // Higher backlog for performance tests
        config.keep_alive = true;

        auto server = std::make_unique<bsd_mllp_server>(config);

        server->on_connection([this](std::unique_ptr<mllp_session> session) {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_.push_back(std::move(session));
        });

        auto result = server->start();
        EXPECT_TRUE(result.has_value()) << "Server failed to start";

        return server;
    }

    /**
     * @brief Create client socket
     */
    socket_t create_client_socket(uint16_t port) {
#ifdef _WIN32
        static bool wsa_initialized = false;
        if (!wsa_initialized) {
            WSADATA wsa_data;
            WSAStartup(MAKEWORD(2, 2), &wsa_data);
            wsa_initialized = true;
        }
        socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            return sock;
        }
#else
        socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return sock;
        }
#endif

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        int result =
            connect(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

#ifdef _WIN32
        if (result == SOCKET_ERROR) {
            closesocket(sock);
            return INVALID_SOCKET;
        }
#else
        if (result < 0) {
            close(sock);
            return -1;
        }
#endif

        return sock;
    }

    /**
     * @brief Close client socket
     */
    void close_client_socket(socket_t sock) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }

    uint16_t test_port_;
    std::unique_ptr<bsd_mllp_server> server_;
    std::vector<std::unique_ptr<mllp_session>> sessions_;
    std::mutex sessions_mutex_;
};

// =============================================================================
// Throughput Benchmarks
// =============================================================================

TEST_F(PerformanceTest, ThroughputBenchmark) {
    const int num_messages = scale_for_ci(10000);
    const std::string test_message = "MSH|^~\\&|TEST|FACILITY|||20240101000000||ADT^A01|MSG001|P|2.5\r";

    std::atomic<int> messages_received{0};
    std::mutex stats_mutex;
    auto start_time = std::chrono::steady_clock::now();

    server_ = create_server(test_port_);

    // Override connection handler to count messages
    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        std::thread([&, s = std::move(session)]() mutable {
            while (messages_received.load() < num_messages) {
                auto result = s->receive(1024, std::chrono::seconds(10));
                if (result.has_value()) {
                    messages_received.fetch_add(1);
                } else {
                    break;
                }
            }
        }).detach();
    });

    // Create client and send messages
    socket_t client = create_client_socket(test_port_);
    ASSERT_TRUE(client >= 0);

    // Allow connection to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < num_messages; ++i) {
        ssize_t sent = send(client, test_message.data(), test_message.size(), 0);
        ASSERT_GT(sent, 0);
    }

    // Wait for all messages to be received
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (messages_received.load() < num_messages &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    close_client_socket(client);

    // Calculate throughput
    double duration_sec = duration_ms / 1000.0;
    double throughput = messages_received.load() / duration_sec;

    // Scale throughput target for CI environment
    const double target_throughput = scale_for_ci(100000) / 100.0;  // CI: 10 msg/s, Normal: 1000 msg/s

    printf("\n=== Throughput Benchmark ===\n");
    printf("Messages: %d\n", messages_received.load());
    printf("Duration: %.2f seconds\n", duration_sec);
    printf("Throughput: %.0f messages/second\n", throughput);
    printf("Target: >%.0f messages/second\n", target_throughput);

    EXPECT_EQ(num_messages, messages_received.load());
    EXPECT_GT(throughput, target_throughput) << "Throughput below target";
}

// =============================================================================
// Latency Benchmarks
// =============================================================================

TEST_F(PerformanceTest, LatencyBenchmark) {
    const int num_messages = scale_for_ci(1000);
    const std::string test_message = "MSH|^~\\&|TEST|FACILITY|||20240101000000||ADT^A01|MSG001|P|2.5\r";

    std::vector<double> latencies_us;
    std::mutex latencies_mutex;
    std::atomic<bool> server_ready{false};

    server_ = create_server(test_port_);

    // Echo server
    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        server_ready.store(true);
        std::thread([&, s = std::move(session)]() mutable {
            for (int i = 0; i < num_messages; ++i) {
                auto result = s->receive(1024, std::chrono::seconds(10));
                if (result.has_value()) {
                    s->send(result.value());
                } else {
                    break;
                }
            }
        }).detach();
    });

    socket_t client = create_client_socket(test_port_);
    ASSERT_TRUE(client >= 0);

    // Wait for server to be ready
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!server_ready.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(server_ready.load());

    // Measure round-trip latency for each message
    for (int i = 0; i < num_messages; ++i) {
        auto send_start = std::chrono::steady_clock::now();

        // Send message
        ssize_t sent = send(client, test_message.data(), test_message.size(), 0);
        ASSERT_GT(sent, 0);

        // Receive echo
        char buffer[1024] = {};
        ssize_t received = recv(client, buffer, sizeof(buffer), 0);
        ASSERT_GT(received, 0);

        auto send_end = std::chrono::steady_clock::now();
        auto latency_us =
            std::chrono::duration_cast<std::chrono::microseconds>(send_end - send_start).count();

        latencies_us.push_back(static_cast<double>(latency_us));
    }

    close_client_socket(client);

    // Calculate statistics
    auto stats = calculate_latency_stats(latencies_us);

    printf("\n=== Latency Benchmark ===\n");
    printf("Messages: %d\n", num_messages);
    stats.print();
    printf("Target: p95 < 10 ms\n");

    EXPECT_LT(stats.p95_ms, 10.0) << "p95 latency exceeds target";
}

// =============================================================================
// Connection Churn Performance
// =============================================================================

TEST_F(PerformanceTest, ConnectionChurnPerformance) {
    const int num_iterations = scale_for_ci(1000);

    std::atomic<int> connections_accepted{0};

    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        connections_accepted.fetch_add(1);
        // Immediately release session (connection will close)
    });

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < num_iterations; ++i) {
        socket_t client = create_client_socket(test_port_);
        if (client >= 0) {
            close_client_socket(client);
        }

        // Small delay to avoid overwhelming the server
        if (i % 100 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Wait for all connections to be processed
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (connections_accepted.load() < num_iterations &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    printf("\n=== Connection Churn Performance ===\n");
    printf("Iterations: %d\n", num_iterations);
    printf("Accepted: %d\n", connections_accepted.load());
    printf("Duration: %.2f seconds\n", duration_ms / 1000.0);
    printf("Rate: %.0f connections/second\n",
           connections_accepted.load() / (duration_ms / 1000.0));

    EXPECT_GE(connections_accepted.load(), num_iterations * 0.95)
        << "Less than 95% of connections succeeded";
}

// =============================================================================
// Large Message Throughput
// =============================================================================

TEST_F(PerformanceTest, LargeMessageThroughput) {
    const int num_messages = scale_for_ci(100);
    const size_t message_size = 1024 * 1024;  // 1MB per message

    std::vector<uint8_t> large_message(message_size, 0xAB);
    std::atomic<int> messages_received{0};

    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        std::thread([&, s = std::move(session)]() mutable {
            for (int i = 0; i < num_messages; ++i) {
                auto result = s->receive(message_size, std::chrono::seconds(30));
                if (result.has_value()) {
                    messages_received.fetch_add(1);
                } else {
                    break;
                }
            }
        }).detach();
    });

    socket_t client = create_client_socket(test_port_);
    ASSERT_TRUE(client >= 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto start_time = std::chrono::steady_clock::now();

    // Send large messages
    for (int i = 0; i < num_messages; ++i) {
        size_t total_sent = 0;
        while (total_sent < large_message.size()) {
            ssize_t sent = send(client, reinterpret_cast<const char*>(large_message.data()) + total_sent,
                               large_message.size() - total_sent, 0);
            ASSERT_GT(sent, 0);
            total_sent += sent;
        }
    }

    // Wait for all messages
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (messages_received.load() < num_messages &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    close_client_socket(client);

    double duration_sec = duration_ms / 1000.0;
    double throughput_mb = (messages_received.load() * message_size) / (1024.0 * 1024.0) /
                          duration_sec;

    // Scale throughput target for CI environment
    const double target_throughput_mb = scale_for_ci(1000) / 100.0;  // CI: 0.1 MB/s, Normal: 10 MB/s

    printf("\n=== Large Message Throughput ===\n");
    printf("Messages: %d x %zu bytes\n", messages_received.load(), message_size);
    printf("Duration: %.2f seconds\n", duration_sec);
    printf("Throughput: %.2f MB/second\n", throughput_mb);
    printf("Target: >%.2f MB/second\n", target_throughput_mb);

    EXPECT_EQ(num_messages, messages_received.load());
    EXPECT_GT(throughput_mb, target_throughput_mb) << "Throughput too low for large messages";
}

// =============================================================================
// Concurrent Connection Performance
// =============================================================================

TEST_F(PerformanceTest, ConcurrentConnectionPerformance) {
    const int num_clients = std::max(1, scale_for_ci(50));
    const int messages_per_client = std::max(1, scale_for_ci(100));

    std::atomic<int> total_messages_received{0};

    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        std::thread([&, s = std::move(session)]() mutable {
            for (int i = 0; i < messages_per_client; ++i) {
                auto result = s->receive(1024, std::chrono::seconds(10));
                if (result.has_value()) {
                    total_messages_received.fetch_add(1);
                } else {
                    break;
                }
            }
        }).detach();
    });

    std::vector<std::thread> client_threads;
    auto start_time = std::chrono::steady_clock::now();

    // Launch concurrent clients
    for (int i = 0; i < num_clients; ++i) {
        client_threads.emplace_back([this, messages_per_client]() {
            socket_t client = create_client_socket(test_port_);
            if (client < 0) {
                return;
            }

            std::string test_message =
                "MSH|^~\\&|TEST|FACILITY|||20240101000000||ADT^A01|MSG001|P|2.5\r";

            for (int j = 0; j < messages_per_client; ++j) {
                send(client, test_message.data(), test_message.size(), 0);
            }

            close_client_socket(client);
        });
    }

    // Wait for all client threads
    for (auto& t : client_threads) {
        t.join();
    }

    // Wait for server to process all messages
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    int expected_messages = num_clients * messages_per_client;
    while (total_messages_received.load() < expected_messages &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    double throughput = total_messages_received.load() / (duration_ms / 1000.0);

    // Scale throughput target for CI environment
    const double target_throughput = scale_for_ci(100000) / 100.0;  // CI: 10 msg/s, Normal: 1000 msg/s

    printf("\n=== Concurrent Connection Performance ===\n");
    printf("Clients: %d\n", num_clients);
    printf("Messages per client: %d\n", messages_per_client);
    printf("Total messages: %d\n", total_messages_received.load());
    printf("Duration: %.2f seconds\n", duration_ms / 1000.0);
    printf("Throughput: %.0f messages/second\n", throughput);
    printf("Target: >%.0f messages/second\n", target_throughput);

    EXPECT_GE(total_messages_received.load(), expected_messages * 0.95)
        << "Less than 95% of messages received";
    EXPECT_GT(throughput, target_throughput) << "Concurrent throughput below target";
}

}  // namespace pacs::bridge::mllp::test
