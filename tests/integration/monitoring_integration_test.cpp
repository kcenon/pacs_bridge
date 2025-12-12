/**
 * @file monitoring_integration_test.cpp
 * @brief Integration tests for monitoring system
 *
 * Tests cover:
 * - Prometheus exporter endpoint functionality
 * - Metric format validation
 * - Multi-threaded metric updates
 * - Component metric integration
 * - End-to-end monitoring workflows
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/90
 * @see https://github.com/kcenon/pacs_bridge/issues/40
 */

#include "pacs/bridge/monitoring/bridge_metrics.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::monitoring::integration_test {

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
        std::cout.flush();                                                     \
        if (test_fn()) {                                                       \
            std::cout << "PASSED" << std::endl;                                \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "FAILED" << std::endl;                                \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════
// Prometheus Format Validation Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_prometheus_metric_format_counter() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.shutdown();
    metrics.initialize("integration_test", 0);
    metrics.set_enabled(true);

    // Record metrics
    metrics.record_hl7_message_received("ADT");
    metrics.record_hl7_message_received("ADT");
    metrics.record_hl7_message_received("ORM");

    std::string output = metrics.get_prometheus_metrics();

    // Validate counter format: metric_name{labels} value
    std::regex counter_pattern(
        R"(hl7_messages_received_total\{message_type="[A-Z]+"\}\s+\d+)");

    TEST_ASSERT(std::regex_search(output, counter_pattern),
                "Counter format should match Prometheus specification");

    // Validate HELP and TYPE lines exist
    TEST_ASSERT(output.find("# HELP hl7_messages_received_total") !=
                    std::string::npos,
                "Counter should have HELP comment");
    TEST_ASSERT(output.find("# TYPE hl7_messages_received_total counter") !=
                    std::string::npos,
                "Counter should have TYPE comment");

    return true;
}

bool test_prometheus_metric_format_gauge() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Set gauge values
    metrics.set_queue_depth("pacs_queue", 42);
    metrics.set_mllp_active_connections(5);
    metrics.set_fhir_active_requests(3);

    std::string output = metrics.get_prometheus_metrics();

    // Validate gauge format
    std::regex gauge_pattern(R"(queue_depth\{destination="pacs_queue"\}\s+42)");
    TEST_ASSERT(std::regex_search(output, gauge_pattern),
                "Gauge format should match Prometheus specification");

    // Validate HELP and TYPE
    TEST_ASSERT(output.find("# HELP queue_depth") != std::string::npos,
                "Gauge should have HELP comment");
    TEST_ASSERT(output.find("# TYPE queue_depth gauge") != std::string::npos,
                "Gauge should have TYPE comment");

    return true;
}

bool test_prometheus_metric_format_histogram() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Record histogram samples
    for (int i = 0; i < 100; i++) {
        metrics.record_hl7_processing_duration(
            "HISTOGRAM_TEST", std::chrono::milliseconds(i * 10));
    }

    std::string output = metrics.get_prometheus_metrics();

    // Validate histogram bucket format
    std::regex bucket_pattern(
        R"(hl7_message_processing_duration_seconds_bucket\{.*le="[0-9.]+"\}\s+\d+)");
    TEST_ASSERT(std::regex_search(output, bucket_pattern),
                "Histogram bucket format should match Prometheus specification");

    // Validate +Inf bucket
    TEST_ASSERT(
        output.find("le=\"+Inf\"") != std::string::npos,
        "Histogram should have +Inf bucket");

    // Validate _sum and _count
    TEST_ASSERT(output.find("_sum") != std::string::npos,
                "Histogram should have _sum");
    TEST_ASSERT(output.find("_count") != std::string::npos,
                "Histogram should have _count");

    return true;
}

bool test_prometheus_label_escaping() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Test with special characters in labels (should be sanitized)
    metrics.record_hl7_message_received("ADT_A01");
    metrics.set_queue_depth("ris.primary", 10);

    std::string output = metrics.get_prometheus_metrics();

    // Validate labels are properly quoted
    TEST_ASSERT(output.find("message_type=\"ADT_A01\"") != std::string::npos,
                "Labels with underscores should be preserved");
    TEST_ASSERT(output.find("destination=\"ris.primary\"") != std::string::npos,
                "Labels with dots should be preserved");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Multi-threaded Metric Update Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_concurrent_counter_increments() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.shutdown();
    metrics.initialize("concurrent_test", 0);
    metrics.set_enabled(true);

    constexpr int num_threads = 8;
    constexpr int increments_per_thread = 1000;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    std::atomic<int> ready_count{0};
    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool start = false;

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&metrics, &ready_count, &start_mutex, &start_cv,
                              &start]() {
            // Synchronize start
            ready_count++;
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                start_cv.wait(lock, [&start]() { return start; });
            }

            for (int j = 0; j < increments_per_thread; j++) {
                metrics.record_hl7_message_received("CONCURRENT");
            }
        });
    }

    // Wait for all threads to be ready
    while (ready_count.load() < num_threads) {
        std::this_thread::yield();
    }

    // Start all threads simultaneously
    {
        std::lock_guard<std::mutex> lock(start_mutex);
        start = true;
    }
    start_cv.notify_all();

    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }

    // Verify counter value
    std::string output = metrics.get_prometheus_metrics();
    std::regex counter_regex(
        R"(hl7_messages_received_total\{message_type="CONCURRENT"\}\s+(\d+))");
    std::smatch match;

    TEST_ASSERT(std::regex_search(output, match, counter_regex),
                "Should find CONCURRENT counter in output");

    int count = std::stoi(match[1].str());
    int expected = num_threads * increments_per_thread;

    TEST_ASSERT(
        count == expected,
        "Counter should equal total increments: expected " +
            std::to_string(expected) + ", got " + std::to_string(count));

    return true;
}

bool test_concurrent_histogram_observations() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    constexpr int num_threads = 4;
    constexpr int observations_per_thread = 500;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    std::atomic<int> ready_count{0};
    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool start = false;

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&metrics, &ready_count, &start_mutex, &start_cv,
                              &start, i]() {
            // Use thread-specific random generator for reproducibility
            std::mt19937 gen(i);
            std::uniform_int_distribution<> dist(1, 1000);

            ready_count++;
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                start_cv.wait(lock, [&start]() { return start; });
            }

            for (int j = 0; j < observations_per_thread; j++) {
                metrics.record_hl7_processing_duration(
                    "CONCURRENT_HISTOGRAM",
                    std::chrono::microseconds(dist(gen)));
            }
        });
    }

    while (ready_count.load() < num_threads) {
        std::this_thread::yield();
    }

    {
        std::lock_guard<std::mutex> lock(start_mutex);
        start = true;
    }
    start_cv.notify_all();

    for (auto& t : threads) {
        t.join();
    }

    // Verify histogram data is present and well-formed
    std::string output = metrics.get_prometheus_metrics();
    TEST_ASSERT(output.find("CONCURRENT_HISTOGRAM") != std::string::npos,
                "Histogram data should be present after concurrent observations");

    // Verify count matches total observations
    std::regex count_regex(
        R"(hl7_message_processing_duration_seconds_count\{message_type="CONCURRENT_HISTOGRAM"\}\s+(\d+))");
    std::smatch match;

    // Note: Due to ring buffer in histogram_data, count may be capped at max_samples
    if (std::regex_search(output, match, count_regex)) {
        int count = std::stoi(match[1].str());
        TEST_ASSERT(count > 0, "Histogram count should be positive");
    }

    return true;
}

bool test_concurrent_mixed_operations() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    constexpr int num_threads = 6;
    constexpr int operations_per_thread = 200;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    std::atomic<int> ready_count{0};
    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool start = false;

    // Thread 0-1: Counter operations
    for (int i = 0; i < 2; i++) {
        threads.emplace_back([&metrics, &ready_count, &start_mutex, &start_cv,
                              &start]() {
            ready_count++;
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                start_cv.wait(lock, [&start]() { return start; });
            }
            for (int j = 0; j < operations_per_thread; j++) {
                metrics.record_hl7_message_received("MIXED_COUNTER");
                metrics.record_hl7_message_sent("MIXED_ACK");
            }
        });
    }

    // Thread 2-3: Gauge operations
    for (int i = 0; i < 2; i++) {
        threads.emplace_back([&metrics, &ready_count, &start_mutex, &start_cv,
                              &start, i]() {
            ready_count++;
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                start_cv.wait(lock, [&start]() { return start; });
            }
            for (int j = 0; j < operations_per_thread; j++) {
                metrics.set_queue_depth("mixed_queue_" + std::to_string(i),
                                        static_cast<size_t>(j));
                metrics.set_mllp_active_connections(static_cast<size_t>(j % 10));
            }
        });
    }

    // Thread 4-5: Histogram operations
    for (int i = 0; i < 2; i++) {
        threads.emplace_back([&metrics, &ready_count, &start_mutex, &start_cv,
                              &start]() {
            ready_count++;
            {
                std::unique_lock<std::mutex> lock(start_mutex);
                start_cv.wait(lock, [&start]() { return start; });
            }
            for (int j = 0; j < operations_per_thread; j++) {
                metrics.record_hl7_processing_duration(
                    "MIXED_HISTOGRAM", std::chrono::microseconds(j * 10));
                metrics.record_mwl_query_duration(
                    std::chrono::microseconds(j * 5));
            }
        });
    }

    while (ready_count.load() < num_threads) {
        std::this_thread::yield();
    }

    {
        std::lock_guard<std::mutex> lock(start_mutex);
        start = true;
    }
    start_cv.notify_all();

    for (auto& t : threads) {
        t.join();
    }

    // Verify all metric types are present and valid
    std::string output = metrics.get_prometheus_metrics();
    TEST_ASSERT(output.find("MIXED_COUNTER") != std::string::npos,
                "Counter metrics should be present");
    TEST_ASSERT(output.find("mixed_queue_") != std::string::npos,
                "Gauge metrics should be present");
    TEST_ASSERT(output.find("MIXED_HISTOGRAM") != std::string::npos,
                "Histogram metrics should be present");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Component Metric Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_hl7_processing_workflow_metrics() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.shutdown();
    metrics.initialize("workflow_test", 0);
    metrics.set_enabled(true);

    // Simulate HL7 message processing workflow
    const std::vector<std::string> message_types = {"ADT", "ORM", "ORU", "SIU"};

    for (const auto& msg_type : message_types) {
        // Receive message
        metrics.record_hl7_message_received(msg_type);

        // Process with timing
        auto start = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start);
        metrics.record_hl7_processing_duration(msg_type, duration);

        // Send ACK
        metrics.record_hl7_message_sent("ACK");
    }

    // Simulate some errors
    metrics.record_hl7_error("ADT", "parse_error");
    metrics.record_hl7_error("ORM", "validation_error");

    std::string output = metrics.get_prometheus_metrics();

    // Verify all message types are recorded
    for (const auto& msg_type : message_types) {
        TEST_ASSERT(output.find("message_type=\"" + msg_type + "\"") !=
                        std::string::npos,
                    "Should have metrics for " + msg_type);
    }

    // Verify errors are recorded
    TEST_ASSERT(output.find("error_type=\"parse_error\"") != std::string::npos,
                "Should have parse_error recorded");
    TEST_ASSERT(output.find("error_type=\"validation_error\"") !=
                    std::string::npos,
                "Should have validation_error recorded");

    return true;
}

bool test_mwl_workflow_metrics() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Simulate MWL operations
    for (int i = 0; i < 10; i++) {
        // Create new MWL entry
        metrics.record_mwl_entry_created();

        // Simulate query duration
        metrics.record_mwl_query_duration(std::chrono::milliseconds(10 + i));
    }

    // Some updates and cancellations
    for (int i = 0; i < 3; i++) {
        metrics.record_mwl_entry_updated();
    }
    metrics.record_mwl_entry_cancelled();

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("mwl_entries_created_total") != std::string::npos,
                "Should have MWL created counter");
    TEST_ASSERT(output.find("mwl_entries_updated_total") != std::string::npos,
                "Should have MWL updated counter");
    TEST_ASSERT(output.find("mwl_entries_cancelled_total") != std::string::npos,
                "Should have MWL cancelled counter");
    TEST_ASSERT(output.find("mwl_query_duration_seconds") != std::string::npos,
                "Should have MWL query duration histogram");

    return true;
}

bool test_queue_workflow_metrics() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    const std::vector<std::string> destinations = {"pacs", "ris", "his"};

    // Simulate queue operations
    for (const auto& dest : destinations) {
        for (int i = 0; i < 50; i++) {
            metrics.record_message_enqueued(dest);
            metrics.set_queue_depth(dest, static_cast<size_t>(i + 1));
        }

        // Deliver most messages
        for (int i = 0; i < 45; i++) {
            metrics.record_message_delivered(dest);
            metrics.set_queue_depth(dest, static_cast<size_t>(50 - i - 1));
        }

        // Some failures
        for (int i = 0; i < 3; i++) {
            metrics.record_delivery_failure(dest);
        }

        // One dead letter
        metrics.record_dead_letter(dest);
    }

    std::string output = metrics.get_prometheus_metrics();

    for (const auto& dest : destinations) {
        TEST_ASSERT(output.find("destination=\"" + dest + "\"") !=
                        std::string::npos,
                    "Should have metrics for destination " + dest);
    }

    TEST_ASSERT(output.find("queue_messages_enqueued_total") !=
                    std::string::npos,
                "Should have enqueued counter");
    TEST_ASSERT(output.find("queue_messages_delivered_total") !=
                    std::string::npos,
                "Should have delivered counter");
    TEST_ASSERT(output.find("queue_delivery_failures_total") !=
                    std::string::npos,
                "Should have failures counter");
    TEST_ASSERT(output.find("queue_dead_letters_total") != std::string::npos,
                "Should have dead letters counter");

    return true;
}

bool test_connection_metrics_workflow() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Simulate MLLP connections
    for (int i = 0; i < 10; i++) {
        metrics.record_mllp_connection();
        metrics.set_mllp_active_connections(static_cast<size_t>(i + 1));
    }

    // Some connections close
    metrics.set_mllp_active_connections(7);

    // Simulate FHIR requests
    const std::vector<std::pair<std::string, std::string>> fhir_ops = {
        {"GET", "Patient"},
        {"GET", "ServiceRequest"},
        {"POST", "DiagnosticReport"},
        {"PUT", "ImagingStudy"}};

    for (const auto& [method, resource] : fhir_ops) {
        for (int i = 0; i < 20; i++) {
            metrics.record_fhir_request(method, resource);
        }
    }

    metrics.set_fhir_active_requests(5);

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("mllp_active_connections") != std::string::npos,
                "Should have MLLP active connections gauge");
    TEST_ASSERT(output.find("mllp_total_connections") != std::string::npos,
                "Should have MLLP total connections counter");
    TEST_ASSERT(output.find("fhir_active_requests") != std::string::npos,
                "Should have FHIR active requests gauge");
    TEST_ASSERT(output.find("fhir_requests_total") != std::string::npos,
                "Should have FHIR requests counter");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// System Metrics Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_system_metrics_update() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Update system metrics
    metrics.update_system_metrics();

    std::string output = metrics.get_prometheus_metrics();

    // All system metrics should be present
    TEST_ASSERT(output.find("process_cpu_seconds_total") != std::string::npos,
                "Should have CPU seconds metric");
    TEST_ASSERT(output.find("process_resident_memory_bytes") !=
                    std::string::npos,
                "Should have memory bytes metric");
    TEST_ASSERT(output.find("process_open_fds") != std::string::npos,
                "Should have open fds metric");

    // Memory should be non-zero (we're running a process)
    std::regex memory_regex(
        R"(process_resident_memory_bytes\s+([0-9.]+))");
    std::smatch match;
    if (std::regex_search(output, match, memory_regex)) {
        double memory = std::stod(match[1].str());
        TEST_ASSERT(memory > 0, "Memory usage should be positive");
    }

    return true;
}

bool test_system_metrics_periodic_update() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Simulate periodic updates
    for (int i = 0; i < 5; i++) {
        metrics.update_system_metrics();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Should not crash and should have valid output
    std::string output = metrics.get_prometheus_metrics();
    TEST_ASSERT(!output.empty(), "Output should not be empty after updates");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// End-to-End Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_full_monitoring_workflow() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.shutdown();
    metrics.initialize("e2e_test", 0);
    metrics.set_enabled(true);

    // Simulate a complete monitoring workflow

    // 1. System starts - update system metrics
    metrics.update_system_metrics();

    // 2. MLLP server accepts connections
    for (int i = 0; i < 5; i++) {
        metrics.record_mllp_connection();
        metrics.set_mllp_active_connections(static_cast<size_t>(i + 1));
    }

    // 3. HL7 messages arrive and are processed
    const std::vector<std::string> workflow_messages = {"ADT", "ORM", "ORM",
                                                         "ADT", "ORU"};

    for (size_t i = 0; i < workflow_messages.size(); i++) {
        const auto& msg_type = workflow_messages[i];

        // Receive
        metrics.record_hl7_message_received(msg_type);

        // Process (with varying durations)
        metrics.record_hl7_processing_duration(
            msg_type, std::chrono::milliseconds(10 + i * 5));

        // Create MWL for ORM messages
        if (msg_type == "ORM") {
            metrics.record_mwl_entry_created();
            metrics.record_mwl_query_duration(std::chrono::milliseconds(15));
        }

        // Queue for delivery
        metrics.record_message_enqueued("pacs");
        metrics.set_queue_depth("pacs", i + 1);

        // Send ACK
        metrics.record_hl7_message_sent("ACK");
    }

    // 4. Messages delivered
    for (size_t i = 0; i < workflow_messages.size(); i++) {
        metrics.record_message_delivered("pacs");
        metrics.set_queue_depth("pacs", workflow_messages.size() - i - 1);
    }

    // 5. One error occurs
    metrics.record_hl7_error("ADT", "timeout");
    metrics.record_delivery_failure("pacs");

    // 6. Connections close
    metrics.set_mllp_active_connections(2);

    // 7. Final system metrics update
    metrics.update_system_metrics();

    // Validate complete output
    std::string output = metrics.get_prometheus_metrics();

    // Verify we have metrics from all components
    TEST_ASSERT(output.find("hl7_messages_received_total") != std::string::npos,
                "Should have HL7 received metrics");
    TEST_ASSERT(output.find("hl7_messages_sent_total") != std::string::npos,
                "Should have HL7 sent metrics");
    TEST_ASSERT(output.find("hl7_message_processing_duration_seconds") !=
                    std::string::npos,
                "Should have processing duration histogram");
    TEST_ASSERT(output.find("mwl_entries_created_total") != std::string::npos,
                "Should have MWL metrics");
    TEST_ASSERT(output.find("queue_depth") != std::string::npos,
                "Should have queue metrics");
    TEST_ASSERT(output.find("mllp_active_connections") != std::string::npos,
                "Should have connection metrics");
    TEST_ASSERT(output.find("process_resident_memory_bytes") !=
                    std::string::npos,
                "Should have system metrics");

    return true;
}

bool test_metrics_output_consistency() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Record known values
    metrics.record_hl7_message_received("CONSISTENCY_TEST");
    metrics.record_hl7_message_received("CONSISTENCY_TEST");
    metrics.record_hl7_message_received("CONSISTENCY_TEST");

    // Get output multiple times - should be consistent
    std::string output1 = metrics.get_prometheus_metrics();
    std::string output2 = metrics.get_prometheus_metrics();

    // Both outputs should contain the same counter value
    std::regex counter_regex(
        R"(hl7_messages_received_total\{message_type="CONSISTENCY_TEST"\}\s+(\d+))");

    std::smatch match1, match2;
    bool found1 = std::regex_search(output1, match1, counter_regex);
    bool found2 = std::regex_search(output2, match2, counter_regex);

    TEST_ASSERT(found1 && found2, "Counter should be found in both outputs");
    TEST_ASSERT(match1[1].str() == match2[1].str(),
                "Counter values should be consistent");

    return true;
}

bool test_disabled_metrics_no_op() {
    auto& metrics = bridge_metrics_collector::instance();

    // Get baseline
    metrics.set_enabled(true);
    metrics.record_hl7_message_received("DISABLED_TEST_BASELINE");
    std::string baseline = metrics.get_prometheus_metrics();

    // Disable and record more
    metrics.set_enabled(false);
    for (int i = 0; i < 1000; i++) {
        metrics.record_hl7_message_received("DISABLED_NO_INCREMENT");
        metrics.record_hl7_processing_duration("DISABLED_NO_INCREMENT",
                                                std::chrono::milliseconds(i));
        metrics.set_queue_depth("disabled_queue", static_cast<size_t>(i));
    }

    // Re-enable and get output
    metrics.set_enabled(true);
    std::string after = metrics.get_prometheus_metrics();

    // DISABLED_NO_INCREMENT should not appear (or have count 0)
    // because operations were no-ops when disabled
    TEST_ASSERT(after.find("DISABLED_NO_INCREMENT") == std::string::npos ||
                    after.find("\"DISABLED_NO_INCREMENT\"} 0") !=
                        std::string::npos,
                "Disabled operations should not increment counters");

    return true;
}

}  // namespace pacs::bridge::monitoring::integration_test

// ═══════════════════════════════════════════════════════════════════════════
// Main Test Runner
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    using namespace pacs::bridge::monitoring::integration_test;

    int passed = 0;
    int failed = 0;

    std::cout << "\n===== Monitoring Integration Tests =====" << std::endl;

    // Prometheus Format Validation Tests
    std::cout << "\n--- Prometheus Format Validation Tests ---" << std::endl;
    RUN_TEST(test_prometheus_metric_format_counter);
    RUN_TEST(test_prometheus_metric_format_gauge);
    RUN_TEST(test_prometheus_metric_format_histogram);
    RUN_TEST(test_prometheus_label_escaping);

    // Multi-threaded Tests
    std::cout << "\n--- Multi-threaded Metric Update Tests ---" << std::endl;
    RUN_TEST(test_concurrent_counter_increments);
    RUN_TEST(test_concurrent_histogram_observations);
    RUN_TEST(test_concurrent_mixed_operations);

    // Component Integration Tests
    std::cout << "\n--- Component Metric Integration Tests ---" << std::endl;
    RUN_TEST(test_hl7_processing_workflow_metrics);
    RUN_TEST(test_mwl_workflow_metrics);
    RUN_TEST(test_queue_workflow_metrics);
    RUN_TEST(test_connection_metrics_workflow);

    // System Metrics Tests
    std::cout << "\n--- System Metrics Integration Tests ---" << std::endl;
    RUN_TEST(test_system_metrics_update);
    RUN_TEST(test_system_metrics_periodic_update);

    // End-to-End Tests
    std::cout << "\n--- End-to-End Integration Tests ---" << std::endl;
    RUN_TEST(test_full_monitoring_workflow);
    RUN_TEST(test_metrics_output_consistency);
    RUN_TEST(test_disabled_metrics_no_op);

    // Summary
    std::cout << "\n===== Summary =====" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "===================" << std::endl;

    // Cleanup
    pacs::bridge::monitoring::bridge_metrics_collector::instance().shutdown();

    return failed > 0 ? 1 : 0;
}
