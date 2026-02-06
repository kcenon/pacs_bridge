/**
 * @file pacs_adapter_test.cpp
 * @brief Unit tests for PACS adapter implementations
 *
 * Tests for memory_pacs_adapter (standalone mode), MPPS operations,
 * MWL queries, and DICOM storage operations.
 *
 * @see include/pacs/bridge/integration/pacs_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/283
 * @see https://github.com/kcenon/pacs_bridge/issues/319
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/integration/pacs_adapter.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;
using namespace std::chrono_literals;

// =============================================================================
// Test Fixtures
// =============================================================================

class PacsAdapterTest : public Test {
protected:
    void SetUp() override {
        pacs_config config;
        config.server_ae_title = "TEST_PACS";
        config.server_hostname = "localhost";
        config.server_port = 11112;
        config.calling_ae_title = "TEST_BRIDGE";
        config.connection_timeout = 5s;
        config.query_timeout = 10s;

        adapter_ = create_pacs_adapter(config);
        ASSERT_NE(adapter_, nullptr);
    }

    void TearDown() override {
        if (adapter_ && adapter_->is_connected()) {
            adapter_->disconnect();
        }
        adapter_.reset();
    }

    std::shared_ptr<pacs_adapter> adapter_;
};

// =============================================================================
// Error Code Tests
// =============================================================================

TEST(PacsErrorTest, ErrorCodeConversion) {
    EXPECT_EQ(to_error_code(pacs_error::connection_failed), -850);
    EXPECT_EQ(to_error_code(pacs_error::query_failed), -851);
    EXPECT_EQ(to_error_code(pacs_error::store_failed), -852);
    EXPECT_EQ(to_error_code(pacs_error::invalid_dataset), -853);
    EXPECT_EQ(to_error_code(pacs_error::association_failed), -854);
    EXPECT_EQ(to_error_code(pacs_error::timeout), -855);
    EXPECT_EQ(to_error_code(pacs_error::not_found), -856);
    EXPECT_EQ(to_error_code(pacs_error::duplicate_entry), -857);
    EXPECT_EQ(to_error_code(pacs_error::validation_failed), -858);
    EXPECT_EQ(to_error_code(pacs_error::mpps_create_failed), -859);
    EXPECT_EQ(to_error_code(pacs_error::mpps_update_failed), -860);
    EXPECT_EQ(to_error_code(pacs_error::mwl_query_failed), -861);
    EXPECT_EQ(to_error_code(pacs_error::storage_failed), -862);
    EXPECT_EQ(to_error_code(pacs_error::invalid_sop_uid), -863);
}

TEST(PacsErrorTest, ErrorMessages) {
    EXPECT_EQ(to_string(pacs_error::connection_failed), "Connection to PACS server failed");
    EXPECT_EQ(to_string(pacs_error::query_failed), "Query execution failed");
    EXPECT_EQ(to_string(pacs_error::mpps_create_failed), "MPPS N-CREATE failed");
    EXPECT_EQ(to_string(pacs_error::mwl_query_failed), "MWL query failed");
}

// =============================================================================
// DICOM Dataset Tests
// =============================================================================

TEST(DicomDatasetTest, SetAndGetString) {
    dicom_dataset dataset;

    // Patient ID tag: 0x00100020
    dataset.set_string(0x00100020, "TEST123");

    auto value = dataset.get_string(0x00100020);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "TEST123");
}

TEST(DicomDatasetTest, GetNonExistentTag) {
    dicom_dataset dataset;

    auto value = dataset.get_string(0x99999999);
    EXPECT_FALSE(value.has_value());
}

TEST(DicomDatasetTest, HasTag) {
    dicom_dataset dataset;
    dataset.set_string(0x00100020, "TEST123");

    EXPECT_TRUE(dataset.has_tag(0x00100020));
    EXPECT_FALSE(dataset.has_tag(0x99999999));
}

TEST(DicomDatasetTest, RemoveTag) {
    dicom_dataset dataset;
    dataset.set_string(0x00100020, "TEST123");
    EXPECT_TRUE(dataset.has_tag(0x00100020));

    dataset.remove_tag(0x00100020);
    EXPECT_FALSE(dataset.has_tag(0x00100020));
}

TEST(DicomDatasetTest, Clear) {
    dicom_dataset dataset;
    dataset.sop_class_uid = "1.2.840.10008.5.1.4.1.1.1";
    dataset.sop_instance_uid = "1.2.3.4.5";
    dataset.set_string(0x00100020, "TEST123");
    dataset.set_string(0x00100010, "Test^Patient");

    dataset.clear();

    EXPECT_TRUE(dataset.sop_class_uid.empty());
    EXPECT_TRUE(dataset.sop_instance_uid.empty());
    EXPECT_FALSE(dataset.has_tag(0x00100020));
    EXPECT_FALSE(dataset.has_tag(0x00100010));
}

TEST(DicomDatasetTest, OverwriteTag) {
    dicom_dataset dataset;
    dataset.set_string(0x00100020, "TEST123");
    dataset.set_string(0x00100020, "TEST456");

    auto value = dataset.get_string(0x00100020);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "TEST456");
}

// =============================================================================
// MPPS Record Tests
// =============================================================================

TEST(MppsRecordTest, ValidRecord) {
    mpps_record record;
    record.sop_instance_uid = "1.2.3.4.5";
    record.scheduled_procedure_step_id = "SPS001";
    record.performed_procedure_step_id = "PPS001";
    record.status = "IN PROGRESS";

    EXPECT_TRUE(record.is_valid());
}

TEST(MppsRecordTest, InvalidStatus) {
    mpps_record record;
    record.sop_instance_uid = "1.2.3.4.5";
    record.scheduled_procedure_step_id = "SPS001";
    record.performed_procedure_step_id = "PPS001";
    record.status = "INVALID_STATUS";

    EXPECT_FALSE(record.is_valid());
}

TEST(MppsRecordTest, MissingRequiredFields) {
    mpps_record record;

    // Missing all required fields
    EXPECT_FALSE(record.is_valid());

    // Has sop_instance_uid and status, but no identifier (no SPS ID, no accession)
    record.sop_instance_uid = "1.2.3.4.5";
    record.status = "IN PROGRESS";
    EXPECT_FALSE(record.is_valid());

    // With accession_number (but no SPS ID) - should be valid
    record.accession_number = "ACC001";
    EXPECT_TRUE(record.is_valid());
}

TEST(MppsRecordTest, ValidStatuses) {
    mpps_record record;
    record.sop_instance_uid = "1.2.3.4.5";
    record.scheduled_procedure_step_id = "SPS001";
    record.performed_procedure_step_id = "PPS001";

    record.status = "IN PROGRESS";
    EXPECT_TRUE(record.is_valid());

    // COMPLETED status requires end_datetime
    record.status = "COMPLETED";
    record.end_datetime = std::chrono::system_clock::now();
    EXPECT_TRUE(record.is_valid());

    record.status = "DISCONTINUED";
    EXPECT_TRUE(record.is_valid());
}

// =============================================================================
// MWL Item Tests
// =============================================================================

TEST(MwlItemTest, ValidItem) {
    mwl_item item;
    item.accession_number = "ACC123";
    item.scheduled_procedure_step_id = "SPS001";
    item.patient_id = "PAT123";
    item.patient_name = "Test^Patient";
    item.modality = "CT";

    EXPECT_TRUE(item.is_valid());
}

TEST(MwlItemTest, MissingRequiredFields) {
    mwl_item item;

    // Missing all required fields
    EXPECT_FALSE(item.is_valid());

    // Missing accession_number
    item.scheduled_procedure_step_id = "SPS001";
    item.patient_id = "PAT123";
    EXPECT_FALSE(item.is_valid());
}

// =============================================================================
// PACS Adapter Connection Tests
// =============================================================================

TEST_F(PacsAdapterTest, InitialState) {
    EXPECT_FALSE(adapter_->is_connected());
    // Stub adapter is only healthy when connected
    EXPECT_FALSE(adapter_->is_healthy());
}

TEST_F(PacsAdapterTest, ConnectAndDisconnect) {
    auto result = adapter_->connect();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(adapter_->is_connected());

    adapter_->disconnect();
    EXPECT_FALSE(adapter_->is_connected());
}

TEST_F(PacsAdapterTest, MultipleConnects) {
    // First connect
    auto result1 = adapter_->connect();
    EXPECT_TRUE(result1.has_value());
    EXPECT_TRUE(adapter_->is_connected());

    // Second connect should succeed (idempotent)
    auto result2 = adapter_->connect();
    EXPECT_TRUE(result2.has_value());
    EXPECT_TRUE(adapter_->is_connected());

    adapter_->disconnect();
}

TEST_F(PacsAdapterTest, MultipleDisconnects) {
    auto result = adapter_->connect();
    EXPECT_TRUE(result.has_value());

    adapter_->disconnect();
    EXPECT_FALSE(adapter_->is_connected());

    // Second disconnect should be safe (idempotent)
    adapter_->disconnect();
    EXPECT_FALSE(adapter_->is_connected());
}

// =============================================================================
// MPPS Adapter Tests
// =============================================================================

TEST_F(PacsAdapterTest, GetMppsAdapter) {
    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);
}

TEST_F(PacsAdapterTest, CreateMpps) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    mpps_record record;
    record.sop_instance_uid = "1.2.840.10008.1.2.3.4.5";
    record.scheduled_procedure_step_id = "SPS001";
    record.performed_procedure_step_id = "PPS001";
    record.performed_station_ae_title = "MODALITY1";
    record.status = "IN PROGRESS";
    record.study_instance_uid = "1.2.840.10008.1.2.3";
    record.patient_id = "PAT123";
    record.patient_name = "Test^Patient";
    record.start_datetime = std::chrono::system_clock::now();

    auto result = mpps->create_mpps(record);
    EXPECT_TRUE(result.has_value());
}

TEST_F(PacsAdapterTest, CreateDuplicateMpps) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    mpps_record record;
    record.sop_instance_uid = "1.2.840.10008.1.2.3.4.999";
    record.scheduled_procedure_step_id = "SPS001";
    record.performed_procedure_step_id = "PPS001";
    record.performed_station_ae_title = "MODALITY1";
    record.status = "IN PROGRESS";
    record.study_instance_uid = "1.2.840.10008.1.2.3";
    record.patient_id = "PAT123";
    record.patient_name = "Test^Patient";
    record.start_datetime = std::chrono::system_clock::now();

    auto result1 = mpps->create_mpps(record);
    EXPECT_TRUE(result1.has_value());

    // Duplicate create should fail
    auto result2 = mpps->create_mpps(record);
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), pacs_error::duplicate_entry);
}

TEST_F(PacsAdapterTest, CreateInvalidMpps) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    mpps_record record;
    // Missing required fields

    auto result = mpps->create_mpps(record);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pacs_error::validation_failed);
}

TEST_F(PacsAdapterTest, UpdateMpps) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Create MPPS first
    mpps_record record;
    record.sop_instance_uid = "1.2.840.10008.1.2.3.4.6";
    record.scheduled_procedure_step_id = "SPS002";
    record.performed_procedure_step_id = "PPS002";
    record.performed_station_ae_title = "MODALITY1";
    record.status = "IN PROGRESS";
    record.study_instance_uid = "1.2.840.10008.1.2.3";
    record.patient_id = "PAT123";
    record.patient_name = "Test^Patient";
    record.start_datetime = std::chrono::system_clock::now();

    auto create_result = mpps->create_mpps(record);
    ASSERT_TRUE(create_result.has_value());

    // Update to COMPLETED
    record.status = "COMPLETED";
    record.end_datetime = std::chrono::system_clock::now();
    record.series_instance_uids.push_back("1.2.840.10008.1.2.3.4.7");

    auto update_result = mpps->update_mpps(record);
    EXPECT_TRUE(update_result.has_value());
}

TEST_F(PacsAdapterTest, UpdateNonExistentMpps) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    mpps_record record;
    record.sop_instance_uid = "999.999.999.999";
    record.scheduled_procedure_step_id = "SPS999";
    record.performed_procedure_step_id = "PPS999";
    record.status = "COMPLETED";
    record.end_datetime = std::chrono::system_clock::now();

    // In-memory adapter checks existence, returns not_found
    auto result = mpps->update_mpps(record);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pacs_error::not_found);
}

TEST_F(PacsAdapterTest, QueryMpps) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Create test MPPS records
    for (int i = 0; i < 3; ++i) {
        mpps_record record;
        record.sop_instance_uid = "1.2.840.10008.1.2.3.4." + std::to_string(100 + i);
        record.scheduled_procedure_step_id = "SPS" + std::to_string(i);
        record.performed_procedure_step_id = "PPS" + std::to_string(i);
        record.performed_station_ae_title = "MODALITY1";
        record.status = (i < 2) ? "IN PROGRESS" : "COMPLETED";
        record.study_instance_uid = "1.2.840.10008.1.2.3";
        record.patient_id = "PAT" + std::to_string(i);
        record.patient_name = "Test^Patient" + std::to_string(i);
        record.start_datetime = std::chrono::system_clock::now();
        if (i >= 2) {
            record.end_datetime = std::chrono::system_clock::now();
        }

        ASSERT_TRUE(mpps->create_mpps(record).has_value());
    }

    // Query all MPPS (in-memory adapter returns stored records)
    mpps_query_params params;
    params.max_results = 100;

    auto result = mpps->query_mpps(params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 3);
}

TEST_F(PacsAdapterTest, QueryMppsByStatus) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Create test records with different statuses
    {
        mpps_record record;
        record.sop_instance_uid = "1.2.840.10008.1.2.3.4.200";
        record.scheduled_procedure_step_id = "SPS200";
        record.performed_procedure_step_id = "PPS200";
        record.performed_station_ae_title = "MODALITY1";
        record.status = "IN PROGRESS";
        record.study_instance_uid = "1.2.840.10008.1.2.3";
        record.patient_id = "PAT200";
        record.patient_name = "Test^Patient200";
        record.start_datetime = std::chrono::system_clock::now();

        ASSERT_TRUE(mpps->create_mpps(record).has_value());
    }

    // Query by status
    mpps_query_params params;
    params.status = "IN PROGRESS";
    params.max_results = 100;

    auto result = mpps->query_mpps(params);
    ASSERT_TRUE(result.has_value());

    // All results should have "IN PROGRESS" status
    for (const auto& record : *result) {
        EXPECT_EQ(record.status, "IN PROGRESS");
    }
}

TEST_F(PacsAdapterTest, GetMpps) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Create MPPS
    std::string sop_uid = "1.2.840.10008.1.2.3.4.300";
    mpps_record record;
    record.sop_instance_uid = sop_uid;
    record.scheduled_procedure_step_id = "SPS300";
    record.performed_procedure_step_id = "PPS300";
    record.performed_station_ae_title = "MODALITY1";
    record.status = "IN PROGRESS";
    record.study_instance_uid = "1.2.840.10008.1.2.3";
    record.patient_id = "PAT300";
    record.patient_name = "Test^Patient300";
    record.start_datetime = std::chrono::system_clock::now();

    ASSERT_TRUE(mpps->create_mpps(record).has_value());

    // Get by SOP Instance UID (in-memory adapter returns the stored record)
    auto result = mpps->get_mpps(sop_uid);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sop_instance_uid, sop_uid);
    EXPECT_EQ(result->patient_id, "PAT300");
    EXPECT_EQ(result->status, "IN PROGRESS");
}

TEST_F(PacsAdapterTest, GetNonExistentMpps) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    auto result = mpps->get_mpps("999.999.999.999");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pacs_error::not_found);
}

// =============================================================================
// MWL Adapter Tests
// =============================================================================

TEST_F(PacsAdapterTest, GetMwlAdapter) {
    auto mwl = adapter_->get_mwl_adapter();
    ASSERT_NE(mwl, nullptr);
}

TEST_F(PacsAdapterTest, QueryMwl) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mwl = adapter_->get_mwl_adapter();
    ASSERT_NE(mwl, nullptr);

    mwl_query_params params;
    params.max_results = 100;

    auto result = mwl->query_mwl(params);
    EXPECT_TRUE(result.has_value());
}

TEST_F(PacsAdapterTest, QueryMwlByPatientId) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mwl = adapter_->get_mwl_adapter();
    ASSERT_NE(mwl, nullptr);

    mwl_query_params params;
    params.patient_id = "PAT123";
    params.max_results = 100;

    auto result = mwl->query_mwl(params);
    EXPECT_TRUE(result.has_value());
}

TEST_F(PacsAdapterTest, GetMwlItem) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mwl = adapter_->get_mwl_adapter();
    ASSERT_NE(mwl, nullptr);

    auto result = mwl->get_mwl_item("ACC123");
    // May return not_found if no item exists
    EXPECT_TRUE(result.has_value() || result.error() == pacs_error::not_found);
}

// =============================================================================
// Storage Adapter Tests
// =============================================================================

TEST_F(PacsAdapterTest, GetStorageAdapter) {
    auto storage = adapter_->get_storage_adapter();
    ASSERT_NE(storage, nullptr);
}

TEST_F(PacsAdapterTest, StoreAndRetrieve) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto storage = adapter_->get_storage_adapter();
    ASSERT_NE(storage, nullptr);

    dicom_dataset dataset;
    dataset.sop_class_uid = "1.2.840.10008.5.1.4.1.1.1";  // CR Image Storage
    dataset.sop_instance_uid = "1.2.840.10008.1.2.3.4.5.400";
    dataset.set_string(0x00100020, "PAT400");  // Patient ID
    dataset.set_string(0x00100010, "Test^Patient400");  // Patient Name

    // Store (stub accepts but doesn't persist)
    auto store_result = storage->store(dataset);
    EXPECT_TRUE(store_result.has_value());

    // Retrieve (stub doesn't store, so always returns not_found)
    auto retrieve_result = storage->retrieve(dataset.sop_instance_uid);
    EXPECT_FALSE(retrieve_result.has_value());
    EXPECT_EQ(retrieve_result.error(), pacs_error::not_found);
}

TEST_F(PacsAdapterTest, StoreInvalidDataset) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto storage = adapter_->get_storage_adapter();
    ASSERT_NE(storage, nullptr);

    dicom_dataset dataset;
    // Missing required fields

    auto result = storage->store(dataset);
    EXPECT_FALSE(result.has_value());
}

TEST_F(PacsAdapterTest, RetrieveNonExistent) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto storage = adapter_->get_storage_adapter();
    ASSERT_NE(storage, nullptr);

    auto result = storage->retrieve("999.999.999.999");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pacs_error::not_found);
}

TEST_F(PacsAdapterTest, Exists) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto storage = adapter_->get_storage_adapter();
    ASSERT_NE(storage, nullptr);

    dicom_dataset dataset;
    dataset.sop_class_uid = "1.2.840.10008.5.1.4.1.1.1";
    dataset.sop_instance_uid = "1.2.840.10008.1.2.3.4.5.500";
    dataset.set_string(0x00100020, "PAT500");

    // Stub doesn't store data, so exists always returns false
    EXPECT_FALSE(storage->exists(dataset.sop_instance_uid));

    // Store (stub accepts but doesn't persist)
    ASSERT_TRUE(storage->store(dataset).has_value());

    // Still doesn't exist (stub behavior)
    EXPECT_FALSE(storage->exists(dataset.sop_instance_uid));
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(PacsAdapterTest, OperationsWithoutConnection) {
    // Stub adapter allows operations even when not connected
    // (connection state is not enforced in stub implementation)
    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    mpps_record record;
    record.sop_instance_uid = "1.2.3.4.5";
    record.scheduled_procedure_step_id = "SPS001";
    record.performed_procedure_step_id = "PPS001";
    record.status = "IN PROGRESS";

    // Stub allows operations without connection (validates and accepts)
    auto result = mpps->create_mpps(record);
    EXPECT_TRUE(result.has_value());
}

TEST_F(PacsAdapterTest, ConcurrentOperations) {
    ASSERT_TRUE(adapter_->connect().has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&, i]() {
            mpps_record record;
            record.sop_instance_uid = "1.2.840.10008.1.2.3.4." + std::to_string(600 + i);
            record.scheduled_procedure_step_id = "SPS" + std::to_string(600 + i);
            record.performed_procedure_step_id = "PPS" + std::to_string(600 + i);
            record.performed_station_ae_title = "MODALITY1";
            record.status = "IN PROGRESS";
            record.study_instance_uid = "1.2.840.10008.1.2.3";
            record.patient_id = "PAT" + std::to_string(600 + i);
            record.patient_name = "Test^Patient" + std::to_string(600 + i);
            record.start_datetime = std::chrono::system_clock::now();

            auto result = mpps->create_mpps(record);
            if (result.has_value()) {
                ++success_count;
            } else {
                ++failure_count;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_GT(success_count.load(), 0);
}

}  // namespace
}  // namespace pacs::bridge::integration
