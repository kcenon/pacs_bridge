#ifndef PACS_BRIDGE_INTEGRATION_DATABASE_ADAPTER_H
#define PACS_BRIDGE_INTEGRATION_DATABASE_ADAPTER_H

/**
 * @file database_adapter.h
 * @brief Integration Module - Database system adapter
 *
 * Provides adapters that bridge pacs_bridge with database_system,
 * enabling standardized database access with connection pooling
 * and prepared statements.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/274
 * @see docs/SDS_COMPONENTS.md - Section 8: Integration Module
 */

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pacs::bridge::integration {

// =============================================================================
// Error Codes (-800 to -849)
// =============================================================================

/**
 * @brief Database adapter specific error codes
 *
 * Allocated range: -800 to -849
 */
enum class database_error : int {
    /** Connection to database failed */
    connection_failed = -800,

    /** Connection timeout exceeded */
    connection_timeout = -801,

    /** Query execution failed */
    query_failed = -802,

    /** Statement preparation failed */
    prepare_failed = -803,

    /** Parameter binding failed */
    bind_failed = -804,

    /** Transaction operation failed */
    transaction_failed = -805,

    /** Connection pool exhausted */
    pool_exhausted = -806,

    /** Invalid configuration provided */
    invalid_config = -807,

    /** Database constraint violation */
    constraint_violation = -808,

    /** Operation timeout */
    timeout = -809,

    /** No result available */
    no_result = -810,

    /** Invalid column index */
    invalid_column = -811,

    /** Type conversion failed */
    type_conversion_failed = -812
};

/**
 * @brief Convert database_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(database_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable error message for database_error
 */
[[nodiscard]] std::string_view to_string(database_error error) noexcept;

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief Database connection configuration
 */
struct database_config {
    /** Connection string (for database_system integration) */
    std::string connection_string;

    /** Database file path (for SQLite standalone mode) */
    std::string database_path;

    /** Connection pool size */
    std::size_t pool_size = 5;

    /** Connection timeout */
    std::chrono::seconds connection_timeout{30};

    /** Query timeout */
    std::chrono::seconds query_timeout{60};

    /** Enable Write-Ahead Logging (SQLite) */
    bool enable_wal = true;

    /** Busy timeout in milliseconds (SQLite) */
    int busy_timeout_ms = 5000;
};

// =============================================================================
// Value Types
// =============================================================================

/**
 * @brief Database value variant type
 *
 * Represents a value that can be stored in or retrieved from a database.
 */
using database_value = std::variant<
    std::monostate,  // NULL
    int64_t,         // INTEGER
    double,          // REAL
    std::string,     // TEXT
    std::vector<uint8_t>  // BLOB
>;

// =============================================================================
// Database Row
// =============================================================================

/**
 * @brief Result row abstraction
 *
 * Provides type-safe access to column values in a result row.
 */
class database_row {
public:
    virtual ~database_row() = default;

    /**
     * @brief Get column value as string
     * @param index Zero-based column index
     * @return Column value as string, or empty string if NULL
     */
    [[nodiscard]] virtual std::string get_string(std::size_t index) const = 0;

    /**
     * @brief Get column value as 64-bit integer
     * @param index Zero-based column index
     * @return Column value as int64_t, or 0 if NULL
     */
    [[nodiscard]] virtual int64_t get_int64(std::size_t index) const = 0;

    /**
     * @brief Get column value as double
     * @param index Zero-based column index
     * @return Column value as double, or 0.0 if NULL
     */
    [[nodiscard]] virtual double get_double(std::size_t index) const = 0;

    /**
     * @brief Get column value as blob (binary data)
     * @param index Zero-based column index
     * @return Column value as byte vector, or empty if NULL
     */
    [[nodiscard]] virtual std::vector<uint8_t> get_blob(std::size_t index) const = 0;

    /**
     * @brief Check if column value is NULL
     * @param index Zero-based column index
     * @return true if column is NULL
     */
    [[nodiscard]] virtual bool is_null(std::size_t index) const = 0;

    /**
     * @brief Get number of columns in this row
     */
    [[nodiscard]] virtual std::size_t column_count() const = 0;

    /**
     * @brief Get column name by index
     * @param index Zero-based column index
     * @return Column name
     */
    [[nodiscard]] virtual std::string column_name(std::size_t index) const = 0;

    /**
     * @brief Get column value as variant type
     * @param index Zero-based column index
     * @return Column value as database_value variant
     */
    [[nodiscard]] virtual database_value get_value(std::size_t index) const = 0;
};

// =============================================================================
// Database Result
// =============================================================================

/**
 * @brief Result set abstraction
 *
 * Provides iteration over query results.
 */
class database_result {
public:
    virtual ~database_result() = default;

    /**
     * @brief Advance to next row
     * @return true if there is another row, false if at end
     */
    [[nodiscard]] virtual bool next() = 0;

    /**
     * @brief Get current row
     * @return Reference to current row (valid until next() is called)
     */
    [[nodiscard]] virtual const database_row& current_row() const = 0;

    /**
     * @brief Get number of affected rows (for INSERT/UPDATE/DELETE)
     */
    [[nodiscard]] virtual std::size_t affected_rows() const = 0;

    /**
     * @brief Get last insert row ID
     */
    [[nodiscard]] virtual int64_t last_insert_id() const = 0;

    /**
     * @brief Check if result set is empty
     */
    [[nodiscard]] virtual bool empty() const = 0;
};

// =============================================================================
// Database Statement
// =============================================================================

/**
 * @brief Prepared statement abstraction
 *
 * Provides safe parameter binding and reusable query execution.
 * Parameters are 1-indexed (first parameter is index 1).
 */
class database_statement {
public:
    virtual ~database_statement() = default;

    /**
     * @brief Bind string value to parameter
     * @param index One-based parameter index
     * @param value Value to bind
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error>
    bind_string(std::size_t index, std::string_view value) = 0;

    /**
     * @brief Bind 64-bit integer value to parameter
     * @param index One-based parameter index
     * @param value Value to bind
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error>
    bind_int64(std::size_t index, int64_t value) = 0;

    /**
     * @brief Bind double value to parameter
     * @param index One-based parameter index
     * @param value Value to bind
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error>
    bind_double(std::size_t index, double value) = 0;

    /**
     * @brief Bind blob (binary) value to parameter
     * @param index One-based parameter index
     * @param value Value to bind
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error>
    bind_blob(std::size_t index, const std::vector<uint8_t>& value) = 0;

    /**
     * @brief Bind NULL to parameter
     * @param index One-based parameter index
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error>
    bind_null(std::size_t index) = 0;

    /**
     * @brief Clear all parameter bindings
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error> clear_bindings() = 0;

    /**
     * @brief Reset statement for re-execution
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error> reset() = 0;

    /**
     * @brief Execute the prepared statement
     * @return Result set or error
     */
    [[nodiscard]] virtual std::expected<std::unique_ptr<database_result>, database_error>
    execute() = 0;

    /**
     * @brief Get number of parameters in statement
     */
    [[nodiscard]] virtual std::size_t parameter_count() const = 0;
};

// =============================================================================
// Database Connection
// =============================================================================

/**
 * @brief Connection abstraction
 *
 * Provides direct SQL execution and transaction management.
 */
class database_connection {
public:
    virtual ~database_connection() = default;

    /**
     * @brief Prepare a SQL statement
     * @param sql SQL statement with parameter placeholders (?)
     * @return Prepared statement or error
     */
    [[nodiscard]] virtual std::expected<std::unique_ptr<database_statement>, database_error>
    prepare(std::string_view sql) = 0;

    /**
     * @brief Execute a SQL statement directly
     * @param sql SQL statement to execute
     * @return Result set or error
     */
    [[nodiscard]] virtual std::expected<std::unique_ptr<database_result>, database_error>
    execute(std::string_view sql) = 0;

    /**
     * @brief Begin a transaction
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error> begin_transaction() = 0;

    /**
     * @brief Commit the current transaction
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error> commit() = 0;

    /**
     * @brief Rollback the current transaction
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error> rollback() = 0;

    /**
     * @brief Check if connection is valid
     */
    [[nodiscard]] virtual bool is_valid() const = 0;

    /**
     * @brief Get last error message
     */
    [[nodiscard]] virtual std::string last_error() const = 0;

    /**
     * @brief Get number of changes from last statement
     */
    [[nodiscard]] virtual int64_t changes() const = 0;

    /**
     * @brief Get last insert row ID
     */
    [[nodiscard]] virtual int64_t last_insert_rowid() const = 0;
};

// =============================================================================
// Scoped Transaction Guard
// =============================================================================

/**
 * @brief RAII transaction guard
 *
 * Automatically rolls back transaction on scope exit unless committed.
 *
 * @example
 * @code
 * {
 *     auto guard = transaction_guard::begin(conn);
 *     if (!guard) {
 *         // Handle error
 *     }
 *     // Do work...
 *     if (success) {
 *         guard->commit();  // Commit on success
 *     }
 *     // Auto-rollback if not committed
 * }
 * @endcode
 */
class transaction_guard {
public:
    /**
     * @brief Begin a transaction and create guard
     * @param conn Connection to use
     * @return Transaction guard or error
     */
    [[nodiscard]] static std::expected<transaction_guard, database_error>
    begin(database_connection& conn);

    ~transaction_guard();

    // Move only
    transaction_guard(transaction_guard&& other) noexcept;
    transaction_guard& operator=(transaction_guard&& other) noexcept;
    transaction_guard(const transaction_guard&) = delete;
    transaction_guard& operator=(const transaction_guard&) = delete;

    /**
     * @brief Commit the transaction
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, database_error> commit();

    /**
     * @brief Rollback the transaction explicitly
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, database_error> rollback();

private:
    explicit transaction_guard(database_connection& conn);

    database_connection* conn_;
    bool committed_;
};

// =============================================================================
// Database Adapter
// =============================================================================

/**
 * @brief Main database adapter interface
 *
 * Provides connection pool management and schema operations.
 */
class database_adapter {
public:
    virtual ~database_adapter() = default;

    // =========================================================================
    // Connection Management
    // =========================================================================

    /**
     * @brief Acquire a connection from the pool
     * @return Connection or error if pool exhausted
     */
    [[nodiscard]] virtual std::expected<std::shared_ptr<database_connection>, database_error>
    acquire_connection() = 0;

    /**
     * @brief Release a connection back to the pool
     * @param conn Connection to release
     */
    virtual void release_connection(std::shared_ptr<database_connection> conn) = 0;

    // =========================================================================
    // Pool Status
    // =========================================================================

    /**
     * @brief Get number of available connections in pool
     */
    [[nodiscard]] virtual std::size_t available_connections() const = 0;

    /**
     * @brief Get number of connections currently in use
     */
    [[nodiscard]] virtual std::size_t active_connections() const = 0;

    /**
     * @brief Check if the adapter is healthy
     */
    [[nodiscard]] virtual bool is_healthy() const = 0;

    // =========================================================================
    // Schema Management
    // =========================================================================

    /**
     * @brief Execute DDL statement (CREATE, DROP, ALTER)
     * @param ddl DDL statement to execute
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, database_error>
    execute_schema(std::string_view ddl) = 0;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] virtual const database_config& config() const = 0;
};

// =============================================================================
// Connection Scope Guard
// =============================================================================

/**
 * @brief RAII guard for connection pool usage
 *
 * Automatically releases connection when scope exits.
 *
 * @example
 * @code
 * auto guard = connection_scope::acquire(adapter);
 * if (guard) {
 *     auto& conn = guard->connection();
 *     // Use connection...
 * }  // Connection automatically released
 * @endcode
 */
class connection_scope {
public:
    /**
     * @brief Acquire a connection and create scope guard
     * @param adapter Database adapter to acquire from
     * @return Connection scope guard or error
     */
    [[nodiscard]] static std::expected<connection_scope, database_error>
    acquire(database_adapter& adapter);

    ~connection_scope();

    // Move only
    connection_scope(connection_scope&& other) noexcept;
    connection_scope& operator=(connection_scope&& other) noexcept;
    connection_scope(const connection_scope&) = delete;
    connection_scope& operator=(const connection_scope&) = delete;

    /**
     * @brief Get the managed connection
     */
    [[nodiscard]] database_connection& connection() noexcept;

    /**
     * @brief Get the managed connection (const)
     */
    [[nodiscard]] const database_connection& connection() const noexcept;

private:
    connection_scope(database_adapter& adapter,
                     std::shared_ptr<database_connection> conn);

    database_adapter* adapter_;
    std::shared_ptr<database_connection> conn_;
};

// =============================================================================
// Factory Functions
// =============================================================================

/**
 * @brief Create a database adapter with configuration
 *
 * Creates a SQLite-based adapter in standalone mode.
 *
 * @param config Database configuration
 * @return Database adapter instance
 */
[[nodiscard]] std::shared_ptr<database_adapter>
create_database_adapter(const database_config& config);

#ifdef PACS_BRIDGE_HAS_DATABASE_SYSTEM
// Forward declarations for database_system integration
namespace kcenon::database {
class database_pool;
}

/**
 * @brief Create a database adapter wrapping database_system pool
 *
 * Only available when building with database_system integration.
 *
 * @param pool Existing database_system pool
 * @return Database adapter instance
 */
[[nodiscard]] std::shared_ptr<database_adapter>
create_database_adapter(std::shared_ptr<kcenon::database::database_pool> pool);
#endif

}  // namespace pacs::bridge::integration

#endif  // PACS_BRIDGE_INTEGRATION_DATABASE_ADAPTER_H
