/**
 * @file mpps_persistence_test.cpp
 * @brief MPPS (Modality Performed Procedure Step) persistence integration tests
 *
 * Tests the MPPS handler persistence operations against the database:
 * - N-CREATE persists MPPS record
 * - N-SET updates MPPS status (COMPLETED/DISCONTINUED)
 * - Query MPPS by various criteria
 * - Recovery of pending MPPS records
 * - State transition validation
 * - Persistence statistics tracking
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/188
 * @see https://github.com/kcenon/pacs_bridge/issues/193
 * @see https://github.com/kcenon/pacs_bridge/issues/186
 */

#include "pacs_system_test_base.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace pacs::bridge::integration::test {

// =============================================================================
// Test Fixture Setup
// =============================================================================

/**
 * @brief Create MPPS handler for testing
 */
std::unique_ptr<pacs_adapter::mpps_handler> create_test_handler() {
    auto config = pacs_system_test_fixture::create_mpps_test_config();
    return pacs_adapter::mpps_handler::create(config);
}

// =============================================================================
// N-CREATE Persistence Tests
// =============================================================================

/**
 * @brief Test that N-CREATE persists MPPS record with IN PROGRESS status
 */
bool test_n_create_persists_record() {
    auto handler = create_test_handler();
    PACS_TEST_ASSERT(handler != nullptr, "Handler should be created");
    PACS_TEST_ASSERT(handler->is_persistence_enabled(),
                     "Persistence should be enabled");

    // Create and process N-CREATE
    auto dataset = mpps_test_data_generator::create_in_progress();
    auto result = handler->on_n_create(dataset);
    PACS_TEST_ASSERT(result.has_value(), "N-CREATE should succeed");

    // Query the persisted record
    auto query_result = handler->query_mpps(dataset.sop_instance_uid);
    PACS_TEST_ASSERT(query_result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(query_result->has_value(), "Record should exist");
    PACS_TEST_ASSERT(query_result->value().status ==
                         pacs_adapter::mpps_event::in_progress,
                     "Status should be IN PROGRESS");
    PACS_TEST_ASSERT(query_result->value().accession_number ==
                         dataset.accession_number,
                     "Accession number should match");

    handler->stop();
    return true;
}

/**
 * @brief Test that N-CREATE persists complete patient information
 */
bool test_n_create_persists_patient_data() {
    auto handler = create_test_handler();

    auto dataset = mpps_test_data_generator::create_in_progress();
    dataset.patient_id = "MPPS_PAT_FULL";
    dataset.patient_name = "PERSISTENCE^JOHN^MIDDLE";
    (void)handler->on_n_create(dataset);

    auto query_result = handler->query_mpps(dataset.sop_instance_uid);
    PACS_TEST_ASSERT(query_result.has_value() && query_result->has_value(),
                     "Record should exist");
    PACS_TEST_ASSERT(query_result->value().patient_id == "MPPS_PAT_FULL",
                     "Patient ID should match");
    PACS_TEST_ASSERT(
        query_result->value().patient_name == "PERSISTENCE^JOHN^MIDDLE",
        "Patient name should match");

    handler->stop();
    return true;
}

/**
 * @brief Test that N-CREATE persists timing information
 */
bool test_n_create_persists_timing() {
    auto handler = create_test_handler();

    auto dataset = mpps_test_data_generator::create_in_progress();
    dataset.start_date = "20241215";
    dataset.start_time = "143000";
    (void)handler->on_n_create(dataset);

    auto query_result = handler->query_mpps(dataset.sop_instance_uid);
    PACS_TEST_ASSERT(query_result.has_value() && query_result->has_value(),
                     "Record should exist");
    PACS_TEST_ASSERT(query_result->value().start_date == "20241215",
                     "Start date should match");
    PACS_TEST_ASSERT(query_result->value().start_time == "143000",
                     "Start time should match");

    handler->stop();
    return true;
}

/**
 * @brief Test that N-CREATE with invalid dataset fails
 */
bool test_n_create_invalid_dataset_fails() {
    auto handler = create_test_handler();

    pacs_adapter::mpps_dataset invalid_dataset;  // Empty dataset

    auto result = handler->on_n_create(invalid_dataset);
    PACS_TEST_ASSERT(!result.has_value(), "N-CREATE with empty dataset should fail");

    handler->stop();
    return true;
}

// =============================================================================
// N-SET Status Update Tests
// =============================================================================

/**
 * @brief Test that N-SET COMPLETED updates status
 */
bool test_n_set_completed_updates_status() {
    auto handler = create_test_handler();

    // First create the record
    auto dataset = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(dataset);

    // Then complete it
    dataset.status = pacs_adapter::mpps_event::completed;
    dataset.end_date = mpps_test_data_generator::get_today_date();
    dataset.end_time = mpps_test_data_generator::get_offset_time(30);

    auto result = handler->on_n_set(dataset);
    PACS_TEST_ASSERT(result.has_value(), "N-SET COMPLETED should succeed");

    // Verify status updated
    auto query_result = handler->query_mpps(dataset.sop_instance_uid);
    PACS_TEST_ASSERT(query_result.has_value() && query_result->has_value(),
                     "Record should exist");
    PACS_TEST_ASSERT(query_result->value().status ==
                         pacs_adapter::mpps_event::completed,
                     "Status should be COMPLETED");
    PACS_TEST_ASSERT(!query_result->value().end_date.empty(),
                     "End date should be set");

    handler->stop();
    return true;
}

/**
 * @brief Test that N-SET DISCONTINUED updates status
 */
bool test_n_set_discontinued_updates_status() {
    auto handler = create_test_handler();

    auto dataset = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(dataset);

    dataset.status = pacs_adapter::mpps_event::discontinued;
    dataset.end_date = mpps_test_data_generator::get_today_date();
    dataset.end_time = mpps_test_data_generator::get_offset_time(10);
    dataset.discontinuation_reason = "Patient refused";

    auto result = handler->on_n_set(dataset);
    PACS_TEST_ASSERT(result.has_value(), "N-SET DISCONTINUED should succeed");

    auto query_result = handler->query_mpps(dataset.sop_instance_uid);
    PACS_TEST_ASSERT(query_result.has_value() && query_result->has_value(),
                     "Record should exist");
    PACS_TEST_ASSERT(query_result->value().status ==
                         pacs_adapter::mpps_event::discontinued,
                     "Status should be DISCONTINUED");
    PACS_TEST_ASSERT(query_result->value().discontinuation_reason ==
                         "Patient refused",
                     "Discontinuation reason should match");

    handler->stop();
    return true;
}

/**
 * @brief Test that N-SET on non-existent record fails
 */
bool test_n_set_nonexistent_record_fails() {
    auto handler = create_test_handler();

    auto dataset = mpps_test_data_generator::create_completed();
    dataset.sop_instance_uid =
        pacs_system_test_fixture::generate_unique_sop_uid();

    auto result = handler->on_n_set(dataset);
    PACS_TEST_ASSERT(!result.has_value(),
                     "N-SET on non-existent record should fail");
    PACS_TEST_ASSERT(result.error() == pacs_adapter::mpps_error::record_not_found,
                     "Error should be record_not_found");

    handler->stop();
    return true;
}

/**
 * @brief Test that updating a final state (COMPLETED) fails
 */
bool test_n_set_final_state_fails() {
    auto handler = create_test_handler();

    auto dataset = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(dataset);

    // Complete the record
    dataset.status = pacs_adapter::mpps_event::completed;
    dataset.end_date = mpps_test_data_generator::get_today_date();
    dataset.end_time = mpps_test_data_generator::get_offset_time(30);
    (void)handler->on_n_set(dataset);

    // Try to update again (should fail)
    dataset.status = pacs_adapter::mpps_event::discontinued;
    auto result = handler->on_n_set(dataset);
    PACS_TEST_ASSERT(!result.has_value(),
                     "N-SET on completed record should fail");
    PACS_TEST_ASSERT(
        result.error() == pacs_adapter::mpps_error::invalid_state_transition,
        "Error should be invalid_state_transition");

    handler->stop();
    return true;
}

// =============================================================================
// Query Tests
// =============================================================================

/**
 * @brief Test query by SOP Instance UID
 */
bool test_query_by_sop_uid() {
    auto handler = create_test_handler();

    auto dataset = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(dataset);

    auto result = handler->query_mpps(dataset.sop_instance_uid);
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(result->has_value(), "Record should exist");
    PACS_TEST_ASSERT(result->value().sop_instance_uid == dataset.sop_instance_uid,
                     "SOP UID should match");

    handler->stop();
    return true;
}

/**
 * @brief Test query with filter parameters
 */
bool test_query_with_filter() {
    auto handler = create_test_handler();

    // Create multiple records
    auto dataset1 = mpps_test_data_generator::create_with_station("CT_SCANNER_1");
    auto dataset2 = mpps_test_data_generator::create_with_station("MR_SCANNER_1");
    auto dataset3 = mpps_test_data_generator::create_with_station("CT_SCANNER_1");

    (void)handler->on_n_create(dataset1);
    (void)handler->on_n_create(dataset2);
    (void)handler->on_n_create(dataset3);

    // Query by station
    pacs_adapter::mpps_handler::mpps_query_params params;
    params.station_ae_title = "CT_SCANNER_1";

    auto result = handler->query_mpps(params);
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(result->size() >= 2, "Should return at least 2 CT records");

    for (const auto& record : *result) {
        PACS_TEST_ASSERT(record.station_ae_title == "CT_SCANNER_1",
                         "All results should be CT_SCANNER_1");
    }

    handler->stop();
    return true;
}

/**
 * @brief Test query by status filter
 */
bool test_query_by_status() {
    auto handler = create_test_handler();

    // Create in-progress and completed records
    auto in_progress = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(in_progress);

    auto completed = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(completed);
    completed.status = pacs_adapter::mpps_event::completed;
    completed.end_date = mpps_test_data_generator::get_today_date();
    completed.end_time = mpps_test_data_generator::get_offset_time(30);
    (void)handler->on_n_set(completed);

    // Query only in-progress
    pacs_adapter::mpps_handler::mpps_query_params params;
    params.status = pacs_adapter::mpps_event::in_progress;

    auto result = handler->query_mpps(params);
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");

    for (const auto& record : *result) {
        PACS_TEST_ASSERT(record.status == pacs_adapter::mpps_event::in_progress,
                         "All results should be IN PROGRESS");
    }

    handler->stop();
    return true;
}

/**
 * @brief Test get active MPPS records
 */
bool test_get_active_mpps() {
    auto handler = create_test_handler();

    // Create multiple records with different statuses
    auto active1 = mpps_test_data_generator::create_in_progress();
    auto active2 = mpps_test_data_generator::create_in_progress();
    auto completed = mpps_test_data_generator::create_in_progress();

    (void)handler->on_n_create(active1);
    (void)handler->on_n_create(active2);
    (void)handler->on_n_create(completed);

    // Complete one
    completed.status = pacs_adapter::mpps_event::completed;
    completed.end_date = mpps_test_data_generator::get_today_date();
    completed.end_time = mpps_test_data_generator::get_offset_time(30);
    (void)handler->on_n_set(completed);

    auto result = handler->get_active_mpps();
    PACS_TEST_ASSERT(result.has_value(), "Get active should succeed");
    PACS_TEST_ASSERT(result->size() >= 2, "Should have at least 2 active");

    for (const auto& record : *result) {
        PACS_TEST_ASSERT(record.status == pacs_adapter::mpps_event::in_progress,
                         "All active records should be IN PROGRESS");
    }

    handler->stop();
    return true;
}

/**
 * @brief Test get pending MPPS for station
 */
bool test_get_pending_for_station() {
    auto handler = create_test_handler();

    auto ct_active = mpps_test_data_generator::create_with_station("CT_SCANNER_1");
    auto mr_active = mpps_test_data_generator::create_with_station("MR_SCANNER_1");

    (void)handler->on_n_create(ct_active);
    (void)handler->on_n_create(mr_active);

    auto result = handler->get_pending_mpps_for_station("CT_SCANNER_1");
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(result->size() >= 1,
                     "Should have at least 1 pending CT record");

    for (const auto& record : *result) {
        PACS_TEST_ASSERT(record.station_ae_title == "CT_SCANNER_1",
                         "All records should be for CT_SCANNER_1");
    }

    handler->stop();
    return true;
}

// =============================================================================
// Persistence Statistics Tests
// =============================================================================

/**
 * @brief Test persistence statistics tracking
 */
bool test_persistence_statistics() {
    auto handler = create_test_handler();

    // Perform various operations
    auto dataset1 = mpps_test_data_generator::create_in_progress();
    auto dataset2 = mpps_test_data_generator::create_in_progress();

    (void)handler->on_n_create(dataset1);
    (void)handler->on_n_create(dataset2);

    // Complete one
    dataset1.status = pacs_adapter::mpps_event::completed;
    dataset1.end_date = mpps_test_data_generator::get_today_date();
    dataset1.end_time = mpps_test_data_generator::get_offset_time(30);
    (void)handler->on_n_set(dataset1);

    // Discontinue one
    dataset2.status = pacs_adapter::mpps_event::discontinued;
    dataset2.end_date = mpps_test_data_generator::get_today_date();
    dataset2.end_time = mpps_test_data_generator::get_offset_time(10);
    (void)handler->on_n_set(dataset2);

    auto stats = handler->get_persistence_stats();
    PACS_TEST_ASSERT(stats.total_persisted >= 2,
                     "Should have at least 2 persisted");
    PACS_TEST_ASSERT(stats.completed_count >= 1,
                     "Should have at least 1 completed");
    PACS_TEST_ASSERT(stats.discontinued_count >= 1,
                     "Should have at least 1 discontinued");

    handler->stop();
    return true;
}

// =============================================================================
// Callback Integration Tests
// =============================================================================

/**
 * @brief Test callback invocation on N-CREATE
 */
bool test_callback_on_n_create() {
    auto handler = create_test_handler();

    bool callback_invoked = false;
    pacs_adapter::mpps_event received_event;
    std::string received_accession;

    handler->set_callback(
        [&](pacs_adapter::mpps_event event,
            const pacs_adapter::mpps_dataset& dataset) {
            callback_invoked = true;
            received_event = event;
            received_accession = dataset.accession_number;
        });

    auto dataset = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(dataset);

    PACS_TEST_ASSERT(callback_invoked, "Callback should be invoked");
    PACS_TEST_ASSERT(received_event == pacs_adapter::mpps_event::in_progress,
                     "Event should be IN PROGRESS");
    PACS_TEST_ASSERT(received_accession == dataset.accession_number,
                     "Accession number should match");

    handler->stop();
    return true;
}

/**
 * @brief Test callback invocation on N-SET COMPLETED
 */
bool test_callback_on_n_set_completed() {
    auto handler = create_test_handler();

    pacs_adapter::mpps_event last_event = pacs_adapter::mpps_event::in_progress;

    handler->set_callback(
        [&](pacs_adapter::mpps_event event,
            const pacs_adapter::mpps_dataset& /*dataset*/) {
            last_event = event;
        });

    auto dataset = mpps_test_data_generator::create_in_progress();
    (void)handler->on_n_create(dataset);

    dataset.status = pacs_adapter::mpps_event::completed;
    dataset.end_date = mpps_test_data_generator::get_today_date();
    dataset.end_time = mpps_test_data_generator::get_offset_time(30);
    (void)handler->on_n_set(dataset);

    PACS_TEST_ASSERT(last_event == pacs_adapter::mpps_event::completed,
                     "Last event should be COMPLETED");

    handler->stop();
    return true;
}

// =============================================================================
// Handler Statistics Tests
// =============================================================================

/**
 * @brief Test handler statistics tracking
 */
bool test_handler_statistics() {
    auto handler = create_test_handler();

    handler->set_callback([](pacs_adapter::mpps_event /*event*/,
                             const pacs_adapter::mpps_dataset& /*dataset*/) {
        // Empty callback
    });

    auto dataset1 = mpps_test_data_generator::create_in_progress();
    auto dataset2 = mpps_test_data_generator::create_in_progress();

    (void)handler->on_n_create(dataset1);
    (void)handler->on_n_create(dataset2);

    dataset1.status = pacs_adapter::mpps_event::completed;
    dataset1.end_date = mpps_test_data_generator::get_today_date();
    dataset1.end_time = mpps_test_data_generator::get_offset_time(30);
    (void)handler->on_n_set(dataset1);

    auto stats = handler->get_statistics();
    PACS_TEST_ASSERT(stats.n_create_count >= 2,
                     "Should have at least 2 N-CREATE");
    PACS_TEST_ASSERT(stats.n_set_count >= 1,
                     "Should have at least 1 N-SET");
    PACS_TEST_ASSERT(stats.in_progress_count >= 2,
                     "Should have at least 2 in-progress");
    PACS_TEST_ASSERT(stats.completed_count >= 1,
                     "Should have at least 1 completed");

    handler->stop();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_mpps_persistence_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== MPPS Persistence Integration Tests ===" << std::endl;
    std::cout << "Testing MPPS record persistence operations\n" << std::endl;

    std::cout << "\n--- N-CREATE Persistence Tests ---" << std::endl;
    RUN_PACS_TEST(test_n_create_persists_record);
    RUN_PACS_TEST(test_n_create_persists_patient_data);
    RUN_PACS_TEST(test_n_create_persists_timing);
    RUN_PACS_TEST(test_n_create_invalid_dataset_fails);

    std::cout << "\n--- N-SET Status Update Tests ---" << std::endl;
    RUN_PACS_TEST(test_n_set_completed_updates_status);
    RUN_PACS_TEST(test_n_set_discontinued_updates_status);
    RUN_PACS_TEST(test_n_set_nonexistent_record_fails);
    RUN_PACS_TEST(test_n_set_final_state_fails);

    std::cout << "\n--- Query Tests ---" << std::endl;
    RUN_PACS_TEST(test_query_by_sop_uid);
    RUN_PACS_TEST(test_query_with_filter);
    RUN_PACS_TEST(test_query_by_status);
    RUN_PACS_TEST(test_get_active_mpps);
    RUN_PACS_TEST(test_get_pending_for_station);

    std::cout << "\n--- Persistence Statistics Tests ---" << std::endl;
    RUN_PACS_TEST(test_persistence_statistics);

    std::cout << "\n--- Callback Integration Tests ---" << std::endl;
    RUN_PACS_TEST(test_callback_on_n_create);
    RUN_PACS_TEST(test_callback_on_n_set_completed);

    std::cout << "\n--- Handler Statistics Tests ---" << std::endl;
    RUN_PACS_TEST(test_handler_statistics);

    std::cout << "\n=== MPPS Persistence Test Summary ===" << std::endl;
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
    return pacs::bridge::integration::test::run_all_mpps_persistence_tests();
}
