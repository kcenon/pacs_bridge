/**
 * @file mpps_hl7_workflow_test.cpp
 * @brief Unit tests for MPPS to HL7 workflow coordinator
 *
 * Tests for workflow configuration, destination selection,
 * routing rules, and end-to-end processing.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/173
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/workflow/mpps_hl7_workflow.h"

#include "utils/test_helpers.h"

#include <chrono>
#include <thread>

namespace pacs::bridge::workflow {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;
using namespace pacs::bridge::pacs_adapter;

// =============================================================================
// Test Fixtures
// =============================================================================

class WorkflowConfigTest : public pacs_bridge_test {};
class WorkflowBuilderTest : public pacs_bridge_test {};
class DestinationRuleTest : public pacs_bridge_test {};
class WorkflowTest : public pacs_bridge_test {};
class WorkflowStatisticsTest : public pacs_bridge_test {};

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Create a sample MPPS dataset for testing
 */
mpps_dataset create_sample_mpps(mpps_event status = mpps_event::completed) {
    mpps_dataset mpps;
    mpps.sop_instance_uid = "1.2.840.10008.5.1.4.1.1.77.1.4.1.123456";
    mpps.study_instance_uid = "1.2.840.10008.5.1.4.1.1.77.1.4.1.789012";
    mpps.accession_number = "ACC001";
    mpps.scheduled_procedure_step_id = "SPS001";
    mpps.performed_procedure_step_id = "PPS001";
    mpps.patient_id = "PAT001";
    mpps.patient_name = "DOE^JOHN";
    mpps.status = status;
    mpps.performed_procedure_description = "Chest X-Ray";
    mpps.start_date = "20240115";
    mpps.start_time = "103000";
    mpps.end_date = "20240115";
    mpps.end_time = "104500";
    mpps.modality = "CT";
    mpps.station_ae_title = "CT_SCANNER_01";
    mpps.station_name = "CT Room 1";
    mpps.referring_physician = "SMITH^ROBERT";
    mpps.requested_procedure_id = "REQ001";

    mpps_performed_series series;
    series.series_instance_uid = "1.2.840.10008.5.1.4.1.1.77.1.4.1.345678";
    series.series_description = "Axial CT";
    series.modality = "CT";
    series.number_of_instances = 150;
    mpps.performed_series.push_back(series);

    return mpps;
}

// =============================================================================
// Configuration Tests
// =============================================================================

TEST_F(WorkflowConfigTest, DefaultValues) {
    mpps_hl7_workflow_config config;

    EXPECT_TRUE(config.enable_queue_fallback);
    EXPECT_EQ(config.fallback_queue_priority, 0);
    EXPECT_TRUE(config.generate_correlation_id);
    EXPECT_TRUE(config.enable_tracing);
    EXPECT_TRUE(config.enable_metrics);
    EXPECT_TRUE(config.routing_rules.empty());
    EXPECT_TRUE(config.default_destination.empty());
    EXPECT_EQ(config.processing_timeout, std::chrono::milliseconds{30000});
    EXPECT_FALSE(config.async_delivery);
    EXPECT_EQ(config.async_workers, 4u);
}

TEST_F(WorkflowConfigTest, ValidationEmpty) {
    mpps_hl7_workflow_config config;
    EXPECT_FALSE(config.is_valid());
}

TEST_F(WorkflowConfigTest, ValidationWithDefaultDestination) {
    mpps_hl7_workflow_config config;
    config.default_destination = "HIS_PRIMARY";
    EXPECT_TRUE(config.is_valid());
}

TEST_F(WorkflowConfigTest, ValidationWithRoutingRules) {
    mpps_hl7_workflow_config config;
    destination_rule rule;
    rule.name = "CT_ROUTE";
    rule.destination = "CT_HIS";
    config.routing_rules.push_back(rule);
    EXPECT_TRUE(config.is_valid());
}

// =============================================================================
// Builder Tests
// =============================================================================

TEST_F(WorkflowBuilderTest, BasicBuild) {
    auto config = workflow_config_builder::create()
                      .default_destination("HIS_PRIMARY")
                      .build();

    EXPECT_EQ(config.default_destination, "HIS_PRIMARY");
    EXPECT_TRUE(config.is_valid());
}

TEST_F(WorkflowBuilderTest, FullConfiguration) {
    auto config = workflow_config_builder::create()
                      .default_destination("HIS_PRIMARY")
                      .enable_queue_fallback(true)
                      .fallback_priority(-10)
                      .generate_correlation_id(true)
                      .enable_tracing(true)
                      .enable_metrics(true)
                      .processing_timeout(std::chrono::milliseconds{60000})
                      .async_delivery(true, 8)
                      .build();

    EXPECT_EQ(config.default_destination, "HIS_PRIMARY");
    EXPECT_TRUE(config.enable_queue_fallback);
    EXPECT_EQ(config.fallback_queue_priority, -10);
    EXPECT_TRUE(config.generate_correlation_id);
    EXPECT_TRUE(config.enable_tracing);
    EXPECT_TRUE(config.enable_metrics);
    EXPECT_EQ(config.processing_timeout, std::chrono::milliseconds{60000});
    EXPECT_TRUE(config.async_delivery);
    EXPECT_EQ(config.async_workers, 8u);
}

TEST_F(WorkflowBuilderTest, AddRoutingRules) {
    destination_rule ct_rule;
    ct_rule.name = "CT_ROUTE";
    ct_rule.criteria = destination_criteria::by_modality;
    ct_rule.pattern = "CT";
    ct_rule.destination = "CT_HIS";
    ct_rule.priority = 1;

    destination_rule mr_rule;
    mr_rule.name = "MR_ROUTE";
    mr_rule.criteria = destination_criteria::by_modality;
    mr_rule.pattern = "MR";
    mr_rule.destination = "MR_HIS";
    mr_rule.priority = 2;

    auto config = workflow_config_builder::create()
                      .default_destination("GENERAL_HIS")
                      .add_rule(ct_rule)
                      .add_rule(mr_rule)
                      .build();

    EXPECT_EQ(config.routing_rules.size(), 2u);
    EXPECT_EQ(config.routing_rules[0].name, "CT_ROUTE");
    EXPECT_EQ(config.routing_rules[1].name, "MR_ROUTE");
}

// =============================================================================
// Destination Rule Tests
// =============================================================================

TEST_F(DestinationRuleTest, DefaultValues) {
    destination_rule rule;

    EXPECT_TRUE(rule.name.empty());
    EXPECT_EQ(rule.criteria, destination_criteria::by_message_type);
    EXPECT_TRUE(rule.pattern.empty());
    EXPECT_TRUE(rule.destination.empty());
    EXPECT_EQ(rule.priority, 100);
    EXPECT_TRUE(rule.enabled);
}

TEST_F(DestinationRuleTest, CriteriaToString) {
    EXPECT_STREQ(to_string(destination_criteria::by_message_type), "by_message_type");
    EXPECT_STREQ(to_string(destination_criteria::by_modality), "by_modality");
    EXPECT_STREQ(to_string(destination_criteria::by_station), "by_station");
    EXPECT_STREQ(to_string(destination_criteria::by_accession_pattern), "by_accession_pattern");
    EXPECT_STREQ(to_string(destination_criteria::custom), "custom");
}

// =============================================================================
// Workflow Tests
// =============================================================================

TEST_F(WorkflowTest, DefaultConstruction) {
    mpps_hl7_workflow workflow;
    EXPECT_FALSE(workflow.is_running());
}

TEST_F(WorkflowTest, StartWithInvalidConfig) {
    mpps_hl7_workflow workflow;  // Default config has no destination
    auto result = workflow.start();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), workflow_error::invalid_configuration);
}

TEST_F(WorkflowTest, StartAndStop) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .enable_queue_fallback(false)  // Disable queue for testing
                      .build();

    mpps_hl7_workflow workflow(config);

    auto result = workflow.start();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(workflow.is_running());

    workflow.stop();
    EXPECT_FALSE(workflow.is_running());
}

TEST_F(WorkflowTest, DoubleStart) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .build();

    mpps_hl7_workflow workflow(config);
    (void)workflow.start();  // First start should succeed

    auto result = workflow.start();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), workflow_error::already_running);

    workflow.stop();
}

TEST_F(WorkflowTest, ProcessWithoutRunning) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .build();

    mpps_hl7_workflow workflow(config);
    auto mpps = create_sample_mpps();

    auto result = workflow.process(mpps_event::completed, mpps);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), workflow_error::not_running);
}

TEST_F(WorkflowTest, DestinationSelectionByModality) {
    destination_rule ct_rule;
    ct_rule.name = "CT_ROUTE";
    ct_rule.criteria = destination_criteria::by_modality;
    ct_rule.pattern = "CT";
    ct_rule.destination = "CT_HIS";
    ct_rule.priority = 1;

    destination_rule mr_rule;
    mr_rule.name = "MR_ROUTE";
    mr_rule.criteria = destination_criteria::by_modality;
    mr_rule.pattern = "MR";
    mr_rule.destination = "MR_HIS";
    mr_rule.priority = 2;

    auto config = workflow_config_builder::create()
                      .default_destination("GENERAL_HIS")
                      .add_rule(ct_rule)
                      .add_rule(mr_rule)
                      .build();

    mpps_hl7_workflow workflow(config);

    // Test CT modality
    auto ct_mpps = create_sample_mpps();
    ct_mpps.modality = "CT";
    auto dest = workflow.select_destination(ct_mpps);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(*dest, "CT_HIS");

    // Test MR modality
    auto mr_mpps = create_sample_mpps();
    mr_mpps.modality = "MR";
    dest = workflow.select_destination(mr_mpps);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(*dest, "MR_HIS");

    // Test unknown modality (should fall back to default)
    auto unknown_mpps = create_sample_mpps();
    unknown_mpps.modality = "DX";
    dest = workflow.select_destination(unknown_mpps);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(*dest, "GENERAL_HIS");
}

TEST_F(WorkflowTest, DestinationSelectionByStation) {
    destination_rule station_rule;
    station_rule.name = "STATION_ROUTE";
    station_rule.criteria = destination_criteria::by_station;
    station_rule.pattern = "CT_SCANNER_*";
    station_rule.destination = "CT_WORKSTATION_HIS";
    station_rule.priority = 1;

    auto config = workflow_config_builder::create()
                      .default_destination("GENERAL_HIS")
                      .add_rule(station_rule)
                      .build();

    mpps_hl7_workflow workflow(config);

    // Test matching station
    auto mpps = create_sample_mpps();
    mpps.station_ae_title = "CT_SCANNER_01";
    auto dest = workflow.select_destination(mpps);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(*dest, "CT_WORKSTATION_HIS");
}

TEST_F(WorkflowTest, DestinationSelectionPriorityOrder) {
    // Rule 1: Lower priority number = higher priority
    destination_rule high_priority;
    high_priority.name = "HIGH_PRIORITY";
    high_priority.criteria = destination_criteria::by_modality;
    high_priority.pattern = "CT";
    high_priority.destination = "HIGH_DEST";
    high_priority.priority = 1;

    // Rule 2: Higher priority number = lower priority
    destination_rule low_priority;
    low_priority.name = "LOW_PRIORITY";
    low_priority.criteria = destination_criteria::by_modality;
    low_priority.pattern = "CT";
    low_priority.destination = "LOW_DEST";
    low_priority.priority = 100;

    // Add in reverse order to test sorting
    auto config = workflow_config_builder::create()
                      .default_destination("GENERAL_HIS")
                      .add_rule(low_priority)
                      .add_rule(high_priority)
                      .build();

    mpps_hl7_workflow workflow(config);
    (void)workflow.start();  // Ignore return value for this test

    auto mpps = create_sample_mpps();
    mpps.modality = "CT";
    auto dest = workflow.select_destination(mpps);
    ASSERT_TRUE(dest.has_value());
    EXPECT_EQ(*dest, "HIGH_DEST");  // Should match higher priority rule

    workflow.stop();
}

TEST_F(WorkflowTest, RoutingRuleManagement) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .build();

    mpps_hl7_workflow workflow(config);

    // Initially no rules
    EXPECT_TRUE(workflow.routing_rules().empty());

    // Add rules
    destination_rule rule1;
    rule1.name = "RULE_1";
    rule1.destination = "DEST_1";
    workflow.add_routing_rule(rule1);

    destination_rule rule2;
    rule2.name = "RULE_2";
    rule2.destination = "DEST_2";
    workflow.add_routing_rule(rule2);

    EXPECT_EQ(workflow.routing_rules().size(), 2u);

    // Remove rule
    bool removed = workflow.remove_routing_rule("RULE_1");
    EXPECT_TRUE(removed);
    EXPECT_EQ(workflow.routing_rules().size(), 1u);

    // Try to remove non-existent rule
    removed = workflow.remove_routing_rule("NON_EXISTENT");
    EXPECT_FALSE(removed);
}

TEST_F(WorkflowTest, CorrelationIdGeneration) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .generate_correlation_id(true)
                      .build();

    mpps_hl7_workflow workflow(config);

    std::string id1 = workflow.generate_correlation_id();
    std::string id2 = workflow.generate_correlation_id();

    // IDs should be unique
    EXPECT_NE(id1, id2);

    // IDs should have UUID-like format (36 chars including dashes)
    EXPECT_EQ(id1.length(), 36u);
    EXPECT_EQ(id2.length(), 36u);
}

TEST_F(WorkflowTest, TraceIdGeneration) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .enable_tracing(true)
                      .build();

    mpps_hl7_workflow workflow(config);

    std::string trace_id = workflow.generate_trace_id();

    // Trace ID should be 32 hex characters (OpenTelemetry format)
    EXPECT_EQ(trace_id.length(), 32u);

    // All characters should be hex
    for (char c : trace_id) {
        EXPECT_TRUE(std::isxdigit(c));
    }
}

// =============================================================================
// Statistics Tests
// =============================================================================

TEST_F(WorkflowStatisticsTest, InitialStatistics) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .build();

    mpps_hl7_workflow workflow(config);
    auto stats = workflow.get_statistics();

    EXPECT_EQ(stats.total_events, 0u);
    EXPECT_EQ(stats.successful_events, 0u);
    EXPECT_EQ(stats.failed_events, 0u);
    EXPECT_EQ(stats.direct_deliveries, 0u);
    EXPECT_EQ(stats.queued_deliveries, 0u);
    EXPECT_EQ(stats.mapping_failures, 0u);
    EXPECT_EQ(stats.delivery_failures, 0u);
    EXPECT_EQ(stats.enqueue_failures, 0u);
    EXPECT_EQ(stats.in_progress_events, 0u);
    EXPECT_EQ(stats.completed_events, 0u);
    EXPECT_EQ(stats.discontinued_events, 0u);
}

TEST_F(WorkflowStatisticsTest, ResetStatistics) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .build();

    mpps_hl7_workflow workflow(config);

    // Manually modify stats would require running workflow
    // Just test reset functionality
    workflow.reset_statistics();

    auto stats = workflow.get_statistics();
    EXPECT_EQ(stats.total_events, 0u);
}

// =============================================================================
// Error Code Tests
// =============================================================================

TEST_F(WorkflowTest, ErrorCodeConversion) {
    EXPECT_EQ(to_error_code(workflow_error::not_running), -900);
    EXPECT_EQ(to_error_code(workflow_error::already_running), -901);
    EXPECT_EQ(to_error_code(workflow_error::mapping_failed), -902);
    EXPECT_EQ(to_error_code(workflow_error::delivery_failed), -903);
    EXPECT_EQ(to_error_code(workflow_error::enqueue_failed), -904);
    EXPECT_EQ(to_error_code(workflow_error::no_destination), -905);
    EXPECT_EQ(to_error_code(workflow_error::invalid_configuration), -906);
}

TEST_F(WorkflowTest, ErrorCodeToString) {
    EXPECT_STREQ(to_string(workflow_error::not_running), "Workflow is not running");
    EXPECT_STREQ(to_string(workflow_error::already_running), "Workflow is already running");
    EXPECT_STREQ(to_string(workflow_error::mapping_failed), "MPPS to HL7 mapping failed");
    EXPECT_STREQ(to_string(workflow_error::delivery_failed), "Outbound delivery failed");
    EXPECT_STREQ(to_string(workflow_error::no_destination),
                 "No destination configured for message type");
    EXPECT_STREQ(to_string(workflow_error::invalid_configuration),
                 "Invalid workflow configuration");
}

// =============================================================================
// Delivery Method Tests
// =============================================================================

TEST_F(WorkflowTest, DeliveryMethodToString) {
    EXPECT_STREQ(to_string(delivery_method::direct), "direct");
    EXPECT_STREQ(to_string(delivery_method::queued), "queued");
    EXPECT_STREQ(to_string(delivery_method::async), "async");
}

// =============================================================================
// Workflow Result Tests
// =============================================================================

TEST_F(WorkflowTest, WorkflowResultOk) {
    auto result = workflow_result::ok("corr-123", "HIS_PRIMARY", delivery_method::direct);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.correlation_id, "corr-123");
    EXPECT_EQ(result.destination, "HIS_PRIMARY");
    EXPECT_EQ(result.method, delivery_method::direct);
    EXPECT_TRUE(result.error_message.empty());
}

TEST_F(WorkflowTest, WorkflowResultError) {
    auto result = workflow_result::error("corr-456", "Delivery timeout");

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.correlation_id, "corr-456");
    EXPECT_EQ(result.error_message, "Delivery timeout");
}

// =============================================================================
// Completion Callback Tests
// =============================================================================

TEST_F(WorkflowTest, CompletionCallback) {
    auto config = workflow_config_builder::create()
                      .default_destination("TEST_HIS")
                      .build();

    mpps_hl7_workflow workflow(config);

    bool callback_invoked = false;
    workflow.set_completion_callback([&](const workflow_result& /* result */) {
        callback_invoked = true;
        // Callback is invoked even on failure (no router configured)
    });

    (void)workflow.start();  // Ignore return value for this test

    auto mpps = create_sample_mpps();
    // This will fail because no router is configured, but callback should still be called
    auto result = workflow.process(mpps_event::completed, mpps);

    // Callback should have been invoked regardless of success/failure
    // Note: In this case it fails because no outbound_router is set
    EXPECT_TRUE(callback_invoked);

    workflow.clear_completion_callback();
    workflow.stop();
}

}  // namespace
}  // namespace pacs::bridge::workflow
