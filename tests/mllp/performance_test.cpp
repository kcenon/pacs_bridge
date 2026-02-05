/**
 * @file performance_test.cpp
 * @brief Performance benchmarks for MLLP network adapter
 *
 * Tests performance characteristics:
 * - Throughput measurement (messages/second)
 * - Latency measurement (p50, p95, p99)
 * - Concurrent connection performance
 * - Large message throughput
 * - Connection churn performance (1000+ connect/disconnect cycles)
 *
 * Performance Targets:
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
// MSG_DONTWAIT is POSIX-only, use 0 on Windows (blocking recv is acceptable for tests)
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pacs::bridge::mllp::test {

// =============================================================================
// Test Constants
// =============================================================================

/// Minimum acceptable throughput (messages per second)
constexpr size_t kMinThroughput = 1000;

/// Maximum acceptable p95 latency (milliseconds)
constexpr double kMaxP95LatencyMs = 10.0;

/// Number of messages for throughput test
constexpr size_t kThroughputMessageCount = 5000;

/// Number of connection churn cycles
constexpr size_t kConnectionChurnCycles = 1000;

/// Sample HL7 message for testing
const std::string kSampleHL7Message =
    "\x0BMSH|^~\\&|SENDING|FACILITY|RECEIVING|FACILITY|"
    "20231215120000||ADT^A01|MSG00001|P|2.5\r"
    "PID|1||12345^^^FACILITY||DOE^JOHN||19800101|M\r"
    "PV1|1|I|ICU^101^A|||||||||||||||V12345\r\x1C\x0D";

// =============================================================================
// Test Utilities
// =============================================================================

/**
 * @brief Generate unique port number for test isolation
 */
static uint16_t generate_test_port() {
    static std::atomic<uint16_t> port_counter{17000};
    return port_counter.fetch_add(1);
}

/**
 * @brief Wait for condition with timeout
 */
template <typename Predicate>
bool wait_for(Predicate condition, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

/**
 * @brief Calculate percentile from sorted latency values
 */
double calculate_percentile(std::vector<double>& latencies, double percentile) {
    if (latencies.empty()) {
        return 0.0;
    }
    std::sort(latencies.begin(), latencies.end());
    size_t index = static_cast<size_t>(percentile * latencies.size() / 100.0);
    if (index >= latencies.size()) {
        index = latencies.size() - 1;
    }
    return latencies[index];
}

/**
 * @brief Test fixture for performance tests
 */
class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port_ = generate_test_port();
#ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
    }

    void TearDown() override {
        if (server_) {
            server_->stop(true);
            server_.reset();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#ifdef _WIN32
        WSACleanup();
#endif
    }

    /**
     * @brief Create and start test server
     */
    std::unique_ptr<bsd_mllp_server> create_server(uint16_t port) {
        server_config config;
        config.port = port;
        config.backlog = 128;
        config.keep_alive = true;
        config.no_delay = true;

        auto server = std::make_unique<bsd_mllp_server>(config);

        server->on_connection([this](std::unique_ptr<mllp_session> session) {
            on_new_connection(std::move(session));
        });

        auto result = server->start();
        EXPECT_TRUE(result.has_value()) << "Server failed to start";

        return server;
    }

    /**
     * @brief Echo handler for performance testing
     */
    void start_echo_handler() {
        echo_thread_ = std::thread([this]() {
            while (!stop_echo_.load()) {
                std::unique_lock<std::mutex> lock(sessions_mutex_);
                sessions_cv_.wait_for(lock, std::chrono::milliseconds(10),
                                       [this] { return !pending_sessions_.empty() || stop_echo_.load(); });

                while (!pending_sessions_.empty()) {
                    auto session = std::move(pending_sessions_.back());
                    pending_sessions_.pop_back();
                    lock.unlock();

                    // Handle session in detached thread
                    std::thread([this, s = std::move(session)]() mutable {
                        while (s->is_open()) {
                            auto result = s->receive(65536, std::chrono::milliseconds(100));
                            if (result.has_value() && !result.value().empty()) {
                                (void)s->send(result.value());
                                messages_processed_.fetch_add(1);
                            }
                        }
                    }).detach();

                    lock.lock();
                }
            }
        });
    }

    void stop_echo_handler() {
        stop_echo_.store(true);
        sessions_cv_.notify_all();
        if (echo_thread_.joinable()) {
            echo_thread_.join();
        }
    }

    virtual void on_new_connection(std::unique_ptr<mllp_session> session) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        pending_sessions_.push_back(std::move(session));
        sessions_cv_.notify_all();
    }

    /**
     * @brief Create client socket connected to server
     */
    socket_t create_client_socket() {
        socket_t sock;
#ifdef _WIN32
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            return INVALID_SOCKET;
        }
#else
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return -1;
        }
#endif

        // Set TCP_NODELAY for low latency
        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(test_port_);
        server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (connect(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
#ifdef _WIN32
            closesocket(sock);
            return INVALID_SOCKET;
#else
            close(sock);
            return -1;
#endif
        }

        return sock;
    }

    void close_socket(socket_t sock) {
#ifdef _WIN32
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
        }
#else
        if (sock >= 0) {
            close(sock);
        }
#endif
    }

    uint16_t test_port_;
    std::unique_ptr<bsd_mllp_server> server_;
    std::vector<std::unique_ptr<mllp_session>> pending_sessions_;
    std::mutex sessions_mutex_;
    std::condition_variable sessions_cv_;
    std::thread echo_thread_;
    std::atomic<bool> stop_echo_{false};
    std::atomic<size_t> messages_processed_{0};
};

// =============================================================================
// Throughput Benchmarks
// =============================================================================

TEST_F(PerformanceTest, ThroughputBenchmark) {
    server_ = create_server(test_port_);
    start_echo_handler();

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    socket_t client = create_client_socket();
#ifdef _WIN32
    ASSERT_NE(INVALID_SOCKET, client);
#else
    ASSERT_GE(client, 0);
#endif

    auto start = std::chrono::steady_clock::now();

    // Send messages as fast as possible
    size_t messages_sent = 0;
    for (size_t i = 0; i < kThroughputMessageCount; ++i) {
        ssize_t sent = send(client, kSampleHL7Message.data(), kSampleHL7Message.size(), 0);
        if (sent > 0) {
            ++messages_sent;
        }

        // Receive echo (non-blocking drain)
        char buffer[4096];
        recv(client, buffer, sizeof(buffer), MSG_DONTWAIT);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double throughput = (messages_sent * 1000.0) / duration.count();

    std::cout << "Throughput: " << throughput << " messages/second" << std::endl;
    std::cout << "Messages sent: " << messages_sent << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;

    close_socket(client);
    stop_echo_handler();

    EXPECT_GE(throughput, kMinThroughput)
        << "Throughput " << throughput << " below target " << kMinThroughput;
}

TEST_F(PerformanceTest, ConcurrentClientThroughput) {
    server_ = create_server(test_port_);
    start_echo_handler();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const int num_clients = 10;
    const size_t messages_per_client = kThroughputMessageCount / num_clients;
    std::atomic<size_t> total_messages{0};
    std::vector<std::thread> client_threads;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_clients; ++i) {
        client_threads.emplace_back([this, &total_messages, count = messages_per_client]() {
            socket_t client = create_client_socket();
#ifdef _WIN32
            if (client == INVALID_SOCKET) return;
#else
            if (client < 0) return;
#endif

            for (size_t j = 0; j < count; ++j) {
                ssize_t sent = send(client, kSampleHL7Message.data(),
                                   kSampleHL7Message.size(), 0);
                if (sent > 0) {
                    total_messages.fetch_add(1);
                }
            }

            close_socket(client);
        });
    }

    for (auto& t : client_threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double throughput = (total_messages.load() * 1000.0) / duration.count();

    std::cout << "Concurrent throughput: " << throughput << " messages/second" << std::endl;
    std::cout << "Total messages: " << total_messages.load() << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;

    stop_echo_handler();

    EXPECT_GE(throughput, kMinThroughput)
        << "Concurrent throughput " << throughput << " below target " << kMinThroughput;
}

// =============================================================================
// Latency Benchmarks
// =============================================================================

TEST_F(PerformanceTest, LatencyMeasurement) {
    server_ = create_server(test_port_);
    start_echo_handler();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    socket_t client = create_client_socket();
#ifdef _WIN32
    ASSERT_NE(INVALID_SOCKET, client);
#else
    ASSERT_GE(client, 0);
#endif

    // Set socket to blocking for accurate latency measurement
    std::vector<double> latencies;
    latencies.reserve(1000);

    for (int i = 0; i < 1000; ++i) {
        auto start = std::chrono::steady_clock::now();

        // Send
        send(client, kSampleHL7Message.data(), kSampleHL7Message.size(), 0);

        // Receive echo
        char buffer[4096];
        ssize_t received = recv(client, buffer, sizeof(buffer), 0);

        auto end = std::chrono::steady_clock::now();

        if (received > 0) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            latencies.push_back(duration.count() / 1000.0);  // Convert to ms
        }
    }

    close_socket(client);
    stop_echo_handler();

    if (!latencies.empty()) {
        double p50 = calculate_percentile(latencies, 50);
        double p95 = calculate_percentile(latencies, 95);
        double p99 = calculate_percentile(latencies, 99);
        double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

        std::cout << "Latency (ms):" << std::endl;
        std::cout << "  Average: " << avg << std::endl;
        std::cout << "  p50: " << p50 << std::endl;
        std::cout << "  p95: " << p95 << std::endl;
        std::cout << "  p99: " << p99 << std::endl;

        EXPECT_LE(p95, kMaxP95LatencyMs)
            << "p95 latency " << p95 << "ms exceeds target " << kMaxP95LatencyMs << "ms";
    }
}

// =============================================================================
// Connection Churn Benchmarks
// =============================================================================

TEST_F(PerformanceTest, ConnectionChurn) {
    server_ = create_server(test_port_);

    std::atomic<size_t> successful_connections{0};

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        successful_connections.fetch_add(1);
        // Session will be destroyed when leaving scope
    });

    auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < kConnectionChurnCycles; ++i) {
        socket_t client = create_client_socket();
#ifdef _WIN32
        if (client != INVALID_SOCKET) {
            closesocket(client);
        }
#else
        if (client >= 0) {
            close(client);
        }
#endif
    }

    // Wait for all connections to be processed
    wait_for([&] { return successful_connections.load() >= kConnectionChurnCycles; },
             std::chrono::seconds(30));

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double connections_per_second = (successful_connections.load() * 1000.0) / duration.count();

    std::cout << "Connection churn:" << std::endl;
    std::cout << "  Connections: " << successful_connections.load() << std::endl;
    std::cout << "  Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "  Rate: " << connections_per_second << " connections/second" << std::endl;

    EXPECT_GE(successful_connections.load(), kConnectionChurnCycles * 0.99)
        << "Too many connection failures during churn test";
}

TEST_F(PerformanceTest, ConcurrentConnectionChurn) {
    server_ = create_server(test_port_);

    std::atomic<size_t> successful_connections{0};

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        successful_connections.fetch_add(1);
    });

    const int num_threads = 10;
    const size_t cycles_per_thread = kConnectionChurnCycles / num_threads;
    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, count = cycles_per_thread]() {
            for (size_t i = 0; i < count; ++i) {
                socket_t client = create_client_socket();
                close_socket(client);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Wait for server to process all connections
    wait_for([&] { return successful_connections.load() >= kConnectionChurnCycles * 0.9; },
             std::chrono::seconds(30));

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Concurrent connection churn:" << std::endl;
    std::cout << "  Connections: " << successful_connections.load() << std::endl;
    std::cout << "  Duration: " << duration.count() << " ms" << std::endl;

    EXPECT_GE(successful_connections.load(), kConnectionChurnCycles * 0.9)
        << "Too many failures during concurrent connection churn";
}

// =============================================================================
// Large Message Benchmarks
// =============================================================================

TEST_F(PerformanceTest, LargeMessageThroughput) {
    server_ = create_server(test_port_);
    start_echo_handler();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    socket_t client = create_client_socket();
#ifdef _WIN32
    ASSERT_NE(INVALID_SOCKET, client);
#else
    ASSERT_GE(client, 0);
#endif

    // Create large message (100KB)
    std::string large_message(100 * 1024, 'X');

    auto start = std::chrono::steady_clock::now();

    size_t total_bytes_sent = 0;
    const size_t target_bytes = 10 * 1024 * 1024;  // 10MB total

    while (total_bytes_sent < target_bytes) {
        ssize_t sent = send(client, large_message.data(), large_message.size(), 0);
        if (sent > 0) {
            total_bytes_sent += static_cast<size_t>(sent);
        } else {
            break;
        }

        // Drain receive buffer
        char buffer[65536];
        recv(client, buffer, sizeof(buffer), MSG_DONTWAIT);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double mb_per_second = (total_bytes_sent / (1024.0 * 1024.0)) / (duration.count() / 1000.0);

    std::cout << "Large message throughput:" << std::endl;
    std::cout << "  Total bytes: " << total_bytes_sent << std::endl;
    std::cout << "  Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "  Throughput: " << mb_per_second << " MB/s" << std::endl;

    close_socket(client);
    stop_echo_handler();

    EXPECT_GT(total_bytes_sent, target_bytes * 0.9) << "Failed to send most of the data";
}

// =============================================================================
// Concurrent Connection Scaling
// =============================================================================

TEST_F(PerformanceTest, ConnectionScaling) {
    server_ = create_server(test_port_);

    std::vector<size_t> connection_counts = {10, 50, 100};
    std::vector<socket_t> clients;

    for (size_t target_count : connection_counts) {
        // Clear previous connections
        for (auto sock : clients) {
            close_socket(sock);
        }
        clients.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto start = std::chrono::steady_clock::now();

        // Establish connections
        for (size_t i = 0; i < target_count; ++i) {
            socket_t client = create_client_socket();
#ifdef _WIN32
            if (client != INVALID_SOCKET) {
                clients.push_back(client);
            }
#else
            if (client >= 0) {
                clients.push_back(client);
            }
#endif
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Connection scaling (" << target_count << " connections):" << std::endl;
        std::cout << "  Established: " << clients.size() << std::endl;
        std::cout << "  Duration: " << duration.count() << " ms" << std::endl;

        EXPECT_GE(clients.size(), target_count * 0.95)
            << "Failed to establish " << target_count << " connections";
    }

    // Cleanup
    for (auto sock : clients) {
        close_socket(sock);
    }
}

}  // namespace pacs::bridge::mllp::test
