/**
 * @file mpps_handler_test.cpp
 * @brief Comprehensive unit tests for MPPS handler module
 *
 * Tests for MPPS handler operations including lifecycle management,
 * event handling, callback invocation, and statistics.
 * Target coverage: >= 80%
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/23
 */

#include "pacs/bridge/pacs_adapter/mpps_handler.h"

#include <atomic>
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

/**
 * @brief Create a test MPPS dataset with basic attributes
 */
mpps_dataset create_test_mpps_dataset(
    const std::string& sop_instance_uid,
    const std::string& accession_number,
    mpps_event status = mpps_event::in_progress) {

    mpps_dataset dataset;

    // SOP Instance identification
    dataset.sop_instance_uid = sop_instance_uid;

    // Relationship
    dataset.study_instance_uid = "1.2.840.10008.5.1.4.1.1.2.1";
    dataset.accession_number = accession_number;
    dataset.scheduled_procedure_step_id = "SPS001";
    dataset.performed_procedure_step_id = "PPS001";

    // Patient
    dataset.patient_id = "PAT001";
    dataset.patient_name = "DOE^JOHN";

    // Status
    dataset.status = status;
    dataset.performed_procedure_description = "CT Chest with contrast";

    // Timing
    dataset.start_date = "20241201";
    dataset.start_time = "090000";

    if (status != mpps_event::in_progress) {
        dataset.end_date = "20241201";
        dataset.end_time = "093000";
    }

    // Modality and Station
    dataset.modality = "CT";
    dataset.station_ae_title = "CT_SCANNER_1";
    dataset.station_name = "CT Scanner Room 1";

    // Performed series
    mpps_performed_series series;
    series.series_instance_uid = "1.2.840.10008.5.1.4.1.1.2.1.1";
    series.series_description = "Chest CT Series 1";
    series.modality = "CT";
    series.number_of_instances = 150;
    series.performing_physician = "RADIOLOGIST^DR";
    dataset.performed_series.push_back(series);

    // Additional
    dataset.referring_physician = "JONES^DR";
    dataset.requested_procedure_id = "RP001";

    return dataset;
}

// =============================================================================
// Error Code Tests
// =============================================================================

bool test_mpps_error_codes() {
    TEST_ASSERT(to_error_code(mpps_error::connection_failed) == -970,
                "connection_failed should be -970");
    TEST_ASSERT(to_error_code(mpps_error::registration_failed) == -971,
                "registration_failed should be -971");
    TEST_ASSERT(to_error_code(mpps_error::invalid_dataset) == -972,
                "invalid_dataset should be -972");
    TEST_ASSERT(to_error_code(mpps_error::status_parse_failed) == -973,
                "status_parse_failed should be -973");
    TEST_ASSERT(to_error_code(mpps_error::missing_attribute) == -974,
                "missing_attribute should be -974");
    TEST_ASSERT(to_error_code(mpps_error::callback_failed) == -975,
                "callback_failed should be -975");
    TEST_ASSERT(to_error_code(mpps_error::not_registered) == -976,
                "not_registered should be -976");
    TEST_ASSERT(to_error_code(mpps_error::already_registered) == -977,
                "already_registered should be -977");
    TEST_ASSERT(to_error_code(mpps_error::invalid_sop_instance) == -978,
                "invalid_sop_instance should be -978");
    TEST_ASSERT(to_error_code(mpps_error::unexpected_operation) == -979,
                "unexpected_operation should be -979");

    return true;
}

bool test_mpps_error_to_string() {
    TEST_ASSERT(std::string(to_string(mpps_error::connection_failed)) ==
                "Cannot connect to pacs_system MPPS SCP",
                "connection_failed string mismatch");
    TEST_ASSERT(std::string(to_string(mpps_error::registration_failed)) ==
                "Registration with MPPS SCP failed",
                "registration_failed string mismatch");
    TEST_ASSERT(std::string(to_string(mpps_error::invalid_dataset)) ==
                "Invalid MPPS dataset received",
                "invalid_dataset string mismatch");
    TEST_ASSERT(std::string(to_string(mpps_error::missing_attribute)) ==
                "Missing required attribute in MPPS",
                "missing_attribute string mismatch");

    return true;
}

// =============================================================================
// Event Type Tests
// =============================================================================

bool test_mpps_event_to_string() {
    TEST_ASSERT(std::string(to_string(mpps_event::in_progress)) == "IN PROGRESS",
                "in_progress string mismatch");
    TEST_ASSERT(std::string(to_string(mpps_event::completed)) == "COMPLETED",
                "completed string mismatch");
    TEST_ASSERT(std::string(to_string(mpps_event::discontinued)) == "DISCONTINUED",
                "discontinued string mismatch");

    return true;
}

bool test_parse_mpps_status() {
    auto result1 = parse_mpps_status("IN PROGRESS");
    TEST_ASSERT(result1.has_value(), "Should parse IN PROGRESS");
    TEST_ASSERT(*result1 == mpps_event::in_progress, "Should be in_progress");

    auto result2 = parse_mpps_status("COMPLETED");
    TEST_ASSERT(result2.has_value(), "Should parse COMPLETED");
    TEST_ASSERT(*result2 == mpps_event::completed, "Should be completed");

    auto result3 = parse_mpps_status("DISCONTINUED");
    TEST_ASSERT(result3.has_value(), "Should parse DISCONTINUED");
    TEST_ASSERT(*result3 == mpps_event::discontinued, "Should be discontinued");

    auto result4 = parse_mpps_status("UNKNOWN");
    TEST_ASSERT(!result4.has_value(), "Should not parse UNKNOWN");

    auto result5 = parse_mpps_status("in progress");
    TEST_ASSERT(!result5.has_value(), "Should not parse lowercase");

    return true;
}

// =============================================================================
// Dataset Tests
// =============================================================================

bool test_mpps_dataset_creation() {
    mpps_dataset dataset = create_test_mpps_dataset("1.2.3.4.5", "ACC001");

    TEST_ASSERT(dataset.sop_instance_uid == "1.2.3.4.5",
                "SOP Instance UID should match");
    TEST_ASSERT(dataset.accession_number == "ACC001",
                "Accession number should match");
    TEST_ASSERT(dataset.status == mpps_event::in_progress,
                "Default status should be in_progress");
    TEST_ASSERT(dataset.performed_series.size() == 1,
                "Should have one performed series");

    return true;
}

bool test_mpps_dataset_total_instances() {
    mpps_dataset dataset;

    mpps_performed_series series1;
    series1.number_of_instances = 50;
    dataset.performed_series.push_back(series1);

    mpps_performed_series series2;
    series2.number_of_instances = 100;
    dataset.performed_series.push_back(series2);

    TEST_ASSERT(dataset.total_instances() == 150,
                "Total instances should be 150");

    // Empty dataset
    mpps_dataset empty_dataset;
    TEST_ASSERT(empty_dataset.total_instances() == 0,
                "Empty dataset should have 0 instances");

    return true;
}

bool test_mpps_dataset_status_methods() {
    mpps_dataset dataset;

    dataset.status = mpps_event::in_progress;
    TEST_ASSERT(!dataset.is_completed(), "in_progress should not be completed");
    TEST_ASSERT(!dataset.is_discontinued(), "in_progress should not be discontinued");

    dataset.status = mpps_event::completed;
    TEST_ASSERT(dataset.is_completed(), "completed should be completed");
    TEST_ASSERT(!dataset.is_discontinued(), "completed should not be discontinued");

    dataset.status = mpps_event::discontinued;
    TEST_ASSERT(!dataset.is_completed(), "discontinued should not be completed");
    TEST_ASSERT(dataset.is_discontinued(), "discontinued should be discontinued");

    return true;
}

bool test_mpps_dataset_timing() {
    mpps_dataset dataset;

    // No timing
    TEST_ASSERT(!dataset.has_complete_timing(), "Empty should have incomplete timing");

    // Start only (valid for in_progress)
    dataset.status = mpps_event::in_progress;
    dataset.start_date = "20241201";
    dataset.start_time = "090000";
    TEST_ASSERT(dataset.has_complete_timing(), "in_progress with start should have complete timing");

    // Completed requires end time
    dataset.status = mpps_event::completed;
    TEST_ASSERT(!dataset.has_complete_timing(), "completed without end should have incomplete timing");

    dataset.end_date = "20241201";
    dataset.end_time = "093000";
    TEST_ASSERT(dataset.has_complete_timing(), "completed with end should have complete timing");

    return true;
}

// =============================================================================
// Validation Tests
// =============================================================================

bool test_validate_mpps_dataset() {
    // Valid dataset
    mpps_dataset valid = create_test_mpps_dataset("1.2.3.4.5", "ACC001");
    auto result1 = validate_mpps_dataset(valid);
    TEST_ASSERT(result1.has_value(), "Valid dataset should pass validation");

    // Missing SOP Instance UID
    mpps_dataset no_sop;
    no_sop.accession_number = "ACC001";
    auto result2 = validate_mpps_dataset(no_sop);
    TEST_ASSERT(!result2.has_value(), "Missing SOP UID should fail validation");
    TEST_ASSERT(result2.error() == mpps_error::missing_attribute,
                "Should return missing_attribute error");

    // Missing both accession and SPS ID
    mpps_dataset no_id;
    no_id.sop_instance_uid = "1.2.3.4.5";
    auto result3 = validate_mpps_dataset(no_id);
    TEST_ASSERT(!result3.has_value(), "Missing IDs should fail validation");

    // Has SPS ID but no accession
    mpps_dataset has_sps;
    has_sps.sop_instance_uid = "1.2.3.4.5";
    has_sps.scheduled_procedure_step_id = "SPS001";
    auto result4 = validate_mpps_dataset(has_sps);
    TEST_ASSERT(result4.has_value(), "Having SPS ID should pass validation");

    return true;
}

// =============================================================================
// Duration Calculation Tests
// =============================================================================

bool test_calculate_procedure_duration() {
    mpps_dataset dataset;

    // No timing info
    auto result1 = calculate_procedure_duration(dataset);
    TEST_ASSERT(!result1.has_value(), "No timing should return nullopt");

    // Only start timing
    dataset.start_date = "20241201";
    dataset.start_time = "090000";
    auto result2 = calculate_procedure_duration(dataset);
    TEST_ASSERT(!result2.has_value(), "Incomplete timing should return nullopt");

    // Complete timing - 30 minutes
    dataset.end_date = "20241201";
    dataset.end_time = "093000";
    auto result3 = calculate_procedure_duration(dataset);
    TEST_ASSERT(result3.has_value(), "Complete timing should return duration");
    TEST_ASSERT(result3->count() == 1800, "Duration should be 1800 seconds (30 min)");

    // 1 hour duration
    dataset.end_time = "100000";
    auto result4 = calculate_procedure_duration(dataset);
    TEST_ASSERT(result4.has_value(), "Should calculate 1 hour duration");
    TEST_ASSERT(result4->count() == 3600, "Duration should be 3600 seconds (1 hour)");

    return true;
}

bool test_calculate_duration_invalid_format() {
    mpps_dataset dataset;

    // Invalid date format
    dataset.start_date = "2024";
    dataset.start_time = "090000";
    dataset.end_date = "20241201";
    dataset.end_time = "093000";
    auto result1 = calculate_procedure_duration(dataset);
    TEST_ASSERT(!result1.has_value(), "Invalid date format should return nullopt");

    // Invalid time format
    dataset.start_date = "20241201";
    dataset.start_time = "09";
    auto result2 = calculate_procedure_duration(dataset);
    TEST_ASSERT(!result2.has_value(), "Invalid time format should return nullopt");

    return true;
}

// =============================================================================
// Configuration Tests
// =============================================================================

bool test_mpps_handler_config_defaults() {
    mpps_handler_config config;

    TEST_ASSERT(config.pacs_host == "localhost", "Default host should be localhost");
    TEST_ASSERT(config.pacs_port == 11113, "Default port should be 11113");
    TEST_ASSERT(config.our_ae_title == "PACS_BRIDGE", "Default AE title should be PACS_BRIDGE");
    TEST_ASSERT(config.pacs_ae_title == "MPPS_SCP", "Default PACS AE should be MPPS_SCP");
    TEST_ASSERT(config.auto_reconnect == true, "Auto reconnect should be true by default");
    TEST_ASSERT(config.max_reconnect_attempts == 0, "Max reconnect attempts should be 0 (unlimited)");
    TEST_ASSERT(config.verbose_logging == false, "Verbose logging should be false by default");

    return true;
}

bool test_mpps_handler_config_custom() {
    mpps_handler_config config;
    config.pacs_host = "pacs.hospital.local";
    config.pacs_port = 11115;
    config.our_ae_title = "CUSTOM_BRIDGE";
    config.pacs_ae_title = "CUSTOM_MPPS";
    config.auto_reconnect = false;
    config.max_reconnect_attempts = 5;
    config.verbose_logging = true;

    TEST_ASSERT(config.pacs_host == "pacs.hospital.local", "Custom host mismatch");
    TEST_ASSERT(config.pacs_port == 11115, "Custom port mismatch");
    TEST_ASSERT(config.our_ae_title == "CUSTOM_BRIDGE", "Custom AE title mismatch");
    TEST_ASSERT(config.auto_reconnect == false, "Auto reconnect mismatch");
    TEST_ASSERT(config.max_reconnect_attempts == 5, "Max reconnect attempts mismatch");
    TEST_ASSERT(config.verbose_logging == true, "Verbose logging mismatch");

    return true;
}

// =============================================================================
// Handler Lifecycle Tests
// =============================================================================

bool test_handler_creation() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    TEST_ASSERT(handler != nullptr, "Handler should be created");
    TEST_ASSERT(!handler->is_running(), "Handler should not be running initially");
    TEST_ASSERT(!handler->is_connected(), "Handler should not be connected initially");
    TEST_ASSERT(!handler->has_callback(), "Handler should not have callback initially");

    return true;
}

bool test_handler_start_stop() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    // Start handler
    auto start_result = handler->start();
    TEST_ASSERT(start_result.has_value(), "Handler should start successfully");
    TEST_ASSERT(handler->is_running(), "Handler should be running after start");
    TEST_ASSERT(handler->is_connected(), "Handler should be connected after start");

    // Double start should fail
    auto double_start = handler->start();
    TEST_ASSERT(!double_start.has_value(), "Double start should fail");
    TEST_ASSERT(double_start.error() == mpps_error::already_registered,
                "Should return already_registered error");

    // Stop handler
    handler->stop(true);
    TEST_ASSERT(!handler->is_running(), "Handler should not be running after stop");

    return true;
}

bool test_handler_callback_management() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    TEST_ASSERT(!handler->has_callback(), "Should not have callback initially");

    // Set callback
    std::atomic<int> callback_count{0};
    handler->set_callback([&callback_count](mpps_event event, const mpps_dataset& mpps) {
        callback_count++;
    });

    TEST_ASSERT(handler->has_callback(), "Should have callback after set");

    // Clear callback
    handler->clear_callback();
    TEST_ASSERT(!handler->has_callback(), "Should not have callback after clear");

    return true;
}

// =============================================================================
// Event Handler Tests
// =============================================================================

bool test_handler_on_n_create() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    std::atomic<int> callback_count{0};
    mpps_event last_event;
    std::string last_accession;

    handler->set_callback([&](mpps_event event, const mpps_dataset& mpps) {
        callback_count++;
        last_event = event;
        last_accession = mpps.accession_number;
    });

    handler->start();

    // Create test dataset
    mpps_dataset dataset = create_test_mpps_dataset("1.2.3.4.5", "ACC001");
    auto result = handler->on_n_create(dataset);

    TEST_ASSERT(result.has_value(), "N-CREATE should succeed");
    TEST_ASSERT(callback_count == 1, "Callback should be called once");
    TEST_ASSERT(last_event == mpps_event::in_progress, "Event should be in_progress");
    TEST_ASSERT(last_accession == "ACC001", "Accession should match");

    handler->stop();
    return true;
}

bool test_handler_on_n_set_completed() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    std::atomic<int> callback_count{0};
    mpps_event last_event;

    handler->set_callback([&](mpps_event event, const mpps_dataset& mpps) {
        callback_count++;
        last_event = event;
    });

    handler->start();

    // Create completed dataset
    mpps_dataset dataset = create_test_mpps_dataset("1.2.3.4.5", "ACC001", mpps_event::completed);
    auto result = handler->on_n_set(dataset);

    TEST_ASSERT(result.has_value(), "N-SET should succeed");
    TEST_ASSERT(callback_count == 1, "Callback should be called once");
    TEST_ASSERT(last_event == mpps_event::completed, "Event should be completed");

    handler->stop();
    return true;
}

bool test_handler_on_n_set_discontinued() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    std::atomic<int> callback_count{0};
    mpps_event last_event;
    std::string discontinuation_reason;

    handler->set_callback([&](mpps_event event, const mpps_dataset& mpps) {
        callback_count++;
        last_event = event;
        discontinuation_reason = mpps.discontinuation_reason;
    });

    handler->start();

    // Create discontinued dataset
    mpps_dataset dataset = create_test_mpps_dataset("1.2.3.4.5", "ACC001", mpps_event::discontinued);
    dataset.discontinuation_reason = "Patient refused";
    auto result = handler->on_n_set(dataset);

    TEST_ASSERT(result.has_value(), "N-SET should succeed");
    TEST_ASSERT(callback_count == 1, "Callback should be called once");
    TEST_ASSERT(last_event == mpps_event::discontinued, "Event should be discontinued");
    TEST_ASSERT(discontinuation_reason == "Patient refused", "Reason should match");

    handler->stop();
    return true;
}

bool test_handler_invalid_dataset() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    std::atomic<int> callback_count{0};
    handler->set_callback([&](mpps_event event, const mpps_dataset& mpps) {
        callback_count++;
    });

    handler->start();

    // Create invalid dataset (missing SOP Instance UID)
    mpps_dataset invalid_dataset;
    invalid_dataset.accession_number = "ACC001";

    auto result = handler->on_n_create(invalid_dataset);

    TEST_ASSERT(!result.has_value(), "Invalid dataset should fail");
    TEST_ASSERT(result.error() == mpps_error::missing_attribute,
                "Should return missing_attribute error");
    TEST_ASSERT(callback_count == 0, "Callback should not be called for invalid dataset");

    handler->stop();
    return true;
}

bool test_handler_no_callback() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    handler->start();

    // N-CREATE without callback should succeed
    mpps_dataset dataset = create_test_mpps_dataset("1.2.3.4.5", "ACC001");
    auto result = handler->on_n_create(dataset);

    TEST_ASSERT(result.has_value(), "N-CREATE without callback should succeed");

    handler->stop();
    return true;
}

// =============================================================================
// Statistics Tests
// =============================================================================

bool test_handler_statistics() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    handler->set_callback([](mpps_event event, const mpps_dataset& mpps) {
        // Empty callback
    });

    handler->start();

    // Initial statistics
    auto stats1 = handler->get_statistics();
    TEST_ASSERT(stats1.n_create_count == 0, "Initial n_create_count should be 0");
    TEST_ASSERT(stats1.n_set_count == 0, "Initial n_set_count should be 0");
    TEST_ASSERT(stats1.connect_successes >= 1, "Should have at least one connect success");

    // Send N-CREATE
    mpps_dataset dataset1 = create_test_mpps_dataset("1.2.3.4.5", "ACC001");
    handler->on_n_create(dataset1);

    auto stats2 = handler->get_statistics();
    TEST_ASSERT(stats2.n_create_count == 1, "n_create_count should be 1");
    TEST_ASSERT(stats2.in_progress_count == 1, "in_progress_count should be 1");

    // Send N-SET completed
    mpps_dataset dataset2 = create_test_mpps_dataset("1.2.3.4.6", "ACC002", mpps_event::completed);
    handler->on_n_set(dataset2);

    auto stats3 = handler->get_statistics();
    TEST_ASSERT(stats3.n_set_count == 1, "n_set_count should be 1");
    TEST_ASSERT(stats3.completed_count == 1, "completed_count should be 1");

    // Send N-SET discontinued
    mpps_dataset dataset3 = create_test_mpps_dataset("1.2.3.4.7", "ACC003", mpps_event::discontinued);
    handler->on_n_set(dataset3);

    auto stats4 = handler->get_statistics();
    TEST_ASSERT(stats4.n_set_count == 2, "n_set_count should be 2");
    TEST_ASSERT(stats4.discontinued_count == 1, "discontinued_count should be 1");

    handler->stop();
    return true;
}

bool test_handler_reset_statistics() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    handler->set_callback([](mpps_event event, const mpps_dataset& mpps) {});
    handler->start();

    // Generate some events
    mpps_dataset dataset = create_test_mpps_dataset("1.2.3.4.5", "ACC001");
    handler->on_n_create(dataset);
    handler->on_n_create(dataset);

    auto stats1 = handler->get_statistics();
    TEST_ASSERT(stats1.n_create_count == 2, "Should have 2 n_create events");

    // Reset statistics
    handler->reset_statistics();

    auto stats2 = handler->get_statistics();
    TEST_ASSERT(stats2.n_create_count == 0, "n_create_count should be 0 after reset");
    TEST_ASSERT(stats2.n_set_count == 0, "n_set_count should be 0 after reset");

    handler->stop();
    return true;
}

// =============================================================================
// Callback Exception Tests
// =============================================================================

bool test_handler_callback_exception() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    handler->set_callback([](mpps_event event, const mpps_dataset& mpps) {
        throw std::runtime_error("Test exception");
    });

    handler->start();

    mpps_dataset dataset = create_test_mpps_dataset("1.2.3.4.5", "ACC001");
    auto result = handler->on_n_create(dataset);

    TEST_ASSERT(!result.has_value(), "Callback exception should cause failure");
    TEST_ASSERT(result.error() == mpps_error::callback_failed,
                "Should return callback_failed error");

    auto stats = handler->get_statistics();
    TEST_ASSERT(stats.callback_error_count == 1, "Should count callback error");

    handler->stop();
    return true;
}

// =============================================================================
// Concurrent Access Tests
// =============================================================================

bool test_handler_concurrent_events() {
    mpps_handler_config config;
    auto handler = mpps_handler::create(config);

    std::atomic<int> callback_count{0};
    handler->set_callback([&](mpps_event event, const mpps_dataset& mpps) {
        callback_count++;
        // Simulate brief work using yield-based wait
        auto work_deadline = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(1);
        while (std::chrono::steady_clock::now() < work_deadline) {
            std::this_thread::yield();
        }
    });

    handler->start();

    // Launch multiple threads sending events
    std::vector<std::thread> threads;
    const int num_threads = 4;
    const int events_per_thread = 10;

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&handler, i, events_per_thread]() {
            for (int j = 0; j < events_per_thread; j++) {
                std::string sop_uid = "1.2.3.4." + std::to_string(i) + "." + std::to_string(j);
                std::string acc = "ACC" + std::to_string(i * 100 + j);
                mpps_dataset dataset = create_test_mpps_dataset(sop_uid, acc);
                handler->on_n_create(dataset);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    TEST_ASSERT(callback_count == num_threads * events_per_thread,
                "All callbacks should be invoked");

    auto stats = handler->get_statistics();
    TEST_ASSERT(stats.n_create_count == static_cast<size_t>(num_threads * events_per_thread),
                "All events should be counted");

    handler->stop();
    return true;
}

// =============================================================================
// Test Runner
// =============================================================================

}  // namespace pacs::bridge::pacs_adapter::test

int main() {
    using namespace pacs::bridge::pacs_adapter::test;

    std::cout << "========================================" << std::endl;
    std::cout << "MPPS Handler Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int failed = 0;

    // Error code tests
    std::cout << "\n--- Error Code Tests ---" << std::endl;
    RUN_TEST(test_mpps_error_codes);
    RUN_TEST(test_mpps_error_to_string);

    // Event type tests
    std::cout << "\n--- Event Type Tests ---" << std::endl;
    RUN_TEST(test_mpps_event_to_string);
    RUN_TEST(test_parse_mpps_status);

    // Dataset tests
    std::cout << "\n--- Dataset Tests ---" << std::endl;
    RUN_TEST(test_mpps_dataset_creation);
    RUN_TEST(test_mpps_dataset_total_instances);
    RUN_TEST(test_mpps_dataset_status_methods);
    RUN_TEST(test_mpps_dataset_timing);

    // Validation tests
    std::cout << "\n--- Validation Tests ---" << std::endl;
    RUN_TEST(test_validate_mpps_dataset);

    // Duration calculation tests
    std::cout << "\n--- Duration Calculation Tests ---" << std::endl;
    RUN_TEST(test_calculate_procedure_duration);
    RUN_TEST(test_calculate_duration_invalid_format);

    // Configuration tests
    std::cout << "\n--- Configuration Tests ---" << std::endl;
    RUN_TEST(test_mpps_handler_config_defaults);
    RUN_TEST(test_mpps_handler_config_custom);

    // Handler lifecycle tests
    std::cout << "\n--- Handler Lifecycle Tests ---" << std::endl;
    RUN_TEST(test_handler_creation);
    RUN_TEST(test_handler_start_stop);
    RUN_TEST(test_handler_callback_management);

    // Event handler tests
    std::cout << "\n--- Event Handler Tests ---" << std::endl;
    RUN_TEST(test_handler_on_n_create);
    RUN_TEST(test_handler_on_n_set_completed);
    RUN_TEST(test_handler_on_n_set_discontinued);
    RUN_TEST(test_handler_invalid_dataset);
    RUN_TEST(test_handler_no_callback);

    // Statistics tests
    std::cout << "\n--- Statistics Tests ---" << std::endl;
    RUN_TEST(test_handler_statistics);
    RUN_TEST(test_handler_reset_statistics);

    // Callback exception tests
    std::cout << "\n--- Callback Exception Tests ---" << std::endl;
    RUN_TEST(test_handler_callback_exception);

    // Concurrent access tests
    std::cout << "\n--- Concurrent Access Tests ---" << std::endl;
    RUN_TEST(test_handler_concurrent_events);

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return failed > 0 ? 1 : 0;
}
