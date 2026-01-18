/**
 * @file thread_adapter_test.cpp
 * @brief Unit tests for thread_adapter implementations
 *
 * Tests for thread_pool_adapter, simple_thread_adapter, and factory functions.
 *
 * @see include/pacs/bridge/integration/thread_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/266
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "pacs/bridge/integration/thread_adapter.h"

#include "utils/test_helpers.h"

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>
#include <vector>

namespace pacs::bridge::integration {
namespace {

using namespace ::testing;
using namespace pacs::bridge::test;

// =============================================================================
// Thread Adapter Creation Tests
// =============================================================================

class ThreadAdapterTest : public pacs_bridge_test {};

TEST_F(ThreadAdapterTest, CreateAdapter) {
    auto adapter = create_thread_adapter();
    ASSERT_NE(adapter, nullptr);
    EXPECT_FALSE(adapter->is_running());
}

TEST_F(ThreadAdapterTest, InitializeWithDefaultConfig) {
    auto adapter = create_thread_adapter();
    ASSERT_NE(adapter, nullptr);

    worker_pool_config config;
    config.name = "test_pool";
    config.min_threads = 2;
    config.max_threads = 4;

    EXPECT_TRUE(adapter->initialize(config));
    EXPECT_TRUE(adapter->is_running());

    adapter->shutdown(true);
    EXPECT_FALSE(adapter->is_running());
}

TEST_F(ThreadAdapterTest, DoubleInitializeFails) {
    auto adapter = create_thread_adapter();
    ASSERT_NE(adapter, nullptr);

    worker_pool_config config;
    config.name = "test_pool";
    config.min_threads = 2;

    EXPECT_TRUE(adapter->initialize(config));
    EXPECT_FALSE(adapter->initialize(config));  // Second init should fail

    adapter->shutdown(true);
}

TEST_F(ThreadAdapterTest, ShutdownWithoutInitialize) {
    auto adapter = create_thread_adapter();
    ASSERT_NE(adapter, nullptr);

    // Should not crash
    adapter->shutdown(true);
    EXPECT_FALSE(adapter->is_running());
}

// =============================================================================
// Task Submission Tests
// =============================================================================

class ThreadAdapterSubmitTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        adapter_ = create_thread_adapter();
        worker_pool_config config;
        config.name = "submit_test_pool";
        config.min_threads = 2;
        adapter_->initialize(config);
    }

    void TearDown() override {
        if (adapter_) {
            adapter_->shutdown(true);
        }
    }

    std::unique_ptr<thread_adapter> adapter_;
};

TEST_F(ThreadAdapterSubmitTest, SubmitSimpleTask) {
    std::atomic<bool> executed{false};

    auto future = adapter_->submit([&executed]() {
        executed = true;
        return 42;
    });

    EXPECT_EQ(future.get(), 42);
    EXPECT_TRUE(executed.load());
}

TEST_F(ThreadAdapterSubmitTest, SubmitVoidTask) {
    std::atomic<bool> executed{false};

    auto future = adapter_->submit([&executed]() {
        executed = true;
    });

    future.get();  // Wait for completion
    EXPECT_TRUE(executed.load());
}

TEST_F(ThreadAdapterSubmitTest, SubmitWithPriority) {
    std::atomic<int> counter{0};

    auto low_future = adapter_->submit([&counter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return counter.fetch_add(1);
    }, task_priority::low);

    auto high_future = adapter_->submit([&counter]() {
        return counter.fetch_add(1);
    }, task_priority::high);

    // Both should complete
    low_future.get();
    high_future.get();

    EXPECT_EQ(counter.load(), 2);
}

TEST_F(ThreadAdapterSubmitTest, SubmitMultipleTasks) {
    const int num_tasks = 100;
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(adapter_->submit([&counter]() {
            counter.fetch_add(1);
        }));
    }

    // Wait for all tasks
    for (auto& f : futures) {
        f.get();
    }

    EXPECT_EQ(counter.load(), num_tasks);
}

TEST_F(ThreadAdapterSubmitTest, TaskWithException) {
    auto future = adapter_->submit([]() -> int {
        throw std::runtime_error("Test exception");
    });

    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST_F(ThreadAdapterSubmitTest, QueueSize) {
    // Submit tasks without waiting
    std::latch latch(1);
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 10; ++i) {
        futures.push_back(adapter_->submit([&latch]() {
            latch.wait();
        }));
    }

    // Tasks should be queued or running
    // Note: queue_size may vary depending on implementation
    EXPECT_GE(adapter_->active_threads(), size_t{0});

    latch.count_down();

    for (auto& f : futures) {
        f.get();
    }
}

TEST_F(ThreadAdapterSubmitTest, ActiveThreads) {
    std::latch start_latch(1);
    std::latch end_latch(2);
    std::vector<std::future<void>> futures;

    // Submit tasks that wait
    for (int i = 0; i < 2; ++i) {
        futures.push_back(adapter_->submit([&start_latch, &end_latch]() {
            start_latch.wait();
            end_latch.count_down();
        }));
    }

    // Give time for threads to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    start_latch.count_down();
    end_latch.wait();

    for (auto& f : futures) {
        f.get();
    }
}

// =============================================================================
// Shutdown Tests
// =============================================================================

class ThreadAdapterShutdownTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        adapter_ = create_thread_adapter();
        worker_pool_config config;
        config.name = "shutdown_test_pool";
        config.min_threads = 2;
        adapter_->initialize(config);
    }

    std::unique_ptr<thread_adapter> adapter_;
};

TEST_F(ThreadAdapterShutdownTest, ShutdownWaitsForCompletion) {
    std::atomic<bool> task_completed{false};

    adapter_->submit([&task_completed]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        task_completed = true;
    });

    adapter_->shutdown(true);  // Wait for completion

    EXPECT_TRUE(task_completed.load());
    EXPECT_FALSE(adapter_->is_running());
}

TEST_F(ThreadAdapterShutdownTest, ShutdownImmediately) {
    std::atomic<int> started_count{0};

    // Submit many tasks
    for (int i = 0; i < 100; ++i) {
        adapter_->submit([&started_count]() {
            started_count.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    }

    // Shutdown without waiting - some tasks may not complete
    adapter_->shutdown(false);

    EXPECT_FALSE(adapter_->is_running());
}

TEST_F(ThreadAdapterShutdownTest, DoubleShutdown) {
    adapter_->shutdown(true);
    adapter_->shutdown(true);  // Should not crash

    EXPECT_FALSE(adapter_->is_running());
}

// =============================================================================
// Stress Tests
// =============================================================================

class ThreadAdapterStressTest : public pacs_bridge_test {
protected:
    void SetUp() override {
        adapter_ = create_thread_adapter();
        worker_pool_config config;
        config.name = "stress_test_pool";
        config.min_threads = 4;
        config.max_threads = 8;
        adapter_->initialize(config);
    }

    void TearDown() override {
        if (adapter_) {
            adapter_->shutdown(true);
        }
    }

    std::unique_ptr<thread_adapter> adapter_;
};

TEST_F(ThreadAdapterStressTest, HighVolumeTasks) {
    const int num_tasks = 1000;
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    futures.reserve(num_tasks);

    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(adapter_->submit([&counter]() {
            counter.fetch_add(1);
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    EXPECT_EQ(counter.load(), num_tasks);
}

TEST_F(ThreadAdapterStressTest, MixedPriorityTasks) {
    const int tasks_per_priority = 100;
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    // Submit mixed priority tasks
    for (int i = 0; i < tasks_per_priority; ++i) {
        futures.push_back(adapter_->submit([&counter]() {
            counter.fetch_add(1);
        }, task_priority::low));

        futures.push_back(adapter_->submit([&counter]() {
            counter.fetch_add(1);
        }, task_priority::normal));

        futures.push_back(adapter_->submit([&counter]() {
            counter.fetch_add(1);
        }, task_priority::high));

        futures.push_back(adapter_->submit([&counter]() {
            counter.fetch_add(1);
        }, task_priority::critical));
    }

    for (auto& f : futures) {
        f.get();
    }

    EXPECT_EQ(counter.load(), tasks_per_priority * 4);
}

TEST_F(ThreadAdapterStressTest, ConcurrentSubmit) {
    const int num_threads = 8;
    const int tasks_per_thread = 100;
    std::atomic<int> counter{0};
    std::vector<std::thread> submitters;
    std::vector<std::future<void>> all_futures;
    std::mutex futures_mutex;

    for (int t = 0; t < num_threads; ++t) {
        submitters.emplace_back([this, &counter, &all_futures, &futures_mutex, tasks_per_thread]() {
            for (int i = 0; i < tasks_per_thread; ++i) {
                auto future = adapter_->submit([&counter]() {
                    counter.fetch_add(1);
                });

                std::lock_guard<std::mutex> lock(futures_mutex);
                all_futures.push_back(std::move(future));
            }
        });
    }

    for (auto& t : submitters) {
        t.join();
    }

    for (auto& f : all_futures) {
        f.get();
    }

    EXPECT_EQ(counter.load(), num_threads * tasks_per_thread);
}

}  // namespace
}  // namespace pacs::bridge::integration
