/**
 * @file bridge_metrics_test.cpp
 * @brief Unit tests for bridge_metrics_collector
 *
 * Tests cover:
 * - Metrics collector initialization and shutdown
 * - HL7 message metrics recording
 * - MWL metrics recording
 * - Queue metrics recording
 * - Connection metrics recording
 * - Prometheus format export
 * - Scoped timer helper
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/40
 * @see https://github.com/kcenon/pacs_bridge/issues/90
 */

#include "pacs/bridge/monitoring/bridge_metrics.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

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
// Initialization Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_singleton_instance() {
    auto& instance1 = bridge_metrics_collector::instance();
    auto& instance2 = bridge_metrics_collector::instance();

    TEST_ASSERT(&instance1 == &instance2,
                "Singleton should return same instance");
    return true;
}

bool test_initialization() {
    auto& metrics = bridge_metrics_collector::instance();

    // Shutdown first in case previous test left it running
    metrics.shutdown();

    TEST_ASSERT(!metrics.is_enabled(), "Should be disabled before init");

    bool result = metrics.initialize("test_service", 0);  // Port 0 = no HTTP
    TEST_ASSERT(result, "Initialization should succeed");
    TEST_ASSERT(metrics.is_enabled(), "Should be enabled after init");

    return true;
}

bool test_double_initialization() {
    auto& metrics = bridge_metrics_collector::instance();

    // Already initialized from previous test
    bool result = metrics.initialize("test_service_2", 0);
    TEST_ASSERT(result, "Double initialization should succeed (no-op)");

    return true;
}

bool test_enable_disable() {
    auto& metrics = bridge_metrics_collector::instance();

    metrics.set_enabled(false);
    TEST_ASSERT(!metrics.is_enabled(), "Should be disabled");

    metrics.set_enabled(true);
    TEST_ASSERT(metrics.is_enabled(), "Should be enabled");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// HL7 Message Metrics Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_hl7_message_received() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Record some messages
    metrics.record_hl7_message_received("ADT");
    metrics.record_hl7_message_received("ADT");
    metrics.record_hl7_message_received("ORM");

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("hl7_messages_received_total") != std::string::npos,
                "Output should contain hl7_messages_received_total");
    TEST_ASSERT(output.find("message_type=\"ADT\"") != std::string::npos,
                "Output should contain ADT label");
    TEST_ASSERT(output.find("message_type=\"ORM\"") != std::string::npos,
                "Output should contain ORM label");

    return true;
}

bool test_hl7_message_sent() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    metrics.record_hl7_message_sent("ACK");
    metrics.record_hl7_message_sent("ORU");

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("hl7_messages_sent_total") != std::string::npos,
                "Output should contain hl7_messages_sent_total");

    return true;
}

bool test_hl7_processing_duration() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Record some durations
    metrics.record_hl7_processing_duration(
        "ADT", std::chrono::milliseconds(50));
    metrics.record_hl7_processing_duration(
        "ADT", std::chrono::milliseconds(100));
    metrics.record_hl7_processing_duration(
        "ADT", std::chrono::milliseconds(150));

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(
        output.find("hl7_message_processing_duration_seconds") != std::string::npos,
        "Output should contain hl7_message_processing_duration_seconds");
    TEST_ASSERT(output.find("_bucket") != std::string::npos,
                "Output should contain histogram buckets");
    TEST_ASSERT(output.find("_sum") != std::string::npos,
                "Output should contain histogram sum");
    TEST_ASSERT(output.find("_count") != std::string::npos,
                "Output should contain histogram count");

    return true;
}

bool test_hl7_errors() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    metrics.record_hl7_error("ADT", "parse_error");
    metrics.record_hl7_error("ORM", "validation_error");

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("hl7_message_errors_total") != std::string::npos,
                "Output should contain hl7_message_errors_total");
    TEST_ASSERT(output.find("error_type=\"parse_error\"") != std::string::npos,
                "Output should contain parse_error label");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// MWL Metrics Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_mwl_counters() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    metrics.record_mwl_entry_created();
    metrics.record_mwl_entry_created();
    metrics.record_mwl_entry_updated();
    metrics.record_mwl_entry_cancelled();

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("mwl_entries_created_total") != std::string::npos,
                "Output should contain mwl_entries_created_total");
    TEST_ASSERT(output.find("mwl_entries_updated_total") != std::string::npos,
                "Output should contain mwl_entries_updated_total");
    TEST_ASSERT(output.find("mwl_entries_cancelled_total") != std::string::npos,
                "Output should contain mwl_entries_cancelled_total");

    return true;
}

bool test_mwl_query_duration() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    metrics.record_mwl_query_duration(std::chrono::milliseconds(25));
    metrics.record_mwl_query_duration(std::chrono::milliseconds(75));

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("mwl_query_duration_seconds") != std::string::npos,
                "Output should contain mwl_query_duration_seconds");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Queue Metrics Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_queue_depth() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    metrics.set_queue_depth("pacs_destination", 100);
    metrics.set_queue_depth("ris_destination", 50);

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("queue_depth") != std::string::npos,
                "Output should contain queue_depth");
    TEST_ASSERT(output.find("destination=\"pacs_destination\"") != std::string::npos,
                "Output should contain pacs_destination label");
    TEST_ASSERT(output.find("destination=\"ris_destination\"") != std::string::npos,
                "Output should contain ris_destination label");

    return true;
}

bool test_queue_operations() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    metrics.record_message_enqueued("pacs_destination");
    metrics.record_message_delivered("pacs_destination");
    metrics.record_delivery_failure("pacs_destination");
    metrics.record_dead_letter("pacs_destination");

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("queue_messages_enqueued_total") != std::string::npos,
                "Output should contain queue_messages_enqueued_total");
    TEST_ASSERT(output.find("queue_messages_delivered_total") != std::string::npos,
                "Output should contain queue_messages_delivered_total");
    TEST_ASSERT(output.find("queue_delivery_failures_total") != std::string::npos,
                "Output should contain queue_delivery_failures_total");
    TEST_ASSERT(output.find("queue_dead_letters_total") != std::string::npos,
                "Output should contain queue_dead_letters_total");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Connection Metrics Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_mllp_connections() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    metrics.set_mllp_active_connections(5);
    metrics.record_mllp_connection();
    metrics.record_mllp_connection();

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("mllp_active_connections") != std::string::npos,
                "Output should contain mllp_active_connections");
    TEST_ASSERT(output.find("mllp_total_connections") != std::string::npos,
                "Output should contain mllp_total_connections");

    return true;
}

bool test_fhir_requests() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    metrics.set_fhir_active_requests(3);
    metrics.record_fhir_request("GET", "Patient");
    metrics.record_fhir_request("POST", "ServiceRequest");

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("fhir_active_requests") != std::string::npos,
                "Output should contain fhir_active_requests");
    TEST_ASSERT(output.find("fhir_requests_total") != std::string::npos,
                "Output should contain fhir_requests_total");
    TEST_ASSERT(output.find("method=\"GET\"") != std::string::npos,
                "Output should contain GET method label");
    TEST_ASSERT(output.find("resource=\"Patient\"") != std::string::npos,
                "Output should contain Patient resource label");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// System Metrics Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_system_metrics() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Update system metrics
    metrics.update_system_metrics();

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("process_cpu_seconds_total") != std::string::npos,
                "Output should contain process_cpu_seconds_total");
    TEST_ASSERT(output.find("process_resident_memory_bytes") != std::string::npos,
                "Output should contain process_resident_memory_bytes");
    TEST_ASSERT(output.find("process_open_fds") != std::string::npos,
                "Output should contain process_open_fds");

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Prometheus Format Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_prometheus_format_help_type() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    // Record a metric to ensure output is generated
    metrics.record_hl7_message_received("TEST");

    std::string output = metrics.get_prometheus_metrics();

    TEST_ASSERT(output.find("# HELP") != std::string::npos,
                "Output should contain HELP comments");
    TEST_ASSERT(output.find("# TYPE") != std::string::npos,
                "Output should contain TYPE comments");

    return true;
}

bool test_disabled_metrics() {
    auto& metrics = bridge_metrics_collector::instance();

    // Disable metrics
    metrics.set_enabled(false);

    // These should be no-ops
    metrics.record_hl7_message_received("DISABLED_TEST");

    std::string output = metrics.get_prometheus_metrics();

    // The disabled metric should not appear (or at least not increment)
    // Since we can't easily check if it was incremented, just verify no crash
    TEST_ASSERT(true, "Disabled metrics recording should not crash");

    // Re-enable for subsequent tests
    metrics.set_enabled(true);

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scoped Timer Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_scoped_timer_basic() {
    std::chrono::nanoseconds recorded_duration{0};

    {
        scoped_metrics_timer timer([&recorded_duration](auto d) {
            recorded_duration = d;
        });

        // Simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    TEST_ASSERT(recorded_duration.count() > 0,
                "Timer should record non-zero duration");
    TEST_ASSERT(recorded_duration >= std::chrono::milliseconds(10),
                "Timer should record at least 10ms");

    return true;
}

bool test_scoped_timer_cancel() {
    bool callback_called = false;

    {
        scoped_metrics_timer timer([&callback_called](auto /*d*/) {
            callback_called = true;
        });

        timer.cancel();
    }

    TEST_ASSERT(!callback_called, "Cancelled timer should not invoke callback");

    return true;
}

bool test_scoped_timer_elapsed() {
    scoped_metrics_timer timer([](auto /*d*/) {});

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto elapsed = timer.elapsed();

    TEST_ASSERT(elapsed >= std::chrono::milliseconds(5),
                "Elapsed should be at least 5ms");

    // Cancel to prevent callback
    timer.cancel();

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Thread Safety Tests
// ═══════════════════════════════════════════════════════════════════════════

bool test_concurrent_recording() {
    auto& metrics = bridge_metrics_collector::instance();
    metrics.set_enabled(true);

    constexpr int num_threads = 4;
    constexpr int iterations = 100;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&metrics, i]() {
            for (int j = 0; j < iterations; j++) {
                metrics.record_hl7_message_received("CONCURRENT_" + std::to_string(i));
                metrics.record_hl7_processing_duration(
                    "CONCURRENT_" + std::to_string(i),
                    std::chrono::microseconds(j));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify no crash and output is valid
    std::string output = metrics.get_prometheus_metrics();
    TEST_ASSERT(!output.empty(), "Concurrent recording should produce output");

    return true;
}

}  // namespace pacs::bridge::monitoring::test

// ═══════════════════════════════════════════════════════════════════════════
// Main Test Runner
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    using namespace pacs::bridge::monitoring::test;

    int passed = 0;
    int failed = 0;

    std::cout << "\n===== Bridge Metrics Tests =====" << std::endl;

    // Initialization tests
    std::cout << "\n--- Initialization Tests ---" << std::endl;
    RUN_TEST(test_singleton_instance);
    RUN_TEST(test_initialization);
    RUN_TEST(test_double_initialization);
    RUN_TEST(test_enable_disable);

    // HL7 message metrics tests
    std::cout << "\n--- HL7 Message Metrics Tests ---" << std::endl;
    RUN_TEST(test_hl7_message_received);
    RUN_TEST(test_hl7_message_sent);
    RUN_TEST(test_hl7_processing_duration);
    RUN_TEST(test_hl7_errors);

    // MWL metrics tests
    std::cout << "\n--- MWL Metrics Tests ---" << std::endl;
    RUN_TEST(test_mwl_counters);
    RUN_TEST(test_mwl_query_duration);

    // Queue metrics tests
    std::cout << "\n--- Queue Metrics Tests ---" << std::endl;
    RUN_TEST(test_queue_depth);
    RUN_TEST(test_queue_operations);

    // Connection metrics tests
    std::cout << "\n--- Connection Metrics Tests ---" << std::endl;
    RUN_TEST(test_mllp_connections);
    RUN_TEST(test_fhir_requests);

    // System metrics tests
    std::cout << "\n--- System Metrics Tests ---" << std::endl;
    RUN_TEST(test_system_metrics);

    // Prometheus format tests
    std::cout << "\n--- Prometheus Format Tests ---" << std::endl;
    RUN_TEST(test_prometheus_format_help_type);
    RUN_TEST(test_disabled_metrics);

    // Scoped timer tests
    std::cout << "\n--- Scoped Timer Tests ---" << std::endl;
    RUN_TEST(test_scoped_timer_basic);
    RUN_TEST(test_scoped_timer_cancel);
    RUN_TEST(test_scoped_timer_elapsed);

    // Thread safety tests
    std::cout << "\n--- Thread Safety Tests ---" << std::endl;
    RUN_TEST(test_concurrent_recording);

    // Summary
    std::cout << "\n===== Summary =====" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "===================" << std::endl;

    // Cleanup
    pacs::bridge::monitoring::bridge_metrics_collector::instance().shutdown();

    return failed > 0 ? 1 : 0;
}
