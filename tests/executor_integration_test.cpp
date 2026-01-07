/**
 * @file executor_integration_test.cpp
 * @brief Integration tests for IExecutor across pacs_bridge components
 *
 * Tests executor injection and integration with:
 * - queue_manager
 * - mpps_hl7_workflow
 * - messaging_backend
 * - bridge_server (when available)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/229
 * @see https://github.com/kcenon/pacs_bridge/issues/198
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/integration/executor_adapter.h"
#include "pacs/bridge/router/queue_manager.h"

#include "utils/test_helpers.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Mock Executor for Testing
// =============================================================================

/**
 * @class mock_executor
 * @brief Mock IExecutor for verifying executor integration
 *
 * Tracks job submissions and execution counts for test assertions.
 */
class mock_executor : public kcenon::common::interfaces::IExecutor {
public:
    explicit mock_executor(std::size_t workers = 4)
        : worker_count_(workers)
        , running_(true)
        , real_executor_(std::make_shared<simple_executor>(workers)) {}

    ~mock_executor() override {
        shutdown(true);
    }

    kcenon::common::Result<std::future<void>> execute(
        std::unique_ptr<kcenon::common::interfaces::IJob>&& job) override {
        if (!running_.load()) {
            return kcenon::common::Result<std::future<void>>(
                kcenon::common::error_info{-1, "Not running", "mock_executor"});
        }

        execute_count_.fetch_add(1, std::memory_order_relaxed);

        // Delegate to real executor for actual execution
        return real_executor_->execute(std::move(job));
    }

    kcenon::common::Result<std::future<void>> execute_delayed(
        std::unique_ptr<kcenon::common::interfaces::IJob>&& job,
        std::chrono::milliseconds delay) override {
        if (!running_.load()) {
            return kcenon::common::Result<std::future<void>>(
                kcenon::common::error_info{-1, "Not running", "mock_executor"});
        }

        delayed_execute_count_.fetch_add(1, std::memory_order_relaxed);
        return real_executor_->execute_delayed(std::move(job), delay);
    }

    [[nodiscard]] std::size_t worker_count() const override {
        return worker_count_;
    }

    [[nodiscard]] bool is_running() const override {
        return running_.load();
    }

    [[nodiscard]] std::size_t pending_tasks() const override {
        return real_executor_->pending_tasks();
    }

    void shutdown(bool wait_for_completion) override {
        if (!running_.exchange(false)) {
            return;
        }
        real_executor_->shutdown(wait_for_completion);
    }

    // Test accessors
    [[nodiscard]] std::size_t get_execute_count() const {
        return execute_count_.load();
    }

    [[nodiscard]] std::size_t get_delayed_execute_count() const {
        return delayed_execute_count_.load();
    }

    void reset_counts() {
        execute_count_.store(0);
        delayed_execute_count_.store(0);
    }

private:
    std::size_t worker_count_;
    std::atomic<bool> running_;
    std::atomic<std::size_t> execute_count_{0};
    std::atomic<std::size_t> delayed_execute_count_{0};
    std::shared_ptr<simple_executor> real_executor_;
};

// =============================================================================
// Queue Manager with Executor Tests
// =============================================================================

class QueueManagerExecutorTest : public pacs_bridge_test {
protected:
    std::string test_db_path_;
    std::shared_ptr<mock_executor> executor_;

    void SetUp() override {
        pacs_bridge_test::SetUp();
        test_db_path_ = "/tmp/test_queue_executor_" +
                        std::to_string(std::time(nullptr)) + ".db";
        executor_ = std::make_shared<mock_executor>(2);
    }

    void TearDown() override {
        executor_->shutdown(true);
        std::filesystem::remove(test_db_path_);
        std::filesystem::remove(test_db_path_ + "-wal");
        std::filesystem::remove(test_db_path_ + "-shm");
        pacs_bridge_test::TearDown();
    }
};

TEST_F(QueueManagerExecutorTest, ConfigWithExecutor) {
    router::queue_config config;
    config.database_path = test_db_path_;
    config.worker_count = 2;
    config.executor = executor_;

    EXPECT_TRUE(config.is_valid());
    EXPECT_NE(config.executor, nullptr);
}

TEST_F(QueueManagerExecutorTest, CreateWithExecutor) {
    router::queue_config config;
    config.database_path = test_db_path_;
    config.worker_count = 2;
    config.executor = executor_;

    router::queue_manager queue(config);

    auto result = queue.start();
    EXPECT_TRUE(result.has_value()) << "Queue should start with executor";
    EXPECT_TRUE(queue.is_running());

    // Give workers time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    queue.stop();
    EXPECT_FALSE(queue.is_running());
}

TEST_F(QueueManagerExecutorTest, EnqueueWithExecutorConfig) {
    router::queue_config config;
    config.database_path = test_db_path_;
    config.worker_count = 2;
    config.executor = executor_;

    router::queue_manager queue(config);
    auto start_result = queue.start();
    ASSERT_TRUE(start_result.has_value()) << "Queue should start";

    // Enqueue a message using the correct API
    std::string_view destination = "test_dest";
    std::string_view payload = "MSH|^~\\&|TEST|TEST|TEST|TEST|20240101||ADT^A01|1|P|2.4\r";
    int priority = 5;

    auto result = queue.enqueue(destination, payload, priority);
    EXPECT_TRUE(result.has_value()) << "Enqueue should succeed";

    // Verify the message was stored
    auto stats = queue.get_statistics();
    EXPECT_GT(stats.total_enqueued, 0u) << "Message should be enqueued";

    // Queue should remain operational
    EXPECT_TRUE(queue.is_running());

    queue.stop();
}

// =============================================================================
// Executor Factory Tests
// =============================================================================

class ExecutorFactoryIntegrationTest : public pacs_bridge_test {};

TEST_F(ExecutorFactoryIntegrationTest, MakeExecutorDefault) {
    auto executor = make_executor();

    EXPECT_NE(executor, nullptr);
    EXPECT_TRUE(executor->is_running());
    EXPECT_GT(executor->worker_count(), 0u);

    executor->shutdown(true);
    EXPECT_FALSE(executor->is_running());
}

TEST_F(ExecutorFactoryIntegrationTest, MakeExecutorWithCount) {
    constexpr std::size_t worker_count = 8;
    auto executor = make_executor(worker_count);

    EXPECT_NE(executor, nullptr);
    EXPECT_EQ(executor->worker_count(), worker_count);
    EXPECT_TRUE(executor->is_running());

    executor->shutdown(true);
}

TEST_F(ExecutorFactoryIntegrationTest, ExecutorLifecycle) {
    auto executor = make_executor(2);

    // Verify running state
    EXPECT_TRUE(executor->is_running());
    EXPECT_EQ(executor->pending_tasks(), 0u);

    // Submit work
    std::atomic<int> counter{0};
    for (int i = 0; i < 5; ++i) {
        auto job = std::make_unique<lambda_job>([&counter]() {
            counter.fetch_add(1);
        });
        auto result = executor->execute(std::move(job));
        EXPECT_TRUE(result.is_ok());
        result.value().wait();
    }

    EXPECT_EQ(counter.load(), 5);

    // Shutdown
    executor->shutdown(true);
    EXPECT_FALSE(executor->is_running());

    // Execute after shutdown should fail
    auto job = std::make_unique<lambda_job>([]() {});
    auto result = executor->execute(std::move(job));
    EXPECT_FALSE(result.is_ok());
}

// =============================================================================
// Executor Sharing Tests
// =============================================================================

class ExecutorSharingTest : public pacs_bridge_test {
protected:
    std::string test_db_path1_;
    std::string test_db_path2_;

    void SetUp() override {
        pacs_bridge_test::SetUp();
        auto timestamp = std::to_string(std::time(nullptr));
        test_db_path1_ = "/tmp/test_queue1_" + timestamp + ".db";
        test_db_path2_ = "/tmp/test_queue2_" + timestamp + ".db";
    }

    void TearDown() override {
        for (const auto& path : {test_db_path1_, test_db_path2_}) {
            std::filesystem::remove(path);
            std::filesystem::remove(path + "-wal");
            std::filesystem::remove(path + "-shm");
        }
        pacs_bridge_test::TearDown();
    }
};

TEST_F(ExecutorSharingTest, SharedExecutorAcrossComponents) {
    // Create a shared executor
    auto shared_executor = std::make_shared<mock_executor>(4);

    // Create two queue managers sharing the executor
    router::queue_config config1;
    config1.database_path = test_db_path1_;
    config1.worker_count = 2;
    config1.executor = shared_executor;

    router::queue_config config2;
    config2.database_path = test_db_path2_;
    config2.worker_count = 2;
    config2.executor = shared_executor;

    router::queue_manager queue1(config1);
    router::queue_manager queue2(config2);

    auto result1 = queue1.start();
    auto result2 = queue2.start();
    ASSERT_TRUE(result1.has_value()) << "Queue1 should start";
    ASSERT_TRUE(result2.has_value()) << "Queue2 should start";

    // Both should be using the same executor
    EXPECT_TRUE(queue1.is_running());
    EXPECT_TRUE(queue2.is_running());
    EXPECT_TRUE(shared_executor->is_running());

    // Stop queues (executor remains running)
    queue1.stop();
    queue2.stop();

    // Shared executor should still be running
    EXPECT_TRUE(shared_executor->is_running());

    // Cleanup
    shared_executor->shutdown(true);
}

// =============================================================================
// Executor Error Handling Tests
// =============================================================================

class ExecutorErrorHandlingTest : public pacs_bridge_test {};

TEST_F(ExecutorErrorHandlingTest, ExecuteNullJob) {
    auto executor = make_executor(2);

    auto result = executor->execute(nullptr);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, -2);

    executor->shutdown(true);
}

TEST_F(ExecutorErrorHandlingTest, ExecuteAfterShutdown) {
    auto executor = make_executor(2);
    executor->shutdown(true);

    auto job = std::make_unique<lambda_job>([]() {});
    auto result = executor->execute(std::move(job));

    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, -1);
}

TEST_F(ExecutorErrorHandlingTest, JobWithException) {
    auto executor = make_executor(2);

    auto job = std::make_unique<lambda_job>(
        []() -> kcenon::common::VoidResult {
            throw std::runtime_error("Test exception");
        });

    auto result = executor->execute(std::move(job));
    ASSERT_TRUE(result.is_ok());

    EXPECT_THROW(result.value().get(), std::runtime_error);

    executor->shutdown(true);
}

TEST_F(ExecutorErrorHandlingTest, JobWithErrorResult) {
    auto executor = make_executor(2);

    auto job = std::make_unique<lambda_job>(
        []() -> kcenon::common::VoidResult {
            return kcenon::common::VoidResult(
                kcenon::common::error_info{-100, "Custom error", "test"});
        });

    auto result = executor->execute(std::move(job));
    ASSERT_TRUE(result.is_ok());

    // Error should be converted to exception
    EXPECT_THROW(result.value().get(), std::runtime_error);

    executor->shutdown(true);
}

// =============================================================================
// Delayed Execution Tests
// =============================================================================

class ExecutorDelayedExecutionTest : public pacs_bridge_test {};

TEST_F(ExecutorDelayedExecutionTest, DelayedJobExecution) {
    auto executor = make_executor(2);
    std::atomic<bool> executed{false};

    auto start_time = std::chrono::steady_clock::now();

    auto job = std::make_unique<lambda_job>([&executed]() {
        executed = true;
    });

    auto result = executor->execute_delayed(
        std::move(job), std::chrono::milliseconds{100});
    ASSERT_TRUE(result.is_ok());

    result.value().wait();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);

    EXPECT_TRUE(executed.load());
    EXPECT_GE(elapsed.count(), 90);  // Allow timing tolerance

    executor->shutdown(true);
}

TEST_F(ExecutorDelayedExecutionTest, MultipleDelayedJobs) {
    auto executor = make_executor(2);
    std::vector<int> execution_order;
    std::mutex order_mutex;

    // Submit jobs with different delays (in reverse order)
    auto job3 = std::make_unique<lambda_job>([&]() {
        std::lock_guard lock(order_mutex);
        execution_order.push_back(3);
    });
    auto result3 = executor->execute_delayed(
        std::move(job3), std::chrono::milliseconds{150});

    auto job1 = std::make_unique<lambda_job>([&]() {
        std::lock_guard lock(order_mutex);
        execution_order.push_back(1);
    });
    auto result1 = executor->execute_delayed(
        std::move(job1), std::chrono::milliseconds{50});

    auto job2 = std::make_unique<lambda_job>([&]() {
        std::lock_guard lock(order_mutex);
        execution_order.push_back(2);
    });
    auto result2 = executor->execute_delayed(
        std::move(job2), std::chrono::milliseconds{100});

    ASSERT_TRUE(result1.is_ok());
    ASSERT_TRUE(result2.is_ok());
    ASSERT_TRUE(result3.is_ok());

    // Wait for all to complete
    result1.value().wait();
    result2.value().wait();
    result3.value().wait();

    // Jobs should execute in delay order
    ASSERT_EQ(execution_order.size(), 3u);
    EXPECT_EQ(execution_order[0], 1);
    EXPECT_EQ(execution_order[1], 2);
    EXPECT_EQ(execution_order[2], 3);

    executor->shutdown(true);
}

}  // namespace
}  // namespace pacs::bridge::integration
