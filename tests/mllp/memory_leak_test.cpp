/**
 * @file memory_leak_test.cpp
 * @brief Memory leak detection tests for MLLP network adapter
 *
 * Tests memory management:
 * - Connection lifecycle leak check
 * - Large message handling leak check
 * - Long-running server leak check
 * - Error path leak check
 *
 * Integration with:
 * - Valgrind (Linux)
 * - AddressSanitizer (all platforms)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/317
 */

#include "src/mllp/bsd_mllp_server.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// Platform-specific headers
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
// Test Utilities
// =============================================================================

/**
 * @brief Generate unique port number for test isolation
 */
static uint16_t generate_test_port() {
    static std::atomic<uint16_t> port_counter{18000};
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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

/**
 * @brief Test fixture for memory leak tests
 */
class MemoryLeakTest : public ::testing::Test {
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

        auto server = std::make_unique<bsd_mllp_server>(config);

        server->on_connection([this](std::unique_ptr<mllp_session> session) {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_.push_back(std::move(session));
            connection_count_.fetch_add(1);
            sessions_cv_.notify_all();
        });

        auto result = server->start();
        EXPECT_TRUE(result.has_value()) << "Server failed to start";

        return server;
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

    /**
     * @brief Wait for N connections
     */
    bool wait_for_connections(size_t count, std::chrono::milliseconds timeout) {
        return wait_for([this, count] { return connection_count_.load() >= count; }, timeout);
    }

    /**
     * @brief Clear all sessions
     */
    void clear_sessions() {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.clear();
    }

    uint16_t test_port_;
    std::unique_ptr<bsd_mllp_server> server_;
    std::vector<std::unique_ptr<mllp_session>> sessions_;
    std::mutex sessions_mutex_;
    std::condition_variable sessions_cv_;
    std::atomic<size_t> connection_count_{0};
};

// =============================================================================
// Connection Lifecycle Leak Tests
// =============================================================================

TEST_F(MemoryLeakTest, ConnectionLifecycleLeak) {
    // This test creates and destroys many connections
    // Memory leaks will be detected by Valgrind or ASan

    server_ = create_server(test_port_);

    const size_t num_iterations = 100;

    for (size_t i = 0; i < num_iterations; ++i) {
        socket_t client = create_client_socket();
#ifdef _WIN32
        ASSERT_NE(INVALID_SOCKET, client);
#else
        ASSERT_GE(client, 0);
#endif

        // Send some data
        const char* message = "Test message";
        send(client, message, strlen(message), 0);

        // Close connection
        close_socket(client);
    }

    // Wait for server to process all connections
    ASSERT_TRUE(wait_for_connections(num_iterations, std::chrono::seconds(10)));

    // Clear sessions to trigger destruction
    clear_sessions();

    // Server should still be healthy
    EXPECT_TRUE(server_->is_running());
}

TEST_F(MemoryLeakTest, RapidConnectionChurnLeak) {
    // Rapidly create and destroy connections to stress memory management

    server_ = create_server(test_port_);

    const size_t num_iterations = 1000;
    std::atomic<size_t> successful{0};

    // Use multiple threads for rapid churn
    std::vector<std::thread> threads;
    const int num_threads = 4;
    const size_t iterations_per_thread = num_iterations / num_threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &successful, count = iterations_per_thread]() {
            for (size_t i = 0; i < count; ++i) {
                socket_t client = create_client_socket();
#ifdef _WIN32
                if (client != INVALID_SOCKET) {
                    closesocket(client);
                    successful.fetch_add(1);
                }
#else
                if (client >= 0) {
                    close(client);
                    successful.fetch_add(1);
                }
#endif
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Wait for connections to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Clear sessions
    clear_sessions();

    std::cout << "Rapid churn: " << successful.load() << " successful connections" << std::endl;

    EXPECT_GE(successful.load(), num_iterations * 0.9)
        << "Too many connection failures";
}

// =============================================================================
// Large Message Leak Tests
// =============================================================================

TEST_F(MemoryLeakTest, LargeMessageHandlingLeak) {
    // Test that large message buffers are properly freed

    server_ = create_server(test_port_);

    // Create large message (1MB)
    std::vector<char> large_message(1024 * 1024, 'A');

    const size_t num_iterations = 10;

    for (size_t i = 0; i < num_iterations; ++i) {
        socket_t client = create_client_socket();
#ifdef _WIN32
        ASSERT_NE(INVALID_SOCKET, client);
#else
        ASSERT_GE(client, 0);
#endif

        // Send large message
        size_t total_sent = 0;
        while (total_sent < large_message.size()) {
            ssize_t sent = send(client, large_message.data() + total_sent,
                               large_message.size() - total_sent, 0);
            if (sent <= 0) break;
            total_sent += static_cast<size_t>(sent);
        }

        close_socket(client);
    }

    // Wait and clear
    ASSERT_TRUE(wait_for_connections(num_iterations, std::chrono::seconds(10)));
    clear_sessions();

    EXPECT_TRUE(server_->is_running());
}

TEST_F(MemoryLeakTest, RepeatedLargeAllocations) {
    // Stress test large allocations

    server_ = create_server(test_port_);

    // Set up echo handler
    server_->on_connection([](std::unique_ptr<mllp_session> session) {
        std::thread([s = std::move(session)]() mutable {
            while (s->is_open()) {
                auto result = s->receive(1024 * 1024, std::chrono::milliseconds(100));
                if (result.has_value() && !result.value().empty()) {
                    // Allocate and immediately free large buffer
                    std::vector<uint8_t> temp(result.value().size() * 2);
                    std::copy(result.value().begin(), result.value().end(), temp.begin());
                    temp.clear();
                    temp.shrink_to_fit();
                }
            }
        }).detach();
    });

    // Send varying size messages
    std::vector<size_t> sizes = {1024, 10240, 102400, 1024000};

    for (size_t size : sizes) {
        socket_t client = create_client_socket();
#ifdef _WIN32
        if (client == INVALID_SOCKET) continue;
#else
        if (client < 0) continue;
#endif

        std::vector<char> message(size, 'X');
        send(client, message.data(), message.size(), 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        close_socket(client);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(server_->is_running());
}

// =============================================================================
// Long Running Server Leak Tests
// =============================================================================

TEST_F(MemoryLeakTest, LongRunningServerLeak) {
    // Simulate long-running server with periodic connections

    server_ = create_server(test_port_);

    const auto test_duration = std::chrono::seconds(3);
    const auto connection_interval = std::chrono::milliseconds(50);

    auto start = std::chrono::steady_clock::now();
    size_t connections_made = 0;

    while (std::chrono::steady_clock::now() - start < test_duration) {
        socket_t client = create_client_socket();
#ifdef _WIN32
        if (client != INVALID_SOCKET) {
            const char* msg = "Periodic test";
            send(client, msg, strlen(msg), 0);
            closesocket(client);
            ++connections_made;
        }
#else
        if (client >= 0) {
            const char* msg = "Periodic test";
            send(client, msg, strlen(msg), 0);
            close(client);
            ++connections_made;
        }
#endif

        std::this_thread::sleep_for(connection_interval);
    }

    std::cout << "Long running test: " << connections_made << " connections over "
              << test_duration.count() << " seconds" << std::endl;

    // Clear sessions
    clear_sessions();

    EXPECT_TRUE(server_->is_running());
    EXPECT_GT(connections_made, 0u);
}

// =============================================================================
// Error Path Leak Tests
// =============================================================================

TEST_F(MemoryLeakTest, ErrorPathLeak_InvalidPort) {
    // Test that error paths don't leak memory

    for (int i = 0; i < 100; ++i) {
        server_config config;
        config.port = 0;  // Invalid port

        auto server = std::make_unique<bsd_mllp_server>(config);
        server->on_connection([](std::unique_ptr<mllp_session>) {});

        auto result = server->start();
        EXPECT_FALSE(result.has_value());

        // Server should clean up properly
        server.reset();
    }
}

TEST_F(MemoryLeakTest, ErrorPathLeak_AbruptDisconnect) {
    // Test memory cleanup when clients disconnect abruptly

    server_ = create_server(test_port_);

    std::atomic<size_t> abrupt_disconnects{0};

    for (size_t i = 0; i < 100; ++i) {
        socket_t client = create_client_socket();
#ifdef _WIN32
        if (client != INVALID_SOCKET) {
            // Send partial data then abruptly close
            const char* partial = "Partial...";
            send(client, partial, strlen(partial), 0);

            // Abrupt close with SO_LINGER = 0
            struct linger lin = {1, 0};
            setsockopt(client, SOL_SOCKET, SO_LINGER, (const char*)&lin, sizeof(lin));
            closesocket(client);
            abrupt_disconnects.fetch_add(1);
        }
#else
        if (client >= 0) {
            // Send partial data then abruptly close
            const char* partial = "Partial...";
            send(client, partial, strlen(partial), 0);

            // Abrupt close with SO_LINGER = 0
            struct linger lin = {1, 0};
            setsockopt(client, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
            close(client);
            abrupt_disconnects.fetch_add(1);
        }
#endif
    }

    std::cout << "Abrupt disconnects: " << abrupt_disconnects.load() << std::endl;

    // Wait for server to handle all disconnects
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Clear sessions
    clear_sessions();

    EXPECT_TRUE(server_->is_running());
}

TEST_F(MemoryLeakTest, ErrorPathLeak_ReceiveTimeout) {
    // Test memory cleanup on receive timeout

    server_ = create_server(test_port_);

    std::atomic<size_t> timeouts{0};

    server_->on_connection([&timeouts](std::unique_ptr<mllp_session> session) {
        std::thread([&timeouts, s = std::move(session)]() mutable {
            // Wait for data with short timeout
            auto result = s->receive(1024, std::chrono::milliseconds(100));
            if (!result.has_value()) {
                timeouts.fetch_add(1);
            }
        }).detach();
    });

    // Create connections but don't send data
    for (size_t i = 0; i < 10; ++i) {
        socket_t client = create_client_socket();
        // Don't send anything, just wait
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        close_socket(client);
    }

    // Wait for all timeouts
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Receive timeouts: " << timeouts.load() << std::endl;

    EXPECT_TRUE(server_->is_running());
}

// =============================================================================
// Server Start/Stop Cycle Leak Test
// =============================================================================

TEST_F(MemoryLeakTest, ServerStartStopCycleLeak) {
    // Test that repeatedly starting and stopping servers doesn't leak

    for (int i = 0; i < 20; ++i) {
        uint16_t port = generate_test_port();

        server_config config;
        config.port = port;
        config.backlog = 10;

        auto server = std::make_unique<bsd_mllp_server>(config);
        server->on_connection([](std::unique_ptr<mllp_session>) {});

        auto result = server->start();
        ASSERT_TRUE(result.has_value()) << "Failed to start server on iteration " << i;
        EXPECT_TRUE(server->is_running());

        // Create a few connections
        for (int j = 0; j < 5; ++j) {
            socket_t client = create_client_socket();
            close_socket(client);
        }

        // Stop server
        server->stop(true);
        EXPECT_FALSE(server->is_running());

        // Destroy server
        server.reset();

        // Give OS time to release resources
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

}  // namespace pacs::bridge::mllp::test
