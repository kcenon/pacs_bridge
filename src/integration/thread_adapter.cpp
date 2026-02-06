/**
 * @file thread_adapter.cpp
 * @brief Implementation of thread adapter for pacs_bridge
 *
 * Bridges thread_adapter interface with thread_system's thread_pool.
 * Provides two implementations:
 *   - thread_pool_adapter: wraps thread_pool for full ecosystem integration
 *   - simple_thread_adapter: standalone fallback for simpler deployments
 *
 * @see include/pacs/bridge/integration/thread_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/266
 */

#include "pacs/bridge/integration/thread_adapter.h"

#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include <kcenon/thread/thread_pool.h>
#endif

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace pacs::bridge::integration {

#ifndef PACS_BRIDGE_STANDALONE_BUILD

// =============================================================================
// thread_pool_adapter - Wraps thread_system's thread_pool
// =============================================================================

/**
 * @class thread_pool_adapter
 * @brief Thread adapter that wraps thread_system's thread_pool
 *
 * Provides full integration with the kcenon ecosystem threading infrastructure.
 * Supports priority-based task scheduling through submit options.
 */
class thread_pool_adapter : public thread_adapter {
public:
    thread_pool_adapter() = default;

    ~thread_pool_adapter() override {
        shutdown(true);
    }

    [[nodiscard]] bool initialize(const worker_pool_config& config) override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (pool_ && running_.load(std::memory_order_acquire)) {
            return false;  // Already initialized
        }

        config_ = config;
        pool_ = std::make_shared<kcenon::thread::thread_pool>(config.name);

        auto result = pool_->start();
        if (!result.is_ok()) {
            pool_.reset();
            return false;
        }

        running_.store(true, std::memory_order_release);
        return true;
    }

    void shutdown(bool wait_for_completion) override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!pool_ || !running_.load(std::memory_order_acquire)) {
            return;
        }

        pool_->stop(!wait_for_completion);
        running_.store(false, std::memory_order_release);
    }

    [[nodiscard]] size_t queue_size() const noexcept override {
        if (!pool_) {
            return 0;
        }
        return pool_->get_pending_task_count();
    }

    [[nodiscard]] size_t active_threads() const noexcept override {
        if (!pool_) {
            return 0;
        }
        return pool_->get_active_worker_count();
    }

    [[nodiscard]] bool is_running() const noexcept override {
        return running_.load(std::memory_order_acquire) && pool_ && pool_->is_running();
    }

protected:
    void submit_internal(std::function<void()> task,
                         task_priority /*priority*/) override {
        if (!pool_ || !running_.load(std::memory_order_acquire)) {
            return;
        }

        // thread_pool's submit returns a future; we discard it for fire-and-forget
        (void)pool_->submit(std::move(task));
    }

private:
    std::shared_ptr<kcenon::thread::thread_pool> pool_;
    worker_pool_config config_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
};

#endif  // PACS_BRIDGE_STANDALONE_BUILD

// =============================================================================
// simple_thread_adapter - Standalone Fallback
// =============================================================================

/**
 * @class simple_thread_adapter
 * @brief Lightweight thread adapter for standalone deployments
 *
 * Provides basic thread pool functionality without external dependencies.
 * Supports priority-based task scheduling with a simple priority queue.
 */
class simple_thread_adapter : public thread_adapter {
public:
    simple_thread_adapter() = default;

    ~simple_thread_adapter() override {
        shutdown(true);
    }

    [[nodiscard]] bool initialize(const worker_pool_config& config) override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (running_.load(std::memory_order_acquire)) {
            return false;  // Already initialized
        }

        config_ = config;
        running_.store(true, std::memory_order_release);

        // Create worker threads
        size_t thread_count = std::max(config.min_threads, size_t{1});
        workers_.reserve(thread_count);

        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }

        return true;
    }

    void shutdown(bool wait_for_completion) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_.load(std::memory_order_acquire)) {
                return;
            }
            running_.store(false, std::memory_order_release);
        }

        // Wake up all workers
        cv_.notify_all();

        // Wait for workers to finish
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();

        // Clear remaining tasks if not waiting
        if (!wait_for_completion) {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!task_queue_.empty()) {
                task_queue_.pop();
            }
        }
    }

    [[nodiscard]] size_t queue_size() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return task_queue_.size();
    }

    [[nodiscard]] size_t active_threads() const noexcept override {
        return active_count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_running() const noexcept override {
        return running_.load(std::memory_order_acquire);
    }

protected:
    void submit_internal(std::function<void()> task,
                         task_priority priority) override {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            task_queue_.push(prioritized_task{priority, std::move(task)});
        }
        cv_.notify_one();
    }

private:
    struct prioritized_task {
        task_priority priority;
        std::function<void()> task;

        bool operator<(const prioritized_task& other) const {
            // Higher priority = lower enum value, should come first
            return static_cast<int>(priority) > static_cast<int>(other.priority);
        }
    };

    void worker_loop() {
        while (running_.load(std::memory_order_acquire)) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] {
                    return !running_.load(std::memory_order_acquire) ||
                           !task_queue_.empty();
                });

                if (!running_.load(std::memory_order_acquire) && task_queue_.empty()) {
                    return;
                }

                if (!task_queue_.empty()) {
                    task = std::move(const_cast<prioritized_task&>(task_queue_.top()).task);
                    task_queue_.pop();
                }
            }

            if (task) {
                active_count_.fetch_add(1, std::memory_order_release);
                try {
                    task();
                } catch (...) {
                    // Swallow exceptions to prevent worker thread termination
                }
                active_count_.fetch_sub(1, std::memory_order_release);
            }
        }
    }

    worker_pool_config config_;
    std::vector<std::thread> workers_;
    std::priority_queue<prioritized_task> task_queue_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> active_count_{0};
};

// =============================================================================
// Factory Function with Fallback
// =============================================================================

/**
 * @brief Create thread adapter with automatic fallback
 *
 * Attempts to create thread_pool_adapter (wraps thread_system).
 * Falls back to simple_thread_adapter if thread_system is unavailable.
 *
 * @return Unique pointer to thread_adapter implementation
 */
std::unique_ptr<thread_adapter> create_thread_adapter() {
#ifndef PACS_BRIDGE_STANDALONE_BUILD
    // Try thread_pool_adapter first for full ecosystem integration
    try {
        return std::make_unique<thread_pool_adapter>();
    } catch (const std::exception&) {
        // Fall through to simple_thread_adapter
    }
#endif
    // Fallback to standalone implementation
    return std::make_unique<simple_thread_adapter>();
}

}  // namespace pacs::bridge::integration
