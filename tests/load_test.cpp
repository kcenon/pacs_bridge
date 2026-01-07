/**
 * @file load_test.cpp
 * @brief Unit tests for load and stress testing functionality
 *
 * Tests cover:
 * - Load types and configurations
 * - Message distribution validation
 * - HL7 message generation
 * - Latency histogram recording
 * - Test metrics tracking
 * - Report generation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/45
 */

#include "pacs/bridge/testing/load_generator.h"
#include "pacs/bridge/testing/load_reporter.h"
#include "pacs/bridge/testing/load_runner.h"
#include "pacs/bridge/testing/load_types.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

namespace pacs::bridge::testing::test {

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

// =============================================================================
// Error Code Tests
// =============================================================================

bool test_load_error_codes() {
    TEST_ASSERT(to_error_code(load_error::invalid_configuration) == -960,
                "invalid_configuration should be -960");
    TEST_ASSERT(to_error_code(load_error::not_initialized) == -961,
                "not_initialized should be -961");
    TEST_ASSERT(to_error_code(load_error::already_running) == -962,
                "already_running should be -962");
    TEST_ASSERT(to_error_code(load_error::cancelled) == -963,
                "cancelled should be -963");
    TEST_ASSERT(to_error_code(load_error::connection_failed) == -964,
                "connection_failed should be -964");
    TEST_ASSERT(to_error_code(load_error::generation_failed) == -965,
                "generation_failed should be -965");
    TEST_ASSERT(to_error_code(load_error::timeout) == -966,
                "timeout should be -966");
    TEST_ASSERT(to_error_code(load_error::resource_exhausted) == -967,
                "resource_exhausted should be -967");
    TEST_ASSERT(to_error_code(load_error::target_error) == -968,
                "target_error should be -968");
    TEST_ASSERT(to_error_code(load_error::report_failed) == -969,
                "report_failed should be -969");
    return true;
}

bool test_load_error_to_string() {
    TEST_ASSERT(std::string(to_string(load_error::invalid_configuration)) ==
                "Invalid test configuration",
                "invalid_configuration string");
    TEST_ASSERT(std::string(to_string(load_error::connection_failed)) ==
                "Connection to target failed",
                "connection_failed string");
    return true;
}

// =============================================================================
// Test Type Tests
// =============================================================================

bool test_test_type_to_string() {
    TEST_ASSERT(std::string(to_string(test_type::sustained)) == "sustained",
                "sustained should be 'sustained'");
    TEST_ASSERT(std::string(to_string(test_type::peak)) == "peak",
                "peak should be 'peak'");
    TEST_ASSERT(std::string(to_string(test_type::endurance)) == "endurance",
                "endurance should be 'endurance'");
    TEST_ASSERT(std::string(to_string(test_type::concurrent)) == "concurrent",
                "concurrent should be 'concurrent'");
    TEST_ASSERT(std::string(to_string(test_type::queue_stress)) == "queue_stress",
                "queue_stress should be 'queue_stress'");
    TEST_ASSERT(std::string(to_string(test_type::failover)) == "failover",
                "failover should be 'failover'");
    return true;
}

bool test_test_type_parsing() {
    auto sustained = parse_test_type("sustained");
    TEST_ASSERT(sustained.has_value() && *sustained == test_type::sustained,
                "should parse sustained");

    auto peak = parse_test_type("peak");
    TEST_ASSERT(peak.has_value() && *peak == test_type::peak,
                "should parse peak");

    auto invalid = parse_test_type("invalid");
    TEST_ASSERT(!invalid.has_value(), "invalid should return nullopt");

    return true;
}

bool test_test_state_to_string() {
    TEST_ASSERT(std::string(to_string(test_state::idle)) == "idle",
                "idle should be 'idle'");
    TEST_ASSERT(std::string(to_string(test_state::running)) == "running",
                "running should be 'running'");
    TEST_ASSERT(std::string(to_string(test_state::completed)) == "completed",
                "completed should be 'completed'");
    TEST_ASSERT(std::string(to_string(test_state::failed)) == "failed",
                "failed should be 'failed'");
    return true;
}

// =============================================================================
// HL7 Message Type Tests
// =============================================================================

bool test_hl7_message_type_to_string() {
    TEST_ASSERT(std::string(to_string(hl7_message_type::ORM)) == "ORM",
                "ORM should be 'ORM'");
    TEST_ASSERT(std::string(to_string(hl7_message_type::ADT)) == "ADT",
                "ADT should be 'ADT'");
    TEST_ASSERT(std::string(to_string(hl7_message_type::SIU)) == "SIU",
                "SIU should be 'SIU'");
    TEST_ASSERT(std::string(to_string(hl7_message_type::ORU)) == "ORU",
                "ORU should be 'ORU'");
    TEST_ASSERT(std::string(to_string(hl7_message_type::MDM)) == "MDM",
                "MDM should be 'MDM'");
    return true;
}

// =============================================================================
// Message Distribution Tests
// =============================================================================

bool test_message_distribution_valid() {
    message_distribution dist{70, 20, 10, 0, 0};
    TEST_ASSERT(dist.is_valid(), "70+20+10=100 should be valid");

    message_distribution default_dist = message_distribution::default_mix();
    TEST_ASSERT(default_dist.is_valid(), "default mix should be valid");
    TEST_ASSERT(default_dist.orm_percent == 70, "default ORM should be 70%");
    TEST_ASSERT(default_dist.adt_percent == 20, "default ADT should be 20%");
    TEST_ASSERT(default_dist.siu_percent == 10, "default SIU should be 10%");

    return true;
}

bool test_message_distribution_invalid() {
    message_distribution dist{50, 20, 10, 0, 0};  // 80% != 100%
    TEST_ASSERT(!dist.is_valid(), "80% total should be invalid");

    message_distribution over{60, 30, 20, 0, 0};  // 110% > 100%
    TEST_ASSERT(!over.is_valid(), "110% total should be invalid");

    return true;
}

// =============================================================================
// Load Configuration Tests
// =============================================================================

bool test_load_config_validation() {
    load_config valid;
    valid.target_host = "localhost";
    valid.target_port = 2575;
    valid.messages_per_second = 500;
    valid.concurrent_connections = 10;
    TEST_ASSERT(valid.is_valid(), "complete config should be valid");

    load_config no_host;
    no_host.target_host = "";  // Explicitly set empty (default is "localhost")
    no_host.target_port = 2575;
    no_host.messages_per_second = 500;
    no_host.concurrent_connections = 10;
    TEST_ASSERT(!no_host.is_valid(), "empty host should be invalid");

    load_config no_port;
    no_port.target_host = "localhost";
    no_port.target_port = 0;
    no_port.messages_per_second = 500;
    no_port.concurrent_connections = 10;
    TEST_ASSERT(!no_port.is_valid(), "zero port should be invalid");

    return true;
}

bool test_load_config_sustained_factory() {
    auto config = load_config::sustained("ris.local", 2576,
                                         std::chrono::hours(1), 500);

    TEST_ASSERT(config.type == test_type::sustained,
                "type should be sustained");
    TEST_ASSERT(config.target_host == "ris.local",
                "host should be ris.local");
    TEST_ASSERT(config.target_port == 2576, "port should be 2576");
    TEST_ASSERT(config.duration == std::chrono::hours(1),
                "duration should be 1 hour");
    TEST_ASSERT(config.messages_per_second == 500, "rate should be 500");
    TEST_ASSERT(config.is_valid(), "factory config should be valid");

    return true;
}

bool test_load_config_peak_factory() {
    auto config = load_config::peak("localhost", 2575, 1000);

    TEST_ASSERT(config.type == test_type::peak, "type should be peak");
    TEST_ASSERT(config.messages_per_second == 1000, "max rate should be 1000");
    TEST_ASSERT(config.duration == std::chrono::seconds(900),
                "duration should be 15 minutes");
    TEST_ASSERT(config.is_valid(), "peak config should be valid");

    return true;
}

bool test_load_config_endurance_factory() {
    auto config = load_config::endurance("localhost", 2575);

    TEST_ASSERT(config.type == test_type::endurance,
                "type should be endurance");
    TEST_ASSERT(config.duration == std::chrono::seconds(86400),
                "duration should be 24 hours");
    TEST_ASSERT(config.messages_per_second == 200, "rate should be 200");
    TEST_ASSERT(!config.detailed_timing,
                "detailed timing should be false for long test");

    return true;
}

bool test_load_config_concurrent_factory() {
    auto config = load_config::concurrent("localhost", 2575, 100, 100);

    TEST_ASSERT(config.type == test_type::concurrent,
                "type should be concurrent");
    TEST_ASSERT(config.concurrent_connections == 100,
                "connections should be 100");
    TEST_ASSERT(config.is_valid(), "concurrent config should be valid");

    return true;
}

// =============================================================================
// Latency Histogram Tests
// =============================================================================

bool test_latency_histogram_record() {
    latency_histogram hist;

    // Record some samples
    hist.record(500);    // 0.5ms
    hist.record(2000);   // 2ms
    hist.record(5000);   // 5ms
    hist.record(10000);  // 10ms
    hist.record(50000);  // 50ms

    TEST_ASSERT(hist.count.load() == 5, "should have 5 samples");
    TEST_ASSERT(hist.min_latency.load() == 500, "min should be 500us");
    TEST_ASSERT(hist.max_latency.load() == 50000, "max should be 50000us");

    return true;
}

bool test_latency_histogram_mean() {
    latency_histogram hist;

    hist.record(1000);
    hist.record(2000);
    hist.record(3000);

    double mean = hist.mean_us();
    TEST_ASSERT(mean == 2000.0, "mean should be 2000us");

    return true;
}

bool test_latency_histogram_percentile() {
    latency_histogram hist;

    // Record many samples in first bucket (0-1ms)
    for (int i = 0; i < 90; ++i) {
        hist.record(500);  // 0.5ms
    }

    // Record some samples in second bucket (1-5ms)
    for (int i = 0; i < 10; ++i) {
        hist.record(3000);  // 3ms
    }

    // P50 should be in first bucket
    auto p50 = hist.percentile_us(50);
    TEST_ASSERT(p50 <= 1000, "P50 should be <= 1ms");

    // P95 should be in second bucket
    auto p95 = hist.percentile_us(95);
    TEST_ASSERT(p95 <= 5000, "P95 should be <= 5ms");

    return true;
}

bool test_latency_histogram_reset() {
    latency_histogram hist;

    hist.record(1000);
    hist.record(2000);

    hist.reset();

    TEST_ASSERT(hist.count.load() == 0, "count should be 0 after reset");
    TEST_ASSERT(hist.min_latency.load() == UINT64_MAX,
                "min should be reset");
    TEST_ASSERT(hist.max_latency.load() == 0, "max should be reset");

    return true;
}

// =============================================================================
// Test Metrics Tests
// =============================================================================

bool test_metrics_success_rate() {
    test_metrics metrics;
    metrics.start_time = std::chrono::steady_clock::now();

    // total_messages() = messages_sent + messages_failed = 95 + 5 = 100
    // success_rate() = messages_acked / total_messages() = 95 / 100 = 95%
    metrics.messages_sent.store(95);   // Successfully transmitted messages
    metrics.messages_acked.store(95);  // Messages that received ACK
    metrics.messages_failed.store(5);  // Messages that failed to send

    auto success_rate = metrics.success_rate();
    TEST_ASSERT(success_rate >= 94.9 && success_rate <= 95.1,
                "success rate should be ~95%");

    return true;
}

bool test_metrics_total_messages() {
    test_metrics metrics;
    metrics.start_time = std::chrono::steady_clock::now();

    metrics.messages_sent.store(90);
    metrics.messages_failed.store(10);

    TEST_ASSERT(metrics.total_messages() == 100,
                "total should be sent + failed");

    return true;
}

bool test_metrics_reset() {
    test_metrics metrics;

    metrics.messages_sent.store(100);
    metrics.messages_acked.store(100);
    metrics.bytes_sent.store(10000);

    metrics.reset();

    TEST_ASSERT(metrics.messages_sent.load() == 0,
                "messages_sent should be 0");
    TEST_ASSERT(metrics.bytes_sent.load() == 0, "bytes_sent should be 0");

    return true;
}

// =============================================================================
// Test Result Tests
// =============================================================================

bool test_result_passed() {
    test_result result;
    result.state = test_state::completed;
    result.success_rate_percent = 100.0;
    result.latency_p95_ms = 25.0;

    TEST_ASSERT(result.passed(), "100% success, 25ms P95 should pass");
    TEST_ASSERT(result.passed(99.0, 50.0),
                "should pass with relaxed criteria");

    return true;
}

bool test_result_failed_success_rate() {
    test_result result;
    result.state = test_state::completed;
    result.success_rate_percent = 98.0;  // Below 100%
    result.latency_p95_ms = 25.0;

    TEST_ASSERT(!result.passed(), "98% success should not pass default");
    TEST_ASSERT(result.passed(95.0, 50.0),
                "98% should pass with 95% threshold");

    return true;
}

bool test_result_failed_latency() {
    test_result result;
    result.state = test_state::completed;
    result.success_rate_percent = 100.0;
    result.latency_p95_ms = 75.0;  // Above 50ms

    TEST_ASSERT(!result.passed(), "75ms P95 should not pass default");
    TEST_ASSERT(result.passed(99.0, 100.0),
                "75ms should pass with 100ms threshold");

    return true;
}

bool test_result_failed_state() {
    test_result result;
    result.state = test_state::failed;  // Not completed
    result.success_rate_percent = 100.0;
    result.latency_p95_ms = 25.0;

    TEST_ASSERT(!result.passed(), "failed state should not pass");

    return true;
}

bool test_result_summary() {
    test_result result;
    result.type = test_type::sustained;
    result.state = test_state::completed;
    result.duration = std::chrono::seconds(3600);
    result.target_host = "localhost";
    result.target_port = 2575;
    result.messages_sent = 1800000;
    result.messages_acked = 1800000;
    result.messages_failed = 0;
    result.success_rate_percent = 100.0;
    result.throughput = 500.0;
    result.latency_p50_ms = 5.0;
    result.latency_p95_ms = 25.0;
    result.latency_p99_ms = 45.0;

    std::string summary = result.summary();

    TEST_ASSERT(summary.find("sustained") != std::string::npos,
                "summary should contain test type");
    TEST_ASSERT(summary.find("completed") != std::string::npos,
                "summary should contain state");
    TEST_ASSERT(summary.find("1800000") != std::string::npos,
                "summary should contain message count");
    TEST_ASSERT(summary.find("100.00%") != std::string::npos,
                "summary should contain success rate");

    return true;
}

// =============================================================================
// Load Generator Tests
// =============================================================================

bool test_generator_generate_orm() {
    load_generator generator;

    auto result = generator.generate_orm();
    TEST_ASSERT(result.has_value(), "ORM generation should succeed");

    auto& msg = *result;
    TEST_ASSERT(msg.find("MSH|^~\\&|") != std::string::npos,
                "should have MSH segment");
    TEST_ASSERT(msg.find("ORM^O01") != std::string::npos,
                "should have ORM^O01 message type");
    TEST_ASSERT(msg.find("PID|") != std::string::npos,
                "should have PID segment");
    TEST_ASSERT(msg.find("ORC|") != std::string::npos,
                "should have ORC segment");
    TEST_ASSERT(msg.find("OBR|") != std::string::npos,
                "should have OBR segment");

    return true;
}

bool test_generator_generate_adt() {
    load_generator generator;

    auto result = generator.generate_adt();
    TEST_ASSERT(result.has_value(), "ADT generation should succeed");

    auto& msg = *result;
    TEST_ASSERT(msg.find("ADT^A01") != std::string::npos,
                "should have ADT^A01 message type");
    TEST_ASSERT(msg.find("EVN|") != std::string::npos,
                "should have EVN segment");
    TEST_ASSERT(msg.find("PV1|") != std::string::npos,
                "should have PV1 segment");

    return true;
}

bool test_generator_generate_siu() {
    load_generator generator;

    auto result = generator.generate_siu();
    TEST_ASSERT(result.has_value(), "SIU generation should succeed");

    auto& msg = *result;
    TEST_ASSERT(msg.find("SIU^S12") != std::string::npos,
                "should have SIU^S12 message type");
    TEST_ASSERT(msg.find("SCH|") != std::string::npos,
                "should have SCH segment");

    return true;
}

bool test_generator_generate_oru() {
    load_generator generator;

    auto result = generator.generate_oru();
    TEST_ASSERT(result.has_value(), "ORU generation should succeed");

    auto& msg = *result;
    TEST_ASSERT(msg.find("ORU^R01") != std::string::npos,
                "should have ORU^R01 message type");
    TEST_ASSERT(msg.find("OBX|") != std::string::npos,
                "should have OBX segment");

    return true;
}

bool test_generator_generate_mdm() {
    load_generator generator;

    auto result = generator.generate_mdm();
    TEST_ASSERT(result.has_value(), "MDM generation should succeed");

    auto& msg = *result;
    TEST_ASSERT(msg.find("MDM^T02") != std::string::npos,
                "should have MDM^T02 message type");
    TEST_ASSERT(msg.find("TXA|") != std::string::npos,
                "should have TXA segment");

    return true;
}

bool test_generator_generate_random() {
    load_generator generator;
    message_distribution dist{70, 20, 10, 0, 0};

    for (int i = 0; i < 100; ++i) {
        auto result = generator.generate_random(dist);
        TEST_ASSERT(result.has_value(), "random generation should succeed");
        TEST_ASSERT(result->find("MSH|") != std::string::npos,
                    "all messages should have MSH");
    }

    return true;
}

bool test_generator_message_counter() {
    load_generator generator;

    TEST_ASSERT(generator.messages_generated() == 0,
                "initial count should be 0");

    (void)generator.generate_orm();
    (void)generator.generate_adt();
    (void)generator.generate_siu();

    TEST_ASSERT(generator.messages_generated() == 3,
                "should have generated 3 messages");

    generator.reset();
    TEST_ASSERT(generator.messages_generated() == 0,
                "count should be 0 after reset");

    return true;
}

bool test_generator_type_counter() {
    load_generator generator;

    (void)generator.generate_orm();
    (void)generator.generate_orm();
    (void)generator.generate_adt();

    TEST_ASSERT(generator.messages_generated(hl7_message_type::ORM) == 2,
                "ORM count should be 2");
    TEST_ASSERT(generator.messages_generated(hl7_message_type::ADT) == 1,
                "ADT count should be 1");
    TEST_ASSERT(generator.messages_generated(hl7_message_type::SIU) == 0,
                "SIU count should be 0");

    return true;
}

bool test_generator_unique_message_ids() {
    load_generator generator;

    std::string id1 = generator.generate_message_id();
    std::string id2 = generator.generate_message_id();
    std::string id3 = generator.generate_message_id();

    TEST_ASSERT(id1 != id2, "message IDs should be unique");
    TEST_ASSERT(id2 != id3, "message IDs should be unique");
    TEST_ASSERT(id1 != id3, "message IDs should be unique");

    return true;
}

bool test_generator_timestamp_format() {
    std::string timestamp = load_generator::current_timestamp();

    // Should be YYYYMMDDHHMMSS format (14 characters)
    TEST_ASSERT(timestamp.length() == 14, "timestamp should be 14 chars");

    // All characters should be digits
    for (char c : timestamp) {
        TEST_ASSERT(c >= '0' && c <= '9', "timestamp should be all digits");
    }

    return true;
}

bool test_generator_builder() {
    auto generator = load_generator_builder()
        .sending_application("TEST_APP")
        .sending_facility("TEST_FAC")
        .receiving_application("RIS")
        .receiving_facility("HOSPITAL")
        .seed(12345)
        .build();

    auto result = generator.generate_orm();
    TEST_ASSERT(result.has_value(), "generation should succeed");
    TEST_ASSERT(result->find("TEST_APP") != std::string::npos,
                "should contain custom sending application");
    TEST_ASSERT(result->find("TEST_FAC") != std::string::npos,
                "should contain custom sending facility");

    return true;
}

// =============================================================================
// Load Runner Tests
// =============================================================================

bool test_runner_initial_state() {
    load_runner runner;

    TEST_ASSERT(!runner.is_running(), "should not be running initially");
    TEST_ASSERT(runner.state() == test_state::idle,
                "should be idle initially");
    TEST_ASSERT(!runner.last_result().has_value(),
                "should have no result initially");

    return true;
}

bool test_runner_invalid_config() {
    load_runner runner;

    load_config invalid;  // Empty config
    auto result = runner.run(invalid);

    TEST_ASSERT(!result.has_value(), "should fail with invalid config");
    TEST_ASSERT(result.error() == load_error::invalid_configuration,
                "error should be invalid_configuration");

    return true;
}

// =============================================================================
// Report Format Tests
// =============================================================================

bool test_report_format_to_string() {
    TEST_ASSERT(std::string(to_string(report_format::text)) == "text",
                "text format string");
    TEST_ASSERT(std::string(to_string(report_format::json)) == "json",
                "json format string");
    TEST_ASSERT(std::string(to_string(report_format::markdown)) == "markdown",
                "markdown format string");
    TEST_ASSERT(std::string(to_string(report_format::csv)) == "csv",
                "csv format string");
    TEST_ASSERT(std::string(to_string(report_format::html)) == "html",
                "html format string");

    return true;
}

bool test_report_format_extension() {
    TEST_ASSERT(std::string(extension_for(report_format::text)) == ".txt",
                "text extension");
    TEST_ASSERT(std::string(extension_for(report_format::json)) == ".json",
                "json extension");
    TEST_ASSERT(std::string(extension_for(report_format::markdown)) == ".md",
                "markdown extension");
    TEST_ASSERT(std::string(extension_for(report_format::csv)) == ".csv",
                "csv extension");
    TEST_ASSERT(std::string(extension_for(report_format::html)) == ".html",
                "html extension");

    return true;
}

// =============================================================================
// Load Reporter Tests
// =============================================================================

bool test_reporter_generate_text() {
    load_reporter reporter;

    test_result result;
    result.type = test_type::sustained;
    result.state = test_state::completed;
    result.duration = std::chrono::seconds(60);
    result.target_host = "localhost";
    result.target_port = 2575;
    result.messages_sent = 30000;
    result.messages_acked = 30000;
    result.messages_failed = 0;
    result.success_rate_percent = 100.0;
    result.throughput = 500.0;
    result.latency_p50_ms = 5.0;
    result.latency_p95_ms = 25.0;
    result.latency_p99_ms = 45.0;
    result.latency_min_ms = 1.0;
    result.latency_max_ms = 100.0;
    result.latency_mean_ms = 10.0;
    result.bytes_sent = 15000000;
    result.bytes_received = 1500000;

    auto report = reporter.generate(result, report_format::text);

    TEST_ASSERT(report.has_value(), "should generate text report");
    TEST_ASSERT(report->find("sustained") != std::string::npos,
                "should contain test type");

    return true;
}

bool test_reporter_generate_json() {
    load_reporter reporter;

    test_result result;
    result.type = test_type::peak;
    result.state = test_state::completed;
    result.duration = std::chrono::seconds(900);
    result.target_host = "ris.local";
    result.target_port = 2576;
    result.messages_sent = 450000;
    result.messages_acked = 449500;
    result.messages_failed = 500;
    result.success_rate_percent = 99.89;
    result.throughput = 500.0;
    result.latency_p95_ms = 35.0;

    auto report = reporter.to_json(result);

    TEST_ASSERT(report.has_value(), "should generate JSON report");
    TEST_ASSERT(report->find("\"type\": \"peak\"") != std::string::npos,
                "JSON should contain type");
    TEST_ASSERT(report->find("\"target\"") != std::string::npos,
                "JSON should contain target");
    TEST_ASSERT(report->find("\"messages\"") != std::string::npos,
                "JSON should contain messages");
    TEST_ASSERT(report->find("\"latency_ms\"") != std::string::npos,
                "JSON should contain latency");

    return true;
}

bool test_reporter_generate_markdown() {
    load_reporter reporter;

    test_result result;
    result.type = test_type::endurance;
    result.state = test_state::completed;
    result.duration = std::chrono::seconds(86400);
    result.target_host = "localhost";
    result.target_port = 2575;
    result.messages_sent = 17280000;
    result.messages_acked = 17280000;
    result.success_rate_percent = 100.0;
    result.throughput = 200.0;
    result.latency_p95_ms = 15.0;

    auto report = reporter.generate(result, report_format::markdown);

    TEST_ASSERT(report.has_value(), "should generate markdown report");
    TEST_ASSERT(report->find("# ") != std::string::npos,
                "markdown should have heading");
    TEST_ASSERT(report->find("| ") != std::string::npos,
                "markdown should have table");
    TEST_ASSERT(report->find("endurance") != std::string::npos,
                "markdown should contain test type");

    return true;
}

bool test_reporter_generate_csv() {
    load_reporter reporter;

    test_result result;
    result.type = test_type::concurrent;
    result.state = test_state::completed;
    result.duration = std::chrono::seconds(120);
    result.messages_sent = 10000;
    result.messages_acked = 10000;
    result.success_rate_percent = 100.0;
    result.throughput = 83.3;
    result.latency_p95_ms = 20.0;

    auto report = reporter.to_csv(result);

    TEST_ASSERT(report.has_value(), "should generate CSV report");

    // Check header
    TEST_ASSERT(report->find("type,state,duration") != std::string::npos,
                "CSV should have header");

    // Check data row
    TEST_ASSERT(report->find("concurrent,completed") != std::string::npos,
                "CSV should have data row");

    return true;
}

bool test_reporter_config_builder() {
    auto config = report_config_builder()
        .format(report_format::html)
        .title("Custom Test Report")
        .include_timing_details(true)
        .include_resource_usage(true)
        .notes("Test run notes")
        .build();

    TEST_ASSERT(config.format == report_format::html, "format should be html");
    TEST_ASSERT(config.title == "Custom Test Report", "title should match");
    TEST_ASSERT(config.include_timing_details, "timing details should be true");
    TEST_ASSERT(config.notes == "Test run notes", "notes should match");

    return true;
}

}  // namespace pacs::bridge::testing::test

// =============================================================================
// Test Runner
// =============================================================================

int main() {
    using namespace pacs::bridge::testing::test;

    int passed = 0;
    int failed = 0;

    std::cout << "═══════════════════════════════════════════════════════════"
              << std::endl;
    std::cout << "PACS Bridge Load Testing Unit Tests" << std::endl;
    std::cout << "═══════════════════════════════════════════════════════════"
              << std::endl;

    // Error Code Tests
    std::cout << "\n--- Error Code Tests ---" << std::endl;
    RUN_TEST(test_load_error_codes);
    RUN_TEST(test_load_error_to_string);

    // Test Type Tests
    std::cout << "\n--- Test Type Tests ---" << std::endl;
    RUN_TEST(test_test_type_to_string);
    RUN_TEST(test_test_type_parsing);
    RUN_TEST(test_test_state_to_string);

    // HL7 Message Type Tests
    std::cout << "\n--- HL7 Message Type Tests ---" << std::endl;
    RUN_TEST(test_hl7_message_type_to_string);

    // Message Distribution Tests
    std::cout << "\n--- Message Distribution Tests ---" << std::endl;
    RUN_TEST(test_message_distribution_valid);
    RUN_TEST(test_message_distribution_invalid);

    // Load Configuration Tests
    std::cout << "\n--- Load Configuration Tests ---" << std::endl;
    RUN_TEST(test_load_config_validation);
    RUN_TEST(test_load_config_sustained_factory);
    RUN_TEST(test_load_config_peak_factory);
    RUN_TEST(test_load_config_endurance_factory);
    RUN_TEST(test_load_config_concurrent_factory);

    // Latency Histogram Tests
    std::cout << "\n--- Latency Histogram Tests ---" << std::endl;
    RUN_TEST(test_latency_histogram_record);
    RUN_TEST(test_latency_histogram_mean);
    RUN_TEST(test_latency_histogram_percentile);
    RUN_TEST(test_latency_histogram_reset);

    // Test Metrics Tests
    std::cout << "\n--- Test Metrics Tests ---" << std::endl;
    RUN_TEST(test_metrics_success_rate);
    RUN_TEST(test_metrics_total_messages);
    RUN_TEST(test_metrics_reset);

    // Test Result Tests
    std::cout << "\n--- Test Result Tests ---" << std::endl;
    RUN_TEST(test_result_passed);
    RUN_TEST(test_result_failed_success_rate);
    RUN_TEST(test_result_failed_latency);
    RUN_TEST(test_result_failed_state);
    RUN_TEST(test_result_summary);

    // Load Generator Tests
    std::cout << "\n--- Load Generator Tests ---" << std::endl;
    RUN_TEST(test_generator_generate_orm);
    RUN_TEST(test_generator_generate_adt);
    RUN_TEST(test_generator_generate_siu);
    RUN_TEST(test_generator_generate_oru);
    RUN_TEST(test_generator_generate_mdm);
    RUN_TEST(test_generator_generate_random);
    RUN_TEST(test_generator_message_counter);
    RUN_TEST(test_generator_type_counter);
    RUN_TEST(test_generator_unique_message_ids);
    RUN_TEST(test_generator_timestamp_format);
    RUN_TEST(test_generator_builder);

    // Load Runner Tests
    std::cout << "\n--- Load Runner Tests ---" << std::endl;
    RUN_TEST(test_runner_initial_state);
    RUN_TEST(test_runner_invalid_config);

    // Report Format Tests
    std::cout << "\n--- Report Format Tests ---" << std::endl;
    RUN_TEST(test_report_format_to_string);
    RUN_TEST(test_report_format_extension);

    // Load Reporter Tests
    std::cout << "\n--- Load Reporter Tests ---" << std::endl;
    RUN_TEST(test_reporter_generate_text);
    RUN_TEST(test_reporter_generate_json);
    RUN_TEST(test_reporter_generate_markdown);
    RUN_TEST(test_reporter_generate_csv);
    RUN_TEST(test_reporter_config_builder);

    // Summary
    std::cout << "\n═══════════════════════════════════════════════════════════"
              << std::endl;
    std::cout << "Test Results: " << passed << " passed, " << failed << " failed"
              << std::endl;
    std::cout << "═══════════════════════════════════════════════════════════"
              << std::endl;

    return failed == 0 ? 0 : 1;
}
