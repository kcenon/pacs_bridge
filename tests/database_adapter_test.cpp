/**
 * @file database_adapter_test.cpp
 * @brief Unit tests for database adapter implementations
 *
 * Tests for sqlite_database_adapter, connection pooling, transactions,
 * and prepared statements.
 *
 * @see include/pacs/bridge/integration/database_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/274
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/integration/database_adapter.h"

#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;

// =============================================================================
// Test Fixtures
// =============================================================================

class DatabaseAdapterTest : public Test {
protected:
    void SetUp() override {
        // Create a temporary database file for testing
        test_db_path_ = std::filesystem::temp_directory_path() / "test_database_adapter.db";

        // Remove if exists
        std::filesystem::remove(test_db_path_);

        database_config config;
        config.database_path = test_db_path_.string();
        config.pool_size = 3;
        config.enable_wal = true;
        config.busy_timeout_ms = 5000;

        adapter_ = create_database_adapter(config);
    }

    void TearDown() override {
        adapter_.reset();
        std::filesystem::remove(test_db_path_);
    }

    std::filesystem::path test_db_path_;
    std::shared_ptr<database_adapter> adapter_;
};

// =============================================================================
// Error Code Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, ErrorCodeValues) {
    EXPECT_EQ(to_error_code(database_error::connection_failed), -800);
    EXPECT_EQ(to_error_code(database_error::connection_timeout), -801);
    EXPECT_EQ(to_error_code(database_error::query_failed), -802);
    EXPECT_EQ(to_error_code(database_error::prepare_failed), -803);
    EXPECT_EQ(to_error_code(database_error::bind_failed), -804);
    EXPECT_EQ(to_error_code(database_error::transaction_failed), -805);
    EXPECT_EQ(to_error_code(database_error::pool_exhausted), -806);
    EXPECT_EQ(to_error_code(database_error::invalid_config), -807);
    EXPECT_EQ(to_error_code(database_error::constraint_violation), -808);
    EXPECT_EQ(to_error_code(database_error::timeout), -809);
}

TEST_F(DatabaseAdapterTest, ErrorCodeStrings) {
    EXPECT_FALSE(to_string(database_error::connection_failed).empty());
    EXPECT_FALSE(to_string(database_error::query_failed).empty());
    EXPECT_FALSE(to_string(database_error::pool_exhausted).empty());
}

// =============================================================================
// Adapter Creation Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, CreateAdapter) {
    ASSERT_NE(adapter_, nullptr);
    EXPECT_TRUE(adapter_->is_healthy());
}

TEST_F(DatabaseAdapterTest, ConfigAccess) {
    const auto& config = adapter_->config();
    EXPECT_EQ(config.database_path, test_db_path_.string());
    EXPECT_EQ(config.pool_size, 3u);
    EXPECT_TRUE(config.enable_wal);
}

// =============================================================================
// Connection Pool Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, AcquireConnection) {
    auto result = adapter_->acquire_connection();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE((*result)->is_valid());

    adapter_->release_connection(*result);
}

TEST_F(DatabaseAdapterTest, ConnectionPoolSize) {
    EXPECT_GT(adapter_->available_connections(), 0u);
    EXPECT_EQ(adapter_->active_connections(), 0u);

    auto conn = adapter_->acquire_connection();
    ASSERT_TRUE(conn.has_value());

    EXPECT_EQ(adapter_->active_connections(), 1u);

    adapter_->release_connection(*conn);
    EXPECT_EQ(adapter_->active_connections(), 0u);
}

TEST_F(DatabaseAdapterTest, MultipleConnections) {
    std::vector<std::shared_ptr<database_connection>> connections;

    // Acquire multiple connections
    for (int i = 0; i < 3; ++i) {
        auto result = adapter_->acquire_connection();
        ASSERT_TRUE(result.has_value()) << "Failed to acquire connection " << i;
        connections.push_back(*result);
    }

    EXPECT_EQ(adapter_->active_connections(), 3u);

    // Release all connections
    for (auto& conn : connections) {
        adapter_->release_connection(conn);
    }

    EXPECT_EQ(adapter_->active_connections(), 0u);
}

// =============================================================================
// Schema Execution Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, ExecuteSchema) {
    auto result = adapter_->execute_schema(
        "CREATE TABLE IF NOT EXISTS test_table ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "value REAL"
        ")"
    );
    EXPECT_TRUE(result.has_value());
}

TEST_F(DatabaseAdapterTest, ExecuteInvalidSchema) {
    auto result = adapter_->execute_schema("CREATE TABLE");
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// Direct SQL Execution Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, ExecuteDirectSQL) {
    // Create table
    auto schema_result = adapter_->execute_schema(
        "CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)"
    );
    if (!schema_result.has_value()) {
        std::cerr << "Schema error code: " << to_error_code(schema_result.error()) << std::endl;
    }
    ASSERT_TRUE(schema_result.has_value()) << "Schema creation failed";

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    // Insert data
    auto insert_result = conn->execute("INSERT INTO test (name) VALUES ('Alice')");
    if (!insert_result.has_value()) {
        std::cerr << "Insert error: " << conn->last_error() << std::endl;
    }
    ASSERT_TRUE(insert_result.has_value()) << "Insert failed: " << conn->last_error();

    // Step through result to complete execution
    auto& insert_res = *insert_result;
    while (insert_res->next()) {
        // consume any results
    }

    // Query data
    auto select_result = conn->execute("SELECT id, name FROM test");
    if (!select_result.has_value()) {
        std::cerr << "Select error: " << conn->last_error() << std::endl;
    }
    ASSERT_TRUE(select_result.has_value()) << "Select failed: " << conn->last_error();

    auto& result = *select_result;
    EXPECT_TRUE(result->next());

    const auto& row = result->current_row();
    EXPECT_EQ(row.column_count(), 2u);
    EXPECT_EQ(row.get_string(1), "Alice");

    adapter_->release_connection(*conn_result);
}

// =============================================================================
// Prepared Statement Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, PreparedStatement) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, price REAL)"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    // Prepare insert statement
    auto stmt_result = conn->prepare("INSERT INTO items (name, price) VALUES (?, ?)");
    ASSERT_TRUE(stmt_result.has_value());
    auto& stmt = *stmt_result;

    EXPECT_EQ(stmt->parameter_count(), 2u);

    // Bind and execute
    EXPECT_TRUE(stmt->bind_string(1, "Widget").has_value());
    EXPECT_TRUE(stmt->bind_double(2, 19.99).has_value());

    auto exec_result = stmt->execute();
    EXPECT_TRUE(exec_result.has_value());

    adapter_->release_connection(*conn_result);
}

TEST_F(DatabaseAdapterTest, PreparedStatementWithBlob) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE data (id INTEGER PRIMARY KEY, content BLOB)"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    // Insert blob data
    auto stmt = conn->prepare("INSERT INTO data (content) VALUES (?)");
    ASSERT_TRUE(stmt.has_value());

    std::vector<uint8_t> blob_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    EXPECT_TRUE((*stmt)->bind_blob(1, blob_data).has_value());

    auto exec_result = (*stmt)->execute();
    EXPECT_TRUE(exec_result.has_value());

    // Query blob data
    auto select = conn->execute("SELECT content FROM data");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());

    auto retrieved_blob = (*select)->current_row().get_blob(0);
    EXPECT_EQ(retrieved_blob, blob_data);

    adapter_->release_connection(*conn_result);
}

TEST_F(DatabaseAdapterTest, NullBinding) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE nullable (id INTEGER PRIMARY KEY, value TEXT)"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    auto stmt = conn->prepare("INSERT INTO nullable (value) VALUES (?)");
    ASSERT_TRUE(stmt.has_value());

    EXPECT_TRUE((*stmt)->bind_null(1).has_value());
    auto exec_result = (*stmt)->execute();
    EXPECT_TRUE(exec_result.has_value());

    // Query and verify NULL
    auto select = conn->execute("SELECT value FROM nullable");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());
    EXPECT_TRUE((*select)->current_row().is_null(0));

    adapter_->release_connection(*conn_result);
}

// =============================================================================
// Transaction Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, TransactionCommit) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE accounts (id INTEGER PRIMARY KEY, balance REAL)"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    // Insert initial data
    ASSERT_TRUE(conn->execute("INSERT INTO accounts (balance) VALUES (100.0)").has_value());

    // Begin transaction
    EXPECT_TRUE(conn->begin_transaction().has_value());

    // Update balance
    EXPECT_TRUE(conn->execute("UPDATE accounts SET balance = 150.0 WHERE id = 1").has_value());

    // Commit
    EXPECT_TRUE(conn->commit().has_value());

    // Verify change persisted
    auto select = conn->execute("SELECT balance FROM accounts WHERE id = 1");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());
    EXPECT_DOUBLE_EQ((*select)->current_row().get_double(0), 150.0);

    adapter_->release_connection(*conn_result);
}

TEST_F(DatabaseAdapterTest, TransactionRollback) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE accounts (id INTEGER PRIMARY KEY, balance REAL)"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    // Insert initial data
    ASSERT_TRUE(conn->execute("INSERT INTO accounts (balance) VALUES (100.0)").has_value());

    // Begin transaction
    EXPECT_TRUE(conn->begin_transaction().has_value());

    // Update balance
    EXPECT_TRUE(conn->execute("UPDATE accounts SET balance = 50.0 WHERE id = 1").has_value());

    // Rollback
    EXPECT_TRUE(conn->rollback().has_value());

    // Verify change was rolled back
    auto select = conn->execute("SELECT balance FROM accounts WHERE id = 1");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());
    EXPECT_DOUBLE_EQ((*select)->current_row().get_double(0), 100.0);

    adapter_->release_connection(*conn_result);
}

// =============================================================================
// Transaction Guard Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, TransactionGuardCommit) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    {
        auto guard = transaction_guard::begin(*conn);
        ASSERT_TRUE(guard.has_value());

        EXPECT_TRUE(conn->execute("INSERT INTO items (name) VALUES ('test')").has_value());

        EXPECT_TRUE(guard->commit().has_value());
    }

    // Verify data persisted
    auto select = conn->execute("SELECT COUNT(*) FROM items");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());
    EXPECT_EQ((*select)->current_row().get_int64(0), 1);

    adapter_->release_connection(*conn_result);
}

TEST_F(DatabaseAdapterTest, TransactionGuardAutoRollback) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    {
        auto guard = transaction_guard::begin(*conn);
        ASSERT_TRUE(guard.has_value());

        EXPECT_TRUE(conn->execute("INSERT INTO items (name) VALUES ('test')").has_value());

        // Don't commit - let guard go out of scope
    }

    // Verify data was rolled back
    auto select = conn->execute("SELECT COUNT(*) FROM items");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());
    EXPECT_EQ((*select)->current_row().get_int64(0), 0);

    adapter_->release_connection(*conn_result);
}

// =============================================================================
// Connection Scope Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, ConnectionScopeGuard) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE test (id INTEGER PRIMARY KEY)"
    ).has_value());

    size_t initial_active = adapter_->active_connections();

    {
        auto scope = connection_scope::acquire(*adapter_);
        ASSERT_TRUE(scope.has_value());

        EXPECT_EQ(adapter_->active_connections(), initial_active + 1);

        auto& conn = scope->connection();
        EXPECT_TRUE(conn.is_valid());
        EXPECT_TRUE(conn.execute("INSERT INTO test DEFAULT VALUES").has_value());
    }

    // Connection should be released
    EXPECT_EQ(adapter_->active_connections(), initial_active);
}

// =============================================================================
// Row Data Access Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, RowDataTypes) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE types ("
        "int_col INTEGER,"
        "real_col REAL,"
        "text_col TEXT,"
        "null_col TEXT"
        ")"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    ASSERT_TRUE(conn->execute(
        "INSERT INTO types VALUES (42, 3.14, 'hello', NULL)"
    ).has_value());

    auto select = conn->execute("SELECT * FROM types");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());

    const auto& row = (*select)->current_row();

    EXPECT_EQ(row.get_int64(0), 42);
    EXPECT_DOUBLE_EQ(row.get_double(1), 3.14);
    EXPECT_EQ(row.get_string(2), "hello");
    EXPECT_TRUE(row.is_null(3));

    EXPECT_EQ(row.column_count(), 4u);
    EXPECT_EQ(row.column_name(0), "int_col");
    EXPECT_EQ(row.column_name(1), "real_col");

    adapter_->release_connection(*conn_result);
}

TEST_F(DatabaseAdapterTest, RowValueVariant) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE types (int_col INTEGER, text_col TEXT)"
    ).has_value());

    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto& conn = *conn_result;

    ASSERT_TRUE(conn->execute("INSERT INTO types VALUES (123, 'test')").has_value());

    auto select = conn->execute("SELECT * FROM types");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());

    const auto& row = (*select)->current_row();

    auto int_value = row.get_value(0);
    EXPECT_TRUE(std::holds_alternative<int64_t>(int_value));
    EXPECT_EQ(std::get<int64_t>(int_value), 123);

    auto text_value = row.get_value(1);
    EXPECT_TRUE(std::holds_alternative<std::string>(text_value));
    EXPECT_EQ(std::get<std::string>(text_value), "test");

    adapter_->release_connection(*conn_result);
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST_F(DatabaseAdapterTest, ConcurrentAccess) {
    ASSERT_TRUE(adapter_->execute_schema(
        "CREATE TABLE counter (id INTEGER PRIMARY KEY, value INTEGER)"
    ).has_value());

    // Insert initial value
    auto conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    ASSERT_TRUE((*conn_result)->execute("INSERT INTO counter VALUES (1, 0)").has_value());
    adapter_->release_connection(*conn_result);

    const int num_threads = 4;
    const int increments_per_thread = 10;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this]() {
            for (int j = 0; j < 10; ++j) {
                auto conn = adapter_->acquire_connection();
                if (conn) {
                    auto result = (*conn)->execute(
                        "UPDATE counter SET value = value + 1 WHERE id = 1"
                    );
                    adapter_->release_connection(*conn);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all increments were applied
    conn_result = adapter_->acquire_connection();
    ASSERT_TRUE(conn_result.has_value());
    auto select = (*conn_result)->execute("SELECT value FROM counter WHERE id = 1");
    ASSERT_TRUE(select.has_value());
    EXPECT_TRUE((*select)->next());
    EXPECT_EQ((*select)->current_row().get_int64(0), num_threads * increments_per_thread);
    adapter_->release_connection(*conn_result);
}

}  // namespace
}  // namespace pacs::bridge::integration
