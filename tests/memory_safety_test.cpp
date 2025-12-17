/**
 * @file memory_safety_test.cpp
 * @brief Memory leak detection and safety tests
 *
 * Tests for memory management correctness:
 * - Resource cleanup verification
 * - Memory allocation patterns
 * - Long-running operation memory stability
 * - RAII compliance verification
 *
 * Note: These tests verify memory safety patterns programmatically.
 * For comprehensive leak detection, run with Valgrind or AddressSanitizer:
 *   valgrind --leak-check=full ./memory_safety_test
 *   ./memory_safety_test (built with -fsanitize=address)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/163
 */

#include "pacs/bridge/mllp/mllp_client.h"
#include "pacs/bridge/mllp/mllp_server.h"
#include "pacs/bridge/mllp/mllp_types.h"
#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace pacs::bridge::memory::test {

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
        auto start = std::chrono::high_resolution_clock::now();                \
        if (test_func()) {                                                     \
            auto end = std::chrono::high_resolution_clock::now();              \
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( \
                end - start);                                                  \
            std::cout << "  PASSED (" << duration.count() << "ms)"             \
                      << std::endl;                                            \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED" << std::endl;                              \
            failed++;                                                          \
        }                                                                      \
    } while (0)

/**
 * @brief Wait until a condition is met or timeout occurs
 */
template <typename Predicate>
bool wait_for(Predicate condition, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

/**
 * @brief Generate unique port number for test isolation
 */
static uint16_t generate_test_port() {
    static std::atomic<uint16_t> port_counter{15000};
    return port_counter.fetch_add(1);
}

// =============================================================================
// Memory Monitoring Utilities
// =============================================================================

/**
 * @brief Get current process memory usage in bytes
 *
 * Cross-platform implementation for macOS and Linux.
 */
size_t get_current_memory_usage() {
#if defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t size = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &size) != KERN_SUCCESS) {
        return 0;
    }
    return info.resident_size;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) {
        return 0;
    }
    size_t total_pages = 0;
    statm >> total_pages;
    return total_pages * static_cast<size_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;  // Unsupported platform
#endif
}

/**
 * @brief Memory usage tracker for detecting leaks
 */
class memory_tracker {
public:
    memory_tracker() : baseline_(get_current_memory_usage()) {}

    void reset_baseline() {
        baseline_ = get_current_memory_usage();
    }

    size_t current_usage() const {
        return get_current_memory_usage();
    }

    int64_t delta_bytes() const {
        return static_cast<int64_t>(current_usage()) - static_cast<int64_t>(baseline_);
    }

    double delta_mb() const {
        return static_cast<double>(delta_bytes()) / (1024.0 * 1024.0);
    }

    void record_sample() {
        samples_.push_back(current_usage());
    }

    size_t max_usage() const {
        if (samples_.empty()) {
            return current_usage();
        }
        return *std::max_element(samples_.begin(), samples_.end());
    }

    size_t min_usage() const {
        if (samples_.empty()) {
            return current_usage();
        }
        return *std::min_element(samples_.begin(), samples_.end());
    }

    void print_summary(const std::string& test_name) const {
        std::cout << "\n  Memory Summary for " << test_name << ":" << std::endl;
        std::cout << "    Baseline:      " << (baseline_ / 1024) << " KB" << std::endl;
        std::cout << "    Current:       " << (current_usage() / 1024) << " KB" << std::endl;
        std::cout << "    Delta:         " << std::fixed << std::setprecision(2)
                  << delta_mb() << " MB" << std::endl;
        if (!samples_.empty()) {
            std::cout << "    Min:           " << (min_usage() / 1024) << " KB" << std::endl;
            std::cout << "    Max:           " << (max_usage() / 1024) << " KB" << std::endl;
            std::cout << "    Samples:       " << samples_.size() << std::endl;
        }
    }

private:
    size_t baseline_;
    std::vector<size_t> samples_;
};

// =============================================================================
// Sample HL7 Messages
// =============================================================================

static const std::string SAMPLE_ORM =
    "MSH|^~\\&|PACS|RADIOLOGY|RIS|HOSPITAL|20240115120000||ORM^O01|MSG00001|P|2.4\r"
    "PID|1||12345^^^MRN||DOE^JOHN^A||19800101|M|||123 MAIN ST^^CITY^ST^12345\r"
    "PV1|1|O|RADIOLOGY|||||||||||||||V123456\r"
    "ORC|NW|ORDER123|PLACER456||SC||^^^20240115120000||20240115120000|SMITH^JOHN\r"
    "OBR|1|ORDER123|FILLER789|12345^CHEST XRAY^LOCAL|||20240115120000|||||||ORDERING^PHYSICIAN\r";

// =============================================================================
// Mock Server for Memory Tests
// =============================================================================

class memory_test_server {
public:
    explicit memory_test_server(uint16_t port)
        : port_(port), running_(false), messages_processed_(0) {}

    ~memory_test_server() {
        stop();
    }

    bool start() {
        if (running_) {
            return false;
        }

        mllp::mllp_server_config config;
        config.port = port_;

        server_ = std::make_unique<mllp::mllp_server>(config);

        server_->set_message_handler(
            [this](const mllp::mllp_message& msg,
                   const mllp::mllp_session_info& /*session*/)
                -> std::optional<mllp::mllp_message> {
                return handle_message(msg);
            });

        auto result = server_->start();
        if (!result.has_value()) {
            return false;
        }

        running_ = true;
        return true;
    }

    void stop() {
        if (server_ && running_) {
            server_->stop(true, std::chrono::seconds{5});
            running_ = false;
        }
        server_.reset();
    }

    bool is_running() const {
        return running_;
    }

    uint64_t messages_processed() const {
        return messages_processed_.load();
    }

    uint16_t port() const {
        return port_;
    }

private:
    std::optional<mllp::mllp_message> handle_message(const mllp::mllp_message& msg) {
        messages_processed_++;

        // Parse and extract control ID
        hl7::hl7_parser parser;
        auto parse_result = parser.parse(msg.to_string());

        std::string msg_control_id = "0";
        if (parse_result.has_value()) {
            msg_control_id = parse_result->get_value("MSH.10");
        }

        // Generate ACK
        std::string ack =
            "MSH|^~\\&|RIS|HOSPITAL|PACS|RADIOLOGY|20240115||ACK|ACK" +
            msg_control_id + "|P|2.4\r" +
            "MSA|AA|" + msg_control_id + "\r";

        return mllp::mllp_message::from_string(ack);
    }

    uint16_t port_;
    std::unique_ptr<mllp::mllp_server> server_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> messages_processed_;
};

// =============================================================================
// Memory Leak Detection Tests
// =============================================================================

/**
 * @brief Test parser memory cleanup after repeated operations
 *
 * Verifies that repeated parsing doesn't accumulate memory.
 */
bool test_parser_memory_cleanup() {
    memory_tracker tracker;

    // Warm-up phase
    hl7::hl7_parser parser;
    for (int i = 0; i < 100; ++i) {
        auto result = parser.parse(SAMPLE_ORM);
    }

    tracker.reset_baseline();

    // Main test phase - many iterations
    const int iterations = 10000;
    for (int i = 0; i < iterations; ++i) {
        auto result = parser.parse(SAMPLE_ORM);
        TEST_ASSERT(result.has_value(), "Parse should succeed");

        if (i % 1000 == 0) {
            tracker.record_sample();
        }
    }

    tracker.print_summary("Parser Memory Cleanup");

    // Memory should not grow significantly (allow 5MB tolerance)
    TEST_ASSERT(tracker.delta_mb() < 5.0,
                "Memory growth should be < 5MB after " + std::to_string(iterations) + " parses");

    return true;
}

/**
 * @brief Test builder memory cleanup after repeated operations
 *
 * Verifies that repeated message building doesn't accumulate memory.
 */
bool test_builder_memory_cleanup() {
    memory_tracker tracker;

    // Warm-up phase
    for (int i = 0; i < 100; ++i) {
        auto result = hl7::hl7_builder::create()
            .message_type("ORM", "O01")
            .sending_app("PACS")
            .sending_facility("RADIOLOGY")
            .receiving_app("RIS")
            .receiving_facility("HOSPITAL")
            .control_id("MSG" + std::to_string(i))
            .build();
        (void)result;
    }

    tracker.reset_baseline();

    // Main test phase
    const int iterations = 10000;
    for (int i = 0; i < iterations; ++i) {
        auto result = hl7::hl7_builder::create()
            .message_type("ORM", "O01")
            .sending_app("PACS")
            .sending_facility("RADIOLOGY")
            .receiving_app("RIS")
            .receiving_facility("HOSPITAL")
            .control_id("MSG" + std::to_string(i))
            .build();
        TEST_ASSERT(result.has_value(), "Build should succeed");

        if (i % 1000 == 0) {
            tracker.record_sample();
        }
    }

    tracker.print_summary("Builder Memory Cleanup");

    TEST_ASSERT(tracker.delta_mb() < 5.0,
                "Memory growth should be < 5MB after " + std::to_string(iterations) + " builds");

    return true;
}

/**
 * @brief Test server resource cleanup
 *
 * Verifies that server start/stop cycles don't leak memory.
 */
bool test_server_resource_cleanup() {
    memory_tracker tracker;

    // Warm-up
    {
        uint16_t port = generate_test_port();
        memory_test_server server(port);
        server.start();
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{1000});
        server.stop();
    }

    tracker.reset_baseline();

    // Multiple start/stop cycles
    const int cycles = 10;
    for (int c = 0; c < cycles; ++c) {
        uint16_t port = generate_test_port();
        memory_test_server server(port);

        TEST_ASSERT(server.start(), "Server should start");
        TEST_ASSERT(
            wait_for([&server]() { return server.is_running(); },
                     std::chrono::milliseconds{2000}),
            "Server should be running");

        // Brief operation
        std::this_thread::sleep_for(std::chrono::milliseconds{50});

        server.stop();

        if (c % 2 == 0) {
            tracker.record_sample();
        }
    }

    tracker.print_summary("Server Resource Cleanup");

    TEST_ASSERT(tracker.delta_mb() < 10.0,
                "Memory growth should be < 10MB after " + std::to_string(cycles) + " server cycles");

    return true;
}

/**
 * @brief Test client connection cleanup
 *
 * Verifies that client connect/disconnect cycles don't leak memory.
 */
bool test_client_connection_cleanup() {
    uint16_t port = generate_test_port();
    memory_test_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    memory_tracker tracker;

    // Warm-up
    for (int i = 0; i < 5; ++i) {
        mllp::mllp_client_config config;
        config.host = "localhost";
        config.port = port;
        config.connect_timeout = std::chrono::milliseconds{5000};

        mllp::mllp_client client(config);
        (void)client.connect();
        client.disconnect();
    }

    tracker.reset_baseline();

    // Multiple connect/disconnect cycles
    const int cycles = 50;
    for (int c = 0; c < cycles; ++c) {
        mllp::mllp_client_config config;
        config.host = "localhost";
        config.port = port;
        config.connect_timeout = std::chrono::milliseconds{5000};

        mllp::mllp_client client(config);

        auto connect_result = client.connect();
        TEST_ASSERT(connect_result.has_value(), "Client should connect");

        // Send a message
        auto mllp_msg = mllp::mllp_message::from_string(SAMPLE_ORM);
        auto send_result = client.send(mllp_msg);

        client.disconnect();

        if (c % 10 == 0) {
            tracker.record_sample();
        }
    }

    server.stop();

    tracker.print_summary("Client Connection Cleanup");

    TEST_ASSERT(tracker.delta_mb() < 10.0,
                "Memory growth should be < 10MB after " + std::to_string(cycles) + " client cycles");

    return true;
}

/**
 * @brief Test concurrent operation memory stability
 *
 * Verifies memory doesn't accumulate under concurrent load.
 */
bool test_concurrent_memory_stability() {
    uint16_t port = generate_test_port();
    memory_test_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    memory_tracker tracker;

    // Warm-up phase with concurrent clients
    {
        std::vector<std::future<void>> warmup_futures;
        for (int t = 0; t < 4; ++t) {
            warmup_futures.push_back(std::async(std::launch::async, [&]() {
                mllp::mllp_client_config config;
                config.host = "localhost";
                config.port = port;
                config.connect_timeout = std::chrono::milliseconds{5000};

                mllp::mllp_client client(config);
                if (client.connect().has_value()) {
                    for (int i = 0; i < 10; ++i) {
                        auto mllp_msg = mllp::mllp_message::from_string(SAMPLE_ORM);
                        (void)client.send(mllp_msg);
                    }
                    client.disconnect();
                }
            }));
        }
        for (auto& f : warmup_futures) {
            f.wait();
        }
    }

    tracker.reset_baseline();

    // Main concurrent test
    const int num_threads = 8;
    const int messages_per_thread = 100;
    std::atomic<uint32_t> successful{0};

    std::vector<std::future<void>> futures;
    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            mllp::mllp_client_config config;
            config.host = "localhost";
            config.port = port;
            config.connect_timeout = std::chrono::milliseconds{10000};
            config.keep_alive = true;

            mllp::mllp_client client(config);
            if (!client.connect().has_value()) {
                return;
            }

            for (int i = 0; i < messages_per_thread; ++i) {
                std::string msg_id = "MEMTEST_" + std::to_string(t) + "_" + std::to_string(i);
                std::string message =
                    "MSH|^~\\&|PACS|RAD|RIS|HOSP|20240115||ORM^O01|" + msg_id + "|P|2.4\r"
                    "PID|1||MEM" + std::to_string(t * 1000 + i) + "|||TEST^MEMORY\r";

                auto mllp_msg = mllp::mllp_message::from_string(message);
                auto send_result = client.send(mllp_msg);

                if (send_result.has_value()) {
                    successful++;
                }
            }

            client.disconnect();
        }));
    }

    // Sample memory during execution
    for (int sample = 0; sample < 5; ++sample) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        tracker.record_sample();
    }

    for (auto& f : futures) {
        f.wait();
    }

    server.stop();

    tracker.print_summary("Concurrent Memory Stability");

    uint32_t total = num_threads * messages_per_thread;
    std::cout << "    Messages Sent:   " << successful << "/" << total << std::endl;

    // Allow some memory variation but it should stabilize
    TEST_ASSERT(tracker.delta_mb() < 20.0,
                "Memory growth should be < 20MB under concurrent load");

    return true;
}

/**
 * @brief Test long-running operation memory stability
 *
 * Simulates sustained operation and checks for memory leaks.
 */
bool test_long_running_memory_stability() {
    uint16_t port = generate_test_port();
    memory_test_server server(port);

    TEST_ASSERT(server.start(), "Server should start");
    TEST_ASSERT(
        wait_for([&server]() { return server.is_running(); },
                 std::chrono::milliseconds{2000}),
        "Server should be running");

    memory_tracker tracker;

    // Warm-up
    {
        mllp::mllp_client_config config;
        config.host = "localhost";
        config.port = port;
        config.connect_timeout = std::chrono::milliseconds{5000};
        config.keep_alive = true;

        mllp::mllp_client client(config);
        if (client.connect().has_value()) {
            for (int i = 0; i < 100; ++i) {
                auto mllp_msg = mllp::mllp_message::from_string(SAMPLE_ORM);
                (void)client.send(mllp_msg);
            }
            client.disconnect();
        }
    }

    tracker.reset_baseline();

    // Long-running simulation (3 seconds)
    const auto test_duration = std::chrono::seconds{3};
    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> messages_sent{0};

    auto sender_future = std::async(std::launch::async, [&]() {
        mllp::mllp_client_config config;
        config.host = "localhost";
        config.port = port;
        config.connect_timeout = std::chrono::milliseconds{5000};
        config.keep_alive = true;

        mllp::mllp_client client(config);
        if (!client.connect().has_value()) {
            return;
        }

        while (!stop_flag) {
            std::string msg_id = "LONG_" + std::to_string(messages_sent.load());
            std::string message =
                "MSH|^~\\&|PACS|RAD|RIS|HOSP|20240115||ORM^O01|" + msg_id + "|P|2.4\r"
                "PID|1||LONG" + std::to_string(messages_sent.load()) + "|||LONG^TEST\r";

            auto mllp_msg = mllp::mllp_message::from_string(message);
            auto send_result = client.send(mllp_msg);

            if (send_result.has_value()) {
                messages_sent++;
            }
        }

        client.disconnect();
    });

    // Sample memory periodically during the test
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < test_duration) {
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        tracker.record_sample();
    }

    stop_flag = true;
    sender_future.wait();

    server.stop();

    tracker.print_summary("Long-Running Memory Stability");
    std::cout << "    Messages Processed: " << messages_sent << std::endl;

    // Check that memory didn't grow significantly
    TEST_ASSERT(tracker.delta_mb() < 15.0,
                "Memory growth should be < 15MB during long-running test");

    // Check that max memory stayed within bounds
    double max_delta = static_cast<double>(tracker.max_usage() - tracker.min_usage()) /
                       (1024.0 * 1024.0);
    std::cout << "    Memory Variance:    " << std::fixed << std::setprecision(2)
              << max_delta << " MB" << std::endl;

    TEST_ASSERT(max_delta < 25.0,
                "Memory variance should be < 25MB during operation");

    return true;
}

/**
 * @brief Test RAII compliance for message objects
 *
 * Verifies that message objects properly clean up in all scenarios.
 */
bool test_message_raii_compliance() {
    memory_tracker tracker;

    // Test scope-based cleanup
    tracker.reset_baseline();

    for (int i = 0; i < 1000; ++i) {
        // Create message in inner scope
        {
            auto mllp_msg = mllp::mllp_message::from_string(SAMPLE_ORM);
            std::string content = mllp_msg.to_string();
            TEST_ASSERT(!content.empty(), "Message should have content");
        }
        // Message should be destroyed here

        // Create parsed message in inner scope
        {
            hl7::hl7_parser parser;
            auto result = parser.parse(SAMPLE_ORM);
            TEST_ASSERT(result.has_value(), "Parse should succeed");

            std::string value(result->get_value("MSH.10"));
            TEST_ASSERT(!value.empty(), "Should extract value");
        }
        // Parsed message should be destroyed here
    }

    tracker.print_summary("RAII Compliance");

    TEST_ASSERT(tracker.delta_mb() < 2.0,
                "Memory should not grow significantly with proper RAII");

    return true;
}

/**
 * @brief Test exception safety memory handling
 *
 * Verifies memory is properly cleaned up when errors occur.
 */
bool test_exception_safety_memory() {
    memory_tracker tracker;

    // Warm-up
    hl7::hl7_parser parser;
    for (int i = 0; i < 50; ++i) {
        auto result = parser.parse("INVALID_MESSAGE");
    }

    tracker.reset_baseline();

    // Test parsing invalid messages
    const int iterations = 5000;
    for (int i = 0; i < iterations; ++i) {
        // Various invalid inputs
        auto result1 = parser.parse("");
        auto result2 = parser.parse("NOT_HL7");
        auto result3 = parser.parse("MSH|incomplete");
        auto result4 = parser.parse("PID|1||12345"); // No MSH

        // These should all fail gracefully
        TEST_ASSERT(!result1.has_value(), "Empty should fail");
        TEST_ASSERT(!result2.has_value(), "Invalid should fail");

        if (i % 1000 == 0) {
            tracker.record_sample();
        }
    }

    tracker.print_summary("Exception Safety Memory");

    TEST_ASSERT(tracker.delta_mb() < 5.0,
                "Memory should not grow from error handling");

    return true;
}

/**
 * @brief Test large message handling memory
 *
 * Verifies memory is properly managed with large messages.
 */
bool test_large_message_memory() {
    memory_tracker tracker;

    // Create a large message
    std::ostringstream large_msg;
    large_msg << "MSH|^~\\&|PACS|RAD|RIS|HOSP|20240115||ORU^R01|LARGE001|P|2.4\r";
    large_msg << "PID|1||12345|||DOE^JOHN\r";

    // Add many OBX segments to create ~100KB message
    for (int i = 0; i < 500; ++i) {
        large_msg << "OBX|" << i << "|TX|FINDING" << i
                  << "||This is a test finding segment number " << i
                  << " with additional text to increase size. "
                  << "More padding text here to make segments larger.||||||F\r";
    }

    std::string large_message = large_msg.str();
    std::cout << "    Large Message Size: " << (large_message.size() / 1024) << " KB" << std::endl;

    // Warm-up
    hl7::hl7_parser parser;
    for (int i = 0; i < 5; ++i) {
        auto result = parser.parse(large_message);
    }

    tracker.reset_baseline();

    // Parse large message repeatedly
    const int iterations = 100;
    for (int i = 0; i < iterations; ++i) {
        auto result = parser.parse(large_message);
        TEST_ASSERT(result.has_value(), "Large message parse should succeed");

        if (i % 20 == 0) {
            tracker.record_sample();
        }
    }

    tracker.print_summary("Large Message Memory");

    // Allow larger tolerance for big messages
    TEST_ASSERT(tracker.delta_mb() < 20.0,
                "Memory should not grow excessively with large messages");

    return true;
}

}  // namespace pacs::bridge::memory::test

// =============================================================================
// Main
// =============================================================================

int main() {
    using namespace pacs::bridge::memory::test;

    std::cout << "==================================" << std::endl;
    std::cout << "PACS Bridge Memory Safety Tests" << std::endl;
    std::cout << "Issue #163: Memory Leak Detection" << std::endl;
    std::cout << "==================================" << std::endl;

    std::cout << "\nNote: For comprehensive leak detection, run with:" << std::endl;
    std::cout << "  valgrind --leak-check=full ./memory_safety_test" << std::endl;
    std::cout << "  or build with -fsanitize=address" << std::endl;

    int passed = 0;
    int failed = 0;

    // Memory Leak Detection Tests
    std::cout << "\n--- Parser and Builder Memory ---" << std::endl;
    RUN_TEST(test_parser_memory_cleanup);
    RUN_TEST(test_builder_memory_cleanup);

    std::cout << "\n--- Resource Cleanup ---" << std::endl;
    RUN_TEST(test_server_resource_cleanup);
    RUN_TEST(test_client_connection_cleanup);

    std::cout << "\n--- Stability Under Load ---" << std::endl;
    RUN_TEST(test_concurrent_memory_stability);
    RUN_TEST(test_long_running_memory_stability);

    std::cout << "\n--- RAII and Exception Safety ---" << std::endl;
    RUN_TEST(test_message_raii_compliance);
    RUN_TEST(test_exception_safety_memory);

    std::cout << "\n--- Large Message Handling ---" << std::endl;
    RUN_TEST(test_large_message_memory);

    // Summary
    std::cout << "\n==================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "==================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
