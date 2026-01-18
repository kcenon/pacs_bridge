/**
 * @file logger_adapter_test.cpp
 * @brief Unit tests for logger adapter implementations
 *
 * Tests for console_logger_adapter, ilogger_adapter, and factory functions.
 *
 * @see include/pacs/bridge/integration/logger_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/267
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/integration/logger_adapter.h"

#include "utils/test_helpers.h"

#include <kcenon/common/interfaces/logger_interface.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>
#include <vector>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Mock ILogger for Testing
// =============================================================================

class MockILogger : public kcenon::common::interfaces::ILogger {
public:
    MOCK_METHOD(kcenon::common::VoidResult, log,
                (kcenon::common::interfaces::log_level level, const std::string& message),
                (override));
    MOCK_METHOD(kcenon::common::VoidResult, log,
                (kcenon::common::interfaces::log_level level,
                 std::string_view message,
                 const kcenon::common::source_location& loc),
                (override));
    MOCK_METHOD(kcenon::common::VoidResult, log,
                (kcenon::common::interfaces::log_level level,
                 const std::string& message,
                 const std::string& file,
                 int line,
                 const std::string& function),
                (override));
    MOCK_METHOD(kcenon::common::VoidResult, log,
                (const kcenon::common::interfaces::log_entry& entry), (override));
    MOCK_METHOD(bool, is_enabled, (kcenon::common::interfaces::log_level level), (const, override));
    MOCK_METHOD(kcenon::common::VoidResult, set_level,
                (kcenon::common::interfaces::log_level level), (override));
    MOCK_METHOD(kcenon::common::interfaces::log_level, get_level, (), (const, override));
    MOCK_METHOD(kcenon::common::VoidResult, flush, (), (override));
};

// =============================================================================
// Console Logger Adapter Tests
// =============================================================================

class ConsoleLoggerAdapterTest : public pacs_bridge_test {};

TEST_F(ConsoleLoggerAdapterTest, CreateNamedLogger) {
    auto logger = create_logger("test_logger");
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->get_level(), log_level::info);
}

TEST_F(ConsoleLoggerAdapterTest, SetAndGetLevel) {
    auto logger = create_logger("test");

    logger->set_level(log_level::debug);
    EXPECT_EQ(logger->get_level(), log_level::debug);

    logger->set_level(log_level::error);
    EXPECT_EQ(logger->get_level(), log_level::error);
}

TEST_F(ConsoleLoggerAdapterTest, LogLevelFiltering) {
    auto logger = create_logger("test");
    logger->set_level(log_level::warning);

    // These should be filtered out (below warning level)
    logger->trace("trace message");
    logger->debug("debug message");
    logger->info("info message");

    // These should pass through
    logger->warning("warning message");
    logger->error("error message");
    logger->critical("critical message");
}

TEST_F(ConsoleLoggerAdapterTest, FlushDoesNotCrash) {
    auto logger = create_logger("test");
    logger->info("message before flush");
    EXPECT_NO_THROW(logger->flush());
}

TEST_F(ConsoleLoggerAdapterTest, ConvenienceMethods) {
    auto logger = create_logger("test");
    logger->set_level(log_level::trace);

    EXPECT_NO_THROW(logger->trace("trace"));
    EXPECT_NO_THROW(logger->debug("debug"));
    EXPECT_NO_THROW(logger->info("info"));
    EXPECT_NO_THROW(logger->warning("warning"));
    EXPECT_NO_THROW(logger->error("error"));
    EXPECT_NO_THROW(logger->critical("critical"));
}

// =============================================================================
// ILogger Adapter Tests
// =============================================================================

class ILoggerAdapterTest : public pacs_bridge_test {};

TEST_F(ILoggerAdapterTest, WrapILogger) {
    auto mock_logger = std::make_shared<MockILogger>();

    EXPECT_CALL(*mock_logger, get_level())
        .WillOnce(Return(kcenon::common::interfaces::log_level::info));

    auto adapter = create_logger(mock_logger);
    ASSERT_NE(adapter, nullptr);
    EXPECT_EQ(adapter->get_level(), log_level::info);
}

TEST_F(ILoggerAdapterTest, ForwardsLogCalls) {
    auto mock_logger = std::make_shared<MockILogger>();

    EXPECT_CALL(*mock_logger, get_level())
        .WillOnce(Return(kcenon::common::interfaces::log_level::trace));

    EXPECT_CALL(*mock_logger,
                log(kcenon::common::interfaces::log_level::info, _, _))
        .WillOnce(Return(kcenon::common::VoidResult(std::monostate{})));

    auto adapter = create_logger(mock_logger);
    adapter->info("test message");
}

TEST_F(ILoggerAdapterTest, SetLevelForwardsToILogger) {
    auto mock_logger = std::make_shared<MockILogger>();

    EXPECT_CALL(*mock_logger, get_level())
        .WillOnce(Return(kcenon::common::interfaces::log_level::info));

    EXPECT_CALL(*mock_logger, set_level(kcenon::common::interfaces::log_level::debug))
        .WillOnce(Return(kcenon::common::VoidResult(std::monostate{})));

    auto adapter = create_logger(mock_logger);
    adapter->set_level(log_level::debug);

    EXPECT_EQ(adapter->get_level(), log_level::debug);
}

TEST_F(ILoggerAdapterTest, FlushForwardsToILogger) {
    auto mock_logger = std::make_shared<MockILogger>();

    EXPECT_CALL(*mock_logger, get_level())
        .WillOnce(Return(kcenon::common::interfaces::log_level::info));

    EXPECT_CALL(*mock_logger, flush())
        .WillOnce(Return(kcenon::common::VoidResult(std::monostate{})));

    auto adapter = create_logger(mock_logger);
    adapter->flush();
}

TEST_F(ILoggerAdapterTest, NullILoggerHandledGracefully) {
    std::shared_ptr<kcenon::common::interfaces::ILogger> null_logger;
    auto adapter = create_logger(null_logger);

    ASSERT_NE(adapter, nullptr);
    EXPECT_NO_THROW(adapter->info("message"));
    EXPECT_NO_THROW(adapter->flush());
}

// =============================================================================
// Global Logger Tests
// =============================================================================

class GlobalLoggerTest : public pacs_bridge_test {
protected:
    void TearDown() override {
        reset_default_logger();
        pacs_bridge_test::TearDown();
    }
};

TEST_F(GlobalLoggerTest, GetLoggerReturnsNonNull) {
    auto& logger = get_logger();
    EXPECT_NO_THROW(logger.info("global logger test"));
}

TEST_F(GlobalLoggerTest, SetDefaultLoggerWithILogger) {
    auto mock_logger = std::make_shared<MockILogger>();

    EXPECT_CALL(*mock_logger, get_level())
        .WillRepeatedly(Return(kcenon::common::interfaces::log_level::info));

    EXPECT_CALL(*mock_logger,
                log(kcenon::common::interfaces::log_level::info, _, _))
        .WillOnce(Return(kcenon::common::VoidResult(std::monostate{})));

    set_default_logger(mock_logger);

    auto& logger = get_logger();
    logger.info("using custom ILogger");
}

TEST_F(GlobalLoggerTest, ResetDefaultLogger) {
    auto mock_logger = std::make_shared<MockILogger>();

    EXPECT_CALL(*mock_logger, get_level())
        .WillRepeatedly(Return(kcenon::common::interfaces::log_level::info));

    set_default_logger(mock_logger);
    reset_default_logger();

    // After reset, should use console fallback
    auto& logger = get_logger();
    EXPECT_NO_THROW(logger.info("back to console logger"));
}

// =============================================================================
// Log Level Conversion Tests
// =============================================================================

class LogLevelConversionTest : public pacs_bridge_test {};

TEST_F(LogLevelConversionTest, AllLevelsSupported) {
    auto logger = create_logger("test");

    std::vector<log_level> levels = {log_level::trace, log_level::debug,
                                      log_level::info, log_level::warning,
                                      log_level::error, log_level::critical};

    for (auto level : levels) {
        logger->set_level(level);
        EXPECT_EQ(logger->get_level(), level);
    }
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

class LoggerThreadSafetyTest : public pacs_bridge_test {};

TEST_F(LoggerThreadSafetyTest, ConcurrentLogging) {
    auto logger = create_logger("concurrent_test");
    logger->set_level(log_level::trace);

    std::atomic<int> counter{0};
    constexpr int logs_per_thread = 100;
    constexpr int thread_count = 4;

    std::vector<std::thread> threads;
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&logger, &counter, t]() {
            for (int i = 0; i < logs_per_thread; ++i) {
                logger->info("Thread " + std::to_string(t) + " message " +
                             std::to_string(i));
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(counter.load(), logs_per_thread * thread_count);
}

TEST_F(LoggerThreadSafetyTest, ConcurrentGlobalLoggerAccess) {
    constexpr int thread_count = 4;

    std::vector<std::thread> threads;
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < 25; ++i) {
                auto& logger = get_logger();
                logger.info("Thread " + std::to_string(t));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

}  // namespace
}  // namespace pacs::bridge::integration
