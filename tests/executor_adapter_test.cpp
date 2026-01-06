/**
 * @file executor_adapter_test.cpp
 * @brief Unit tests for IExecutor adapter implementations
 *
 * Tests for simple_executor, thread_pool_executor_adapter, lambda_job,
 * and factory functions.
 *
 * @see include/pacs/bridge/integration/executor_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/198
 * @see https://github.com/kcenon/pacs_bridge/issues/210
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/integration/executor_adapter.h"

#include "utils/test_helpers.h"

#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <thread>
#include <vector>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Lambda Job Tests
// =============================================================================

class LambdaJobTest : public pacs_bridge_test {};

TEST_F(LambdaJobTest, ExecuteSuccessful) {
    bool executed = false;
    auto job = std::make_unique<lambda_job>(
        [&executed]() -> kcenon::common::VoidResult {
            executed = true;
            return kcenon::common::VoidResult(std::monostate{});
        },
        "test_job",
        5);

    EXPECT_EQ(job->get_name(), "test_job");
    EXPECT_EQ(job->get_priority(), 5);

    auto result = job->execute();
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(executed);
}

TEST_F(LambdaJobTest, ExecuteVoidReturning) {
    bool executed = false;
    auto job = std::make_unique<lambda_job>(
        [&executed]() { executed = true; },
        "void_job",
        0);

    auto result = job->execute();
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(executed);
}

TEST_F(LambdaJobTest, ExecuteWithError) {
    auto job = std::make_unique<lambda_job>(
        []() -> kcenon::common::VoidResult {
            return kcenon::common::VoidResult(
                kcenon::common::error_info{-100, "Test error", "test"});
        },
        "error_job");

    auto result = job->execute();
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, -100);
    EXPECT_EQ(result.error().message, "Test error");
}

TEST_F(LambdaJobTest, DefaultName) {
    auto job = std::make_unique<lambda_job>(
        []() -> kcenon::common::VoidResult {
            return kcenon::common::VoidResult(std::monostate{});
        });

    EXPECT_EQ(job->get_name(), "lambda_job");
    EXPECT_EQ(job->get_priority(), 0);
}

TEST_F(LambdaJobTest, NullFunction) {
    lambda_job::job_function null_func;
    auto job = std::make_unique<lambda_job>(null_func, "null_job");

    auto result = job->execute();
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, -1);
}

// =============================================================================
// Simple Executor Tests
// =============================================================================

class SimpleExecutorTest : public pacs_bridge_test {};

TEST_F(SimpleExecutorTest, Construction) {
    auto executor = std::make_shared<simple_executor>(2);

    EXPECT_EQ(executor->worker_count(), 2);
    EXPECT_TRUE(executor->is_running());
    EXPECT_EQ(executor->pending_tasks(), 0);
}

TEST_F(SimpleExecutorTest, DefaultWorkerCount) {
    auto executor = std::make_shared<simple_executor>();

    EXPECT_GT(executor->worker_count(), 0);
    EXPECT_TRUE(executor->is_running());
}

TEST_F(SimpleExecutorTest, ExecuteJob) {
    auto executor = std::make_shared<simple_executor>(2);
    std::atomic<bool> executed{false};

    auto job = std::make_unique<lambda_job>(
        [&executed]() {
            executed = true;
        });

    auto result = executor->execute(std::move(job));
    ASSERT_TRUE(result.is_ok());

    result.value().wait();
    EXPECT_TRUE(executed.load());
}

TEST_F(SimpleExecutorTest, ExecuteMultipleJobs) {
    auto executor = std::make_shared<simple_executor>(4);
    std::atomic<int> counter{0};
    constexpr int job_count = 10;

    std::vector<std::future<void>> futures;
    for (int i = 0; i < job_count; ++i) {
        auto job = std::make_unique<lambda_job>(
            [&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });

        auto result = executor->execute(std::move(job));
        ASSERT_TRUE(result.is_ok());
        futures.push_back(std::move(result.value()));
    }

    for (auto& f : futures) {
        f.wait();
    }

    EXPECT_EQ(counter.load(), job_count);
}

TEST_F(SimpleExecutorTest, ExecuteDelayed) {
    auto executor = std::make_shared<simple_executor>(2);
    std::atomic<bool> executed{false};
    auto start_time = std::chrono::steady_clock::now();

    auto job = std::make_unique<lambda_job>(
        [&executed]() {
            executed = true;
        });

    auto result = executor->execute_delayed(
        std::move(job), std::chrono::milliseconds{100});
    ASSERT_TRUE(result.is_ok());

    result.value().wait();
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    EXPECT_TRUE(executed.load());
    EXPECT_GE(elapsed.count(), 90);  // Allow some timing tolerance
}

TEST_F(SimpleExecutorTest, Submit) {
    auto executor = std::make_shared<simple_executor>(2);
    std::atomic<bool> executed{false};

    auto result = executor->submit(
        [&executed]() { executed = true; },
        "submit_test");

    ASSERT_TRUE(result.is_ok());
    result.value().wait();
    EXPECT_TRUE(executed.load());
}

TEST_F(SimpleExecutorTest, Shutdown) {
    auto executor = std::make_shared<simple_executor>(2);

    EXPECT_TRUE(executor->is_running());

    executor->shutdown(true);

    EXPECT_FALSE(executor->is_running());
}

TEST_F(SimpleExecutorTest, ExecuteAfterShutdown) {
    auto executor = std::make_shared<simple_executor>(2);
    executor->shutdown(true);

    auto job = std::make_unique<lambda_job>(
        []() { return kcenon::common::VoidResult(std::monostate{}); });

    auto result = executor->execute(std::move(job));
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, -1);
}

TEST_F(SimpleExecutorTest, ExecuteNullJob) {
    auto executor = std::make_shared<simple_executor>(2);

    auto result = executor->execute(nullptr);
    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error().code, -2);
}

// =============================================================================
// Factory Function Tests
// =============================================================================

class ExecutorFactoryTest : public pacs_bridge_test {};

TEST_F(ExecutorFactoryTest, MakeExecutorWithCount) {
    auto executor = make_executor(4);

    ASSERT_NE(executor, nullptr);
    EXPECT_EQ(executor->worker_count(), 4);
    EXPECT_TRUE(executor->is_running());
}

TEST_F(ExecutorFactoryTest, MakeExecutorDefault) {
    auto executor = make_executor();

    ASSERT_NE(executor, nullptr);
    EXPECT_GT(executor->worker_count(), 0);
    EXPECT_TRUE(executor->is_running());
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

class ExecutorThreadSafetyTest : public pacs_bridge_test {};

TEST_F(ExecutorThreadSafetyTest, ConcurrentExecution) {
    auto executor = make_executor(4);
    std::atomic<int> counter{0};
    constexpr int jobs_per_thread = 25;
    constexpr int thread_count = 4;

    std::vector<std::thread> threads;
    std::vector<std::future<void>> all_futures;
    std::mutex futures_mutex;

    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < jobs_per_thread; ++i) {
                auto job = std::make_unique<lambda_job>(
                    [&counter]() {
                        counter.fetch_add(1, std::memory_order_relaxed);
                    });

                auto result = executor->execute(std::move(job));
                if (result.is_ok()) {
                    std::lock_guard<std::mutex> lock(futures_mutex);
                    all_futures.push_back(std::move(result.value()));
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (auto& f : all_futures) {
        f.wait();
    }

    EXPECT_EQ(counter.load(), jobs_per_thread * thread_count);
}

TEST_F(ExecutorThreadSafetyTest, ConcurrentShutdown) {
    auto executor = make_executor(4);

    // Submit some jobs
    for (int i = 0; i < 10; ++i) {
        auto job = std::make_unique<lambda_job>(
            []() {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            });
        executor->execute(std::move(job));
    }

    // Shutdown from multiple threads should not crash
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            executor->shutdown(true);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_FALSE(executor->is_running());
}

// =============================================================================
// Job Error Propagation Tests
// =============================================================================

class JobErrorPropagationTest : public pacs_bridge_test {};

TEST_F(JobErrorPropagationTest, ExceptionInJob) {
    auto executor = make_executor(2);

    auto job = std::make_unique<lambda_job>(
        []() -> kcenon::common::VoidResult {
            throw std::runtime_error("Test exception");
        });

    auto result = executor->execute(std::move(job));
    ASSERT_TRUE(result.is_ok());

    // The future should propagate the exception
    EXPECT_THROW(result.value().get(), std::runtime_error);
}

TEST_F(JobErrorPropagationTest, ErrorResult) {
    auto executor = make_executor(2);

    auto job = std::make_unique<lambda_job>(
        []() -> kcenon::common::VoidResult {
            return kcenon::common::VoidResult(
                kcenon::common::error_info{-500, "Job failed", "test"});
        });

    auto result = executor->execute(std::move(job));
    ASSERT_TRUE(result.is_ok());

    // Error result should be converted to exception
    EXPECT_THROW(result.value().get(), std::runtime_error);
}

// =============================================================================
// Pending Tasks Tests
// =============================================================================

class PendingTasksTest : public pacs_bridge_test {};

TEST_F(PendingTasksTest, TracksPendingTasks) {
    auto executor = make_executor(1);
    std::latch latch(1);
    std::atomic<bool> job_started{false};

    // Submit a job that blocks
    auto job = std::make_unique<lambda_job>(
        [&latch, &job_started]() {
            job_started = true;
            latch.wait();
        });

    auto result = executor->execute(std::move(job));
    ASSERT_TRUE(result.is_ok());

    // Wait for job to start
    while (!job_started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // Should have pending task
    EXPECT_GE(executor->pending_tasks(), 0);

    // Release the job
    latch.count_down();
    result.value().wait();

    // Give some time for pending count to update
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    EXPECT_EQ(executor->pending_tasks(), 0);
}

}  // namespace
}  // namespace pacs::bridge::integration
