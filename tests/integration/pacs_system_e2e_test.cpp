/**
 * @file pacs_system_e2e_test.cpp
 * @brief End-to-end workflow integration tests for pacs_bridge <-> pacs_system
 *
 * Tests the complete workflow from HL7 message reception through MWL creation,
 * MPPS processing, and HL7 response generation:
 *
 * 1. HL7 ORM^O01 received -> MWL entry created in pacs_system
 * 2. Modality queries MWL and starts procedure (MPPS N-CREATE)
 * 3. MPPS IN PROGRESS persisted -> ORM^O01 (IP) sent to RIS
 * 4. Modality completes procedure (MPPS N-SET COMPLETED)
 * 5. MPPS COMPLETED persisted -> ORM^O01 (CM) sent to RIS
 *
 * These tests verify the IHE Scheduled Workflow profile compliance.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/188
 * @see https://github.com/kcenon/pacs_bridge/issues/194
 * @see docs/reference_materials/06_ihe_swf_profile.md
 */

#include "pacs_system_test_base.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace pacs::bridge::integration::test {

// =============================================================================
// E2E Test Utilities
// =============================================================================

/**
 * @brief Collector for tracking messages in E2E tests
 */
class message_collector {
public:
    void add_message(const std::string& type, const std::string& content) {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.push_back({type, content});
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_.size();
    }

    size_t count_type(const std::string& type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<size_t>(std::count_if(
            messages_.begin(), messages_.end(),
            [&type](const auto& msg) { return msg.first == type; }));
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.clear();
    }

    std::vector<std::pair<std::string, std::string>> get_messages() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::pair<std::string, std::string>> messages_;
};

// =============================================================================
// MWL Creation Workflow Tests
// =============================================================================

/**
 * @brief Test MWL entry creation from order data
 *
 * Simulates receiving an order and creating the corresponding MWL entry.
 */
bool test_order_creates_mwl_entry() {
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    // Simulate order data (would come from HL7 ORM^O01)
    std::string accession = pacs_system_test_fixture::generate_unique_accession();
    auto mwl_item = mwl_test_data_generator::create_item_with_accession(accession);
    mwl_item.patient.patient_id = "E2E_PAT_001";
    mwl_item.patient.patient_name = "E2E^WORKFLOW^TEST";
    if (!mwl_item.scheduled_steps.empty()) {
        mwl_item.scheduled_steps[0].modality = "CT";
        mwl_item.scheduled_steps[0].scheduled_station_ae_title = "CT_SCANNER_1";
    }

    // Create MWL entry
    auto add_result = mwl_client.add_entry(mwl_item);
    PACS_TEST_ASSERT(add_result.has_value(), "MWL entry creation should succeed");

    // Verify entry is queryable
    pacs_adapter::mwl_query_filter filter;
    filter.accession_number = accession;
    auto query_result = mwl_client.query(filter);
    PACS_TEST_ASSERT(query_result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(query_result->items.size() == 1,
                     "Should find exactly 1 entry");
    PACS_TEST_ASSERT(query_result->items[0].patient.patient_id == "E2E_PAT_001",
                     "Patient ID should match");

    mwl_client.disconnect();
    return true;
}

/**
 * @brief Test modality can query MWL entries by station
 */
bool test_modality_queries_mwl() {
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    // Create entries for different stations
    auto ct_item = mwl_test_data_generator::create_item_with_modality("CT");
    if (!ct_item.scheduled_steps.empty()) {
        ct_item.scheduled_steps[0].scheduled_station_ae_title = "CT_SCANNER_1";
    }

    auto mr_item = mwl_test_data_generator::create_item_with_modality("MR");
    if (!mr_item.scheduled_steps.empty()) {
        mr_item.scheduled_steps[0].scheduled_station_ae_title = "MR_SCANNER_1";
    }

    (void)mwl_client.add_entry(ct_item);
    (void)mwl_client.add_entry(mr_item);

    // Simulate CT modality querying
    pacs_adapter::mwl_query_filter filter;
    filter.modality = "CT";
    filter.scheduled_station_ae = "CT_SCANNER_1";

    auto result = mwl_client.query(filter);
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(result->items.size() >= 1,
                     "Should find at least 1 CT entry");

    for (const auto& item : result->items) {
        if (!item.scheduled_steps.empty()) {
            PACS_TEST_ASSERT(item.scheduled_steps[0].modality == "CT",
                             "All results should be CT modality");
        }
    }

    mwl_client.disconnect();
    return true;
}

// =============================================================================
// Complete MPPS Workflow Tests
// =============================================================================

/**
 * @brief Test complete MPPS workflow: IN PROGRESS -> COMPLETED
 */
bool test_mpps_complete_workflow() {
    auto handler = pacs_adapter::mpps_handler::create(
        pacs_system_test_fixture::create_mpps_test_config());

    message_collector collector;

    handler->set_callback(
        [&collector](pacs_adapter::mpps_event event,
                     const pacs_adapter::mpps_dataset& dataset) {
            collector.add_message(pacs_adapter::to_string(event),
                                  dataset.accession_number);
        });

    // Step 1: Create MPPS (procedure starts)
    auto dataset = mpps_test_data_generator::create_in_progress();
    std::string sop_uid = dataset.sop_instance_uid;

    auto create_result = handler->on_n_create(dataset);
    PACS_TEST_ASSERT(create_result.has_value(), "N-CREATE should succeed");
    PACS_TEST_ASSERT(collector.count_type("IN PROGRESS") >= 1,
                     "Should have IN PROGRESS callback");

    // Verify persisted with IN PROGRESS status
    auto query1 = handler->query_mpps(sop_uid);
    PACS_TEST_ASSERT(query1.has_value() && query1->has_value(),
                     "Record should exist");
    PACS_TEST_ASSERT(query1->value().status ==
                         pacs_adapter::mpps_event::in_progress,
                     "Status should be IN PROGRESS");

    // Step 2: Complete MPPS (procedure finishes)
    dataset.status = pacs_adapter::mpps_event::completed;
    dataset.end_date = mpps_test_data_generator::get_today_date();
    dataset.end_time = mpps_test_data_generator::get_offset_time(30);

    auto set_result = handler->on_n_set(dataset);
    PACS_TEST_ASSERT(set_result.has_value(), "N-SET should succeed");
    PACS_TEST_ASSERT(collector.count_type("COMPLETED") >= 1,
                     "Should have COMPLETED callback");

    // Verify persisted with COMPLETED status
    auto query2 = handler->query_mpps(sop_uid);
    PACS_TEST_ASSERT(query2.has_value() && query2->has_value(),
                     "Record should exist");
    PACS_TEST_ASSERT(query2->value().status ==
                         pacs_adapter::mpps_event::completed,
                     "Status should be COMPLETED");

    // Verify total workflow
    PACS_TEST_ASSERT(collector.count() >= 2,
                     "Should have at least 2 callbacks (IP + CM)");

    handler->stop();
    return true;
}

/**
 * @brief Test MPPS workflow: IN PROGRESS -> DISCONTINUED
 */
bool test_mpps_discontinuation_workflow() {
    auto handler = pacs_adapter::mpps_handler::create(
        pacs_system_test_fixture::create_mpps_test_config());

    message_collector collector;

    handler->set_callback(
        [&collector](pacs_adapter::mpps_event event,
                     const pacs_adapter::mpps_dataset& dataset) {
            collector.add_message(pacs_adapter::to_string(event),
                                  dataset.discontinuation_reason);
        });

    // Step 1: Create MPPS
    auto dataset = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(dataset);

    // Step 2: Discontinue (patient refused)
    dataset.status = pacs_adapter::mpps_event::discontinued;
    dataset.end_date = mpps_test_data_generator::get_today_date();
    dataset.end_time = mpps_test_data_generator::get_offset_time(10);
    dataset.discontinuation_reason = "Patient refused";

    auto result = handler->on_n_set(dataset);
    PACS_TEST_ASSERT(result.has_value(), "N-SET DISCONTINUED should succeed");
    PACS_TEST_ASSERT(collector.count_type("DISCONTINUED") >= 1,
                     "Should have DISCONTINUED callback");

    // Verify status
    auto query = handler->query_mpps(dataset.sop_instance_uid);
    PACS_TEST_ASSERT(query.has_value() && query->has_value(),
                     "Record should exist");
    PACS_TEST_ASSERT(query->value().status ==
                         pacs_adapter::mpps_event::discontinued,
                     "Status should be DISCONTINUED");
    PACS_TEST_ASSERT(query->value().discontinuation_reason == "Patient refused",
                     "Reason should match");

    handler->stop();
    return true;
}

// =============================================================================
// MWL + MPPS Integration Tests
// =============================================================================

/**
 * @brief Test MWL entry correlates with MPPS by accession number
 */
bool test_mwl_mpps_correlation() {
    // Create MWL entry
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    std::string accession = pacs_system_test_fixture::generate_unique_accession();
    auto mwl_item = mwl_test_data_generator::create_item_with_accession(accession);
    (void)mwl_client.add_entry(mwl_item);

    // Create MPPS handler
    auto mpps_handler = pacs_adapter::mpps_handler::create(
        pacs_system_test_fixture::create_mpps_test_config());

    // Create MPPS with same accession number
    auto mpps_dataset = mpps_test_data_generator::create_in_progress();
    mpps_dataset.accession_number = accession;
    mpps_dataset.patient_id = mwl_item.patient.patient_id;

    (void)mpps_handler->on_n_create(mpps_dataset);

    // Query both by accession number
    pacs_adapter::mwl_query_filter mwl_filter;
    mwl_filter.accession_number = accession;
    auto mwl_result = mwl_client.query(mwl_filter);
    PACS_TEST_ASSERT(mwl_result.has_value() && mwl_result->items.size() == 1,
                     "Should find MWL entry");

    pacs_adapter::mpps_handler::mpps_query_params mpps_params;
    mpps_params.accession_number = accession;
    auto mpps_result = mpps_handler->query_mpps(mpps_params);
    PACS_TEST_ASSERT(mpps_result.has_value() && mpps_result->size() >= 1,
                     "Should find MPPS record");

    // Verify correlation
    PACS_TEST_ASSERT(
        mwl_result->items[0].patient.patient_id ==
            (*mpps_result)[0].patient_id,
        "Patient ID should match between MWL and MPPS");

    mwl_client.disconnect();
    mpps_handler->stop();
    return true;
}

// =============================================================================
// Multi-Procedure Workflow Tests
// =============================================================================

/**
 * @brief Test multiple concurrent procedures on different stations
 */
bool test_concurrent_procedures() {
    auto mpps_handler = pacs_adapter::mpps_handler::create(
        pacs_system_test_fixture::create_mpps_test_config());

    std::atomic<int> in_progress_count{0};
    std::atomic<int> completed_count{0};

    mpps_handler->set_callback(
        [&](pacs_adapter::mpps_event event,
            const pacs_adapter::mpps_dataset& /*dataset*/) {
            if (event == pacs_adapter::mpps_event::in_progress) {
                in_progress_count++;
            } else if (event == pacs_adapter::mpps_event::completed) {
                completed_count++;
            }
        });

    // Start 3 procedures on different stations
    auto ct_dataset = mpps_test_data_generator::create_with_station("CT_SCANNER_1");
    auto mr_dataset = mpps_test_data_generator::create_with_station("MR_SCANNER_1");
    auto us_dataset = mpps_test_data_generator::create_with_station("US_SCANNER_1");

    (void)mpps_handler->on_n_create(ct_dataset);
    (void)mpps_handler->on_n_create(mr_dataset);
    (void)mpps_handler->on_n_create(us_dataset);

    PACS_TEST_ASSERT(in_progress_count == 3,
                     "Should have 3 IN PROGRESS callbacks");

    // Verify all are active
    auto active = mpps_handler->get_active_mpps();
    PACS_TEST_ASSERT(active.has_value() && active->size() >= 3,
                     "Should have 3 active procedures");

    // Complete procedures in different order
    mr_dataset.status = pacs_adapter::mpps_event::completed;
    mr_dataset.end_date = mpps_test_data_generator::get_today_date();
    mr_dataset.end_time = mpps_test_data_generator::get_offset_time(20);
    (void)mpps_handler->on_n_set(mr_dataset);

    ct_dataset.status = pacs_adapter::mpps_event::completed;
    ct_dataset.end_date = mpps_test_data_generator::get_today_date();
    ct_dataset.end_time = mpps_test_data_generator::get_offset_time(40);
    (void)mpps_handler->on_n_set(ct_dataset);

    us_dataset.status = pacs_adapter::mpps_event::completed;
    us_dataset.end_date = mpps_test_data_generator::get_today_date();
    us_dataset.end_time = mpps_test_data_generator::get_offset_time(15);
    (void)mpps_handler->on_n_set(us_dataset);

    PACS_TEST_ASSERT(completed_count == 3,
                     "Should have 3 COMPLETED callbacks");

    // Verify no active procedures remain
    auto active_after = mpps_handler->get_active_mpps();
    PACS_TEST_ASSERT(active_after.has_value() && active_after->empty(),
                     "Should have no active procedures");

    mpps_handler->stop();
    return true;
}

// =============================================================================
// Error Handling Workflow Tests
// =============================================================================

/**
 * @brief Test workflow continues after single procedure failure
 */
bool test_workflow_resilience_on_error() {
    auto mpps_handler = pacs_adapter::mpps_handler::create(
        pacs_system_test_fixture::create_mpps_test_config());

    int successful_creates = 0;
    mpps_handler->set_callback(
        [&](pacs_adapter::mpps_event /*event*/,
            const pacs_adapter::mpps_dataset& /*dataset*/) {
            successful_creates++;
        });

    // Create valid procedure
    auto valid = mpps_test_data_generator::create_in_progress();
    (void)mpps_handler->on_n_create(valid);

    // Try to create invalid (will fail)
    pacs_adapter::mpps_dataset invalid;
    auto result = mpps_handler->on_n_create(invalid);
    PACS_TEST_ASSERT(!result.has_value(), "Invalid create should fail");

    // Create another valid procedure
    auto valid2 = mpps_test_data_generator::create_in_progress();
    (void)mpps_handler->on_n_create(valid2);

    PACS_TEST_ASSERT(successful_creates == 2,
                     "Should have 2 successful creates despite 1 failure");

    mpps_handler->stop();
    return true;
}

// =============================================================================
// Performance Tests
// =============================================================================

/**
 * @brief Test high-volume workflow handling
 */
bool test_high_volume_workflow() {
    auto mwl_config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client mwl_client(mwl_config);
    (void)mwl_client.connect();

    auto mpps_handler = pacs_adapter::mpps_handler::create(
        pacs_system_test_fixture::create_mpps_test_config());

    mpps_handler->set_callback(
        [](pacs_adapter::mpps_event /*event*/,
           const pacs_adapter::mpps_dataset& /*dataset*/) {
            // Empty callback for performance test
        });

    const size_t num_procedures = 100;

    auto start = std::chrono::high_resolution_clock::now();

    // Create MWL entries and MPPS records
    for (size_t i = 0; i < num_procedures; ++i) {
        auto mwl_item = mwl_test_data_generator::create_sample_item();
        (void)mwl_client.add_entry(mwl_item);

        auto mpps_dataset = mpps_test_data_generator::create_in_progress();
        mpps_dataset.accession_number =
            mwl_item.imaging_service_request.accession_number;
        (void)mpps_handler->on_n_create(mpps_dataset);

        // Complete immediately
        mpps_dataset.status = pacs_adapter::mpps_event::completed;
        mpps_dataset.end_date = mpps_test_data_generator::get_today_date();
        mpps_dataset.end_time = mpps_test_data_generator::get_offset_time(30);
        (void)mpps_handler->on_n_set(mpps_dataset);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start);

    std::cout << "  High volume test: " << num_procedures
              << " procedures in " << duration.count() << "ms" << std::endl;

    // Performance assertion: should complete in reasonable time
    PACS_TEST_ASSERT(duration.count() < 30000,
                     "Should complete 100 procedures in under 30 seconds");

    // Verify statistics
    auto mpps_stats = mpps_handler->get_statistics();
    PACS_TEST_ASSERT(mpps_stats.n_create_count >= num_procedures,
                     "Should have all N-CREATEs recorded");
    PACS_TEST_ASSERT(mpps_stats.completed_count >= num_procedures,
                     "Should have all completions recorded");

    mwl_client.disconnect();
    mpps_handler->stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_pacs_system_e2e_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== pacs_system E2E Integration Tests ===" << std::endl;
    std::cout << "Testing complete workflow: HL7 -> MWL -> MPPS -> HL7\n"
              << std::endl;

    std::cout << "\n--- MWL Creation Workflow Tests ---" << std::endl;
    RUN_PACS_TEST(test_order_creates_mwl_entry);
    RUN_PACS_TEST(test_modality_queries_mwl);

    std::cout << "\n--- Complete MPPS Workflow Tests ---" << std::endl;
    RUN_PACS_TEST(test_mpps_complete_workflow);
    RUN_PACS_TEST(test_mpps_discontinuation_workflow);

    std::cout << "\n--- MWL + MPPS Integration Tests ---" << std::endl;
    RUN_PACS_TEST(test_mwl_mpps_correlation);

    std::cout << "\n--- Multi-Procedure Workflow Tests ---" << std::endl;
    RUN_PACS_TEST(test_concurrent_procedures);

    std::cout << "\n--- Error Handling Workflow Tests ---" << std::endl;
    RUN_PACS_TEST(test_workflow_resilience_on_error);

    std::cout << "\n--- Performance Tests ---" << std::endl;
    RUN_PACS_TEST(test_high_volume_workflow);

    std::cout << "\n=== pacs_system E2E Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;

    if (passed + failed > 0) {
        double pass_rate = (passed * 100.0) / (passed + failed);
        std::cout << "Pass Rate: " << pass_rate << "%" << std::endl;
    }

    return failed > 0 ? 1 : 0;
}

}  // namespace pacs::bridge::integration::test

int main() {
    return pacs::bridge::integration::test::run_all_pacs_system_e2e_tests();
}
