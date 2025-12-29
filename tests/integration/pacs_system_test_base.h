/**
 * @file pacs_system_test_base.h
 * @brief Base infrastructure for pacs_system integration tests
 *
 * Provides common utilities, test fixtures, and mock components for
 * testing pacs_bridge integration with pacs_system database operations.
 *
 * Features:
 *   - In-memory SQLite database support for fast test execution
 *   - MWL/MPPS test data generators
 *   - Common test fixtures and assertions
 *   - Database transaction helpers
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/188
 * @see https://github.com/kcenon/pacs_bridge/issues/191
 */

#ifndef PACS_BRIDGE_TESTS_INTEGRATION_PACS_SYSTEM_TEST_BASE_H
#define PACS_BRIDGE_TESTS_INTEGRATION_PACS_SYSTEM_TEST_BASE_H

#include "integration_test_base.h"

#include "pacs/bridge/pacs_adapter/mwl_client.h"
#include "pacs/bridge/pacs_adapter/mpps_handler.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace pacs::bridge::integration::test {

// =============================================================================
// Test Macros for pacs_system Integration Tests
// =============================================================================

#define PACS_TEST_ASSERT(condition, message)                                   \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAILED: " << message << " at " << __FILE__ << ":"    \
                      << __LINE__ << std::endl;                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define RUN_PACS_TEST(test_func)                                               \
    do {                                                                       \
        std::cout << "Running " << #test_func << "..." << std::endl;           \
        auto start = std::chrono::high_resolution_clock::now();                \
        bool result = test_func();                                             \
        auto end = std::chrono::high_resolution_clock::now();                  \
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( \
            end - start);                                                      \
        if (result) {                                                          \
            std::cout << "  PASSED (" << duration.count() << "ms)"             \
                      << std::endl;                                            \
            passed++;                                                          \
        } else {                                                               \
            std::cout << "  FAILED (" << duration.count() << "ms)"             \
                      << std::endl;                                            \
            failed++;                                                          \
        }                                                                      \
    } while (0)

// =============================================================================
// MWL Test Data Generator
// =============================================================================

/**
 * @brief Generator for MWL test data items
 *
 * Creates realistic MWL items for testing database operations.
 */
class mwl_test_data_generator {
public:
    /**
     * @brief Create a sample MWL item with default values
     */
    static mapping::mwl_item create_sample_item() {
        static std::atomic<int> counter{0};
        int id = counter++;

        mapping::mwl_item item;

        // Patient information
        item.patient.patient_id = "PAT" + std::to_string(1000 + id);
        item.patient.patient_name = "TEST^PATIENT^" + std::to_string(id);
        item.patient.patient_birth_date = "19800515";
        item.patient.patient_sex = "M";

        // Imaging service request
        item.imaging_service_request.accession_number =
            "ACC" + std::to_string(2000 + id);
        item.imaging_service_request.requesting_physician = "SMITH^DR";
        item.imaging_service_request.requesting_service = "RADIOLOGY";

        // Requested procedure
        item.requested_procedure.requested_procedure_id =
            "RP" + std::to_string(id);
        item.requested_procedure.requested_procedure_description = "CT Chest";
        item.requested_procedure.referring_physician_name = "JONES^DR";

        // Scheduled procedure step
        mapping::dicom_scheduled_procedure_step sps;
        sps.scheduled_station_ae_title = "CT_SCANNER_1";
        sps.scheduled_start_date = get_today_date();
        sps.scheduled_start_time = "090000";
        sps.modality = "CT";
        sps.scheduled_performing_physician = "DOC^RADIOLOGY";
        sps.scheduled_step_description = "CT Chest with contrast";
        sps.scheduled_step_id = "SPS" + std::to_string(id);
        sps.scheduled_step_status = "SCHEDULED";
        item.scheduled_steps.push_back(sps);

        return item;
    }

    /**
     * @brief Create an MWL item with specific accession number
     */
    static mapping::mwl_item create_item_with_accession(
        const std::string& accession_number) {
        auto item = create_sample_item();
        item.imaging_service_request.accession_number = accession_number;
        return item;
    }

    /**
     * @brief Create an MWL item with specific patient data
     */
    static mapping::mwl_item create_item_with_patient(
        const std::string& patient_id,
        const std::string& patient_name) {
        auto item = create_sample_item();
        item.patient.patient_id = patient_id;
        item.patient.patient_name = patient_name;
        return item;
    }

    /**
     * @brief Create an MWL item with specific modality
     */
    static mapping::mwl_item create_item_with_modality(
        const std::string& modality) {
        auto item = create_sample_item();
        if (!item.scheduled_steps.empty()) {
            item.scheduled_steps[0].modality = modality;
        }
        return item;
    }

    /**
     * @brief Create an MWL item with specific scheduled date
     */
    static mapping::mwl_item create_item_with_date(
        const std::string& scheduled_date) {
        auto item = create_sample_item();
        if (!item.scheduled_steps.empty()) {
            item.scheduled_steps[0].scheduled_start_date = scheduled_date;
        }
        return item;
    }

    /**
     * @brief Create a batch of MWL items
     */
    static std::vector<mapping::mwl_item> create_batch(size_t count) {
        std::vector<mapping::mwl_item> items;
        items.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            items.push_back(create_sample_item());
        }
        return items;
    }

    /**
     * @brief Get current date in DICOM format (YYYYMMDD)
     */
    static std::string get_today_date() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char date_buf[9];
        std::strftime(date_buf, sizeof(date_buf), "%Y%m%d",
                      std::localtime(&time_t_now));
        return date_buf;
    }

    /**
     * @brief Get date offset from today in DICOM format
     */
    static std::string get_date_offset(int days) {
        auto now = std::chrono::system_clock::now();
        now += std::chrono::hours(24 * days);
        auto time_t_offset = std::chrono::system_clock::to_time_t(now);
        char date_buf[9];
        std::strftime(date_buf, sizeof(date_buf), "%Y%m%d",
                      std::localtime(&time_t_offset));
        return date_buf;
    }
};

// =============================================================================
// MPPS Test Data Generator
// =============================================================================

/**
 * @brief Generator for MPPS test data items
 *
 * Creates realistic MPPS datasets for testing persistence and workflow.
 */
class mpps_test_data_generator {
public:
    /**
     * @brief Create a sample MPPS dataset with IN PROGRESS status
     */
    static pacs_adapter::mpps_dataset create_in_progress() {
        static std::atomic<int> counter{0};
        int id = counter++;

        pacs_adapter::mpps_dataset dataset;

        // SOP Instance identification
        dataset.sop_instance_uid =
            "1.2.840.10008.5.1.4.1.1.20." + std::to_string(1000 + id);

        // Relationship
        dataset.study_instance_uid =
            "1.2.840.10008.5.1.4.1.1.2." + std::to_string(id);
        dataset.accession_number = "MPPS_ACC" + std::to_string(2000 + id);
        dataset.scheduled_procedure_step_id = "SPS" + std::to_string(id);
        dataset.performed_procedure_step_id = "PPS" + std::to_string(id);

        // Patient
        dataset.patient_id = "MPPS_PAT" + std::to_string(1000 + id);
        dataset.patient_name = "MPPS^PATIENT^" + std::to_string(id);

        // Status
        dataset.status = pacs_adapter::mpps_event::in_progress;
        dataset.performed_procedure_description = "CT Chest with contrast";

        // Timing
        dataset.start_date = get_today_date();
        dataset.start_time = get_current_time();

        // Modality and Station
        dataset.modality = "CT";
        dataset.station_ae_title = "CT_SCANNER_1";
        dataset.station_name = "CT Scanner Room 1";

        // Performed series
        pacs_adapter::mpps_performed_series series;
        series.series_instance_uid =
            dataset.study_instance_uid + ".1." + std::to_string(id);
        series.series_description = "CT Chest Series 1";
        series.modality = "CT";
        series.number_of_instances = 150;
        series.performing_physician = "RADIOLOGIST^DR";
        dataset.performed_series.push_back(series);

        // Additional
        dataset.referring_physician = "JONES^DR";
        dataset.requested_procedure_id = "RP" + std::to_string(id);

        return dataset;
    }

    /**
     * @brief Create a completed MPPS dataset
     */
    static pacs_adapter::mpps_dataset create_completed() {
        auto dataset = create_in_progress();
        dataset.status = pacs_adapter::mpps_event::completed;
        dataset.end_date = get_today_date();
        dataset.end_time = get_offset_time(30);  // 30 minutes after start
        return dataset;
    }

    /**
     * @brief Create a discontinued MPPS dataset
     */
    static pacs_adapter::mpps_dataset create_discontinued(
        const std::string& reason = "Patient refused") {
        auto dataset = create_in_progress();
        dataset.status = pacs_adapter::mpps_event::discontinued;
        dataset.end_date = get_today_date();
        dataset.end_time = get_offset_time(10);  // 10 minutes after start
        dataset.discontinuation_reason = reason;
        return dataset;
    }

    /**
     * @brief Create MPPS dataset with specific SOP Instance UID
     */
    static pacs_adapter::mpps_dataset create_with_sop_uid(
        const std::string& sop_uid) {
        auto dataset = create_in_progress();
        dataset.sop_instance_uid = sop_uid;
        return dataset;
    }

    /**
     * @brief Create MPPS dataset with specific station
     */
    static pacs_adapter::mpps_dataset create_with_station(
        const std::string& station_ae) {
        auto dataset = create_in_progress();
        dataset.station_ae_title = station_ae;
        return dataset;
    }

    /**
     * @brief Create a batch of MPPS datasets
     */
    static std::vector<pacs_adapter::mpps_dataset> create_batch(size_t count) {
        std::vector<pacs_adapter::mpps_dataset> datasets;
        datasets.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            datasets.push_back(create_in_progress());
        }
        return datasets;
    }

    /**
     * @brief Get current date in DICOM format (YYYYMMDD)
     */
    static std::string get_today_date() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char date_buf[9];
        std::strftime(date_buf, sizeof(date_buf), "%Y%m%d",
                      std::localtime(&time_t_now));
        return date_buf;
    }

    /**
     * @brief Get current time in DICOM format (HHMMSS)
     */
    static std::string get_current_time() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char time_buf[7];
        std::strftime(time_buf, sizeof(time_buf), "%H%M%S",
                      std::localtime(&time_t_now));
        return time_buf;
    }

    /**
     * @brief Get time with offset in minutes
     */
    static std::string get_offset_time(int offset_minutes) {
        auto now = std::chrono::system_clock::now();
        now += std::chrono::minutes(offset_minutes);
        auto time_t_offset = std::chrono::system_clock::to_time_t(now);
        char time_buf[7];
        std::strftime(time_buf, sizeof(time_buf), "%H%M%S",
                      std::localtime(&time_t_offset));
        return time_buf;
    }
};

// =============================================================================
// pacs_system Integration Test Fixture
// =============================================================================

/**
 * @brief Base fixture for pacs_system integration tests
 *
 * Provides setup/teardown for database connections and common utilities.
 */
class pacs_system_test_fixture : public integration_test_fixture {
public:
    pacs_system_test_fixture() = default;
    virtual ~pacs_system_test_fixture() = default;

    /**
     * @brief Create MWL client configuration for testing
     *
     * Uses in-memory SQLite for fast test execution.
     */
    static pacs_adapter::mwl_client_config create_mwl_test_config() {
        pacs_adapter::mwl_client_config config;
        config.pacs_host = "localhost";
        config.pacs_port = 11112;
        config.our_ae_title = "TEST_BRIDGE";
        config.pacs_ae_title = "TEST_PACS";
        config.connect_timeout = std::chrono::seconds{5};
        config.operation_timeout = std::chrono::seconds{10};
        config.max_retries = 1;
#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
        config.pacs_database_path = ":memory:";
#endif
        return config;
    }

    /**
     * @brief Create MPPS handler configuration for testing
     *
     * Uses in-memory SQLite for fast test execution.
     */
    static pacs_adapter::mpps_handler_config create_mpps_test_config() {
        pacs_adapter::mpps_handler_config config;
        config.pacs_host = "localhost";
        config.pacs_port = 11113;
        config.our_ae_title = "TEST_BRIDGE";
        config.pacs_ae_title = "TEST_MPPS";
        config.auto_reconnect = false;
        config.enable_persistence = true;
        config.database_path = ":memory:";
        config.recover_on_startup = false;
        config.verbose_logging = false;
        return config;
    }

    /**
     * @brief Create unique database path for test isolation
     */
    static std::filesystem::path create_test_db_path() {
        static std::atomic<int> counter{0};
        auto temp_dir = std::filesystem::temp_directory_path();
        return temp_dir / ("pacs_bridge_test_" + std::to_string(counter++) +
                           ".db");
    }

    /**
     * @brief Cleanup test database file
     */
    static void cleanup_test_db(const std::filesystem::path& path) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    /**
     * @brief Generate unique accession number for test isolation
     */
    static std::string generate_unique_accession() {
        static std::atomic<int> counter{0};
        return "TEST_ACC_" + std::to_string(counter++);
    }

    /**
     * @brief Generate unique SOP Instance UID for test isolation
     */
    static std::string generate_unique_sop_uid() {
        static std::atomic<int> counter{0};
        return "1.2.840.10008.5.1.4.1.1.99." + std::to_string(counter++);
    }
};

// =============================================================================
// Database Verification Utilities
// =============================================================================

/**
 * @brief Utilities for verifying database state in tests
 */
class db_verification {
public:
    /**
     * @brief Verify MWL entry exists with expected values
     */
    static bool verify_mwl_entry(
        pacs_adapter::mwl_client& client,
        const std::string& accession_number,
        const std::string& expected_patient_id = "") {

        auto result = client.get_entry(accession_number);
        if (!result.has_value()) {
            return false;
        }

        if (!expected_patient_id.empty() &&
            result->patient.patient_id != expected_patient_id) {
            return false;
        }

        return true;
    }

    /**
     * @brief Verify MWL entry count matches expected
     */
    static bool verify_mwl_count(
        pacs_adapter::mwl_client& client,
        size_t expected_count) {

        pacs_adapter::mwl_query_filter filter;
        auto result = client.query(filter);
        if (!result.has_value()) {
            return false;
        }

        return result->items.size() == expected_count;
    }

    /**
     * @brief Verify MPPS record exists with expected status
     */
    static bool verify_mpps_status(
        pacs_adapter::mpps_handler& handler,
        const std::string& sop_uid,
        pacs_adapter::mpps_event expected_status) {

        auto result = handler.query_mpps(sop_uid);
        if (!result.has_value() || !result->has_value()) {
            return false;
        }

        return result->value().status == expected_status;
    }
};

}  // namespace pacs::bridge::integration::test

#endif  // PACS_BRIDGE_TESTS_INTEGRATION_PACS_SYSTEM_TEST_BASE_H
