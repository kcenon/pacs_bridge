/**
 * @file database_adapter_test.cpp
 * @brief Integration tests for database_system adapter
 *
 * Tests for connection pooling, query execution, prepared statements,
 * transaction handling, and error scenarios.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/300
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/integration/database_adapter.h"
#include "test_helpers.h"

#include <chrono>
#include <filesystem>
#include <thread>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Test Fixture
// =============================================================================

class DatabaseAdapterTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        pacs_bridge_test::SetUp();

        // Create temporary database path for tests
        test_db_path_ = std::filesystem::temp_directory_path() /
                        ("test_db_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".db");

        // Clean up any existing test database
        std::filesystem::remove(test_db_path_);
    }

    void TearDown() override {
        // Clean up test database
        adapter_.reset();
        std::filesystem::remove(test_db_path_);

        pacs_bridge_test::TearDown();
    }

    /**
     * @brief Create test database adapter with default configuration
     */
    void create_adapter(std::size_t pool_size = 5) {
        database_config config;
        config.database_path = test_db_path_.string();
        config.pool_size = pool_size;
        config.connection_timeout = std::chrono::seconds{10};
        config.query_timeout = std::chrono::seconds{30};
        config.enable_wal = true;

        adapter_ = create_database_adapter(config);
        ASSERT_NE(adapter_, nullptr);
    }

    /**
     * @brief Create test table in database
     */
    void create_test_table() {
        auto result = adapter_->execute_schema(
            "CREATE TABLE test_users ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT NOT NULL,"
            "  age INTEGER NOT NULL,"
            "  score REAL,"
            "  data BLOB"
            ")"
        );
        ASSERT_EXPECTED_OK(result);
    }

    /**
     * @brief Insert test data into test_users table
     */
    void insert_test_data() {
        auto conn_scope = connection_scope::acquire(*adapter_);
        ASSERT_EXPECTED_OK(conn_scope);

        auto& conn = conn_scope->connection();

        // Insert multiple test users
        for (int i = 1; i <= 5; ++i) {
            std::string sql = "INSERT INTO test_users (name, age, score) VALUES ('User" +
                             std::to_string(i) + "', " + std::to_string(20 + i) +
                             ", " + std::to_string(85.5 + i) + ")";
            auto result = conn.execute(sql);
            ASSERT_EXPECTED_OK(result);
        }
    }

    std::filesystem::path test_db_path_;
    std::shared_ptr<database_adapter> adapter_;
};

// =============================================================================
// Basic Configuration Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, CreateAdapter) {
    database_config config;
    config.database_path = test_db_path_.string();
    config.pool_size = 3;

    auto adapter = create_database_adapter(config);

    ASSERT_NE(adapter, nullptr);
    EXPECT_EQ(adapter->config().database_path, test_db_path_.string());
    EXPECT_EQ(adapter->config().pool_size, 3u);
    EXPECT_TRUE(adapter->is_healthy());
}

TEST_F(DatabaseAdapterTest, InvalidConfiguration) {
    database_config config;
    config.database_path = "";  // Empty path

    // Should still create adapter, but may fail on first connection
    auto adapter = create_database_adapter(config);
    ASSERT_NE(adapter, nullptr);
}

// =============================================================================
// Connection Pool Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, ConnectionPoolBasic) {
    create_adapter(3);  // Pool size = 3

    // Initially all connections available
    EXPECT_EQ(adapter_->available_connections(), 3u);
    EXPECT_EQ(adapter_->active_connections(), 0u);

    // Acquire one connection
    auto conn1 = adapter_->acquire_connection();
    ASSERT_EXPECTED_OK(conn1);
    EXPECT_EQ(adapter_->available_connections(), 2u);
    EXPECT_EQ(adapter_->active_connections(), 1u);

    // Acquire second connection
    auto conn2 = adapter_->acquire_connection();
    ASSERT_EXPECTED_OK(conn2);
    EXPECT_EQ(adapter_->available_connections(), 1u);
    EXPECT_EQ(adapter_->active_connections(), 2u);

    // Release first connection
    adapter_->release_connection(*conn1);
    EXPECT_EQ(adapter_->available_connections(), 2u);
    EXPECT_EQ(adapter_->active_connections(), 1u);
}

TEST_F(DatabaseAdapterTest, ConnectionPoolExhaustion) {
    create_adapter(2);  // Small pool size

    auto conn1 = adapter_->acquire_connection();
    ASSERT_EXPECTED_OK(conn1);

    auto conn2 = adapter_->acquire_connection();
    ASSERT_EXPECTED_OK(conn2);

    // Pool should be exhausted
    EXPECT_EQ(adapter_->available_connections(), 0u);
    EXPECT_EQ(adapter_->active_connections(), 2u);

    // Attempting to acquire another should fail or timeout
    // Note: Actual behavior depends on implementation
    // (may block or return error immediately)
}

TEST_F(DatabaseAdapterTest, ConnectionScope) {
    create_adapter();

    {
        auto scope = connection_scope::acquire(*adapter_);
        ASSERT_EXPECTED_OK(scope);

        EXPECT_EQ(adapter_->active_connections(), 1u);

        auto& conn = scope->connection();
        EXPECT_TRUE(conn.is_valid());
    }

    // Connection should be released after scope
    EXPECT_EQ(adapter_->active_connections(), 0u);
}

// =============================================================================
// Direct Query Execution Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, ExecuteSchemaCreateTable) {
    create_adapter();

    auto result = adapter_->execute_schema(
        "CREATE TABLE users ("
        "  id INTEGER PRIMARY KEY,"
        "  username TEXT NOT NULL"
        ")"
    );

    ASSERT_EXPECTED_OK(result);
}

TEST_F(DatabaseAdapterTest, ExecuteDirectQuery) {
    create_adapter();
    create_test_table();
    insert_test_data();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto& conn = conn_scope->connection();

    auto result = conn.execute("SELECT name, age FROM test_users WHERE age > 22");
    ASSERT_EXPECTED_OK(result);

    auto& rs = *result.value();
    int count = 0;
    while (rs.next()) {
        count++;
        const auto& row = rs.current_row();
        EXPECT_FALSE(row.is_null(0));
        EXPECT_FALSE(row.is_null(1));
    }

    EXPECT_GT(count, 0);
}

TEST_F(DatabaseAdapterTest, QueryResultIteration) {
    create_adapter();
    create_test_table();
    insert_test_data();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto result = conn_scope->connection().execute(
        "SELECT id, name, age, score FROM test_users ORDER BY id"
    );
    ASSERT_EXPECTED_OK(result);

    auto& rs = *result.value();

    int id_counter = 1;
    while (rs.next()) {
        const auto& row = rs.current_row();

        EXPECT_EQ(row.column_count(), 4u);
        EXPECT_EQ(row.get_int64(0), id_counter);
        EXPECT_THAT(row.get_string(1), HasSubstr("User"));
        EXPECT_GT(row.get_int64(2), 20);
        EXPECT_GT(row.get_double(3), 85.0);

        id_counter++;
    }

    EXPECT_EQ(id_counter, 6);  // 5 users inserted
}

TEST_F(DatabaseAdapterTest, InsertAndLastInsertId) {
    create_adapter();
    create_test_table();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto result = conn_scope->connection().execute(
        "INSERT INTO test_users (name, age, score) VALUES ('Alice', 25, 90.5)"
    );
    ASSERT_EXPECTED_OK(result);

    auto& rs = *result.value();
    EXPECT_GT(rs.last_insert_id(), 0);
}

TEST_F(DatabaseAdapterTest, UpdateAndAffectedRows) {
    create_adapter();
    create_test_table();
    insert_test_data();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto result = conn_scope->connection().execute(
        "UPDATE test_users SET age = 30 WHERE age < 23"
    );
    ASSERT_EXPECTED_OK(result);

    auto& rs = *result.value();
    EXPECT_GT(rs.affected_rows(), 0u);
}

// =============================================================================
// Prepared Statement Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, PrepareStatementBasic) {
    create_adapter();
    create_test_table();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto stmt = conn_scope->connection().prepare(
        "INSERT INTO test_users (name, age, score) VALUES (?, ?, ?)"
    );
    ASSERT_EXPECTED_OK(stmt);

    EXPECT_EQ(stmt.value()->parameter_count(), 3u);
}

TEST_F(DatabaseAdapterTest, PreparedStatementBindString) {
    create_adapter();
    create_test_table();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto stmt = conn_scope->connection().prepare(
        "INSERT INTO test_users (name, age, score) VALUES (?, ?, ?)"
    );
    ASSERT_EXPECTED_OK(stmt);

    auto& prepared = *stmt.value();

    ASSERT_EXPECTED_OK(prepared.bind_string(1, "Bob"));
    ASSERT_EXPECTED_OK(prepared.bind_int64(2, 28));
    ASSERT_EXPECTED_OK(prepared.bind_double(3, 88.5));

    auto result = prepared.execute();
    ASSERT_EXPECTED_OK(result);

    EXPECT_GT(result.value()->last_insert_id(), 0);
}

TEST_F(DatabaseAdapterTest, PreparedStatementBindAllTypes) {
    create_adapter();

    // Create table with all types
    auto create_result = adapter_->execute_schema(
        "CREATE TABLE all_types ("
        "  id INTEGER PRIMARY KEY,"
        "  text_col TEXT,"
        "  int_col INTEGER,"
        "  real_col REAL,"
        "  blob_col BLOB,"
        "  null_col INTEGER"
        ")"
    );
    ASSERT_EXPECTED_OK(create_result);

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto stmt = conn_scope->connection().prepare(
        "INSERT INTO all_types (text_col, int_col, real_col, blob_col, null_col) "
        "VALUES (?, ?, ?, ?, ?)"
    );
    ASSERT_EXPECTED_OK(stmt);

    auto& prepared = *stmt.value();

    std::vector<uint8_t> blob_data = {0x01, 0x02, 0x03, 0x04};

    ASSERT_EXPECTED_OK(prepared.bind_string(1, "test"));
    ASSERT_EXPECTED_OK(prepared.bind_int64(2, 42));
    ASSERT_EXPECTED_OK(prepared.bind_double(3, 3.14));
    ASSERT_EXPECTED_OK(prepared.bind_blob(4, blob_data));
    ASSERT_EXPECTED_OK(prepared.bind_null(5));

    auto result = prepared.execute();
    ASSERT_EXPECTED_OK(result);
}

TEST_F(DatabaseAdapterTest, PreparedStatementReuse) {
    create_adapter();
    create_test_table();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto stmt = conn_scope->connection().prepare(
        "INSERT INTO test_users (name, age, score) VALUES (?, ?, ?)"
    );
    ASSERT_EXPECTED_OK(stmt);

    auto& prepared = *stmt.value();

    // Execute with different values
    for (int i = 1; i <= 3; ++i) {
        ASSERT_EXPECTED_OK(prepared.bind_string(1, "User" + std::to_string(i)));
        ASSERT_EXPECTED_OK(prepared.bind_int64(2, 20 + i));
        ASSERT_EXPECTED_OK(prepared.bind_double(3, 85.0 + i));

        auto result = prepared.execute();
        ASSERT_EXPECTED_OK(result);

        ASSERT_EXPECTED_OK(prepared.reset());
        ASSERT_EXPECTED_OK(prepared.clear_bindings());
    }

    // Verify all inserted
    auto count_result = conn_scope->connection().execute(
        "SELECT COUNT(*) FROM test_users"
    );
    ASSERT_EXPECTED_OK(count_result);

    auto& rs = *count_result.value();
    ASSERT_TRUE(rs.next());
    EXPECT_EQ(rs.current_row().get_int64(0), 3);
}

// =============================================================================
// Transaction Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, TransactionCommit) {
    create_adapter();
    create_test_table();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto& conn = conn_scope->connection();

    ASSERT_EXPECTED_OK(conn.begin_transaction());

    auto insert1 = conn.execute(
        "INSERT INTO test_users (name, age, score) VALUES ('Alice', 25, 90.0)"
    );
    ASSERT_EXPECTED_OK(insert1);

    auto insert2 = conn.execute(
        "INSERT INTO test_users (name, age, score) VALUES ('Bob', 30, 85.0)"
    );
    ASSERT_EXPECTED_OK(insert2);

    ASSERT_EXPECTED_OK(conn.commit());

    // Verify data committed
    auto result = conn.execute("SELECT COUNT(*) FROM test_users");
    ASSERT_EXPECTED_OK(result);

    auto& rs = *result.value();
    ASSERT_TRUE(rs.next());
    EXPECT_EQ(rs.current_row().get_int64(0), 2);
}

TEST_F(DatabaseAdapterTest, TransactionRollback) {
    create_adapter();
    create_test_table();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto& conn = conn_scope->connection();

    ASSERT_EXPECTED_OK(conn.begin_transaction());

    auto insert1 = conn.execute(
        "INSERT INTO test_users (name, age, score) VALUES ('Alice', 25, 90.0)"
    );
    ASSERT_EXPECTED_OK(insert1);

    ASSERT_EXPECTED_OK(conn.rollback());

    // Verify data NOT committed
    auto result = conn.execute("SELECT COUNT(*) FROM test_users");
    ASSERT_EXPECTED_OK(result);

    auto& rs = *result.value();
    ASSERT_TRUE(rs.next());
    EXPECT_EQ(rs.current_row().get_int64(0), 0);
}

TEST_F(DatabaseAdapterTest, TransactionGuard) {
    create_adapter();
    create_test_table();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto& conn = conn_scope->connection();

    {
        auto guard = transaction_guard::begin(conn);
        ASSERT_EXPECTED_OK(guard);

        auto insert = conn.execute(
            "INSERT INTO test_users (name, age, score) VALUES ('Alice', 25, 90.0)"
        );
        ASSERT_EXPECTED_OK(insert);

        // Commit explicitly
        ASSERT_EXPECTED_OK(guard->commit());
    }

    // Verify data committed
    auto result = conn.execute("SELECT COUNT(*) FROM test_users");
    ASSERT_EXPECTED_OK(result);

    auto& rs = *result.value();
    ASSERT_TRUE(rs.next());
    EXPECT_EQ(rs.current_row().get_int64(0), 1);
}

TEST_F(DatabaseAdapterTest, TransactionGuardAutoRollback) {
    create_adapter();
    create_test_table();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto& conn = conn_scope->connection();

    {
        auto guard = transaction_guard::begin(conn);
        ASSERT_EXPECTED_OK(guard);

        auto insert = conn.execute(
            "INSERT INTO test_users (name, age, score) VALUES ('Alice', 25, 90.0)"
        );
        ASSERT_EXPECTED_OK(insert);

        // No commit - should auto-rollback
    }

    // Verify data NOT committed
    auto result = conn.execute("SELECT COUNT(*) FROM test_users");
    ASSERT_EXPECTED_OK(result);

    auto& rs = *result.value();
    ASSERT_TRUE(rs.next());
    EXPECT_EQ(rs.current_row().get_int64(0), 0);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, InvalidSQL) {
    create_adapter();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto result = conn_scope->connection().execute("INVALID SQL STATEMENT");
    ASSERT_EXPECTED_ERROR(result);
}

TEST_F(DatabaseAdapterTest, PrepareInvalidSQL) {
    create_adapter();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto stmt = conn_scope->connection().prepare("SELECT * FROM nonexistent_table");
    // Preparation might succeed, but execution will fail
}

TEST_F(DatabaseAdapterTest, QueryNonexistentTable) {
    create_adapter();

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto result = conn_scope->connection().execute(
        "SELECT * FROM nonexistent_table"
    );
    ASSERT_EXPECTED_ERROR(result);
}

TEST_F(DatabaseAdapterTest, ConstraintViolation) {
    create_adapter();

    auto create_result = adapter_->execute_schema(
        "CREATE TABLE unique_test ("
        "  id INTEGER PRIMARY KEY,"
        "  username TEXT UNIQUE NOT NULL"
        ")"
    );
    ASSERT_EXPECTED_OK(create_result);

    auto conn_scope = connection_scope::acquire(*adapter_);
    ASSERT_EXPECTED_OK(conn_scope);

    auto& conn = conn_scope->connection();

    // Insert first user
    auto insert1 = conn.execute(
        "INSERT INTO unique_test (username) VALUES ('alice')"
    );
    ASSERT_EXPECTED_OK(insert1);

    // Try to insert duplicate username
    auto insert2 = conn.execute(
        "INSERT INTO unique_test (username) VALUES ('alice')"
    );
    ASSERT_EXPECTED_ERROR(insert2);
}

// =============================================================================
// Conditional Compilation - database_system Integration Tests
// =============================================================================

#ifdef PACS_BRIDGE_HAS_DATABASE_SYSTEM

TEST_F(DatabaseAdapterTest, DatabaseSystemPoolIntegration) {
    // Test that database_system pool integration works
    // This test requires database_system to be available

    database_config config;
    config.connection_string = "test_connection_string";
    config.pool_size = 5;

    // Note: This requires actual database_system::database_pool implementation
    // which is part of issue #299
    GTEST_SKIP() << "database_system integration pending (issue #299)";
}

TEST_F(DatabaseAdapterTest, PerformanceComparison) {
    // Compare performance between database_system and SQLite
    GTEST_SKIP() << "Performance comparison pending (issue #299)";
}

TEST_F(DatabaseAdapterTest, FallbackBehavior) {
    // Test fallback to SQLite when database_system unavailable
    GTEST_SKIP() << "Fallback behavior pending (issue #299)";
}

#endif  // PACS_BRIDGE_HAS_DATABASE_SYSTEM

}  // namespace
}  // namespace pacs::bridge::integration
