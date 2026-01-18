/**
 * @file network_adapter_test.cpp
 * @brief Unit tests for network adapter implementations
 *
 * Tests for messaging_client_adapter, secure_messaging_client_adapter, and factory functions.
 *
 * @see include/pacs/bridge/integration/network_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/270
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/integration/network_adapter.h"

#include "utils/test_helpers.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Factory Function Tests
// =============================================================================

class NetworkAdapterFactoryTest : public pacs_bridge_test {};

TEST_F(NetworkAdapterFactoryTest, CreatePlainAdapter) {
    auto adapter = create_network_adapter();
    ASSERT_NE(adapter, nullptr);
    EXPECT_FALSE(adapter->is_connected());
}

TEST_F(NetworkAdapterFactoryTest, CreatePlainAdapterExplicit) {
    auto adapter = create_network_adapter(false);
    ASSERT_NE(adapter, nullptr);
    EXPECT_FALSE(adapter->is_connected());
}

TEST_F(NetworkAdapterFactoryTest, CreateTlsAdapter) {
    auto adapter = create_network_adapter(true);
    ASSERT_NE(adapter, nullptr);
    EXPECT_FALSE(adapter->is_connected());
}

TEST_F(NetworkAdapterFactoryTest, CreateTlsAdapterWithVerification) {
    auto adapter = create_network_adapter(true, true);
    ASSERT_NE(adapter, nullptr);
    EXPECT_FALSE(adapter->is_connected());
}

TEST_F(NetworkAdapterFactoryTest, CreateTlsAdapterWithoutVerification) {
    auto adapter = create_network_adapter(true, false);
    ASSERT_NE(adapter, nullptr);
    EXPECT_FALSE(adapter->is_connected());
}

// =============================================================================
// Connection Configuration Tests
// =============================================================================

class NetworkAdapterConfigTest : public pacs_bridge_test {};

TEST_F(NetworkAdapterConfigTest, DefaultConfiguration) {
    connection_config config;

    EXPECT_TRUE(config.host.empty());
    EXPECT_EQ(config.port, 0);
    EXPECT_FALSE(config.use_tls);
    EXPECT_EQ(config.connect_timeout, std::chrono::milliseconds(5000));
    EXPECT_EQ(config.read_timeout, std::chrono::milliseconds(30000));
    EXPECT_EQ(config.write_timeout, std::chrono::milliseconds(30000));
}

TEST_F(NetworkAdapterConfigTest, CustomConfiguration) {
    connection_config config{
        .host = "localhost",
        .port = 8080,
        .use_tls = true,
        .connect_timeout = std::chrono::milliseconds(10000),
        .read_timeout = std::chrono::milliseconds(60000),
        .write_timeout = std::chrono::milliseconds(60000)
    };

    EXPECT_EQ(config.host, "localhost");
    EXPECT_EQ(config.port, 8080);
    EXPECT_TRUE(config.use_tls);
    EXPECT_EQ(config.connect_timeout, std::chrono::milliseconds(10000));
    EXPECT_EQ(config.read_timeout, std::chrono::milliseconds(60000));
    EXPECT_EQ(config.write_timeout, std::chrono::milliseconds(60000));
}

// =============================================================================
// Error Handling Tests
// =============================================================================

class NetworkAdapterErrorTest : public pacs_bridge_test {};

TEST_F(NetworkAdapterErrorTest, ConnectWithEmptyHost) {
    auto adapter = create_network_adapter();

    connection_config config{
        .host = "",
        .port = 8080
    };

    EXPECT_FALSE(adapter->connect(config));
    EXPECT_FALSE(adapter->is_connected());
    EXPECT_FALSE(adapter->last_error().empty());
}

TEST_F(NetworkAdapterErrorTest, ConnectToInvalidPort) {
    auto adapter = create_network_adapter();

    connection_config config{
        .host = "localhost",
        .port = 0,
        .connect_timeout = std::chrono::milliseconds(100)  // Short timeout
    };

    // Connection should fail (no server on port 0)
    EXPECT_FALSE(adapter->connect(config));
    EXPECT_FALSE(adapter->is_connected());
}

TEST_F(NetworkAdapterErrorTest, SendWithoutConnection) {
    auto adapter = create_network_adapter();

    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto result = adapter->send(data);

    EXPECT_EQ(result, -1);
    EXPECT_FALSE(adapter->last_error().empty());
}

TEST_F(NetworkAdapterErrorTest, ReceiveWithoutConnection) {
    auto adapter = create_network_adapter();

    auto result = adapter->receive(1024);

    EXPECT_TRUE(result.empty());
}

TEST_F(NetworkAdapterErrorTest, DoubleDisconnect) {
    auto adapter = create_network_adapter();

    // Should not crash
    EXPECT_NO_THROW(adapter->disconnect());
    EXPECT_NO_THROW(adapter->disconnect());
}

// =============================================================================
// Error Code Tests
// =============================================================================

class IntegrationErrorCodeTest : public pacs_bridge_test {};

TEST_F(IntegrationErrorCodeTest, ErrorCodeValues) {
    EXPECT_EQ(to_error_code(integration_error::connection_failed), -700);
    EXPECT_EQ(to_error_code(integration_error::connection_timeout), -701);
    EXPECT_EQ(to_error_code(integration_error::send_failed), -702);
    EXPECT_EQ(to_error_code(integration_error::receive_failed), -703);
    EXPECT_EQ(to_error_code(integration_error::tls_handshake_failed), -704);
    EXPECT_EQ(to_error_code(integration_error::invalid_config), -705);
}

TEST_F(IntegrationErrorCodeTest, ErrorCodeRange) {
    // All error codes should be in the -700 to -749 range
    EXPECT_GE(to_error_code(integration_error::connection_failed), -749);
    EXPECT_LE(to_error_code(integration_error::connection_failed), -700);

    EXPECT_GE(to_error_code(integration_error::invalid_config), -749);
    EXPECT_LE(to_error_code(integration_error::invalid_config), -700);
}

// =============================================================================
// Lifecycle Tests
// =============================================================================

class NetworkAdapterLifecycleTest : public pacs_bridge_test {};

TEST_F(NetworkAdapterLifecycleTest, CreateAndDestroy) {
    // Test that adapters can be created and destroyed without issues
    {
        auto adapter = create_network_adapter();
        EXPECT_NE(adapter, nullptr);
    }  // Adapter destroyed here

    {
        auto adapter = create_network_adapter(true);
        EXPECT_NE(adapter, nullptr);
    }  // TLS adapter destroyed here
}

TEST_F(NetworkAdapterLifecycleTest, MultipleAdapters) {
    std::vector<std::unique_ptr<network_adapter>> adapters;

    for (int i = 0; i < 5; ++i) {
        adapters.push_back(create_network_adapter());
        adapters.push_back(create_network_adapter(true));
    }

    for (const auto& adapter : adapters) {
        EXPECT_NE(adapter, nullptr);
        EXPECT_FALSE(adapter->is_connected());
    }
}

// =============================================================================
// Thread Safety Tests (Basic)
// =============================================================================

class NetworkAdapterThreadSafetyTest : public pacs_bridge_test {};

TEST_F(NetworkAdapterThreadSafetyTest, ConcurrentIsConnectedChecks) {
    auto adapter = create_network_adapter();

    std::atomic<int> check_count{0};
    constexpr int checks_per_thread = 100;
    constexpr int thread_count = 4;

    std::vector<std::thread> threads;
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&adapter, &check_count]() {
            for (int i = 0; i < checks_per_thread; ++i) {
                [[maybe_unused]] bool connected = adapter->is_connected();
                check_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(check_count.load(), checks_per_thread * thread_count);
}

TEST_F(NetworkAdapterThreadSafetyTest, ConcurrentDisconnectCalls) {
    auto adapter = create_network_adapter();

    constexpr int thread_count = 4;
    std::vector<std::thread> threads;

    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&adapter]() {
            for (int i = 0; i < 10; ++i) {
                adapter->disconnect();
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_FALSE(adapter->is_connected());
}

}  // namespace
}  // namespace pacs::bridge::integration
