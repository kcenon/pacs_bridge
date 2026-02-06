/**
 * @file test_utilities.h
 * @brief Common utilities for system adapter integration tests
 *
 * Provides helper functions, fixtures, and utilities for testing adapter
 * integration across different build modes (standalone vs integrated).
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/320
 */

#ifndef PACS_BRIDGE_TESTS_INTEGRATION_TEST_UTILITIES_H
#define PACS_BRIDGE_TESTS_INTEGRATION_TEST_UTILITIES_H

#include "test_helpers.h"

#include "pacs/bridge/integration/database_adapter.h"
#include "pacs/bridge/integration/pacs_adapter.h"
#include "pacs/bridge/mllp/mllp_network_adapter.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace pacs::bridge::integration::test {

// =============================================================================
// Port Generation
// =============================================================================

/**
 * @brief Generate unique port number for test isolation
 *
 * Uses a shared atomic counter to avoid port collisions between tests
 * running in the same process.
 */
inline uint16_t generate_test_port() {
    static std::atomic<uint16_t> port_counter{16000};
    return port_counter.fetch_add(1);
}

// =============================================================================
// Temporary File Management
// =============================================================================

/**
 * @brief Generate a unique temporary database path for testing
 */
inline std::filesystem::path generate_temp_db_path(const std::string& prefix = "integration_test") {
    static std::atomic<int> counter{0};
    auto temp_dir = std::filesystem::temp_directory_path();
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    return temp_dir / (prefix + "_" + std::to_string(timestamp) + "_" +
                       std::to_string(counter.fetch_add(1)) + ".db");
}

/**
 * @brief RAII guard for temporary files
 *
 * Automatically removes the file when the guard goes out of scope.
 */
class temp_file_guard {
public:
    explicit temp_file_guard(std::filesystem::path path)
        : path_(std::move(path)) {}

    ~temp_file_guard() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    temp_file_guard(const temp_file_guard&) = delete;
    temp_file_guard& operator=(const temp_file_guard&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] std::string string() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

// =============================================================================
// Adapter Factory Helpers
// =============================================================================

/**
 * @brief Create a database adapter configured for testing
 *
 * Creates a SQLite adapter with a temporary database file.
 * Returns both the adapter and a path guard for cleanup.
 */
struct test_database {
    std::shared_ptr<database_adapter> adapter;
    std::filesystem::path db_path;

    ~test_database() {
        adapter.reset();
        std::error_code ec;
        std::filesystem::remove(db_path, ec);
    }
};

inline std::unique_ptr<test_database> create_test_database(
    std::size_t pool_size = 3) {
    auto result = std::make_unique<test_database>();
    result->db_path = generate_temp_db_path();

    database_config config;
    config.database_path = result->db_path.string();
    config.pool_size = pool_size;
    config.connection_timeout = std::chrono::seconds{5};
    config.query_timeout = std::chrono::seconds{10};
    config.enable_wal = true;
    config.busy_timeout_ms = 3000;

    result->adapter = create_database_adapter(config);
    return result;
}

/**
 * @brief Create a PACS adapter configured for testing (standalone mode)
 */
inline std::shared_ptr<pacs_adapter> create_test_pacs_adapter() {
    pacs_config config;
    config.server_ae_title = "TEST_PACS";
    config.server_hostname = "localhost";
    config.server_port = 11112;
    config.calling_ae_title = "TEST_BRIDGE";
    config.connection_timeout = std::chrono::seconds{5};
    config.query_timeout = std::chrono::seconds{10};

    return create_pacs_adapter(config);
}

/**
 * @brief Create an MLLP server config for testing
 */
inline mllp::server_config create_test_server_config(uint16_t port = 0) {
    mllp::server_config config;
    config.port = port > 0 ? port : generate_test_port();
    config.backlog = 5;
    config.reuse_addr = true;
    config.no_delay = true;
    config.keep_alive = false;
    return config;
}

// =============================================================================
// Wait Utilities
// =============================================================================

/**
 * @brief Wait for a condition with timeout using sleep-based polling
 *
 * More appropriate for integration tests where responsiveness is less
 * critical than CPU efficiency.
 */
template <typename Predicate>
bool wait_for_condition(Predicate pred,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds{5000},
                        std::chrono::milliseconds interval = std::chrono::milliseconds{10}) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(interval);
    }
    return true;
}

}  // namespace pacs::bridge::integration::test

#endif  // PACS_BRIDGE_TESTS_INTEGRATION_TEST_UTILITIES_H
