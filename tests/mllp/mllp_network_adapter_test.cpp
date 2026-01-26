/**
 * @file mllp_network_adapter_test.cpp
 * @brief Unit tests for MLLP network adapter interface
 *
 * Tests basic interface components:
 * - network_error enum and to_string() conversion
 * - server_config validation
 * - session_stats structure
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/315
 */

#include "pacs/bridge/mllp/mllp_network_adapter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <string>
#include <thread>

namespace pacs::bridge::mllp::test {

// =============================================================================
// network_error Tests
// =============================================================================

TEST(NetworkErrorTest, ToString) {
    // Test all error codes have string representations
    EXPECT_STREQ("Operation timed out", to_string(network_error::timeout));
    EXPECT_STREQ("Connection closed by peer",
                 to_string(network_error::connection_closed));
    EXPECT_STREQ("Socket operation failed",
                 to_string(network_error::socket_error));
    EXPECT_STREQ("Failed to bind or listen on port",
                 to_string(network_error::bind_failed));
    EXPECT_STREQ("TLS handshake failed",
                 to_string(network_error::tls_handshake_failed));
    EXPECT_STREQ("Invalid configuration",
                 to_string(network_error::invalid_config));
    EXPECT_STREQ("Operation would block",
                 to_string(network_error::would_block));
    EXPECT_STREQ("Connection refused by peer",
                 to_string(network_error::connection_refused));
}

TEST(NetworkErrorTest, ErrorCodesAreUnique) {
    // Verify all error codes are unique
    std::set<int> error_codes = {
        static_cast<int>(network_error::timeout),
        static_cast<int>(network_error::connection_closed),
        static_cast<int>(network_error::socket_error),
        static_cast<int>(network_error::bind_failed),
        static_cast<int>(network_error::tls_handshake_failed),
        static_cast<int>(network_error::invalid_config),
        static_cast<int>(network_error::would_block),
        static_cast<int>(network_error::connection_refused),
    };

    // If all unique, set size should equal number of error codes
    EXPECT_EQ(8u, error_codes.size());
}

TEST(NetworkErrorTest, ErrorCodesAreNegative) {
    // All error codes should be negative for consistency
    EXPECT_LT(static_cast<int>(network_error::timeout), 0);
    EXPECT_LT(static_cast<int>(network_error::connection_closed), 0);
    EXPECT_LT(static_cast<int>(network_error::socket_error), 0);
    EXPECT_LT(static_cast<int>(network_error::bind_failed), 0);
    EXPECT_LT(static_cast<int>(network_error::tls_handshake_failed), 0);
    EXPECT_LT(static_cast<int>(network_error::invalid_config), 0);
    EXPECT_LT(static_cast<int>(network_error::would_block), 0);
    EXPECT_LT(static_cast<int>(network_error::connection_refused), 0);
}

// =============================================================================
// server_config Tests
// =============================================================================

TEST(ServerConfigTest, DefaultValues) {
    server_config config;

    // Verify sensible defaults
    EXPECT_EQ(2575, config.port);
    EXPECT_TRUE(config.bind_address.empty());
    EXPECT_EQ(128, config.backlog);
    EXPECT_EQ(0u, config.recv_buffer_size);  // 0 = system default
    EXPECT_EQ(0u, config.send_buffer_size);  // 0 = system default
    EXPECT_TRUE(config.keep_alive);
    EXPECT_EQ(60, config.keep_alive_idle);
    EXPECT_EQ(10, config.keep_alive_interval);
    EXPECT_EQ(3, config.keep_alive_count);
    EXPECT_TRUE(config.no_delay);
    EXPECT_TRUE(config.reuse_addr);
}

TEST(ServerConfigTest, ValidationValid) {
    server_config config;
    config.port = 8080;
    config.backlog = 256;

    EXPECT_TRUE(config.is_valid());
}

TEST(ServerConfigTest, ValidationInvalidPort) {
    server_config config;
    config.port = 0;  // Invalid

    EXPECT_FALSE(config.is_valid());
}

TEST(ServerConfigTest, ValidationInvalidBacklog) {
    server_config config;
    config.backlog = 0;  // Invalid

    EXPECT_FALSE(config.is_valid());
}

TEST(ServerConfigTest, ValidationNegativeBacklog) {
    server_config config;
    config.backlog = -1;  // Invalid

    EXPECT_FALSE(config.is_valid());
}

TEST(ServerConfigTest, CustomBindAddress) {
    server_config config;
    config.bind_address = "127.0.0.1";

    EXPECT_EQ("127.0.0.1", config.bind_address);
    EXPECT_TRUE(config.is_valid());
}

TEST(ServerConfigTest, CustomBufferSizes) {
    server_config config;
    config.recv_buffer_size = 65536;
    config.send_buffer_size = 32768;

    EXPECT_EQ(65536u, config.recv_buffer_size);
    EXPECT_EQ(32768u, config.send_buffer_size);
    EXPECT_TRUE(config.is_valid());
}

TEST(ServerConfigTest, DisableKeepAlive) {
    server_config config;
    config.keep_alive = false;

    EXPECT_FALSE(config.keep_alive);
    EXPECT_TRUE(config.is_valid());
}

TEST(ServerConfigTest, CustomKeepAliveSettings) {
    server_config config;
    config.keep_alive_idle = 120;
    config.keep_alive_interval = 30;
    config.keep_alive_count = 5;

    EXPECT_EQ(120, config.keep_alive_idle);
    EXPECT_EQ(30, config.keep_alive_interval);
    EXPECT_EQ(5, config.keep_alive_count);
}

// =============================================================================
// session_stats Tests
// =============================================================================

TEST(SessionStatsTest, DefaultInitialization) {
    session_stats stats;

    EXPECT_EQ(0u, stats.bytes_received);
    EXPECT_EQ(0u, stats.bytes_sent);
    EXPECT_EQ(0u, stats.messages_received);
    EXPECT_EQ(0u, stats.messages_sent);

    // Default time points should be default-constructed
    // (not testing exact values as they're implementation-defined)
}

TEST(SessionStatsTest, UpdateStatistics) {
    session_stats stats;

    // Simulate some activity
    stats.bytes_received = 1024;
    stats.bytes_sent = 2048;
    stats.messages_received = 10;
    stats.messages_sent = 15;
    stats.connected_at = std::chrono::system_clock::now();
    stats.last_activity = std::chrono::system_clock::now();

    EXPECT_EQ(1024u, stats.bytes_received);
    EXPECT_EQ(2048u, stats.bytes_sent);
    EXPECT_EQ(10u, stats.messages_received);
    EXPECT_EQ(15u, stats.messages_sent);
}

TEST(SessionStatsTest, TimeProgression) {
    session_stats stats;

    auto start_time = std::chrono::system_clock::now();
    stats.connected_at = start_time;

    // Simulate some delay
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    stats.last_activity = std::chrono::system_clock::now();

    // last_activity should be after connected_at
    EXPECT_GT(stats.last_activity, stats.connected_at);
}

// =============================================================================
// Interface Contracts
// =============================================================================

TEST(InterfaceTest, SessionInterfaceIsAbstract) {
    // Verify mllp_session is abstract (cannot be instantiated)
    // This is a compile-time test - if this compiles, the interface is correct
    EXPECT_TRUE(std::is_abstract_v<mllp_session>);
}

TEST(InterfaceTest, ServerAdapterInterfaceIsAbstract) {
    // Verify mllp_server_adapter is abstract
    EXPECT_TRUE(std::is_abstract_v<mllp_server_adapter>);
}

TEST(InterfaceTest, SessionIsNonCopyable) {
    // Verify mllp_session is non-copyable
    EXPECT_FALSE(std::is_copy_constructible_v<mllp_session>);
    EXPECT_FALSE(std::is_copy_assignable_v<mllp_session>);
}

// Note: Cannot directly test move constructibility of abstract classes
// Concrete implementations should be movable

TEST(InterfaceTest, ServerAdapterIsNonCopyable) {
    // Verify mllp_server_adapter is non-copyable
    EXPECT_FALSE(std::is_copy_constructible_v<mllp_server_adapter>);
    EXPECT_FALSE(std::is_copy_assignable_v<mllp_server_adapter>);
}

TEST(InterfaceTest, ServerAdapterIsNonMovable) {
    // Verify mllp_server_adapter is non-movable (manages server socket)
    EXPECT_FALSE(std::is_move_constructible_v<mllp_server_adapter>);
    EXPECT_FALSE(std::is_move_assignable_v<mllp_server_adapter>);
}

}  // namespace pacs::bridge::mllp::test
