/**
 * @file bsd_adapter_test.cpp
 * @brief Integration tests for BSD socket MLLP network adapter
 *
 * Tests BSD socket implementation:
 * - Connection lifecycle
 * - Multiple connections
 * - Large message transmission
 * - Timeout handling
 * - Error conditions
 * - Statistics accuracy
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/315
 */

#include "src/mllp/bsd_mllp_server.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// Platform-specific socket headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// ssize_t is POSIX-specific, define for Windows
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
    static std::atomic<uint16_t> port_counter{15000};
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
 * @brief Test fixture for BSD adapter tests
 */
class BSDAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port_ = generate_test_port();
    }

    void TearDown() override {
        if (server_) {
            server_->stop(true);  // Wait for proper cleanup
            server_.reset();
        }
        // Give time for socket cleanup to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    /**
     * @brief Create and start test server
     */
    std::unique_ptr<bsd_mllp_server> create_server(uint16_t port) {
        server_config config;
        config.port = port;
        config.backlog = 10;
        config.keep_alive = true;

        auto server = std::make_unique<bsd_mllp_server>(config);

        // Set connection callback
        server->on_connection([this](std::unique_ptr<mllp_session> session) {
            on_new_connection(std::move(session));
        });

        auto result = server->start();
        EXPECT_TRUE(result.has_value()) << "Server failed to start";

        return server;
    }

    /**
     * @brief Default connection handler (stores session)
     */
    virtual void on_new_connection(std::unique_ptr<mllp_session> session) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.push_back(std::move(session));
        sessions_cv_.notify_all();
    }

    /**
     * @brief Wait for N sessions to be accepted
     */
    bool wait_for_sessions(size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        return sessions_cv_.wait_for(lock, timeout,
                                      [this, count] { return sessions_.size() >= count; });
    }

    uint16_t test_port_;
    std::unique_ptr<bsd_mllp_server> server_;
    std::vector<std::unique_ptr<mllp_session>> sessions_;
    std::mutex sessions_mutex_;
    std::condition_variable sessions_cv_;
};

// =============================================================================
// Server Lifecycle Tests
// =============================================================================

TEST_F(BSDAdapterTest, ServerStartAndStop) {
    server_ = create_server(test_port_);

    EXPECT_TRUE(server_->is_running());
    EXPECT_EQ(test_port_, server_->port());

    server_->stop();

    EXPECT_FALSE(server_->is_running());
}

TEST_F(BSDAdapterTest, ServerStartOnInvalidPort) {
    server_config config;
    config.port = 0;  // Invalid port

    server_ = std::make_unique<bsd_mllp_server>(config);

    server_->on_connection([](std::unique_ptr<mllp_session>) {});

    auto result = server_->start();

    EXPECT_FALSE(result.has_value());
    // Port 0 is rejected as invalid configuration
    EXPECT_EQ(network_error::invalid_config, result.error());
}

TEST_F(BSDAdapterTest, ServerPortAlreadyInUse) {
    // Start first server
    server_ = create_server(test_port_);
    EXPECT_TRUE(server_->is_running());

    // Allow time for first server to fully bind the port
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Try to start second server on same port
    server_config config;
    config.port = test_port_;

    auto server2 = std::make_unique<bsd_mllp_server>(config);
    server2->on_connection([](std::unique_ptr<mllp_session>) {});

    auto result = server2->start();

    // Stop second server if it somehow started (wait for proper cleanup)
    if (server2) {
        server2->stop(true);  // Wait for complete shutdown
    }

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(network_error::bind_failed, result.error());
}

// =============================================================================
// Connection Lifecycle Tests
// =============================================================================

TEST_F(BSDAdapterTest, SingleConnectionLifecycle) {
    server_ = create_server(test_port_);

    // Connect client
    socket_t client_sock;
#ifdef _WIN32
    WSADATA wsa_data;
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data));
    client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(INVALID_SOCKET, client_sock);
#else
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_sock, 0);
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port_);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int connect_result =
        connect(client_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    ASSERT_EQ(0, connect_result);

    // Wait for server to accept connection
    ASSERT_TRUE(wait_for_sessions(1, std::chrono::seconds(5)));

    // Verify session
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        ASSERT_EQ(1u, sessions_.size());

        auto& session = sessions_[0];
        EXPECT_TRUE(session->is_open());
        EXPECT_EQ("127.0.0.1", session->remote_address());
        EXPECT_GT(session->session_id(), 0u);
    }

    // Close client
#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

TEST_F(BSDAdapterTest, SendAndReceive) {
    std::vector<uint8_t> server_received_data;
    std::mutex data_mutex;
    std::condition_variable data_cv;
    std::atomic<bool> thread_completed{false};

    // Custom connection handler
    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        // Receive data in background thread
        std::thread([&, s = std::move(session)]() mutable {
            auto result = s->receive(1024, std::chrono::seconds(5));
            if (result.has_value()) {
                std::lock_guard<std::mutex> lock(data_mutex);
                server_received_data = std::move(result.value());
                data_cv.notify_all();

                // Echo back
                s->send(server_received_data);
            }
            thread_completed.store(true);
        }).detach();
    });

    // Connect client
    socket_t client_sock;
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port_);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    connect(client_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

    // Send test data
    std::string test_message = "Hello MLLP Server!";
    send(client_sock, test_message.data(), test_message.size(), 0);

    // Wait for server to receive
    {
        std::unique_lock<std::mutex> lock(data_mutex);
        ASSERT_TRUE(data_cv.wait_for(lock, std::chrono::seconds(5),
                                      [&] { return !server_received_data.empty(); }));

        std::string received(server_received_data.begin(), server_received_data.end());
        EXPECT_EQ(test_message, received);
    }

    // Receive echo
    char buffer[1024] = {};
    ssize_t received = recv(client_sock, buffer, sizeof(buffer), 0);
    EXPECT_EQ(test_message.size(), static_cast<size_t>(received));
    EXPECT_EQ(test_message, std::string(buffer, received));

#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif

    // Wait for background thread to complete
    EXPECT_TRUE(wait_for([&] { return thread_completed.load(); }, std::chrono::seconds(2)));
}

// =============================================================================
// Multiple Connection Tests
// =============================================================================

TEST_F(BSDAdapterTest, SequentialConnections) {
    server_ = create_server(test_port_);

    const int num_connections = 10;

    for (int i = 0; i < num_connections; ++i) {
        socket_t client_sock;
#ifdef _WIN32
        if (i == 0) {
            WSADATA wsa_data;
            WSAStartup(MAKEWORD(2, 2), &wsa_data);
        }
        client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
        client_sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(test_port_);
        server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        int result =
            connect(client_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
        ASSERT_EQ(0, result) << "Connection " << i << " failed";

#ifdef _WIN32
        closesocket(client_sock);
#else
        close(client_sock);
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

#ifdef _WIN32
    WSACleanup();
#endif

    // Wait for all connections
    EXPECT_TRUE(wait_for_sessions(num_connections, std::chrono::seconds(5)));
}

TEST_F(BSDAdapterTest, ConcurrentConnections) {
    server_ = create_server(test_port_);

    const int num_clients = 10;
    std::vector<std::thread> client_threads;

    for (int i = 0; i < num_clients; ++i) {
        client_threads.emplace_back([this, i]() {
            socket_t client_sock;
#ifdef _WIN32
            WSADATA wsa_data;
            WSAStartup(MAKEWORD(2, 2), &wsa_data);
            client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
            client_sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(test_port_);
            server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            int result = connect(client_sock, reinterpret_cast<sockaddr*>(&server_addr),
                                 sizeof(server_addr));
            EXPECT_EQ(0, result) << "Client " << i << " connect failed";

            // Keep connection open briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

#ifdef _WIN32
            closesocket(client_sock);
            WSACleanup();
#else
            close(client_sock);
#endif
        });
    }

    // Wait for all client threads
    for (auto& t : client_threads) {
        t.join();
    }

    // Verify all connections were accepted
    EXPECT_TRUE(wait_for_sessions(num_clients, std::chrono::seconds(10)));
    EXPECT_GE(server_->active_session_count(), static_cast<size_t>(num_clients));
}

// =============================================================================
// Large Message Tests
// =============================================================================

TEST_F(BSDAdapterTest, LargeMessageTransmission) {
    std::vector<uint8_t> large_data(1024 * 1024, 0xAB);  // 1MB of data
    std::vector<uint8_t> server_received_data;
    std::mutex data_mutex;
    std::condition_variable data_cv;
    std::atomic<bool> thread_completed{false};

    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        std::thread([&, s = std::move(session)]() mutable {
            auto result = s->receive(large_data.size(), std::chrono::seconds(30));
            if (result.has_value()) {
                std::lock_guard<std::mutex> lock(data_mutex);
                server_received_data = std::move(result.value());
                data_cv.notify_all();
            }
            thread_completed.store(true);
        }).detach();
    });

    // Connect and send large data
    socket_t client_sock;
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port_);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    connect(client_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

    // Send in chunks
    size_t total_sent = 0;
    while (total_sent < large_data.size()) {
        ssize_t sent = send(client_sock, reinterpret_cast<const char*>(large_data.data()) + total_sent,
                            large_data.size() - total_sent, 0);
        ASSERT_GT(sent, 0) << "Send failed";
        total_sent += sent;
    }

    // Wait for server to receive
    {
        std::unique_lock<std::mutex> lock(data_mutex);
        ASSERT_TRUE(data_cv.wait_for(lock, std::chrono::seconds(30),
                                      [&] { return !server_received_data.empty(); }));

        EXPECT_EQ(large_data.size(), server_received_data.size());
        EXPECT_EQ(large_data, server_received_data);
    }

#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif

    // Wait for background thread to complete
    EXPECT_TRUE(wait_for([&] { return thread_completed.load(); }, std::chrono::seconds(5)));
}

// =============================================================================
// Statistics Tests
// =============================================================================

TEST_F(BSDAdapterTest, SessionStatistics) {
    server_ = create_server(test_port_);

    // Connect client
    socket_t client_sock;
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port_);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    connect(client_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

    // Wait for connection
    ASSERT_TRUE(wait_for_sessions(1, std::chrono::seconds(5)));

    // Send some data
    std::string test_data = "Test data for statistics";
    send(client_sock, test_data.data(), test_data.size(), 0);

    // Give server time to receive
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check statistics
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto& session = sessions_[0];

        auto stats = session->get_stats();
        EXPECT_GT(stats.bytes_received, 0u);
        EXPECT_EQ("127.0.0.1", session->remote_address());
    }

#ifdef _WIN32
    closesocket(client_sock);
    WSACleanup();
#else
    close(client_sock);
#endif
}

}  // namespace pacs::bridge::mllp::test
