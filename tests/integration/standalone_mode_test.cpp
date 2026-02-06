/**
 * @file standalone_mode_test.cpp
 * @brief Integration tests verifying standalone (fallback) adapter operation
 *
 * Tests that all adapters function correctly in standalone mode, without
 * any external system modules (database_system, network_system, etc.).
 * This validates the fallback implementations:
 *   - SQLite database adapter
 *   - BSD socket MLLP adapter
 *   - Memory-based PACS adapter
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
// Database Adapter Standalone Tests
// =============================================================================

class StandaloneDatabaseTest : public Test {
protected:
    void SetUp() override {
        db_ = create_test_database();
        ASSERT_NE(db_->adapter, nullptr);
    }

    void TearDown() override {
        db_.reset();
    }

    void create_test_table() {
        auto result = db_->adapter->execute_schema(
            "CREATE TABLE IF NOT EXISTS test_data ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  key TEXT NOT NULL UNIQUE,"
            "  value TEXT,"
            "  created_at INTEGER DEFAULT (strftime('%s','now'))"
            ")"
        );
        ASSERT_TRUE(result.has_value()) << "Failed to create test table";
    }

    std::unique_ptr<test_database> db_;
};

TEST_F(StandaloneDatabaseTest, AdapterCreation) {
    EXPECT_TRUE(db_->adapter->is_healthy());
    EXPECT_GT(db_->adapter->available_connections(), 0u);
    EXPECT_EQ(db_->adapter->active_connections(), 0u);
}

TEST_F(StandaloneDatabaseTest, SchemaExecution) {
    create_test_table();
    EXPECT_TRUE(db_->adapter->is_healthy());
}

TEST_F(StandaloneDatabaseTest, ConnectionAcquireRelease) {
    auto conn_result = db_->adapter->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());

    auto conn = std::move(conn_result.value());
    EXPECT_TRUE(conn->is_valid());

    db_->adapter->release_connection(std::move(conn));
}

TEST_F(StandaloneDatabaseTest, CrudOperations) {
    create_test_table();

    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    // INSERT
    auto insert = conn.execute(
        "INSERT INTO test_data (key, value) VALUES ('test_key', 'test_value')");
    ASSERT_TRUE(insert.has_value());
    EXPECT_EQ(conn.changes(), 1);

    // SELECT
    auto select = conn.execute("SELECT key, value FROM test_data WHERE key = 'test_key'");
    ASSERT_TRUE(select.has_value());
    ASSERT_TRUE(select.value()->next());
    EXPECT_EQ(select.value()->current_row().get_string(0), "test_key");
    EXPECT_EQ(select.value()->current_row().get_string(1), "test_value");

    // UPDATE
    auto update = conn.execute(
        "UPDATE test_data SET value = 'updated' WHERE key = 'test_key'");
    ASSERT_TRUE(update.has_value());
    EXPECT_EQ(conn.changes(), 1);

    // DELETE
    auto del = conn.execute("DELETE FROM test_data WHERE key = 'test_key'");
    ASSERT_TRUE(del.has_value());
    EXPECT_EQ(conn.changes(), 1);

    // Verify deletion
    auto verify = conn.execute("SELECT COUNT(*) FROM test_data WHERE key = 'test_key'");
    ASSERT_TRUE(verify.has_value());
    ASSERT_TRUE(verify.value()->next());
    EXPECT_EQ(verify.value()->current_row().get_int64(0), 0);
}

TEST_F(StandaloneDatabaseTest, PreparedStatements) {
    create_test_table();

    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    auto stmt = conn.prepare("INSERT INTO test_data (key, value) VALUES (?, ?)");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt.value()->parameter_count(), 2u);

    auto bind1 = stmt.value()->bind_string(1, "prepared_key");
    ASSERT_TRUE(bind1.has_value());
    auto bind2 = stmt.value()->bind_string(2, "prepared_value");
    ASSERT_TRUE(bind2.has_value());

    auto exec = stmt.value()->execute();
    ASSERT_TRUE(exec.has_value());
}

TEST_F(StandaloneDatabaseTest, TransactionCommit) {
    create_test_table();

    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    auto guard = transaction_guard::begin(conn);
    ASSERT_TRUE(guard.has_value());

    auto insert = conn.execute(
        "INSERT INTO test_data (key, value) VALUES ('txn_key', 'txn_value')");
    ASSERT_TRUE(insert.has_value());

    auto commit = guard->commit();
    ASSERT_TRUE(commit.has_value());

    // Verify data persisted
    auto select = conn.execute("SELECT value FROM test_data WHERE key = 'txn_key'");
    ASSERT_TRUE(select.has_value());
    ASSERT_TRUE(select.value()->next());
    EXPECT_EQ(select.value()->current_row().get_string(0), "txn_value");
}

TEST_F(StandaloneDatabaseTest, TransactionRollback) {
    create_test_table();

    auto scope = connection_scope::acquire(*db_->adapter);
    ASSERT_TRUE(scope.has_value());
    auto& conn = scope->connection();

    {
        auto guard = transaction_guard::begin(conn);
        ASSERT_TRUE(guard.has_value());

        auto insert = conn.execute(
            "INSERT INTO test_data (key, value) VALUES ('rollback_key', 'rollback_value')");
        ASSERT_TRUE(insert.has_value());

        // guard goes out of scope without commit -> auto rollback
    }

    // Verify data was rolled back
    auto select = conn.execute("SELECT COUNT(*) FROM test_data WHERE key = 'rollback_key'");
    ASSERT_TRUE(select.has_value());
    ASSERT_TRUE(select.value()->next());
    EXPECT_EQ(select.value()->current_row().get_int64(0), 0);
}

TEST_F(StandaloneDatabaseTest, ConnectionPoolExhaustion) {
    // Create adapter with small pool
    auto small_db = create_test_database(2);
    ASSERT_NE(small_db->adapter, nullptr);

    auto conn1 = small_db->adapter->acquire_connection();
    ASSERT_TRUE(conn1.has_value());

    auto conn2 = small_db->adapter->acquire_connection();
    ASSERT_TRUE(conn2.has_value());

    EXPECT_EQ(small_db->adapter->available_connections(), 0u);
    EXPECT_EQ(small_db->adapter->active_connections(), 2u);

    // Release one connection
    small_db->adapter->release_connection(std::move(conn1.value()));
    EXPECT_EQ(small_db->adapter->available_connections(), 1u);
}

// =============================================================================
// PACS Adapter Standalone Tests
// =============================================================================

class StandalonePacsTest : public Test {
protected:
    void SetUp() override {
        adapter_ = create_test_pacs_adapter();
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

TEST_F(StandalonePacsTest, AdapterCreation) {
    EXPECT_NE(adapter_, nullptr);
}

TEST_F(StandalonePacsTest, ConnectDisconnect) {
    auto result = adapter_->connect();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(adapter_->is_connected());
    EXPECT_TRUE(adapter_->is_healthy());

    adapter_->disconnect();
    EXPECT_FALSE(adapter_->is_connected());
}

TEST_F(StandalonePacsTest, MppsCreateValidRecord) {
    auto connect = adapter_->connect();
    ASSERT_TRUE(connect.has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Create valid MPPS record - stub accepts and validates
    mpps_record record;
    record.sop_instance_uid = "1.2.3.4.5.100";
    record.scheduled_procedure_step_id = "SPS001";
    record.performed_procedure_step_id = "PPS001";
    record.performed_station_ae_title = "CT01";
    record.performed_station_name = "CT Scanner";
    record.performed_location = "Room 101";
    record.start_datetime = std::chrono::system_clock::now();
    record.status = "IN PROGRESS";
    record.study_instance_uid = "1.2.3.4.5.200";
    record.patient_id = "PAT001";
    record.patient_name = "DOE^JOHN";

    auto create_result = mpps->create_mpps(record);
    EXPECT_TRUE(create_result.has_value());

    // Stub get_mpps returns not_found (no-op storage)
    auto get_result = mpps->get_mpps(record.sop_instance_uid);
    EXPECT_FALSE(get_result.has_value());
    EXPECT_EQ(get_result.error(), pacs_error::not_found);
}

TEST_F(StandalonePacsTest, MppsCreateInvalidRecord) {
    auto connect = adapter_->connect();
    ASSERT_TRUE(connect.has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Empty record should fail validation
    mpps_record invalid_record;
    auto result = mpps->create_mpps(invalid_record);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pacs_error::validation_failed);
}

TEST_F(StandalonePacsTest, MppsQueryReturnsEmpty) {
    auto connect = adapter_->connect();
    ASSERT_TRUE(connect.has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Stub query returns empty result set
    mpps_query_params params;
    params.max_results = 10;
    auto result = mpps->query_mpps(params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty());
}

TEST_F(StandalonePacsTest, MppsUpdateValidRecord) {
    auto connect = adapter_->connect();
    ASSERT_TRUE(connect.has_value());

    auto mpps = adapter_->get_mpps_adapter();
    ASSERT_NE(mpps, nullptr);

    // Create and update in stub mode - both should validate and succeed
    mpps_record record;
    record.sop_instance_uid = "1.2.3.4.5.101";
    record.scheduled_procedure_step_id = "SPS002";
    record.performed_procedure_step_id = "PPS002";
    record.performed_station_ae_title = "MR01";
    record.start_datetime = std::chrono::system_clock::now();
    record.status = "IN PROGRESS";
    record.study_instance_uid = "1.2.3.4.5.201";
    record.patient_id = "PAT002";
    record.patient_name = "SMITH^JANE";

    EXPECT_TRUE(mpps->create_mpps(record).has_value());

    // Update to COMPLETED (must include end_datetime for validation)
    record.status = "COMPLETED";
    record.end_datetime = std::chrono::system_clock::now();
    auto update = mpps->update_mpps(record);
    EXPECT_TRUE(update.has_value());
}

TEST_F(StandalonePacsTest, MwlQuery) {
    auto connect = adapter_->connect();
    ASSERT_TRUE(connect.has_value());

    auto mwl = adapter_->get_mwl_adapter();
    ASSERT_NE(mwl, nullptr);

    mwl_query_params params;
    params.max_results = 10;
    auto result = mwl->query_mwl(params);
    EXPECT_TRUE(result.has_value());
}

TEST_F(StandalonePacsTest, StorageStubBehavior) {
    auto connect = adapter_->connect();
    ASSERT_TRUE(connect.has_value());

    auto storage = adapter_->get_storage_adapter();
    ASSERT_NE(storage, nullptr);

    // Store succeeds (validation only, no actual storage in stub)
    dicom_dataset dataset;
    dataset.sop_class_uid = "1.2.840.10008.5.1.4.1.1.2";
    dataset.sop_instance_uid = "1.2.3.4.5.300";
    dataset.set_string(0x00100020, "PAT003");
    dataset.set_string(0x00100010, "WILSON^BOB");

    auto store = storage->store(dataset);
    EXPECT_TRUE(store.has_value());

    // Stub always returns false for exists and not_found for retrieve
    EXPECT_FALSE(storage->exists(dataset.sop_instance_uid));

    auto retrieve = storage->retrieve(dataset.sop_instance_uid);
    EXPECT_FALSE(retrieve.has_value());
    EXPECT_EQ(retrieve.error(), pacs_error::not_found);
}

TEST_F(StandalonePacsTest, StorageInvalidDatasetRejected) {
    auto connect = adapter_->connect();
    ASSERT_TRUE(connect.has_value());

    auto storage = adapter_->get_storage_adapter();
    ASSERT_NE(storage, nullptr);

    // Empty SOP Instance UID should fail
    dicom_dataset empty_dataset;
    auto result = storage->store(empty_dataset);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), pacs_error::invalid_dataset);
}

TEST_F(StandalonePacsTest, SubAdaptersAvailable) {
    auto connect = adapter_->connect();
    ASSERT_TRUE(connect.has_value());

    EXPECT_NE(adapter_->get_mpps_adapter(), nullptr);
    EXPECT_NE(adapter_->get_mwl_adapter(), nullptr);
    EXPECT_NE(adapter_->get_storage_adapter(), nullptr);
}

// =============================================================================
// MLLP Network Adapter Standalone Tests
// =============================================================================

class StandaloneMllpTest : public Test {
protected:
    void SetUp() override {
        test_port_ = generate_test_port();
    }

    uint16_t test_port_;
};

TEST_F(StandaloneMllpTest, ServerConfigValidation) {
    mllp::server_config config;
    config.port = test_port_;
    EXPECT_TRUE(config.is_valid());

    mllp::server_config invalid_config;
    invalid_config.port = 0;
    EXPECT_FALSE(invalid_config.is_valid());
}

TEST_F(StandaloneMllpTest, ErrorCodeDescriptions) {
    EXPECT_NE(std::string(mllp::to_string(mllp::network_error::timeout)), "");
    EXPECT_NE(std::string(mllp::to_string(mllp::network_error::connection_closed)), "");
    EXPECT_NE(std::string(mllp::to_string(mllp::network_error::socket_error)), "");
    EXPECT_NE(std::string(mllp::to_string(mllp::network_error::bind_failed)), "");
    EXPECT_NE(std::string(mllp::to_string(mllp::network_error::tls_handshake_failed)), "");
    EXPECT_NE(std::string(mllp::to_string(mllp::network_error::invalid_config)), "");
    EXPECT_NE(std::string(mllp::to_string(mllp::network_error::would_block)), "");
    EXPECT_NE(std::string(mllp::to_string(mllp::network_error::connection_refused)), "");
}

TEST_F(StandaloneMllpTest, SessionStatsDefaultValues) {
    mllp::session_stats stats;
    EXPECT_EQ(stats.bytes_received, 0u);
    EXPECT_EQ(stats.bytes_sent, 0u);
    EXPECT_EQ(stats.messages_received, 0u);
    EXPECT_EQ(stats.messages_sent, 0u);
}

// =============================================================================
// Cross-Adapter Resource Cleanup Tests
// =============================================================================

class StandaloneResourceCleanupTest : public Test {
protected:
    void SetUp() override {
        db_ = create_test_database();
        ASSERT_NE(db_->adapter, nullptr);

        pacs_ = create_test_pacs_adapter();
        ASSERT_NE(pacs_, nullptr);
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

TEST_F(StandaloneResourceCleanupTest, DatabaseCleanShutdown) {
    // Acquire and use connections
    auto conn1 = db_->adapter->acquire_connection();
    ASSERT_TRUE(conn1.has_value());

    auto conn2 = db_->adapter->acquire_connection();
    ASSERT_TRUE(conn2.has_value());

    // Release connections
    db_->adapter->release_connection(std::move(conn1.value()));
    db_->adapter->release_connection(std::move(conn2.value()));

    // Adapter should be healthy after releasing all connections
    EXPECT_TRUE(db_->adapter->is_healthy());
    EXPECT_EQ(db_->adapter->active_connections(), 0u);
}

TEST_F(StandaloneResourceCleanupTest, PacsCleanShutdown) {
    auto connect = pacs_->connect();
    ASSERT_TRUE(connect.has_value());
    EXPECT_TRUE(pacs_->is_connected());

    pacs_->disconnect();
    EXPECT_FALSE(pacs_->is_connected());
}

TEST_F(StandaloneResourceCleanupTest, MultipleAdapterLifecycles) {
    // Create and destroy adapters multiple times to check for resource leaks
    for (int i = 0; i < 3; ++i) {
        auto temp_db = create_test_database();
        ASSERT_NE(temp_db->adapter, nullptr);
        EXPECT_TRUE(temp_db->adapter->is_healthy());

        auto temp_pacs = create_test_pacs_adapter();
        ASSERT_NE(temp_pacs, nullptr);
        auto connect = temp_pacs->connect();
        EXPECT_TRUE(connect.has_value());
        temp_pacs->disconnect();
    }
}

}  // namespace
}  // namespace pacs::bridge::integration
