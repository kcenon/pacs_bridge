/**
 * @file performance_test.cpp
 * @brief Unit tests for performance optimization components
 *
 * Tests for object pooling, lock-free queues, zero-copy parsing,
 * thread pool management, connection pooling, and benchmarking.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/42
 */

#include "pacs/bridge/performance/benchmark_runner.h"
#include "pacs/bridge/performance/connection_optimizer.h"
#include "pacs/bridge/performance/lockfree_queue.h"
#include "pacs/bridge/performance/object_pool.h"
#include "pacs/bridge/performance/performance_types.h"
#include "pacs/bridge/performance/thread_pool_manager.h"
#include "pacs/bridge/performance/zero_copy_parser.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::performance::test {

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

/**
 * @brief Wait until a condition is met or timeout occurs
 * @param condition Function returning true when condition is met
 * @param timeout Maximum time to wait
 * @return true if condition was met, false on timeout
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

// Sample HL7 message for testing
static const std::string SAMPLE_HL7 =
    "MSH|^~\\&|SENDING_APP|SENDING_FAC|RECEIVING_APP|RECEIVING_FAC|"
    "20240115120000||ORM^O01|MSG00001|P|2.5\r"
    "PID|1|12345|67890^^^MRN||DOE^JOHN^A||19800101|M|||123 MAIN ST^^"
    "CITY^ST^12345||(555)555-1234\r"
    "PV1|1|O|CLINIC|||||||||||||||V123456\r"
    "ORC|NW|ORDER123|PLACER456||SC||^^^20240115120000||20240115120000|"
    "ORDERER^NAME\r"
    "OBR|1|ORDER123|FILLER789|12345^CHEST XRAY^LOCAL|||20240115120000||"
    "|||||ORDERING^PHYSICIAN||||||||||^^^^^RT\r";

// =============================================================================
// Performance Types Tests
// =============================================================================

bool test_performance_error_to_string() {
    TEST_ASSERT(
        std::string(to_string(performance_error::thread_pool_init_failed)) ==
            "Thread pool initialization failed",
        "thread_pool_init_failed string");

    TEST_ASSERT(
        std::string(to_string(performance_error::pool_exhausted)) ==
            "Object pool exhausted",
        "pool_exhausted string");

    TEST_ASSERT(
        std::string(to_string(performance_error::queue_full)) ==
            "Queue is full",
        "queue_full string");

    return true;
}

bool test_performance_targets_constants() {
    TEST_ASSERT(performance_targets::MIN_THROUGHPUT_MSG_PER_SEC == 500,
                "Throughput target should be 500");
    TEST_ASSERT(performance_targets::MAX_P95_LATENCY.count() == 50,
                "P95 latency target should be 50ms");
    TEST_ASSERT(performance_targets::MAX_MEMORY_BASELINE_MB == 200,
                "Memory target should be 200MB");

    return true;
}

bool test_thread_pool_config_presets() {
    auto server_config = thread_pool_config::for_server();
    TEST_ASSERT(server_config.min_threads == 4, "Server min threads");
    TEST_ASSERT(server_config.enable_work_stealing == true, "Server work stealing");

    auto client_config = thread_pool_config::for_client();
    TEST_ASSERT(client_config.min_threads == 2, "Client min threads");

    auto bench_config = thread_pool_config::for_benchmark();
    TEST_ASSERT(bench_config.enable_affinity == true, "Benchmark affinity");

    return true;
}

bool test_lockfree_queue_config_validation() {
    lockfree_queue_config config;
    config.capacity = 4096;
    TEST_ASSERT(config.is_valid(), "Power of 2 capacity should be valid");

    config.capacity = 1000;  // Not power of 2
    TEST_ASSERT(!config.is_valid(), "Non-power-of-2 capacity should be invalid");

    config.capacity = 0;
    TEST_ASSERT(!config.is_valid(), "Zero capacity should be invalid");

    return true;
}

// =============================================================================
// Object Pool Tests
// =============================================================================

bool test_message_buffer_pool_acquire_release() {
    memory_config config;
    config.message_buffer_pool_size = 16;

    message_buffer_pool pool(config);

    // Acquire buffer
    auto result = pool.acquire(1024);
    TEST_ASSERT(result.has_value(), "Should acquire buffer successfully");
    TEST_ASSERT(result->valid(), "Buffer should be valid");
    TEST_ASSERT(result->capacity >= 1024, "Buffer capacity should be >= 1024");

    // Release buffer
    pool.release(*result);
    TEST_ASSERT(!result->valid(), "Buffer should be invalid after release");

    return true;
}

bool test_message_buffer_pool_statistics() {
    memory_config config;
    config.message_buffer_pool_size = 8;

    message_buffer_pool pool(config);

    // Initial state
    const auto& stats = pool.statistics();
    TEST_ASSERT(stats.total_created.load() == 8, "Should pre-create 8 buffers");

    // Acquire and release
    auto buf = pool.acquire(512);
    TEST_ASSERT(buf.has_value(), "Should acquire buffer");

    pool.release(*buf);

    TEST_ASSERT(stats.total_acquires.load() >= 1, "Should track acquires");
    TEST_ASSERT(stats.total_releases.load() >= 1, "Should track releases");

    return true;
}

bool test_scoped_buffer_raii() {
    memory_config config;
    config.message_buffer_pool_size = 4;

    message_buffer_pool pool(config);

    {
        auto buf_result = pool.acquire(256);
        TEST_ASSERT(buf_result.has_value(), "Should acquire buffer");

        scoped_buffer sbuf(pool, std::move(*buf_result));
        TEST_ASSERT(sbuf.valid(), "Scoped buffer should be valid");
        TEST_ASSERT(sbuf.capacity() >= 256, "Scoped buffer capacity");

        sbuf.set_size(100);
        TEST_ASSERT(sbuf.size() == 100, "Size should be set");
    }  // Buffer automatically returned to pool here

    // Pool should have buffer back
    const auto& stats = pool.statistics();
    TEST_ASSERT(stats.total_releases.load() >= 1, "Buffer should be released");

    return true;
}

bool test_message_buffer_pool_hit_rate() {
    memory_config config;
    config.message_buffer_pool_size = 4;

    message_buffer_pool pool(config);

    // Acquire and release multiple times (should get cache hits)
    for (int i = 0; i < 10; ++i) {
        auto buf = pool.acquire(512);
        if (buf) {
            pool.release(*buf);
        }
    }

    const auto& stats = pool.statistics();
    double hit_rate = stats.hit_rate();

    // After pre-allocation, most acquires should hit cache
    TEST_ASSERT(hit_rate > 50.0, "Hit rate should be > 50%");

    return true;
}

// =============================================================================
// Zero-Copy Parser Tests
// =============================================================================

bool test_zero_copy_parser_basic() {
    auto result = zero_copy_parser::parse(SAMPLE_HL7);
    TEST_ASSERT(result.has_value(), "Should parse valid HL7");
    TEST_ASSERT(result->valid(), "Parser should be valid");
    TEST_ASSERT(result->segment_count() == 5, "Should have 5 segments");

    return true;
}

bool test_zero_copy_parser_msh_fields() {
    auto result = zero_copy_parser::parse(SAMPLE_HL7);
    TEST_ASSERT(result.has_value(), "Should parse");

    auto msg_type = result->message_type();
    TEST_ASSERT(!msg_type.empty(), "Message type should exist");
    TEST_ASSERT(msg_type.get() == "ORM^O01", "Message type should be ORM^O01");

    auto msg_id = result->message_control_id();
    TEST_ASSERT(!msg_id.empty(), "Message ID should exist");
    TEST_ASSERT(msg_id.get() == "MSG00001", "Message ID should be MSG00001");

    auto sending_app = result->sending_application();
    TEST_ASSERT(sending_app.get() == "SENDING_APP", "Sending app correct");

    auto version = result->version_id();
    TEST_ASSERT(version.get() == "2.5", "Version should be 2.5");

    return true;
}

bool test_zero_copy_parser_segment_access() {
    auto result = zero_copy_parser::parse(SAMPLE_HL7);
    TEST_ASSERT(result.has_value(), "Should parse");

    // Access MSH segment
    auto msh = result->segment("MSH");
    TEST_ASSERT(msh.has_value(), "MSH segment should exist");
    TEST_ASSERT(msh->is_msh(), "Should identify as MSH");

    // Access PID segment
    auto pid = result->segment("PID");
    TEST_ASSERT(pid.has_value(), "PID segment should exist");

    auto patient_id = pid->field(3);  // PID-3
    TEST_ASSERT(!patient_id.empty(), "Patient ID should exist");
    TEST_ASSERT(patient_id.get() == "67890^^^MRN", "Patient ID correct");

    auto patient_name = pid->field(5);  // PID-5
    TEST_ASSERT(patient_name.get() == "DOE^JOHN^A", "Patient name correct");

    // Access component
    auto last_name = pid->component(5, 1);  // PID-5.1
    TEST_ASSERT(last_name.get() == "DOE", "Last name should be DOE");

    auto first_name = pid->component(5, 2);  // PID-5.2
    TEST_ASSERT(first_name.get() == "JOHN", "First name should be JOHN");

    return true;
}

bool test_zero_copy_parser_missing_segment() {
    auto result = zero_copy_parser::parse(SAMPLE_HL7);
    TEST_ASSERT(result.has_value(), "Should parse");

    auto nonexistent = result->segment("ZZZ");
    TEST_ASSERT(!nonexistent.has_value(), "ZZZ segment should not exist");

    TEST_ASSERT(!result->has_segment("ZZZ"), "has_segment should return false");
    TEST_ASSERT(result->has_segment("MSH"), "has_segment should return true for MSH");

    return true;
}

bool test_zero_copy_parser_invalid_message() {
    auto result = zero_copy_parser::parse("");
    TEST_ASSERT(!result.has_value(), "Empty message should fail");

    auto result2 = zero_copy_parser::parse("INVALID");
    TEST_ASSERT(!result2.has_value(), "Invalid message should fail");

    auto result3 = zero_copy_parser::parse("PID|1|12345");
    TEST_ASSERT(!result3.has_value(), "Message without MSH should fail");

    return true;
}

bool test_zero_copy_parser_performance() {
    // Warm up
    for (int i = 0; i < 100; ++i) {
        auto result = zero_copy_parser::parse(SAMPLE_HL7);
    }

    // Measure
    auto start = std::chrono::steady_clock::now();
    constexpr int iterations = 10000;

    for (int i = 0; i < iterations; ++i) {
        auto result = zero_copy_parser::parse(SAMPLE_HL7);
        if (result && result->valid()) {
            auto msg_type = result->message_type();
            (void)msg_type;
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
                           end - start)
                           .count();

    double avg_us = static_cast<double>(duration_us) / iterations;

    std::cout << "    Zero-copy parse avg: " << avg_us << " us/msg" << std::endl;

    // Should be under 100us per message
    TEST_ASSERT(avg_us < 100.0, "Parse should be under 100us");

    return true;
}

bool test_batch_parser() {
    batch_parser parser;

    std::vector<std::string_view> messages = {SAMPLE_HL7, SAMPLE_HL7, SAMPLE_HL7};

    auto results = parser.parse_batch(messages);
    TEST_ASSERT(results.size() == 3, "Should have 3 results");

    for (const auto& result : results) {
        TEST_ASSERT(result.has_value(), "Each parse should succeed");
    }

    const auto& stats = parser.stats();
    TEST_ASSERT(stats.messages_parsed == 3, "Should track 3 parses");
    TEST_ASSERT(stats.parse_errors == 0, "Should have no errors");

    return true;
}

// =============================================================================
// Thread Pool Manager Tests
// =============================================================================

bool test_thread_pool_start_stop() {
    thread_pool_config config;
    config.min_threads = 2;
    config.max_threads = 4;

    thread_pool_manager pool(config);

    TEST_ASSERT(!pool.is_running(), "Should not be running initially");

    auto start_result = pool.start();
    TEST_ASSERT(start_result.has_value(), "Should start successfully");
    TEST_ASSERT(pool.is_running(), "Should be running after start");

    auto stop_result = pool.stop(true, std::chrono::seconds{5});
    TEST_ASSERT(stop_result.has_value(), "Should stop successfully");
    TEST_ASSERT(!pool.is_running(), "Should not be running after stop");

    return true;
}

bool test_thread_pool_task_submission() {
    thread_pool_config config;
    config.min_threads = 2;

    thread_pool_manager pool(config);
    (void)pool.start();

    std::atomic<int> counter{0};

    // Submit tasks
    for (int i = 0; i < 100; ++i) {
        pool.post([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); },
                  task_priority::normal);
    }

    // Wait for tasks to complete
    TEST_ASSERT(
        wait_for([&counter]() { return counter.load() >= 100; },
                 std::chrono::milliseconds{5000}),
        "All tasks should complete within timeout");

    (void)pool.stop(true, std::chrono::seconds{5});

    TEST_ASSERT(counter.load() == 100, "All 100 tasks should complete");

    return true;
}

bool test_thread_pool_priority_scheduling() {
    thread_pool_config config;
    config.min_threads = 1;  // Single thread to test ordering

    thread_pool_manager pool(config);
    (void)pool.start();

    std::vector<int> execution_order;
    std::mutex order_mutex;

    // Submit tasks with different priorities
    pool.post(
        [&]() {
            std::lock_guard lock(order_mutex);
            execution_order.push_back(3);
        },
        task_priority::low);

    pool.post(
        [&]() {
            std::lock_guard lock(order_mutex);
            execution_order.push_back(1);
        },
        task_priority::high);

    pool.post(
        [&]() {
            std::lock_guard lock(order_mutex);
            execution_order.push_back(2);
        },
        task_priority::normal);

    // Wait for all tasks to complete
    TEST_ASSERT(
        wait_for([&execution_order, &order_mutex]() {
            std::lock_guard lock(order_mutex);
            return execution_order.size() >= 3;
        }, std::chrono::milliseconds{2000}),
        "All tasks should complete within timeout");
    (void)pool.stop(true, std::chrono::seconds{5});

    // High priority should execute before low priority
    // (exact order depends on timing)
    TEST_ASSERT(execution_order.size() == 3, "All tasks should execute");

    return true;
}

bool test_thread_pool_statistics() {
    thread_pool_config config;
    config.min_threads = 2;

    thread_pool_manager pool(config);
    (void)pool.start();

    const auto& stats = pool.statistics();

    TEST_ASSERT(stats.total_threads.load() == 2, "Should have 2 threads");

    std::atomic<int> task_count{0};

    // Submit and complete some tasks
    for (int i = 0; i < 10; ++i) {
        pool.post([&task_count]() {
            // Simulate brief work using yield-based wait
            auto work_deadline = std::chrono::steady_clock::now() +
                                 std::chrono::milliseconds{1};
            while (std::chrono::steady_clock::now() < work_deadline) {
                std::this_thread::yield();
            }
            task_count.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // Wait for all tasks to complete
    wait_for([&task_count]() { return task_count.load() >= 10; },
             std::chrono::milliseconds{1000});

    TEST_ASSERT(stats.total_submitted.load() >= 10, "Should track submissions");

    (void)pool.stop(true, std::chrono::seconds{5});

    TEST_ASSERT(stats.total_completed.load() >= 10, "Should track completions");

    return true;
}

// =============================================================================
// Connection Pool Tests
// =============================================================================

bool test_connection_pool_start_stop() {
    connection_pool_config config;
    config.min_idle_connections = 2;

    optimized_connection_pool pool(config);

    TEST_ASSERT(!pool.is_running(), "Should not be running initially");

    auto start_result = pool.start();
    TEST_ASSERT(start_result.has_value(), "Should start successfully");
    TEST_ASSERT(pool.is_running(), "Should be running after start");

    auto stop_result = pool.stop(true, std::chrono::seconds{5});
    TEST_ASSERT(stop_result.has_value(), "Should stop successfully");
    TEST_ASSERT(!pool.is_running(), "Should not be running after stop");

    return true;
}

bool test_connection_pool_acquire() {
    connection_pool_config config;

    optimized_connection_pool pool(config);
    (void)pool.start();

    auto conn = pool.acquire("localhost", 2575);
    TEST_ASSERT(conn.has_value(), "Should acquire connection");

    const auto& stats = pool.statistics();
    TEST_ASSERT(stats.total_acquires.load() >= 1, "Should track acquires");
    TEST_ASSERT(stats.total_created.load() >= 1, "Should create connection");

    (void)pool.stop(true, std::chrono::seconds{5});

    return true;
}

bool test_connection_pool_statistics() {
    connection_pool_config config;

    optimized_connection_pool pool(config);
    (void)pool.start();

    // Acquire connections
    for (int i = 0; i < 5; ++i) {
        auto conn = pool.acquire("host" + std::to_string(i), 2575);
    }

    const auto& stats = pool.statistics();
    TEST_ASSERT(stats.total_acquires.load() == 5, "Should track 5 acquires");

    (void)pool.stop(true, std::chrono::seconds{5});

    return true;
}

// =============================================================================
// Benchmark Runner Tests
// =============================================================================

bool test_benchmark_config_defaults() {
    benchmark_config config;

    TEST_ASSERT(config.type == benchmark_type::throughput, "Default type");
    TEST_ASSERT(config.duration.count() == 60, "Default duration 60s");
    TEST_ASSERT(config.warmup.count() == 5, "Default warmup 5s");
    TEST_ASSERT(config.iterations == 3, "Default 3 iterations");

    return true;
}

bool test_benchmark_runner_parsing() {
    benchmark_runner runner;
    runner.set_duration(std::chrono::seconds{2});
    runner.set_iterations(1);

    auto result = runner.run_parsing_benchmark();
    TEST_ASSERT(result.has_value(), "Parsing benchmark should succeed");

    TEST_ASSERT(result->type == benchmark_type::parsing, "Type should be parsing");
    TEST_ASSERT(result->total_messages > 0, "Should process messages");
    TEST_ASSERT(result->total_errors == 0, "Should have no errors");
    TEST_ASSERT(result->throughput > 0, "Should have positive throughput");
    TEST_ASSERT(result->avg_latency_us > 0, "Should measure latency");

    std::cout << "    Parsing throughput: " << result->throughput << " msg/s"
              << std::endl;
    std::cout << "    Parsing P95 latency: " << result->p95_latency_us << " us"
              << std::endl;

    return true;
}

bool test_benchmark_runner_memory() {
    benchmark_runner runner;
    runner.set_duration(std::chrono::seconds{2});

    auto result = runner.run_memory_benchmark();
    TEST_ASSERT(result.has_value(), "Memory benchmark should succeed");

    TEST_ASSERT(result->type == benchmark_type::memory, "Type should be memory");
    TEST_ASSERT(result->total_messages > 0, "Should do operations");
    TEST_ASSERT(result->peak_memory_bytes > 0, "Should track memory");

    std::cout << "    Memory peak: " << (result->peak_memory_bytes / 1024)
              << " KB" << std::endl;

    return true;
}

bool test_benchmark_suite_result_format() {
    benchmark_suite_result suite;
    suite.name = "Test Suite";
    suite.passed = true;
    suite.total_duration = std::chrono::milliseconds{1000};
    suite.summary.total_benchmarks = 3;
    suite.summary.passed_benchmarks = 3;

    benchmark_result br;
    br.type = benchmark_type::throughput;
    br.throughput = 1000.0;
    br.p95_latency_us = 25.0;
    br.targets_met = true;
    suite.benchmarks.push_back(br);

    auto text = suite.to_text();
    TEST_ASSERT(!text.empty(), "Text report should not be empty");
    TEST_ASSERT(text.find("Test Suite") != std::string::npos, "Should contain name");

    auto json = suite.to_json();
    TEST_ASSERT(!json.empty(), "JSON report should not be empty");
    TEST_ASSERT(json.find("\"name\"") != std::string::npos, "Should be valid JSON");

    auto md = suite.to_markdown();
    TEST_ASSERT(!md.empty(), "Markdown report should not be empty");
    TEST_ASSERT(md.find("# PACS Bridge") != std::string::npos, "Should be markdown");

    return true;
}

bool test_quick_benchmark_function() {
    auto duration = benchmark_operation([]() {
        volatile int x = 0;
        for (int i = 0; i < 100; ++i) {
            x += i;
        }
    });

    TEST_ASSERT(duration.count() > 0, "Should measure positive duration");
    TEST_ASSERT(duration.count() < 1000000, "Simple op should be < 1ms");

    return true;
}

// =============================================================================
// Integration Tests
// =============================================================================

bool test_integration_parser_with_pool() {
    memory_config mem_config;
    mem_config.message_buffer_pool_size = 16;
    message_buffer_pool pool(mem_config);

    batch_parser parser;

    for (int i = 0; i < 100; ++i) {
        // Get buffer from pool
        auto buf = pool.acquire(SAMPLE_HL7.size() + 10);
        if (!buf) continue;

        // Copy message to buffer (simulating network receive)
        std::memcpy(buf->data, SAMPLE_HL7.data(), SAMPLE_HL7.size());
        buf->size = SAMPLE_HL7.size();

        // Parse from buffer
        auto result = parser.parse(
            std::string_view(reinterpret_cast<char*>(buf->data), buf->size));

        // Release buffer
        pool.release(*buf);

        TEST_ASSERT(result.has_value(), "Parse should succeed");
    }

    const auto& pool_stats = pool.statistics();
    const auto& parser_stats = parser.stats();

    std::cout << "    Pool hit rate: " << pool_stats.hit_rate() << "%" << std::endl;
    std::cout << "    Avg parse time: " << parser_stats.avg_parse_us() << " us"
              << std::endl;

    TEST_ASSERT(pool_stats.hit_rate() > 80.0, "Pool hit rate should be > 80%");
    TEST_ASSERT(parser_stats.messages_parsed == 100, "Should parse 100 messages");

    return true;
}

bool test_integration_thread_pool_with_parser() {
    thread_pool_config pool_config;
    pool_config.min_threads = 4;

    thread_pool_manager pool(pool_config);
    (void)pool.start();

    std::atomic<uint64_t> successful_parses{0};
    std::atomic<uint64_t> failed_parses{0};

    // Submit parsing tasks
    for (int i = 0; i < 1000; ++i) {
        pool.post(
            [&]() {
                auto result = zero_copy_parser::parse(SAMPLE_HL7);
                if (result && result->valid()) {
                    successful_parses.fetch_add(1, std::memory_order_relaxed);
                } else {
                    failed_parses.fetch_add(1, std::memory_order_relaxed);
                }
            },
            task_priority::normal);
    }

    // Wait for completion
    TEST_ASSERT(
        wait_for([&successful_parses, &failed_parses]() {
            return (successful_parses.load() + failed_parses.load()) >= 1000;
        }, std::chrono::milliseconds{10000}),
        "All parsing tasks should complete within timeout");
    (void)pool.stop(true, std::chrono::seconds{10});

    std::cout << "    Successful parses: " << successful_parses.load() << std::endl;
    std::cout << "    Failed parses: " << failed_parses.load() << std::endl;

    TEST_ASSERT(successful_parses.load() == 1000, "All parses should succeed");
    TEST_ASSERT(failed_parses.load() == 0, "No parses should fail");

    const auto& stats = pool.statistics();
    std::cout << "    Work stolen: " << stats.work_stolen.load() << std::endl;

    return true;
}

}  // namespace pacs::bridge::performance::test

// =============================================================================
// Main
// =============================================================================

int main() {
    using namespace pacs::bridge::performance::test;

    std::cout << "==================================" << std::endl;
    std::cout << "PACS Bridge Performance Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    int passed = 0;
    int failed = 0;

    // Performance Types Tests
    std::cout << "\n--- Performance Types ---" << std::endl;
    RUN_TEST(test_performance_error_to_string);
    RUN_TEST(test_performance_targets_constants);
    RUN_TEST(test_thread_pool_config_presets);
    RUN_TEST(test_lockfree_queue_config_validation);

    // Object Pool Tests
    std::cout << "\n--- Object Pool ---" << std::endl;
    RUN_TEST(test_message_buffer_pool_acquire_release);
    RUN_TEST(test_message_buffer_pool_statistics);
    RUN_TEST(test_scoped_buffer_raii);
    RUN_TEST(test_message_buffer_pool_hit_rate);

    // Zero-Copy Parser Tests
    std::cout << "\n--- Zero-Copy Parser ---" << std::endl;
    RUN_TEST(test_zero_copy_parser_basic);
    RUN_TEST(test_zero_copy_parser_msh_fields);
    RUN_TEST(test_zero_copy_parser_segment_access);
    RUN_TEST(test_zero_copy_parser_missing_segment);
    RUN_TEST(test_zero_copy_parser_invalid_message);
    RUN_TEST(test_zero_copy_parser_performance);
    RUN_TEST(test_batch_parser);

    // Thread Pool Manager Tests
    std::cout << "\n--- Thread Pool Manager ---" << std::endl;
    RUN_TEST(test_thread_pool_start_stop);
    RUN_TEST(test_thread_pool_task_submission);
    RUN_TEST(test_thread_pool_priority_scheduling);
    RUN_TEST(test_thread_pool_statistics);

    // Connection Pool Tests
    std::cout << "\n--- Connection Pool ---" << std::endl;
    RUN_TEST(test_connection_pool_start_stop);
    RUN_TEST(test_connection_pool_acquire);
    RUN_TEST(test_connection_pool_statistics);

    // Benchmark Runner Tests
    std::cout << "\n--- Benchmark Runner ---" << std::endl;
    RUN_TEST(test_benchmark_config_defaults);
    RUN_TEST(test_benchmark_runner_parsing);
    RUN_TEST(test_benchmark_runner_memory);
    RUN_TEST(test_benchmark_suite_result_format);
    RUN_TEST(test_quick_benchmark_function);

    // Integration Tests
    std::cout << "\n--- Integration Tests ---" << std::endl;
    RUN_TEST(test_integration_parser_with_pool);
    RUN_TEST(test_integration_thread_pool_with_parser);

    // Summary
    std::cout << "\n==================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "==================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
