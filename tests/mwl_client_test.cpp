/**
 * @file mwl_client_test.cpp
 * @brief Comprehensive unit tests for MWL client module
 *
 * Tests for MWL client operations including connection management,
 * CRUD operations, query filtering, bulk operations, and statistics.
 * Target coverage: >= 80%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/17
 */

#include "pacs/bridge/pacs_adapter/mwl_client.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::pacs_adapter::test {

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
        if (test_func()) {                                                     \
            std::cout << "  PASSED" << std::endl;                              \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED" << std::endl;                              \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// Helper to create test MWL item
mapping::mwl_item create_test_mwl_item(const std::string& accession_number,
                                        const std::string& patient_id,
                                        const std::string& patient_name = "DOE^JOHN") {
    mapping::mwl_item item;

    // Patient info
    item.patient.patient_id = patient_id;
    item.patient.patient_name = patient_name;
    item.patient.patient_birth_date = "19800515";
    item.patient.patient_sex = "M";

    // Imaging service request
    item.imaging_service_request.accession_number = accession_number;
    item.imaging_service_request.requesting_physician = "SMITH^DR";
    item.imaging_service_request.requesting_service = "RADIOLOGY";

    // Requested procedure
    item.requested_procedure.requested_procedure_id = "RP001";
    item.requested_procedure.requested_procedure_description = "CT Chest";
    item.requested_procedure.referring_physician_name = "JONES^DR";

    // Scheduled procedure step
    mapping::dicom_scheduled_procedure_step sps;
    sps.scheduled_station_ae_title = "CT_SCANNER_1";
    sps.scheduled_start_date = "20241201";
    sps.scheduled_start_time = "090000";
    sps.modality = "CT";
    sps.scheduled_performing_physician = "DOC^RADIOLOGY";
    sps.scheduled_step_description = "CT Chest with contrast";
    sps.scheduled_step_id = "SPS001";
    sps.scheduled_step_status = "SCHEDULED";
    item.scheduled_steps.push_back(sps);

    return item;
}

// =============================================================================
// Error Code Tests
// =============================================================================

bool test_mwl_error_codes() {
    TEST_ASSERT(to_error_code(mwl_error::connection_failed) == -980,
                "connection_failed should be -980");
    TEST_ASSERT(to_error_code(mwl_error::add_failed) == -981,
                "add_failed should be -981");
    TEST_ASSERT(to_error_code(mwl_error::update_failed) == -982,
                "update_failed should be -982");
    TEST_ASSERT(to_error_code(mwl_error::cancel_failed) == -983,
                "cancel_failed should be -983");
    TEST_ASSERT(to_error_code(mwl_error::query_failed) == -984,
                "query_failed should be -984");
    TEST_ASSERT(to_error_code(mwl_error::entry_not_found) == -985,
                "entry_not_found should be -985");
    TEST_ASSERT(to_error_code(mwl_error::duplicate_entry) == -986,
                "duplicate_entry should be -986");
    TEST_ASSERT(to_error_code(mwl_error::invalid_data) == -987,
                "invalid_data should be -987");
    TEST_ASSERT(to_error_code(mwl_error::timeout) == -988,
                "timeout should be -988");
    TEST_ASSERT(to_error_code(mwl_error::association_rejected) == -989,
                "association_rejected should be -989");

    return true;
}

bool test_mwl_error_strings() {
    TEST_ASSERT(std::string(to_string(mwl_error::connection_failed)) ==
                    "Cannot connect to pacs_system",
                "Error message should match");
    TEST_ASSERT(std::string(to_string(mwl_error::entry_not_found)) ==
                    "MWL entry not found",
                "Error message should match");
    TEST_ASSERT(std::string(to_string(mwl_error::duplicate_entry)) ==
                    "Duplicate MWL entry exists",
                "Error message should match");

    return true;
}

// =============================================================================
// Configuration Tests
// =============================================================================

bool test_config_defaults() {
    mwl_client_config config;

    TEST_ASSERT(config.pacs_host == "localhost", "Default host should be localhost");
    TEST_ASSERT(config.pacs_port == 11112, "Default port should be 11112");
    TEST_ASSERT(config.our_ae_title == "PACS_BRIDGE", "Default AE title should be PACS_BRIDGE");
    TEST_ASSERT(config.pacs_ae_title == "PACS_SCP", "Default PACS AE should be PACS_SCP");
    TEST_ASSERT(config.connect_timeout == std::chrono::seconds{10}, "Default connect timeout");
    TEST_ASSERT(config.operation_timeout == std::chrono::seconds{30}, "Default operation timeout");
    TEST_ASSERT(config.max_retries == 3, "Default max retries should be 3");
    TEST_ASSERT(config.keep_alive, "Keep alive should be enabled by default");

    return true;
}

bool test_custom_config() {
    mwl_client_config config;
    config.pacs_host = "pacs.hospital.local";
    config.pacs_port = 11113;
    config.our_ae_title = "BRIDGE_01";
    config.pacs_ae_title = "PACS_01";

    mwl_client client(config);

    TEST_ASSERT(client.config().pacs_host == "pacs.hospital.local", "Host should match");
    TEST_ASSERT(client.config().pacs_port == 11113, "Port should match");
    TEST_ASSERT(client.config().our_ae_title == "BRIDGE_01", "Our AE should match");
    TEST_ASSERT(client.config().pacs_ae_title == "PACS_01", "PACS AE should match");

    return true;
}

// =============================================================================
// Connection Tests
// =============================================================================

bool test_connect_disconnect() {
    mwl_client_config config;
    mwl_client client(config);

    TEST_ASSERT(!client.is_connected(), "Should not be connected initially");

    auto result = client.connect();
    TEST_ASSERT(result.has_value(), "Connect should succeed");
    TEST_ASSERT(client.is_connected(), "Should be connected after connect()");

    client.disconnect();
    TEST_ASSERT(!client.is_connected(), "Should not be connected after disconnect()");

    return true;
}

bool test_reconnect() {
    mwl_client_config config;
    mwl_client client(config);

    client.connect();
    TEST_ASSERT(client.is_connected(), "Should be connected");

    auto result = client.reconnect();
    TEST_ASSERT(result.has_value(), "Reconnect should succeed");
    TEST_ASSERT(client.is_connected(), "Should be connected after reconnect");

    return true;
}

bool test_multiple_connect_calls() {
    mwl_client_config config;
    mwl_client client(config);

    auto result1 = client.connect();
    TEST_ASSERT(result1.has_value(), "First connect should succeed");

    auto result2 = client.connect();
    TEST_ASSERT(result2.has_value(), "Second connect should also succeed (idempotent)");

    TEST_ASSERT(client.is_connected(), "Should be connected");

    return true;
}

// =============================================================================
// Add Entry Tests
// =============================================================================

bool test_add_entry_success() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto item = create_test_mwl_item("ACC001", "PAT001");

    auto result = client.add_entry(item);
    TEST_ASSERT(result.has_value(), "Add entry should succeed");
    TEST_ASSERT(result->dicom_status == 0x0000, "DICOM status should be success");
    TEST_ASSERT(result->retry_count == 0, "Should not need retries");

    return true;
}

bool test_add_entry_duplicate() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto item = create_test_mwl_item("ACC002", "PAT002");

    auto result1 = client.add_entry(item);
    TEST_ASSERT(result1.has_value(), "First add should succeed");

    auto result2 = client.add_entry(item);
    TEST_ASSERT(!result2.has_value(), "Second add should fail (duplicate)");
    TEST_ASSERT(result2.error() == mwl_error::duplicate_entry, "Should be duplicate error");

    return true;
}

bool test_add_entry_invalid_data() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    mapping::mwl_item item;  // Empty item with no accession number

    auto result = client.add_entry(item);
    TEST_ASSERT(!result.has_value(), "Add with invalid data should fail");
    TEST_ASSERT(result.error() == mwl_error::invalid_data, "Should be invalid data error");

    return true;
}

bool test_add_entry_without_connection() {
    mwl_client_config config;
    mwl_client client(config);
    // Don't connect

    auto item = create_test_mwl_item("ACC003", "PAT003");

    // Should auto-connect
    auto result = client.add_entry(item);
    TEST_ASSERT(result.has_value(), "Add should succeed with auto-connect");

    return true;
}

// =============================================================================
// Update Entry Tests
// =============================================================================

bool test_update_entry_success() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto item = create_test_mwl_item("ACC010", "PAT010");
    client.add_entry(item);

    // Update with new data
    mapping::mwl_item updates;
    updates.patient.patient_name = "SMITH^JANE";
    updates.requested_procedure.referring_physician_name = "BROWN^DR";

    auto result = client.update_entry("ACC010", updates);
    TEST_ASSERT(result.has_value(), "Update should succeed");
    TEST_ASSERT(result->dicom_status == 0x0000, "DICOM status should be success");

    // Verify update was applied
    auto get_result = client.get_entry("ACC010");
    TEST_ASSERT(get_result.has_value(), "Get should succeed");
    TEST_ASSERT(get_result->patient.patient_name == "SMITH^JANE", "Name should be updated");

    return true;
}

bool test_update_entry_not_found() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    mapping::mwl_item updates;
    updates.patient.patient_name = "NEW^NAME";

    auto result = client.update_entry("NONEXISTENT", updates);
    TEST_ASSERT(!result.has_value(), "Update should fail for non-existent entry");
    TEST_ASSERT(result.error() == mwl_error::entry_not_found, "Should be not found error");

    return true;
}

bool test_update_entry_invalid_accession() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    mapping::mwl_item updates;

    auto result = client.update_entry("", updates);
    TEST_ASSERT(!result.has_value(), "Update with empty accession should fail");
    TEST_ASSERT(result.error() == mwl_error::invalid_data, "Should be invalid data error");

    return true;
}

// =============================================================================
// Cancel Entry Tests
// =============================================================================

bool test_cancel_entry_success() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto item = create_test_mwl_item("ACC020", "PAT020");
    client.add_entry(item);

    TEST_ASSERT(client.exists("ACC020"), "Entry should exist before cancel");

    auto result = client.cancel_entry("ACC020");
    TEST_ASSERT(result.has_value(), "Cancel should succeed");

    TEST_ASSERT(!client.exists("ACC020"), "Entry should not exist after cancel");

    return true;
}

bool test_cancel_entry_not_found() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto result = client.cancel_entry("NONEXISTENT");
    TEST_ASSERT(!result.has_value(), "Cancel should fail for non-existent entry");
    TEST_ASSERT(result.error() == mwl_error::entry_not_found, "Should be not found error");

    return true;
}

// =============================================================================
// Query Tests
// =============================================================================

bool test_query_all() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    // Add multiple entries
    client.add_entry(create_test_mwl_item("QRY001", "PAT101", "ALPHA^ONE"));
    client.add_entry(create_test_mwl_item("QRY002", "PAT102", "BETA^TWO"));
    client.add_entry(create_test_mwl_item("QRY003", "PAT103", "GAMMA^THREE"));

    mwl_query_filter filter;  // Empty filter = return all
    auto result = client.query(filter);

    TEST_ASSERT(result.has_value(), "Query should succeed");
    TEST_ASSERT(result->items.size() >= 3, "Should return at least 3 items");

    return true;
}

bool test_query_by_patient_id() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    client.add_entry(create_test_mwl_item("QRY010", "UNIQUE001", "TEST^PATIENT"));

    mwl_query_filter filter;
    filter.patient_id = "UNIQUE001";

    auto result = client.query(filter);
    TEST_ASSERT(result.has_value(), "Query should succeed");
    TEST_ASSERT(result->items.size() == 1, "Should return exactly 1 item");
    TEST_ASSERT(result->items[0].patient.patient_id == "UNIQUE001", "Patient ID should match");

    return true;
}

bool test_query_by_accession_number() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    client.add_entry(create_test_mwl_item("UNIQUE_ACC", "PAT200"));

    mwl_query_filter filter;
    filter.accession_number = "UNIQUE_ACC";

    auto result = client.query(filter);
    TEST_ASSERT(result.has_value(), "Query should succeed");
    TEST_ASSERT(result->items.size() == 1, "Should return exactly 1 item");
    TEST_ASSERT(result->items[0].imaging_service_request.accession_number == "UNIQUE_ACC",
                "Accession number should match");

    return true;
}

bool test_query_by_modality() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    // Add CT entry
    auto ct_item = create_test_mwl_item("MOD001", "PAT301");
    ct_item.scheduled_steps[0].modality = "CT";
    client.add_entry(ct_item);

    // Add MR entry
    auto mr_item = create_test_mwl_item("MOD002", "PAT302");
    mr_item.scheduled_steps[0].modality = "MR";
    client.add_entry(mr_item);

    mwl_query_filter filter;
    filter.modality = "CT";

    auto result = client.query(filter);
    TEST_ASSERT(result.has_value(), "Query should succeed");
    TEST_ASSERT(result->items.size() >= 1, "Should return at least 1 CT item");

    for (const auto& item : result->items) {
        if (!item.scheduled_steps.empty()) {
            TEST_ASSERT(item.scheduled_steps[0].modality == "CT",
                        "All results should be CT modality");
        }
    }

    return true;
}

bool test_query_by_scheduled_date() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto item1 = create_test_mwl_item("DATE001", "PAT401");
    item1.scheduled_steps[0].scheduled_start_date = "20241215";
    client.add_entry(item1);

    auto item2 = create_test_mwl_item("DATE002", "PAT402");
    item2.scheduled_steps[0].scheduled_start_date = "20241216";
    client.add_entry(item2);

    mwl_query_filter filter;
    filter.scheduled_date = "20241215";

    auto result = client.query(filter);
    TEST_ASSERT(result.has_value(), "Query should succeed");
    TEST_ASSERT(result->items.size() >= 1, "Should return at least 1 item");

    return true;
}

bool test_query_with_max_results() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    // Add 5 entries
    for (int i = 0; i < 5; i++) {
        auto item = create_test_mwl_item("MAX" + std::to_string(i), "PAT50" + std::to_string(i));
        item.scheduled_steps[0].modality = "US";
        client.add_entry(item);
    }

    mwl_query_filter filter;
    filter.modality = "US";
    filter.max_results = 3;

    auto result = client.query(filter);
    TEST_ASSERT(result.has_value(), "Query should succeed");
    TEST_ASSERT(result->items.size() <= 3, "Should return at most 3 items");

    return true;
}

bool test_query_with_wildcard() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    client.add_entry(create_test_mwl_item("WILD001", "PAT601", "SMITH^JOHN"));
    client.add_entry(create_test_mwl_item("WILD002", "PAT602", "SMITH^JANE"));
    client.add_entry(create_test_mwl_item("WILD003", "PAT603", "JONES^MARY"));

    mwl_query_filter filter;
    filter.patient_name = "SMITH*";  // Wildcard prefix match

    auto result = client.query(filter);
    TEST_ASSERT(result.has_value(), "Query should succeed");
    TEST_ASSERT(result->items.size() >= 2, "Should return at least 2 SMITH entries");

    return true;
}

bool test_query_with_mwl_item() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto item = create_test_mwl_item("ITEM_QRY", "PAT700");
    item.scheduled_steps[0].modality = "XR";
    client.add_entry(item);

    mapping::mwl_item query_template;
    query_template.scheduled_steps.push_back({});
    query_template.scheduled_steps[0].modality = "XR";

    auto result = client.query(query_template);
    TEST_ASSERT(result.has_value(), "Query should succeed");
    TEST_ASSERT(result->items.size() >= 1, "Should return at least 1 XR item");

    return true;
}

// =============================================================================
// Exists and Get Entry Tests
// =============================================================================

bool test_exists() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    TEST_ASSERT(!client.exists("NONEXISTENT_ACC"), "Should not exist initially");

    auto item = create_test_mwl_item("EXISTS001", "PAT800");
    client.add_entry(item);

    TEST_ASSERT(client.exists("EXISTS001"), "Should exist after adding");

    client.cancel_entry("EXISTS001");
    TEST_ASSERT(!client.exists("EXISTS001"), "Should not exist after canceling");

    return true;
}

bool test_get_entry_success() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto item = create_test_mwl_item("GET001", "PAT900", "RETRIEVAL^TEST");
    client.add_entry(item);

    auto result = client.get_entry("GET001");
    TEST_ASSERT(result.has_value(), "Get should succeed");
    TEST_ASSERT(result->imaging_service_request.accession_number == "GET001",
                "Accession should match");
    TEST_ASSERT(result->patient.patient_id == "PAT900", "Patient ID should match");
    TEST_ASSERT(result->patient.patient_name == "RETRIEVAL^TEST", "Patient name should match");

    return true;
}

bool test_get_entry_not_found() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    auto result = client.get_entry("NONEXISTENT");
    TEST_ASSERT(!result.has_value(), "Get should fail for non-existent entry");
    TEST_ASSERT(result.error() == mwl_error::entry_not_found, "Should be not found error");

    return true;
}

// =============================================================================
// Bulk Operations Tests
// =============================================================================

bool test_add_entries_bulk() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    std::vector<mapping::mwl_item> items;
    for (int i = 0; i < 5; i++) {
        items.push_back(create_test_mwl_item("BULK" + std::to_string(i),
                                              "BULKPAT" + std::to_string(i)));
    }

    auto result = client.add_entries(items, true);
    TEST_ASSERT(result.has_value(), "Bulk add should succeed");
    TEST_ASSERT(*result == 5, "Should add all 5 items");

    // Verify all were added
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(client.exists("BULK" + std::to_string(i)), "Item should exist");
    }

    return true;
}

bool test_add_entries_with_duplicates() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    // Pre-add one entry
    client.add_entry(create_test_mwl_item("DUPE0", "DUPEPAT0"));

    std::vector<mapping::mwl_item> items;
    items.push_back(create_test_mwl_item("DUPE0", "DUPEPAT0"));  // Duplicate
    items.push_back(create_test_mwl_item("DUPE1", "DUPEPAT1"));  // New
    items.push_back(create_test_mwl_item("DUPE2", "DUPEPAT2"));  // New

    auto result = client.add_entries(items, true);  // Continue on error
    TEST_ASSERT(result.has_value(), "Bulk add should succeed with continue_on_error");
    TEST_ASSERT(*result == 2, "Should add 2 new items (1 duplicate skipped)");

    return true;
}

bool test_cancel_entries_before() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    // Add entries with different dates
    auto old_item = create_test_mwl_item("OLD001", "OLDPAT001");
    old_item.scheduled_steps[0].scheduled_start_date = "20231115";
    client.add_entry(old_item);

    auto recent_item = create_test_mwl_item("RECENT001", "RECENTPAT001");
    recent_item.scheduled_steps[0].scheduled_start_date = "20241215";
    client.add_entry(recent_item);

    auto result = client.cancel_entries_before("20241201");
    TEST_ASSERT(result.has_value(), "Cancel before should succeed");
    TEST_ASSERT(*result >= 1, "Should cancel at least 1 old entry");

    TEST_ASSERT(!client.exists("OLD001"), "Old entry should be cancelled");
    TEST_ASSERT(client.exists("RECENT001"), "Recent entry should remain");

    return true;
}

// =============================================================================
// Statistics Tests
// =============================================================================

bool test_statistics() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    // Perform various operations
    client.add_entry(create_test_mwl_item("STAT001", "STATPAT001"));
    client.add_entry(create_test_mwl_item("STAT002", "STATPAT002"));

    mapping::mwl_item updates;
    updates.patient.patient_name = "UPDATED^NAME";
    client.update_entry("STAT001", updates);

    client.cancel_entry("STAT002");

    mwl_query_filter filter;
    client.query(filter);

    auto stats = client.get_statistics();
    TEST_ASSERT(stats.add_count >= 2, "Should have at least 2 adds");
    TEST_ASSERT(stats.update_count >= 1, "Should have at least 1 update");
    TEST_ASSERT(stats.cancel_count >= 1, "Should have at least 1 cancel");
    TEST_ASSERT(stats.query_count >= 1, "Should have at least 1 query");
    TEST_ASSERT(stats.connect_successes >= 1, "Should have at least 1 successful connection");

    return true;
}

bool test_reset_statistics() {
    mwl_client_config config;
    mwl_client client(config);
    client.connect();

    client.add_entry(create_test_mwl_item("RESET001", "RESETPAT001"));

    auto stats_before = client.get_statistics();
    TEST_ASSERT(stats_before.add_count >= 1, "Should have adds before reset");

    client.reset_statistics();

    auto stats_after = client.get_statistics();
    TEST_ASSERT(stats_after.add_count == 0, "Add count should be 0 after reset");
    TEST_ASSERT(stats_after.query_count == 0, "Query count should be 0 after reset");

    return true;
}

// =============================================================================
// Move Semantics Tests
// =============================================================================

bool test_move_constructor() {
    mwl_client_config config;
    mwl_client client1(config);
    client1.connect();
    client1.add_entry(create_test_mwl_item("MOVE001", "MOVEPAT001"));

    mwl_client client2(std::move(client1));

    // client2 should have the state
    TEST_ASSERT(client2.exists("MOVE001"), "Moved client should have the entry");

    return true;
}

bool test_move_assignment() {
    mwl_client_config config;
    mwl_client client1(config);
    client1.connect();
    client1.add_entry(create_test_mwl_item("MOVE002", "MOVEPAT002"));

    mwl_client client2(config);
    client2 = std::move(client1);

    TEST_ASSERT(client2.exists("MOVE002"), "Moved client should have the entry");

    return true;
}

}  // namespace pacs::bridge::pacs_adapter::test

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    using namespace pacs::bridge::pacs_adapter::test;

    int passed = 0;
    int failed = 0;

    std::cout << "\n========================================" << std::endl;
    std::cout << "MWL Client Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Error Code Tests
    std::cout << "--- Error Code Tests ---" << std::endl;
    RUN_TEST(test_mwl_error_codes);
    RUN_TEST(test_mwl_error_strings);

    // Configuration Tests
    std::cout << "\n--- Configuration Tests ---" << std::endl;
    RUN_TEST(test_config_defaults);
    RUN_TEST(test_custom_config);

    // Connection Tests
    std::cout << "\n--- Connection Tests ---" << std::endl;
    RUN_TEST(test_connect_disconnect);
    RUN_TEST(test_reconnect);
    RUN_TEST(test_multiple_connect_calls);

    // Add Entry Tests
    std::cout << "\n--- Add Entry Tests ---" << std::endl;
    RUN_TEST(test_add_entry_success);
    RUN_TEST(test_add_entry_duplicate);
    RUN_TEST(test_add_entry_invalid_data);
    RUN_TEST(test_add_entry_without_connection);

    // Update Entry Tests
    std::cout << "\n--- Update Entry Tests ---" << std::endl;
    RUN_TEST(test_update_entry_success);
    RUN_TEST(test_update_entry_not_found);
    RUN_TEST(test_update_entry_invalid_accession);

    // Cancel Entry Tests
    std::cout << "\n--- Cancel Entry Tests ---" << std::endl;
    RUN_TEST(test_cancel_entry_success);
    RUN_TEST(test_cancel_entry_not_found);

    // Query Tests
    std::cout << "\n--- Query Tests ---" << std::endl;
    RUN_TEST(test_query_all);
    RUN_TEST(test_query_by_patient_id);
    RUN_TEST(test_query_by_accession_number);
    RUN_TEST(test_query_by_modality);
    RUN_TEST(test_query_by_scheduled_date);
    RUN_TEST(test_query_with_max_results);
    RUN_TEST(test_query_with_wildcard);
    RUN_TEST(test_query_with_mwl_item);

    // Exists and Get Entry Tests
    std::cout << "\n--- Exists and Get Entry Tests ---" << std::endl;
    RUN_TEST(test_exists);
    RUN_TEST(test_get_entry_success);
    RUN_TEST(test_get_entry_not_found);

    // Bulk Operations Tests
    std::cout << "\n--- Bulk Operations Tests ---" << std::endl;
    RUN_TEST(test_add_entries_bulk);
    RUN_TEST(test_add_entries_with_duplicates);
    RUN_TEST(test_cancel_entries_before);

    // Statistics Tests
    std::cout << "\n--- Statistics Tests ---" << std::endl;
    RUN_TEST(test_statistics);
    RUN_TEST(test_reset_statistics);

    // Move Semantics Tests
    std::cout << "\n--- Move Semantics Tests ---" << std::endl;
    RUN_TEST(test_move_constructor);
    RUN_TEST(test_move_assignment);

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test Results: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "========================================\n" << std::endl;

    return failed > 0 ? 1 : 0;
}
