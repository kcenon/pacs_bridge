/**
 * @file executor_adapter.cpp
 * @brief Implementation of IExecutor adapter for pacs_bridge
 *
 * @see include/pacs/bridge/integration/executor_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/198
 */

#include "pacs/bridge/integration/executor_adapter.h"

#include <kcenon/thread/thread_pool.h>

#include <exception>

namespace pacs::bridge::integration {

// =============================================================================
// thread_pool_executor_adapter Implementation
// =============================================================================

thread_pool_executor_adapter::thread_pool_executor_adapter(
    std::shared_ptr<kcenon::thread::thread_pool> pool)
    : pool_(std::move(pool))
    , worker_count_(pool_ ? pool_->get_thread_count() : 0) {
    start_delay_thread();
}

thread_pool_executor_adapter::thread_pool_executor_adapter(std::size_t worker_count)
    : pool_(std::make_shared<kcenon::thread::thread_pool>("executor_pool"))
    , worker_count_(worker_count) {
    // Note: thread_pool manages its own workers internally via start()
    // The worker_count is stored for reporting purposes
    start_delay_thread();
}

thread_pool_executor_adapter::~thread_pool_executor_adapter() {
    shutdown(true);
}

void thread_pool_executor_adapter::start_delay_thread() {
    delay_thread_ = std::thread([this] { delay_thread_loop(); });
}

void thread_pool_executor_adapter::delay_thread_loop() {
    while (!shutdown_requested_.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(delay_mutex_);

        if (delayed_tasks_.empty()) {
            delay_cv_.wait(lock, [this] {
                return shutdown_requested_.load(std::memory_order_acquire) ||
                       !delayed_tasks_.empty();
            });
        }

        if (shutdown_requested_.load(std::memory_order_acquire)) {
            break;
        }

        while (!delayed_tasks_.empty()) {
            auto& top = delayed_tasks_.top();
            auto now = std::chrono::steady_clock::now();

            if (top.execute_at <= now) {
                auto task = std::move(const_cast<delayed_task&>(top).task);
                delayed_tasks_.pop();
                lock.unlock();

                if (task) {
                    task();
                }

                lock.lock();
            } else {
                delay_cv_.wait_until(lock, top.execute_at, [this] {
                    return shutdown_requested_.load(std::memory_order_acquire);
                });

                if (shutdown_requested_.load(std::memory_order_acquire)) {
                    break;
                }
            }
        }
    }
}

kcenon::common::Result<std::future<void>> thread_pool_executor_adapter::execute(
    std::unique_ptr<kcenon::common::interfaces::IJob>&& job) {
    if (!running_.load(std::memory_order_acquire)) {
        return kcenon::common::Result<std::future<void>>(
            kcenon::common::error_info{-1, "Executor is not running", "executor"});
    }

    if (!job) {
        return kcenon::common::Result<std::future<void>>(
            kcenon::common::error_info{-2, "Job is null", "executor"});
    }

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    auto shared_job = std::shared_ptr<kcenon::common::interfaces::IJob>(std::move(job));

    pending_count_.fetch_add(1, std::memory_order_release);

    pool_->submit_task([this, shared_job, promise]() {
        try {
            auto result = shared_job->execute();
            if (result.is_ok()) {
                promise->set_value();
            } else {
                promise->set_exception(std::make_exception_ptr(
                    std::runtime_error(result.error().message)));
            }
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
        pending_count_.fetch_sub(1, std::memory_order_release);
    });

    return kcenon::common::Result<std::future<void>>(std::move(future));
}

kcenon::common::Result<std::future<void>> thread_pool_executor_adapter::execute_delayed(
    std::unique_ptr<kcenon::common::interfaces::IJob>&& job,
    std::chrono::milliseconds delay) {
    if (!running_.load(std::memory_order_acquire)) {
        return kcenon::common::Result<std::future<void>>(
            kcenon::common::error_info{-1, "Executor is not running", "executor"});
    }

    if (!job) {
        return kcenon::common::Result<std::future<void>>(
            kcenon::common::error_info{-2, "Job is null", "executor"});
    }

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    auto shared_job = std::shared_ptr<kcenon::common::interfaces::IJob>(std::move(job));

    pending_count_.fetch_add(1, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(delay_mutex_);
        delayed_tasks_.push(delayed_task{
            std::chrono::steady_clock::now() + delay,
            [this, shared_job, promise]() {
                pool_->submit_task([this, shared_job, promise]() {
                    try {
                        auto result = shared_job->execute();
                        if (result.is_ok()) {
                            promise->set_value();
                        } else {
                            promise->set_exception(std::make_exception_ptr(
                                std::runtime_error(result.error().message)));
                        }
                    } catch (...) {
                        promise->set_exception(std::current_exception());
                    }
                    pending_count_.fetch_sub(1, std::memory_order_release);
                });
            }});
    }
    delay_cv_.notify_one();

    return kcenon::common::Result<std::future<void>>(std::move(future));
}

std::size_t thread_pool_executor_adapter::worker_count() const {
    return worker_count_;
}

bool thread_pool_executor_adapter::is_running() const {
    return running_.load(std::memory_order_acquire);
}

std::size_t thread_pool_executor_adapter::pending_tasks() const {
    return pending_count_.load(std::memory_order_acquire);
}

void thread_pool_executor_adapter::shutdown(bool wait_for_completion) {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;  // Already shutdown
    }

    shutdown_requested_.store(true, std::memory_order_release);
    delay_cv_.notify_all();

    if (delay_thread_.joinable()) {
        delay_thread_.join();
    }

    if (pool_ && wait_for_completion) {
        pool_->stop(true);
    }
}

std::shared_ptr<kcenon::thread::thread_pool>
thread_pool_executor_adapter::get_underlying_pool() const {
    return pool_;
}

// =============================================================================
// simple_executor Implementation
// =============================================================================

simple_executor::simple_executor(std::size_t worker_count)
    : worker_count_(worker_count > 0 ? worker_count : 1) {
    for (std::size_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
    delay_thread_ = std::thread([this] { delay_thread_loop(); });
}

simple_executor::~simple_executor() {
    shutdown(true);
}

void simple_executor::worker_loop() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_acquire) || !task_queue_.empty();
            });

            if (!running_.load(std::memory_order_acquire) && task_queue_.empty()) {
                return;
            }

            if (!task_queue_.empty()) {
                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
        }

        if (task) {
            task();
        }
    }
}

void simple_executor::delay_thread_loop() {
    while (running_.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(delay_mutex_);

        if (delayed_tasks_.empty()) {
            delay_cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_acquire) ||
                       !delayed_tasks_.empty();
            });
        }

        if (!running_.load(std::memory_order_acquire)) {
            break;
        }

        while (!delayed_tasks_.empty()) {
            auto& top = delayed_tasks_.top();
            auto now = std::chrono::steady_clock::now();

            if (top.execute_at <= now) {
                auto task = std::move(const_cast<delayed_task&>(top).task);
                delayed_tasks_.pop();
                lock.unlock();

                if (task) {
                    std::lock_guard<std::mutex> queue_lock(queue_mutex_);
                    task_queue_.push(std::move(task));
                    queue_cv_.notify_one();
                }

                lock.lock();
            } else {
                delay_cv_.wait_until(lock, top.execute_at, [this] {
                    return !running_.load(std::memory_order_acquire);
                });

                if (!running_.load(std::memory_order_acquire)) {
                    break;
                }
            }
        }
    }
}

kcenon::common::Result<std::future<void>> simple_executor::execute(
    std::unique_ptr<kcenon::common::interfaces::IJob>&& job) {
    if (!running_.load(std::memory_order_acquire)) {
        return kcenon::common::Result<std::future<void>>(
            kcenon::common::error_info{-1, "Executor is not running", "executor"});
    }

    if (!job) {
        return kcenon::common::Result<std::future<void>>(
            kcenon::common::error_info{-2, "Job is null", "executor"});
    }

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    auto shared_job = std::shared_ptr<kcenon::common::interfaces::IJob>(std::move(job));

    pending_count_.fetch_add(1, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push([this, shared_job, promise]() {
            try {
                auto result = shared_job->execute();
                if (result.is_ok()) {
                    promise->set_value();
                } else {
                    promise->set_exception(std::make_exception_ptr(
                        std::runtime_error(result.error().message)));
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
            pending_count_.fetch_sub(1, std::memory_order_release);
        });
    }
    queue_cv_.notify_one();

    return kcenon::common::Result<std::future<void>>(std::move(future));
}

kcenon::common::Result<std::future<void>> simple_executor::execute_delayed(
    std::unique_ptr<kcenon::common::interfaces::IJob>&& job,
    std::chrono::milliseconds delay) {
    if (!running_.load(std::memory_order_acquire)) {
        return kcenon::common::Result<std::future<void>>(
            kcenon::common::error_info{-1, "Executor is not running", "executor"});
    }

    if (!job) {
        return kcenon::common::Result<std::future<void>>(
            kcenon::common::error_info{-2, "Job is null", "executor"});
    }

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    auto shared_job = std::shared_ptr<kcenon::common::interfaces::IJob>(std::move(job));

    pending_count_.fetch_add(1, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(delay_mutex_);
        delayed_tasks_.push(delayed_task{
            std::chrono::steady_clock::now() + delay,
            [this, shared_job, promise]() {
                try {
                    auto result = shared_job->execute();
                    if (result.is_ok()) {
                        promise->set_value();
                    } else {
                        promise->set_exception(std::make_exception_ptr(
                            std::runtime_error(result.error().message)));
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
                pending_count_.fetch_sub(1, std::memory_order_release);
            }});
    }
    delay_cv_.notify_one();

    return kcenon::common::Result<std::future<void>>(std::move(future));
}

std::size_t simple_executor::worker_count() const {
    return worker_count_;
}

bool simple_executor::is_running() const {
    return running_.load(std::memory_order_acquire);
}

std::size_t simple_executor::pending_tasks() const {
    return pending_count_.load(std::memory_order_acquire);
}

void simple_executor::shutdown(bool wait_for_completion) {
    // Use exchange to ensure only one thread performs shutdown
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;  // Already shutdown or in progress
    }

    queue_cv_.notify_all();
    delay_cv_.notify_all();

    if (delay_thread_.joinable()) {
        delay_thread_.join();
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    // Clear remaining tasks if not waiting for completion
    if (!wait_for_completion) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!task_queue_.empty()) {
            task_queue_.pop();
        }
    }
}

// =============================================================================
// Factory Functions
// =============================================================================

std::shared_ptr<kcenon::common::interfaces::IExecutor>
make_executor(std::size_t worker_count) {
    return std::make_shared<simple_executor>(worker_count);
}

std::shared_ptr<kcenon::common::interfaces::IExecutor>
make_executor(std::shared_ptr<kcenon::thread::thread_pool> pool) {
    return std::make_shared<thread_pool_executor_adapter>(std::move(pool));
}

}  // namespace pacs::bridge::integration
