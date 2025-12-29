/**
 * @file mwl_database_test.cpp
 * @brief MWL (Modality Worklist) database integration tests
 *
 * Tests the MWL client operations against the pacs_system database:
 * - Add entry persistence and retrieval
 * - Query operations with various filters
 * - Update entry operations
 * - Cancel entry operations
 * - Date-based cleanup operations
 * - Transaction handling and error recovery
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/188
 * @see https://github.com/kcenon/pacs_bridge/issues/192
 */

#include "pacs_system_test_base.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace pacs::bridge::integration::test {

// =============================================================================
// Add Entry Tests
// =============================================================================

/**
 * @brief Test that add_entry persists MWL item to database
 */
bool test_add_entry_persists_to_database() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);

    // Connect to database
    auto connect_result = client.connect();
    PACS_TEST_ASSERT(connect_result.has_value(), "Connect should succeed");

    // Create and add test item
    auto item = mwl_test_data_generator::create_sample_item();
    auto add_result = client.add_entry(item);
    PACS_TEST_ASSERT(add_result.has_value(), "Add entry should succeed");
    PACS_TEST_ASSERT(add_result->dicom_status == 0x0000,
                     "DICOM status should be success");

    // Verify entry is retrievable
    auto get_result =
        client.get_entry(item.imaging_service_request.accession_number);
    PACS_TEST_ASSERT(get_result.has_value(), "Get entry should succeed");
    PACS_TEST_ASSERT(get_result->patient.patient_id == item.patient.patient_id,
                     "Patient ID should match");
    PACS_TEST_ASSERT(get_result->patient.patient_name ==
                         item.patient.patient_name,
                     "Patient name should match");

    client.disconnect();
    return true;
}

/**
 * @brief Test add_entry with complete patient demographics
 */
bool test_add_entry_with_full_patient_data() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    auto item = mwl_test_data_generator::create_sample_item();
    item.patient.patient_id = "FULL_PAT_001";
    item.patient.patient_name = "COMPREHENSIVE^JOHN^MIDDLE";
    item.patient.patient_birth_date = "19751225";
    item.patient.patient_sex = "M";

    auto add_result = client.add_entry(item);
    PACS_TEST_ASSERT(add_result.has_value(), "Add entry should succeed");

    auto get_result =
        client.get_entry(item.imaging_service_request.accession_number);
    PACS_TEST_ASSERT(get_result.has_value(), "Get entry should succeed");
    PACS_TEST_ASSERT(get_result->patient.patient_birth_date == "19751225",
                     "Birth date should match");
    PACS_TEST_ASSERT(get_result->patient.patient_sex == "M",
                     "Patient sex should match");

    client.disconnect();
    return true;
}

/**
 * @brief Test that duplicate accession numbers are rejected
 */
bool test_add_entry_duplicate_rejected() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    std::string accession = pacs_system_test_fixture::generate_unique_accession();
    auto item1 = mwl_test_data_generator::create_item_with_accession(accession);
    auto item2 = mwl_test_data_generator::create_item_with_accession(accession);
    item2.patient.patient_id = "DIFFERENT_PATIENT";

    auto result1 = client.add_entry(item1);
    PACS_TEST_ASSERT(result1.has_value(), "First add should succeed");

    auto result2 = client.add_entry(item2);
    PACS_TEST_ASSERT(!result2.has_value(), "Duplicate add should fail");
    PACS_TEST_ASSERT(result2.error() == pacs_adapter::mwl_error::duplicate_entry,
                     "Error should be duplicate_entry");

    client.disconnect();
    return true;
}

/**
 * @brief Test add_entry with invalid data is rejected
 */
bool test_add_entry_invalid_data_rejected() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    mapping::mwl_item invalid_item;  // Empty item with no accession

    auto result = client.add_entry(invalid_item);
    PACS_TEST_ASSERT(!result.has_value(), "Invalid add should fail");
    PACS_TEST_ASSERT(result.error() == pacs_adapter::mwl_error::invalid_data,
                     "Error should be invalid_data");

    client.disconnect();
    return true;
}

// =============================================================================
// Query Tests
// =============================================================================

/**
 * @brief Test querying all entries
 */
bool test_query_all_entries() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    // Add multiple entries
    auto items = mwl_test_data_generator::create_batch(5);
    for (const auto& item : items) {
        auto result = client.add_entry(item);
        PACS_TEST_ASSERT(result.has_value(), "Add entry should succeed");
    }

    // Query all
    pacs_adapter::mwl_query_filter filter;
    auto query_result = client.query(filter);
    PACS_TEST_ASSERT(query_result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(query_result->items.size() >= 5,
                     "Should return at least 5 items");

    client.disconnect();
    return true;
}

/**
 * @brief Test query by patient ID
 */
bool test_query_by_patient_id() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    std::string unique_patient_id = "UNIQUE_PAT_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    auto item = mwl_test_data_generator::create_item_with_patient(
        unique_patient_id, "UNIQUE^PATIENT");
    client.add_entry(item);

    pacs_adapter::mwl_query_filter filter;
    filter.patient_id = unique_patient_id;

    auto result = client.query(filter);
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(result->items.size() == 1,
                     "Should return exactly 1 item");
    PACS_TEST_ASSERT(result->items[0].patient.patient_id == unique_patient_id,
                     "Patient ID should match");

    client.disconnect();
    return true;
}

/**
 * @brief Test query by modality
 */
bool test_query_by_modality() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    // Add entries with different modalities
    auto ct_item = mwl_test_data_generator::create_item_with_modality("CT");
    auto mr_item = mwl_test_data_generator::create_item_with_modality("MR");
    auto us_item = mwl_test_data_generator::create_item_with_modality("US");

    client.add_entry(ct_item);
    client.add_entry(mr_item);
    client.add_entry(us_item);

    // Query CT modality
    pacs_adapter::mwl_query_filter filter;
    filter.modality = "CT";

    auto result = client.query(filter);
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(result->items.size() >= 1, "Should return CT entries");

    for (const auto& item : result->items) {
        if (!item.scheduled_steps.empty()) {
            PACS_TEST_ASSERT(item.scheduled_steps[0].modality == "CT",
                             "All results should be CT modality");
        }
    }

    client.disconnect();
    return true;
}

/**
 * @brief Test query by scheduled date
 */
bool test_query_by_scheduled_date() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    std::string today = mwl_test_data_generator::get_today_date();
    std::string tomorrow = mwl_test_data_generator::get_date_offset(1);

    auto today_item = mwl_test_data_generator::create_item_with_date(today);
    auto tomorrow_item = mwl_test_data_generator::create_item_with_date(tomorrow);

    client.add_entry(today_item);
    client.add_entry(tomorrow_item);

    pacs_adapter::mwl_query_filter filter;
    filter.scheduled_date = today;

    auto result = client.query(filter);
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(result->items.size() >= 1,
                     "Should return today's entries");

    client.disconnect();
    return true;
}

/**
 * @brief Test query with max results limit
 */
bool test_query_with_max_results() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    // Add 10 entries
    auto items = mwl_test_data_generator::create_batch(10);
    for (const auto& item : items) {
        client.add_entry(item);
    }

    pacs_adapter::mwl_query_filter filter;
    filter.max_results = 5;

    auto result = client.query(filter);
    PACS_TEST_ASSERT(result.has_value(), "Query should succeed");
    PACS_TEST_ASSERT(result->items.size() <= 5,
                     "Should return at most 5 items");

    client.disconnect();
    return true;
}

// =============================================================================
// Update Entry Tests
// =============================================================================

/**
 * @brief Test updating an existing entry
 */
bool test_update_entry_success() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    auto item = mwl_test_data_generator::create_sample_item();
    std::string accession = item.imaging_service_request.accession_number;
    client.add_entry(item);

    // Update patient name
    mapping::mwl_item updates;
    updates.patient.patient_name = "UPDATED^PATIENT^NAME";

    auto update_result = client.update_entry(accession, updates);
    PACS_TEST_ASSERT(update_result.has_value(), "Update should succeed");

    // Verify update
    auto get_result = client.get_entry(accession);
    PACS_TEST_ASSERT(get_result.has_value(), "Get should succeed");
    PACS_TEST_ASSERT(get_result->patient.patient_name == "UPDATED^PATIENT^NAME",
                     "Patient name should be updated");

    client.disconnect();
    return true;
}

/**
 * @brief Test updating non-existent entry fails
 */
bool test_update_entry_not_found() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    mapping::mwl_item updates;
    updates.patient.patient_name = "NEW^NAME";

    auto result = client.update_entry("NONEXISTENT_ACC", updates);
    PACS_TEST_ASSERT(!result.has_value(), "Update should fail");
    PACS_TEST_ASSERT(result.error() == pacs_adapter::mwl_error::entry_not_found,
                     "Error should be entry_not_found");

    client.disconnect();
    return true;
}

// =============================================================================
// Cancel Entry Tests
// =============================================================================

/**
 * @brief Test canceling an existing entry
 */
bool test_cancel_entry_success() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    auto item = mwl_test_data_generator::create_sample_item();
    std::string accession = item.imaging_service_request.accession_number;
    client.add_entry(item);

    PACS_TEST_ASSERT(client.exists(accession), "Entry should exist");

    auto cancel_result = client.cancel_entry(accession);
    PACS_TEST_ASSERT(cancel_result.has_value(), "Cancel should succeed");

    PACS_TEST_ASSERT(!client.exists(accession),
                     "Entry should not exist after cancel");

    client.disconnect();
    return true;
}

/**
 * @brief Test canceling non-existent entry fails
 */
bool test_cancel_entry_not_found() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    auto result = client.cancel_entry("NONEXISTENT_ACC");
    PACS_TEST_ASSERT(!result.has_value(), "Cancel should fail");
    PACS_TEST_ASSERT(result.error() == pacs_adapter::mwl_error::entry_not_found,
                     "Error should be entry_not_found");

    client.disconnect();
    return true;
}

// =============================================================================
// Bulk Operations Tests
// =============================================================================

/**
 * @brief Test bulk add entries
 */
bool test_bulk_add_entries() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    auto items = mwl_test_data_generator::create_batch(10);

    auto result = client.add_entries(items, true);
    PACS_TEST_ASSERT(result.has_value(), "Bulk add should succeed");
    PACS_TEST_ASSERT(*result == 10, "Should add all 10 items");

    // Verify all entries exist
    for (const auto& item : items) {
        PACS_TEST_ASSERT(
            client.exists(item.imaging_service_request.accession_number),
            "Entry should exist");
    }

    client.disconnect();
    return true;
}

/**
 * @brief Test cancel entries before date
 */
bool test_cancel_entries_before_date() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    // Add old entry (30 days ago)
    auto old_item = mwl_test_data_generator::create_item_with_date(
        mwl_test_data_generator::get_date_offset(-30));
    std::string old_accession =
        old_item.imaging_service_request.accession_number;
    client.add_entry(old_item);

    // Add recent entry (today)
    auto recent_item = mwl_test_data_generator::create_item_with_date(
        mwl_test_data_generator::get_today_date());
    std::string recent_accession =
        recent_item.imaging_service_request.accession_number;
    client.add_entry(recent_item);

    // Cancel entries before 7 days ago
    std::string cutoff = mwl_test_data_generator::get_date_offset(-7);
    auto result = client.cancel_entries_before(cutoff);
    PACS_TEST_ASSERT(result.has_value(), "Cancel before should succeed");
    PACS_TEST_ASSERT(*result >= 1, "Should cancel at least 1 old entry");

    PACS_TEST_ASSERT(!client.exists(old_accession),
                     "Old entry should be cancelled");
    PACS_TEST_ASSERT(client.exists(recent_accession),
                     "Recent entry should remain");

    client.disconnect();
    return true;
}

/**
 * @brief Test cancel_entries_before with invalid date fails
 */
bool test_cancel_entries_before_invalid_date() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    auto result = client.cancel_entries_before("");
    PACS_TEST_ASSERT(!result.has_value(), "Empty date should fail");

    result = client.cancel_entries_before("invalid");
    PACS_TEST_ASSERT(!result.has_value(), "Invalid format should fail");

    client.disconnect();
    return true;
}

// =============================================================================
// Connection Tests
// =============================================================================

/**
 * @brief Test connection and disconnection
 */
bool test_connection_lifecycle() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);

    PACS_TEST_ASSERT(!client.is_connected(), "Should not be connected initially");

    auto connect_result = client.connect();
    PACS_TEST_ASSERT(connect_result.has_value(), "Connect should succeed");
    PACS_TEST_ASSERT(client.is_connected(), "Should be connected");

    client.disconnect();
    PACS_TEST_ASSERT(!client.is_connected(),
                     "Should not be connected after disconnect");

    return true;
}

/**
 * @brief Test reconnection
 */
bool test_reconnection() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    auto item = mwl_test_data_generator::create_sample_item();
    client.add_entry(item);

    auto reconnect_result = client.reconnect();
    PACS_TEST_ASSERT(reconnect_result.has_value(), "Reconnect should succeed");
    PACS_TEST_ASSERT(client.is_connected(),
                     "Should be connected after reconnect");

    // Verify data still accessible
    PACS_TEST_ASSERT(
        client.exists(item.imaging_service_request.accession_number),
        "Entry should still exist after reconnect");

    client.disconnect();
    return true;
}

// =============================================================================
// Statistics Tests
// =============================================================================

/**
 * @brief Test statistics tracking
 */
bool test_statistics_tracking() {
    auto config = pacs_system_test_fixture::create_mwl_test_config();
    pacs_adapter::mwl_client client(config);
    client.connect();

    // Perform various operations
    auto item1 = mwl_test_data_generator::create_sample_item();
    auto item2 = mwl_test_data_generator::create_sample_item();

    client.add_entry(item1);
    client.add_entry(item2);

    mapping::mwl_item updates;
    updates.patient.patient_name = "UPDATED";
    client.update_entry(item1.imaging_service_request.accession_number, updates);

    client.cancel_entry(item2.imaging_service_request.accession_number);

    pacs_adapter::mwl_query_filter filter;
    client.query(filter);

    auto stats = client.get_statistics();
    PACS_TEST_ASSERT(stats.add_count >= 2, "Should have at least 2 adds");
    PACS_TEST_ASSERT(stats.update_count >= 1, "Should have at least 1 update");
    PACS_TEST_ASSERT(stats.cancel_count >= 1, "Should have at least 1 cancel");
    PACS_TEST_ASSERT(stats.query_count >= 1, "Should have at least 1 query");
    PACS_TEST_ASSERT(stats.connect_successes >= 1,
                     "Should have at least 1 connection");

    client.disconnect();
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_mwl_database_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "=== MWL Database Integration Tests ===" << std::endl;
    std::cout << "Testing pacs_bridge <-> pacs_system MWL operations\n"
              << std::endl;

    std::cout << "\n--- Add Entry Tests ---" << std::endl;
    RUN_PACS_TEST(test_add_entry_persists_to_database);
    RUN_PACS_TEST(test_add_entry_with_full_patient_data);
    RUN_PACS_TEST(test_add_entry_duplicate_rejected);
    RUN_PACS_TEST(test_add_entry_invalid_data_rejected);

    std::cout << "\n--- Query Tests ---" << std::endl;
    RUN_PACS_TEST(test_query_all_entries);
    RUN_PACS_TEST(test_query_by_patient_id);
    RUN_PACS_TEST(test_query_by_modality);
    RUN_PACS_TEST(test_query_by_scheduled_date);
    RUN_PACS_TEST(test_query_with_max_results);

    std::cout << "\n--- Update Entry Tests ---" << std::endl;
    RUN_PACS_TEST(test_update_entry_success);
    RUN_PACS_TEST(test_update_entry_not_found);

    std::cout << "\n--- Cancel Entry Tests ---" << std::endl;
    RUN_PACS_TEST(test_cancel_entry_success);
    RUN_PACS_TEST(test_cancel_entry_not_found);

    std::cout << "\n--- Bulk Operations Tests ---" << std::endl;
    RUN_PACS_TEST(test_bulk_add_entries);
    RUN_PACS_TEST(test_cancel_entries_before_date);
    RUN_PACS_TEST(test_cancel_entries_before_invalid_date);

    std::cout << "\n--- Connection Tests ---" << std::endl;
    RUN_PACS_TEST(test_connection_lifecycle);
    RUN_PACS_TEST(test_reconnection);

    std::cout << "\n--- Statistics Tests ---" << std::endl;
    RUN_PACS_TEST(test_statistics_tracking);

    std::cout << "\n=== MWL Database Test Summary ===" << std::endl;
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
    return pacs::bridge::integration::test::run_all_mwl_database_tests();
}
