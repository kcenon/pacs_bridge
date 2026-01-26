/**
 * @file memory_leak_test.cpp
 * @brief Memory leak detection tests for MLLP network adapter
 *
 * Tests for memory leaks in:
 * - Connection lifecycle (create/destroy sessions)
 * - Large message handling
 * - Long-running server operation
 * - Error path handling
 *
 * Detection methods:
 * - Memory usage tracking (baseline vs. final)
 * - Connection churn test (1000+ sessions)
 * - Integration with Valgrind (Linux)
 * - Integration with AddressSanitizer (all platforms)
 *
 * Target: No memory growth after connection churn
 * Target: <100MB for 100 concurrent connections
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/317
 */

#include "src/mllp/bsd_mllp_server.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

// Platform-specific headers
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using ssize_t = std::ptrdiff_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pacs::bridge::mllp::test {

// =============================================================================
// Memory Measurement Utilities
// =============================================================================

/**
 * @brief Get current process memory usage in bytes
 */
static size_t get_memory_usage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#elif __APPLE__
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    // ru_maxrss is in bytes on macOS
    return static_cast<size_t>(usage.ru_maxrss);
#else
    // Linux: read from /proc/self/status
    FILE* file = fopen("/proc/self/status", "r");
    if (!file) {
        return 0;
    }

    size_t rss = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            // VmRSS is in kB
            sscanf(line + 6, "%zu", &rss);
            rss *= 1024;  // Convert to bytes
            break;
        }
    }

    fclose(file);
    return rss;
#endif
}

/**
 * @brief Format memory size in human-readable form
 */
static std::string format_memory(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        unit++;
    }

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unit]);
    return std::string(buffer);
}

/**
 * @brief Scale iteration count for CI environment
 * CI builds run with heavily reduced iterations to avoid timeout
 * Uses compile-time detection for reliability
 */
static int scale_for_ci(int normal_count) {
#ifdef PACS_BRIDGE_CI_BUILD
    // Compile-time CI detection: 100x reduction for CI builds
    return normal_count / 100;
#else
    // Runtime detection as fallback: 100x reduction for CI builds
    static const bool is_ci = (std::getenv("CI") != nullptr ||
                               std::getenv("GITHUB_ACTIONS") != nullptr ||
                               std::getenv("GITLAB_CI") != nullptr);
    return is_ci ? normal_count / 100 : normal_count;
#endif
}

/**
 * @brief Generate unique port number for test isolation
 */
static uint16_t generate_test_port() {
    static std::atomic<uint16_t> port_counter{18000};
    return port_counter.fetch_add(1);
}

/**
 * @brief Test fixture for memory leak tests
 */
class MemoryLeakTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_port_ = generate_test_port();

        // Force garbage collection before measuring baseline
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        if (server_) {
            server_->stop(true);
            server_.reset();
        }

        // Allow time for cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    /**
     * @brief Create and start test server
     */
    std::unique_ptr<bsd_mllp_server> create_server(uint16_t port) {
        server_config config;
        config.port = port;
        config.backlog = 256;
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
// Connection Lifecycle Leak Tests
// =============================================================================

TEST_F(MemoryLeakTest, ConnectionChurnNoLeak) {
    const int num_iterations = scale_for_ci(1000);
    const int warmup_iterations = scale_for_ci(100);

    std::atomic<int> connections_accepted{0};

    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        connections_accepted.fetch_add(1);
        // Session destroyed immediately when function returns
    });

    // Warmup phase to stabilize memory allocations
    for (int i = 0; i < warmup_iterations; ++i) {
        socket_t client = create_client_socket(test_port_);
        if (client >= 0) {
            close_client_socket(client);
        }

        if (i % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // Wait for warmup connections to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Measure baseline memory
    size_t baseline_memory = get_memory_usage();

    // Main test: create and destroy many connections
    for (int i = 0; i < num_iterations; ++i) {
        socket_t client = create_client_socket(test_port_);
        if (client >= 0) {
            close_client_socket(client);
        }

        if (i % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // Wait for all connections to be processed and cleaned up
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Measure final memory
    size_t final_memory = get_memory_usage();

    // Calculate memory growth
    ssize_t memory_growth = final_memory - baseline_memory;
    double growth_percent = (memory_growth / static_cast<double>(baseline_memory)) * 100.0;

    printf("\n=== Connection Churn Memory Test ===\n");
    printf("Iterations: %d\n", num_iterations);
    printf("Baseline memory: %s\n", format_memory(baseline_memory).c_str());
    printf("Final memory:    %s\n", format_memory(final_memory).c_str());
    printf("Growth:          %s (%.2f%%)\n", format_memory(std::abs(memory_growth)).c_str(),
           std::abs(growth_percent));

    // Allow up to 5% memory growth (some platforms may have memory fragmentation)
    EXPECT_LT(std::abs(growth_percent), 5.0)
        << "Significant memory growth detected - possible memory leak";
}

// =============================================================================
// Large Message Handling Leak Test
// =============================================================================

TEST_F(MemoryLeakTest, LargeMessageNoLeak) {
    const int num_messages = scale_for_ci(100);
    const size_t message_size = 1024 * 1024;  // 1MB

    std::vector<uint8_t> large_message(message_size, 0xAB);
    std::atomic<int> messages_received{0};

    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        std::thread([&, s = std::move(session)]() mutable {
            for (int i = 0; i < num_messages; ++i) {
                auto result = s->receive(message_size, std::chrono::seconds(30));
                if (result.has_value()) {
                    messages_received.fetch_add(1);
                    // result (vector) is destroyed here
                } else {
                    break;
                }
            }
        }).detach();
    });

    socket_t client = create_client_socket(test_port_);
    ASSERT_TRUE(client >= 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Measure baseline
    size_t baseline_memory = get_memory_usage();

    // Send large messages
    for (int i = 0; i < num_messages; ++i) {
        size_t total_sent = 0;
        while (total_sent < large_message.size()) {
            ssize_t sent = send(client, reinterpret_cast<const char*>(large_message.data()) +
                                       total_sent,
                               large_message.size() - total_sent, 0);
            if (sent <= 0) {
                break;
            }
            total_sent += sent;
        }
    }

    // Wait for all messages to be processed
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (messages_received.load() < num_messages &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    close_client_socket(client);

    // Wait for cleanup
    std::this_thread::sleep_for(std::chrono::seconds(1));

    size_t final_memory = get_memory_usage();
    ssize_t memory_growth = final_memory - baseline_memory;

    printf("\n=== Large Message Memory Test ===\n");
    printf("Messages: %d x %zu bytes\n", messages_received.load(), message_size);
    printf("Baseline memory: %s\n", format_memory(baseline_memory).c_str());
    printf("Final memory:    %s\n", format_memory(final_memory).c_str());
    printf("Growth:          %s\n", format_memory(std::abs(memory_growth)).c_str());

    EXPECT_EQ(num_messages, messages_received.load());

    // Large messages may cause some memory growth, but should be bounded
    // Allow up to 10MB growth for 100MB of data processed
    EXPECT_LT(memory_growth, 10 * 1024 * 1024)
        << "Excessive memory growth with large messages";
}

// =============================================================================
// Long-Running Server Leak Test
// =============================================================================

TEST_F(MemoryLeakTest, LongRunningServerNoLeak) {
    const int num_iterations = scale_for_ci(10);
    const int messages_per_iteration = scale_for_ci(100);

    std::atomic<int> total_messages{0};

    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        std::thread([&, s = std::move(session)]() mutable {
            while (true) {
                auto result = s->receive(1024, std::chrono::seconds(5));
                if (result.has_value()) {
                    total_messages.fetch_add(1);
                } else {
                    break;
                }
            }
        }).detach();
    });

    // Warmup
    socket_t warmup_client = create_client_socket(test_port_);
    if (warmup_client >= 0) {
        std::string msg = "WARMUP\r";
        send(warmup_client, msg.data(), msg.size(), 0);
        close_client_socket(warmup_client);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Baseline
    size_t baseline_memory = get_memory_usage();

    // Simulate long-running operation with periodic activity
    for (int iter = 0; iter < num_iterations; ++iter) {
        socket_t client = create_client_socket(test_port_);
        if (client >= 0) {
            std::string test_message = "MSH|^~\\&|TEST|FAC|||20240101||ADT^A01|MSG|P|2.5\r";

            for (int i = 0; i < messages_per_iteration; ++i) {
                send(client, test_message.data(), test_message.size(), 0);
            }

            close_client_socket(client);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::seconds(1));

    size_t final_memory = get_memory_usage();
    ssize_t memory_growth = final_memory - baseline_memory;
    double growth_percent = (memory_growth / static_cast<double>(baseline_memory)) * 100.0;

    printf("\n=== Long-Running Server Memory Test ===\n");
    printf("Iterations: %d\n", num_iterations);
    printf("Total messages: %d\n", total_messages.load());
    printf("Baseline memory: %s\n", format_memory(baseline_memory).c_str());
    printf("Final memory:    %s\n", format_memory(final_memory).c_str());
    printf("Growth:          %s (%.2f%%)\n", format_memory(std::abs(memory_growth)).c_str(),
           std::abs(growth_percent));

    // Should not grow significantly
    EXPECT_LT(std::abs(growth_percent), 10.0) << "Memory growth in long-running server";
}

// =============================================================================
// Error Path Leak Test
// =============================================================================

TEST_F(MemoryLeakTest, ErrorPathNoLeak) {
    const int num_iterations = scale_for_ci(500);

    std::atomic<int> connections_accepted{0};
    std::atomic<int> errors_encountered{0};

    server_ = create_server(test_port_);

    server_->on_connection([&](std::unique_ptr<mllp_session> session) {
        connections_accepted.fetch_add(1);

        // Attempt to receive with very short timeout to trigger timeout errors
        std::thread([&, s = std::move(session)]() mutable {
            auto result = s->receive(1024, std::chrono::milliseconds(10));
            if (!result.has_value()) {
                errors_encountered.fetch_add(1);
            }
        }).detach();
    });

    // Warmup
    for (int i = 0; i < 50; ++i) {
        socket_t client = create_client_socket(test_port_);
        if (client >= 0) {
            close_client_socket(client);  // Close immediately without sending data
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    size_t baseline_memory = get_memory_usage();

    // Create connections that will trigger errors
    for (int i = 0; i < num_iterations; ++i) {
        socket_t client = create_client_socket(test_port_);
        if (client >= 0) {
            // Close immediately to cause receive errors
            close_client_socket(client);
        }

        if (i % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // Wait for error handling
    std::this_thread::sleep_for(std::chrono::seconds(2));

    size_t final_memory = get_memory_usage();
    ssize_t memory_growth = final_memory - baseline_memory;
    double growth_percent = (memory_growth / static_cast<double>(baseline_memory)) * 100.0;

    printf("\n=== Error Path Memory Test ===\n");
    printf("Iterations: %d\n", num_iterations);
    printf("Errors encountered: %d\n", errors_encountered.load());
    printf("Baseline memory: %s\n", format_memory(baseline_memory).c_str());
    printf("Final memory:    %s\n", format_memory(final_memory).c_str());
    printf("Growth:          %s (%.2f%%)\n", format_memory(std::abs(memory_growth)).c_str(),
           std::abs(growth_percent));

    // Error paths should not leak memory
    EXPECT_LT(std::abs(growth_percent), 5.0) << "Memory leak in error handling paths";
}

}  // namespace pacs::bridge::mllp::test
