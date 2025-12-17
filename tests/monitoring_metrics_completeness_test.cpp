/**
 * @file monitoring_metrics_completeness_test.cpp
 * @brief Unit tests for monitoring metrics completeness
 *
 * Tests to ensure all required metrics are properly exported
 * and have correct values.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/145
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/protocol/hl7/hl7_builder.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace pacs::bridge::monitoring::test {
namespace {

using namespace ::testing;
using namespace std::chrono;

// =============================================================================
// Mock Metrics Collector
// =============================================================================

class MockMetricsCollector {
public:
    void increment_counter(const std::string& name, const std::map<std::string, std::string>& labels = {}) {
        ++counters_[make_key(name, labels)];
    }

    void set_gauge(const std::string& name, double value, const std::map<std::string, std::string>& labels = {}) {
        gauges_[make_key(name, labels)] = value;
    }

    void record_histogram(const std::string& name, double value, const std::map<std::string, std::string>& labels = {}) {
        histograms_[make_key(name, labels)].push_back(value);
    }

    int64_t get_counter(const std::string& name, const std::map<std::string, std::string>& labels = {}) const {
        auto key = make_key(name, labels);
        auto it = counters_.find(key);
        return it != counters_.end() ? it->second : 0;
    }

    double get_gauge(const std::string& name, const std::map<std::string, std::string>& labels = {}) const {
        auto key = make_key(name, labels);
        auto it = gauges_.find(key);
        return it != gauges_.end() ? it->second : 0.0;
    }

    std::vector<double> get_histogram(const std::string& name, const std::map<std::string, std::string>& labels = {}) const {
        auto key = make_key(name, labels);
        auto it = histograms_.find(key);
        return it != histograms_.end() ? it->second : std::vector<double>{};
    }

    bool has_metric(const std::string& name) const {
        for (const auto& [key, _] : counters_) {
            if (key.find(name) == 0) return true;
        }
        for (const auto& [key, _] : gauges_) {
            if (key.find(name) == 0) return true;
        }
        for (const auto& [key, _] : histograms_) {
            if (key.find(name) == 0) return true;
        }
        return false;
    }

    void reset() {
        counters_.clear();
        gauges_.clear();
        histograms_.clear();
    }

private:
    static std::string make_key(const std::string& name, const std::map<std::string, std::string>& labels) {
        std::string key = name;
        for (const auto& [k, v] : labels) {
            key += "{" + k + "=" + v + "}";
        }
        return key;
    }

    std::map<std::string, int64_t> counters_;
    std::map<std::string, double> gauges_;
    std::map<std::string, std::vector<double>> histograms_;
};

// =============================================================================
// Message Processor with Metrics
// =============================================================================

class InstrumentedMessageProcessor {
public:
    explicit InstrumentedMessageProcessor(MockMetricsCollector& metrics)
        : metrics_(metrics), parser_() {}

    bool process_message(const std::string& raw_message) {
        auto start = high_resolution_clock::now();
        metrics_.increment_counter("pacs_bridge_messages_received_total");

        auto msg = parser_.parse(raw_message);

        auto elapsed = duration_cast<microseconds>(
            high_resolution_clock::now() - start).count() / 1000.0;

        if (msg.has_value()) {
            std::string msg_type = hl7::to_string(msg->type());
            metrics_.increment_counter("pacs_bridge_messages_processed_total",
                                       {{"message_type", msg_type}});
            metrics_.record_histogram("pacs_bridge_message_processing_duration_ms",
                                      elapsed, {{"message_type", msg_type}});
            return true;
        } else {
            metrics_.increment_counter("pacs_bridge_messages_failed_total",
                                       {{"reason", "parse_error"}});
            return false;
        }
    }

    void update_connection_metrics(int active, int idle) {
        metrics_.set_gauge("pacs_bridge_connections_active", active);
        metrics_.set_gauge("pacs_bridge_connections_idle", idle);
        metrics_.set_gauge("pacs_bridge_connections_total", active + idle);
    }

    void update_queue_metrics(int pending, int processing, int completed) {
        metrics_.set_gauge("pacs_bridge_queue_pending", pending);
        metrics_.set_gauge("pacs_bridge_queue_processing", processing);
        metrics_.set_gauge("pacs_bridge_queue_completed", completed);
    }

    void record_error(const std::string& error_type, const std::string& source) {
        metrics_.increment_counter("pacs_bridge_errors_total",
                                   {{"error_type", error_type}, {"source", source}});
    }

    void update_health_status(bool healthy) {
        metrics_.set_gauge("pacs_bridge_health_status", healthy ? 1.0 : 0.0);
    }

private:
    MockMetricsCollector& metrics_;
    hl7::hl7_parser parser_;
};

// =============================================================================
// Test Fixture
// =============================================================================

class MonitoringMetricsCompletenessTest : public ::testing::Test {
protected:
    void SetUp() override {
        metrics_ = std::make_unique<MockMetricsCollector>();
        processor_ = std::make_unique<InstrumentedMessageProcessor>(*metrics_);
    }

    std::unique_ptr<MockMetricsCollector> metrics_;
    std::unique_ptr<InstrumentedMessageProcessor> processor_;

    std::string create_test_message(const std::string& type, const std::string& trigger) {
        return
            "MSH|^~\\&|HIS|HOSPITAL|PACS|RADIOLOGY|20240115103000||" + type + "^" + trigger + "|MSG001|P|2.4\r"
            "PID|1||12345^^^HOSPITAL^MR||DOE^JOHN||19800515|M\r";
    }
};

// =============================================================================
// Counter Metrics Tests
// =============================================================================

TEST_F(MonitoringMetricsCompletenessTest, MessagesReceivedCounter) {
    processor_->process_message(create_test_message("ADT", "A01"));
    processor_->process_message(create_test_message("ORM", "O01"));
    processor_->process_message(create_test_message("ORU", "R01"));

    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_received_total"), 3);
}

TEST_F(MonitoringMetricsCompletenessTest, MessagesProcessedCounterByType) {
    processor_->process_message(create_test_message("ADT", "A01"));
    processor_->process_message(create_test_message("ADT", "A08"));
    processor_->process_message(create_test_message("ORM", "O01"));

    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_processed_total",
                                    {{"message_type", "ADT"}}), 2);
    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_processed_total",
                                    {{"message_type", "ORM"}}), 1);
}

TEST_F(MonitoringMetricsCompletenessTest, MessagesFailedCounter) {
    processor_->process_message("INVALID MESSAGE");
    processor_->process_message("ANOTHER INVALID");

    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_failed_total",
                                    {{"reason", "parse_error"}}), 2);
}

TEST_F(MonitoringMetricsCompletenessTest, ErrorsCounter) {
    processor_->record_error("connection_timeout", "pacs_primary");
    processor_->record_error("connection_refused", "pacs_secondary");
    processor_->record_error("connection_timeout", "pacs_primary");

    EXPECT_EQ(metrics_->get_counter("pacs_bridge_errors_total",
                                    {{"error_type", "connection_timeout"},
                                     {"source", "pacs_primary"}}), 2);
    EXPECT_EQ(metrics_->get_counter("pacs_bridge_errors_total",
                                    {{"error_type", "connection_refused"},
                                     {"source", "pacs_secondary"}}), 1);
}

// =============================================================================
// Gauge Metrics Tests
// =============================================================================

TEST_F(MonitoringMetricsCompletenessTest, ConnectionGauges) {
    processor_->update_connection_metrics(5, 10);

    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_connections_active"), 5.0);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_connections_idle"), 10.0);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_connections_total"), 15.0);
}

TEST_F(MonitoringMetricsCompletenessTest, QueueGauges) {
    processor_->update_queue_metrics(10, 3, 100);

    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_queue_pending"), 10.0);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_queue_processing"), 3.0);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_queue_completed"), 100.0);
}

TEST_F(MonitoringMetricsCompletenessTest, HealthStatusGauge) {
    processor_->update_health_status(true);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_health_status"), 1.0);

    processor_->update_health_status(false);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_health_status"), 0.0);
}

// =============================================================================
// Histogram Metrics Tests
// =============================================================================

TEST_F(MonitoringMetricsCompletenessTest, ProcessingDurationHistogram) {
    processor_->process_message(create_test_message("ADT", "A01"));
    processor_->process_message(create_test_message("ADT", "A01"));
    processor_->process_message(create_test_message("ADT", "A01"));

    auto durations = metrics_->get_histogram("pacs_bridge_message_processing_duration_ms",
                                             {{"message_type", "ADT"}});

    EXPECT_EQ(durations.size(), 3);
    for (double d : durations) {
        EXPECT_GE(d, 0.0);  // Duration should be non-negative
    }
}

// =============================================================================
// Metric Completeness Tests
// =============================================================================

TEST_F(MonitoringMetricsCompletenessTest, RequiredCountersExist) {
    // Process some messages to trigger metric creation
    processor_->process_message(create_test_message("ADT", "A01"));
    processor_->process_message("INVALID");
    processor_->record_error("test_error", "test_source");

    // Check required counters exist
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_messages_received_total"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_messages_processed_total"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_messages_failed_total"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_errors_total"));
}

TEST_F(MonitoringMetricsCompletenessTest, RequiredGaugesExist) {
    processor_->update_connection_metrics(1, 1);
    processor_->update_queue_metrics(1, 1, 1);
    processor_->update_health_status(true);

    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_connections_active"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_connections_idle"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_connections_total"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_queue_pending"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_queue_processing"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_queue_completed"));
    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_health_status"));
}

TEST_F(MonitoringMetricsCompletenessTest, RequiredHistogramsExist) {
    processor_->process_message(create_test_message("ADT", "A01"));

    EXPECT_TRUE(metrics_->has_metric("pacs_bridge_message_processing_duration_ms"));
}

// =============================================================================
// Label Correctness Tests
// =============================================================================

TEST_F(MonitoringMetricsCompletenessTest, MessageTypeLabels) {
    processor_->process_message(create_test_message("ADT", "A01"));
    processor_->process_message(create_test_message("ORM", "O01"));
    processor_->process_message(create_test_message("ORU", "R01"));
    processor_->process_message(create_test_message("SIU", "S12"));

    // Each message type should have its own metric
    EXPECT_GT(metrics_->get_counter("pacs_bridge_messages_processed_total",
                                    {{"message_type", "ADT"}}), 0);
    EXPECT_GT(metrics_->get_counter("pacs_bridge_messages_processed_total",
                                    {{"message_type", "ORM"}}), 0);
    EXPECT_GT(metrics_->get_counter("pacs_bridge_messages_processed_total",
                                    {{"message_type", "ORU"}}), 0);
    EXPECT_GT(metrics_->get_counter("pacs_bridge_messages_processed_total",
                                    {{"message_type", "SIU"}}), 0);
}

TEST_F(MonitoringMetricsCompletenessTest, ErrorTypeLabels) {
    processor_->record_error("connection_timeout", "pacs_primary");
    processor_->record_error("parse_error", "hl7_parser");
    processor_->record_error("validation_error", "validator");

    EXPECT_GT(metrics_->get_counter("pacs_bridge_errors_total",
                                    {{"error_type", "connection_timeout"},
                                     {"source", "pacs_primary"}}), 0);
    EXPECT_GT(metrics_->get_counter("pacs_bridge_errors_total",
                                    {{"error_type", "parse_error"},
                                     {"source", "hl7_parser"}}), 0);
    EXPECT_GT(metrics_->get_counter("pacs_bridge_errors_total",
                                    {{"error_type", "validation_error"},
                                     {"source", "validator"}}), 0);
}

// =============================================================================
// Metric Value Accuracy Tests
// =============================================================================

TEST_F(MonitoringMetricsCompletenessTest, CounterIncrementsCorrectly) {
    for (int i = 0; i < 100; ++i) {
        processor_->process_message(create_test_message("ADT", "A01"));
    }

    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_received_total"), 100);
    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_processed_total",
                                    {{"message_type", "ADT"}}), 100);
}

TEST_F(MonitoringMetricsCompletenessTest, GaugeUpdatesCorrectly) {
    processor_->update_connection_metrics(1, 0);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_connections_active"), 1.0);

    processor_->update_connection_metrics(5, 3);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_connections_active"), 5.0);

    processor_->update_connection_metrics(2, 8);
    EXPECT_EQ(metrics_->get_gauge("pacs_bridge_connections_active"), 2.0);
}

// =============================================================================
// Metric Reset Tests
// =============================================================================

TEST_F(MonitoringMetricsCompletenessTest, MetricsResetProperly) {
    processor_->process_message(create_test_message("ADT", "A01"));
    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_received_total"), 1);

    metrics_->reset();

    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_received_total"), 0);
}

// =============================================================================
// High Volume Metrics Tests
// =============================================================================

TEST_F(MonitoringMetricsCompletenessTest, HighVolumeMetricAccuracy) {
    const int count = 1000;

    for (int i = 0; i < count; ++i) {
        std::string msg_type = (i % 3 == 0) ? "ADT" : (i % 3 == 1) ? "ORM" : "ORU";
        processor_->process_message(create_test_message(msg_type, "A01"));
    }

    int64_t total_processed =
        metrics_->get_counter("pacs_bridge_messages_processed_total", {{"message_type", "ADT"}}) +
        metrics_->get_counter("pacs_bridge_messages_processed_total", {{"message_type", "ORM"}}) +
        metrics_->get_counter("pacs_bridge_messages_processed_total", {{"message_type", "ORU"}});

    EXPECT_EQ(total_processed, count);
    EXPECT_EQ(metrics_->get_counter("pacs_bridge_messages_received_total"), count);
}

}  // namespace
}  // namespace pacs::bridge::monitoring::test
