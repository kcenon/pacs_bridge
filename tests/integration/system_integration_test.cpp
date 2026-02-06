/**
 * @file system_integration_test.cpp
 * @brief Integration tests verifying adapters work with system modules
 *
 * Tests adapter combinations and cross-adapter workflows. When system
 * modules (database_system, network_system, etc.) are available, tests
 * verify the integrated implementations. Otherwise, tests validate the
 * standalone fallback implementations working together.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/320
 * @see https://github.com/kcenon/pacs_bridge/issues/287
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "test_utilities.h"

#include "pacs/bridge/integration/database_adapter.h"
#include "pacs/bridge/integration/pacs_adapter.h"
#include "pacs/bridge/mllp/mllp_network_adapter.h"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;
using namespace pacs::bridge::integration::test;
using namespace std::chrono_literals;

// =============================================================================
// Database + PACS Combined Workflow Tests
// =============================================================================

class DatabasePacsIntegrationTest : public Test {
protected:
    void SetUp() override {
        db_ = create_test_database();
        ASSERT_NE(db_->adapter, nullptr);

        pacs_ = create_test_pacs_adapter();
        ASSERT_NE(pacs_, nullptr);

        auto connect = pacs_->connect();
        ASSERT_TRUE(connect.has_value());

        // Create schema for storing MPPS tracking data
        auto schema = db_->adapter->execute_schema(
            "CREATE TABLE IF NOT EXISTS mpps_tracking ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  sop_instance_uid TEXT NOT NULL UNIQUE,"
            "  patient_id TEXT NOT NULL,"
            "  status TEXT NOT NULL DEFAULT 'IN PROGRESS',"
            "  created_at INTEGER DEFAULT (strftime('%s','now')),"
            "  updated_at INTEGER DEFAULT (strftime('%s','now'))"
            ")"
        );
        ASSERT_TRUE(schema.has_value());
    }

    void TearDown() override {
        if (pacs_ && pacs_->is_connected()) {
            pacs_->disconnect();
        }
        pacs_.reset();
        db_.reset();
    }

    std::unique_ptr<test_database> db_;
    std::shared_ptr<pacs_adapter> pacs_;
};

TEST_F(DatabasePacsIntegrationTest, MppsCreateWithDatabaseTracking) {
    auto mpps = pacs_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Create MPPS record through PACS adapter
    mpps_record record;
    record.sop_instance_uid = "1.2.3.4.5.500";
    record.scheduled_procedure_step_id = "SPS100";
    record.performed_procedure_step_id = "PPS100";
    record.performed_station_ae_title = "CT01";
    record.start_datetime = std::chrono::system_clock::now();
    record.status = "IN PROGRESS";
    record.study_instance_uid = "1.2.3.4.5.600";
    record.patient_id = "PAT100";
    record.patient_name = "ADAMS^ALICE";

    auto pacs_result = mpps->create_mpps(record);
    ASSERT_TRUE(pacs_result.has_value());

    // Track in database
    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    auto stmt = conn.prepare(
        "INSERT INTO mpps_tracking (sop_instance_uid, patient_id, status) VALUES (?, ?, ?)");
    ASSERT_TRUE(stmt.has_value());
    ASSERT_TRUE(stmt.value()->bind_string(1, record.sop_instance_uid).has_value());
    ASSERT_TRUE(stmt.value()->bind_string(2, record.patient_id).has_value());
    ASSERT_TRUE(stmt.value()->bind_string(3, record.status).has_value());
    auto exec = stmt.value()->execute();
    ASSERT_TRUE(exec.has_value());

    // Verify tracking data
    auto select = conn.execute(
        "SELECT status FROM mpps_tracking WHERE sop_instance_uid = '1.2.3.4.5.500'");
    ASSERT_TRUE(select.has_value());
    ASSERT_TRUE(select.value()->next());
    EXPECT_EQ(select.value()->current_row().get_string(0), "IN PROGRESS");
}

TEST_F(DatabasePacsIntegrationTest, MppsUpdateWithDatabaseSync) {
    auto mpps = pacs_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Create MPPS
    mpps_record record;
    record.sop_instance_uid = "1.2.3.4.5.501";
    record.scheduled_procedure_step_id = "SPS101";
    record.performed_procedure_step_id = "PPS101";
    record.performed_station_ae_title = "MR01";
    record.start_datetime = std::chrono::system_clock::now();
    record.status = "IN PROGRESS";
    record.study_instance_uid = "1.2.3.4.5.601";
    record.patient_id = "PAT101";
    record.patient_name = "BAKER^BOB";

    ASSERT_TRUE(mpps->create_mpps(record).has_value());

    // Track in database
    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    auto track = conn.execute(
        "INSERT INTO mpps_tracking (sop_instance_uid, patient_id, status) "
        "VALUES ('1.2.3.4.5.501', 'PAT101', 'IN PROGRESS')");
    ASSERT_TRUE(track.has_value());

    // Update MPPS to completed
    record.status = "COMPLETED";
    record.end_datetime = std::chrono::system_clock::now();
    ASSERT_TRUE(mpps->update_mpps(record).has_value());

    // Sync status to database
    auto update = conn.execute(
        "UPDATE mpps_tracking SET status = 'COMPLETED', "
        "updated_at = strftime('%s','now') "
        "WHERE sop_instance_uid = '1.2.3.4.5.501'");
    ASSERT_TRUE(update.has_value());
    EXPECT_EQ(conn.changes(), 1);

    // PACS stub doesn't persist, so get_mpps returns not_found
    auto pacs_record = mpps->get_mpps("1.2.3.4.5.501");
    EXPECT_FALSE(pacs_record.has_value());

    // Database is the authoritative source in standalone mode
    auto db_record = conn.execute(
        "SELECT status FROM mpps_tracking WHERE sop_instance_uid = '1.2.3.4.5.501'");
    ASSERT_TRUE(db_record.has_value());
    ASSERT_TRUE(db_record.value()->next());
    EXPECT_EQ(db_record.value()->current_row().get_string(0), "COMPLETED");
}

TEST_F(DatabasePacsIntegrationTest, StorageWithDatabaseIndex) {
    auto storage = pacs_->get_storage_adapter();
    ASSERT_NE(storage, nullptr);

    // Create index table
    auto schema = db_->adapter->execute_schema(
        "CREATE TABLE IF NOT EXISTS dicom_index ("
        "  sop_instance_uid TEXT PRIMARY KEY,"
        "  patient_id TEXT,"
        "  sop_class_uid TEXT"
        ")"
    );
    ASSERT_TRUE(schema.has_value());

    // Store DICOM dataset (stub accepts but doesn't persist)
    dicom_dataset dataset;
    dataset.sop_class_uid = "1.2.840.10008.5.1.4.1.1.2";
    dataset.sop_instance_uid = "1.2.3.4.5.700";
    dataset.set_string(0x00100020, "PAT200");

    auto store = storage->store(dataset);
    ASSERT_TRUE(store.has_value());

    // Index in database - database does persist
    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    auto stmt = conn.prepare(
        "INSERT INTO dicom_index (sop_instance_uid, patient_id, sop_class_uid) "
        "VALUES (?, ?, ?)");
    ASSERT_TRUE(stmt.has_value());
    ASSERT_TRUE(stmt.value()->bind_string(1, dataset.sop_instance_uid).has_value());
    ASSERT_TRUE(stmt.value()->bind_string(2, "PAT200").has_value());
    ASSERT_TRUE(stmt.value()->bind_string(3, dataset.sop_class_uid).has_value());
    ASSERT_TRUE(stmt.value()->execute().has_value());

    // Query from database index - database lookup works
    auto select = conn.execute(
        "SELECT sop_instance_uid FROM dicom_index WHERE patient_id = 'PAT200'");
    ASSERT_TRUE(select.has_value());
    ASSERT_TRUE(select.value()->next());

    auto uid = select.value()->current_row().get_string(0);
    EXPECT_EQ(uid, "1.2.3.4.5.700");

    // Note: PACS storage stub doesn't persist, so exists/retrieve won't work.
    // The database index is the authoritative source in standalone mode.
    EXPECT_FALSE(storage->exists(uid));
}

// =============================================================================
// Concurrent Adapter Usage Tests
// =============================================================================

class ConcurrentAdapterTest : public Test {
protected:
    void SetUp() override {
        db_ = create_test_database(5);
        ASSERT_NE(db_->adapter, nullptr);

        auto schema = db_->adapter->execute_schema(
            "CREATE TABLE IF NOT EXISTS concurrent_test ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  thread_id INTEGER NOT NULL,"
            "  value TEXT NOT NULL"
            ")"
        );
        ASSERT_TRUE(schema.has_value());
    }

    void TearDown() override {
        db_.reset();
    }

    std::unique_ptr<test_database> db_;
};

TEST_F(ConcurrentAdapterTest, ConcurrentDatabaseWrites) {
    constexpr int num_threads = 4;
    constexpr int inserts_per_thread = 10;
    std::vector<std::future<bool>> futures;

    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [this, t]() -> bool {
            for (int i = 0; i < inserts_per_thread; ++i) {
                auto scope = connection_scope::acquire(*db_->adapter);
                if (!scope.has_value()) {
                    return false;
                }
                auto& conn = scope->connection();

                auto stmt = conn.prepare(
                    "INSERT INTO concurrent_test (thread_id, value) VALUES (?, ?)");
                if (!stmt.has_value()) {
                    return false;
                }
                if (!stmt.value()->bind_int64(1, t).has_value()) {
                    return false;
                }
                auto val = "thread_" + std::to_string(t) + "_item_" + std::to_string(i);
                if (!stmt.value()->bind_string(2, val).has_value()) {
                    return false;
                }
                if (!stmt.value()->execute().has_value()) {
                    return false;
                }
            }
            return true;
        }));
    }

    // Wait for all threads
    for (auto& f : futures) {
        EXPECT_TRUE(f.get());
    }

    // Verify total count
    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    auto count = conn.execute("SELECT COUNT(*) FROM concurrent_test");
    ASSERT_TRUE(count.has_value());
    ASSERT_TRUE(count.value()->next());
    EXPECT_EQ(count.value()->current_row().get_int64(0),
              num_threads * inserts_per_thread);
}

TEST_F(ConcurrentAdapterTest, ConcurrentDatabaseReads) {
    // Insert test data
    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    for (int i = 0; i < 20; ++i) {
        auto sql = "INSERT INTO concurrent_test (thread_id, value) VALUES (" +
                   std::to_string(i % 4) + ", 'value_" + std::to_string(i) + "')";
        ASSERT_TRUE(conn.execute(sql).has_value());
    }

    // Concurrent reads
    constexpr int num_readers = 4;
    std::vector<std::future<int64_t>> futures;

    for (int t = 0; t < num_readers; ++t) {
        futures.push_back(std::async(std::launch::async, [this, t]() -> int64_t {
            auto read_scope = connection_scope::acquire(*db_->adapter);
            if (!read_scope.has_value()) {
                return -1;
            }
            auto& read_conn = read_scope->connection();

            auto result = read_conn.execute(
                "SELECT COUNT(*) FROM concurrent_test WHERE thread_id = " +
                std::to_string(t));
            if (!result.has_value() || !result.value()->next()) {
                return -1;
            }
            return result.value()->current_row().get_int64(0);
        }));
    }

    int64_t total = 0;
    for (auto& f : futures) {
        auto count = f.get();
        EXPECT_GE(count, 0);
        total += count;
    }
    EXPECT_EQ(total, 20);
}

// =============================================================================
// Adapter Lifecycle Management Tests
// =============================================================================

class AdapterLifecycleTest : public Test {};

TEST_F(AdapterLifecycleTest, DatabaseAdapterRecreation) {
    // Create, use, destroy, and recreate to verify no state leakage
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto db = create_test_database();
        ASSERT_NE(db->adapter, nullptr);
        EXPECT_TRUE(db->adapter->is_healthy());

        auto schema = db->adapter->execute_schema(
            "CREATE TABLE IF NOT EXISTS lifecycle_test (id INTEGER PRIMARY KEY, data TEXT)");
        ASSERT_TRUE(schema.has_value());

        auto scope = connection_scope::acquire(*db->adapter);
        ASSERT_TRUE(scope.has_value());

        auto insert = scope->connection().execute(
            "INSERT INTO lifecycle_test (data) VALUES ('cycle_" +
            std::to_string(cycle) + "')");
        ASSERT_TRUE(insert.has_value());
    }
}

TEST_F(AdapterLifecycleTest, PacsAdapterReconnection) {
    auto pacs = create_test_pacs_adapter();
    ASSERT_NE(pacs, nullptr);

    // Connect -> disconnect -> reconnect cycle
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto connect = pacs->connect();
        ASSERT_TRUE(connect.has_value());
        EXPECT_TRUE(pacs->is_connected());

        // Perform operation
        auto mpps = pacs->get_mpps_adapter();
        ASSERT_NE(mpps, nullptr);

        mpps_query_params params;
        params.max_results = 5;
        auto query = mpps->query_mpps(params);
        EXPECT_TRUE(query.has_value());

        pacs->disconnect();
        EXPECT_FALSE(pacs->is_connected());
    }
}

TEST_F(AdapterLifecycleTest, AllAdaptersCombinedLifecycle) {
    // Create all adapters
    auto db = create_test_database();
    ASSERT_NE(db->adapter, nullptr);

    auto pacs = create_test_pacs_adapter();
    ASSERT_NE(pacs, nullptr);

    // Initialize all
    auto schema = db->adapter->execute_schema(
        "CREATE TABLE IF NOT EXISTS combined_test (id INTEGER PRIMARY KEY)");
    ASSERT_TRUE(schema.has_value());

    auto connect = pacs->connect();
    ASSERT_TRUE(connect.has_value());

    // Use all adapters
    auto db_scope = connection_scope::acquire(*db->adapter);
    ASSERT_TRUE(db_scope.has_value());
    ASSERT_TRUE(db_scope->connection().execute(
        "INSERT INTO combined_test (id) VALUES (1)").has_value());

    auto mpps = pacs->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Shutdown in reverse order
    pacs->disconnect();
    EXPECT_FALSE(pacs->is_connected());

    // Database cleanup happens automatically via RAII
}

// =============================================================================
// Error Scenario Tests
// =============================================================================

class AdapterErrorTest : public Test {};

TEST_F(AdapterErrorTest, DatabaseInvalidPath) {
    database_config config;
    config.database_path = "/nonexistent/directory/test.db";
    config.pool_size = 1;

    // Factory should still return an adapter (may fail on first use)
    auto adapter = create_database_adapter(config);
    // The adapter may or may not be null depending on implementation
    // If it's not null, operations should fail gracefully
    if (adapter) {
        auto conn = adapter->acquire_connection();
        // Connection may fail on invalid path
        if (conn.has_value()) {
            auto result = conn.value()->execute("SELECT 1");
            // May or may not succeed depending on how error is handled
        }
    }
}

TEST_F(AdapterErrorTest, PacsOperationsWithoutConnect) {
    auto pacs = create_test_pacs_adapter();
    ASSERT_NE(pacs, nullptr);
    EXPECT_FALSE(pacs->is_connected());

    // Operations before connect should handle gracefully
    auto mpps = pacs->get_mpps_adapter();
    // Sub-adapters should still be accessible even before connect
    EXPECT_NE(mpps, nullptr);
}

TEST_F(AdapterErrorTest, DicomDatasetValidation) {
    dicom_dataset dataset;
    EXPECT_TRUE(dataset.attributes.empty());
    EXPECT_EQ(dataset.sop_class_uid, "");
    EXPECT_EQ(dataset.sop_instance_uid, "");

    dataset.set_string(0x00100020, "PAT_TEST");
    EXPECT_TRUE(dataset.has_tag(0x00100020));

    auto value = dataset.get_string(0x00100020);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "PAT_TEST");

    dataset.remove_tag(0x00100020);
    EXPECT_FALSE(dataset.has_tag(0x00100020));

    auto missing = dataset.get_string(0x00100020);
    EXPECT_FALSE(missing.has_value());
}

TEST_F(AdapterErrorTest, MppsRecordValidation) {
    mpps_record record;
    // Empty record should not be valid
    EXPECT_FALSE(record.is_valid());

    // Fill all required fields (sop_instance_uid, scheduled/performed_procedure_step_id, status)
    record.sop_instance_uid = "1.2.3.4.5";
    record.scheduled_procedure_step_id = "SPS001";
    record.performed_procedure_step_id = "PPS001";
    record.status = "IN PROGRESS";
    record.patient_id = "PAT001";
    record.start_datetime = std::chrono::system_clock::now();
    EXPECT_TRUE(record.is_valid());
}

TEST_F(AdapterErrorTest, MwlItemValidation) {
    mwl_item item;
    EXPECT_FALSE(item.is_valid());

    // All required fields: accession_number, scheduled_procedure_step_id,
    // patient_id, patient_name, modality
    item.accession_number = "ACC001";
    item.scheduled_procedure_step_id = "SPS001";
    item.patient_id = "PAT001";
    item.patient_name = "DOE^JOHN";
    item.modality = "CT";
    item.scheduled_datetime = std::chrono::system_clock::now();
    EXPECT_TRUE(item.is_valid());
}

// =============================================================================
// Conditional System Integration Tests
// =============================================================================

#ifdef PACS_BRIDGE_HAS_DATABASE_SYSTEM

class DatabaseSystemIntegrationTest : public Test {
protected:
    void SetUp() override {
        // When database_system is available, the factory function
        // may use database_pool_adapter instead of sqlite_database_adapter
        db_ = create_test_database();
        ASSERT_NE(db_->adapter, nullptr);
    }

    void TearDown() override {
        db_.reset();
    }

    std::unique_ptr<test_database> db_;
};

TEST_F(DatabaseSystemIntegrationTest, PooledConnectionBehavior) {
    EXPECT_TRUE(db_->adapter->is_healthy());
    EXPECT_GT(db_->adapter->available_connections(), 0u);
}

#endif  // PACS_BRIDGE_HAS_DATABASE_SYSTEM

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM

class PacsSystemIntegrationTest : public Test {
protected:
    void SetUp() override {
        pacs_ = create_test_pacs_adapter();
        ASSERT_NE(pacs_, nullptr);
    }

    void TearDown() override {
        if (pacs_ && pacs_->is_connected()) {
            pacs_->disconnect();
        }
        pacs_.reset();
    }

    std::shared_ptr<pacs_adapter> pacs_;
};

TEST_F(PacsSystemIntegrationTest, SystemBackedOperations) {
    auto connect = pacs_->connect();
    EXPECT_TRUE(connect.has_value());
    EXPECT_TRUE(pacs_->is_healthy());
}

#endif  // PACS_BRIDGE_HAS_PACS_SYSTEM

}  // namespace
}  // namespace pacs::bridge::integration
