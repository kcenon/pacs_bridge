/**
 * @file thread_pool_manager.cpp
 * @brief Implementation of thread pool management with work-stealing
 */

#include "pacs/bridge/performance/thread_pool_manager.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// Thread Pool Manager Implementation
// =============================================================================

struct thread_pool_manager::impl {
    thread_pool_config config;
    thread_pool_statistics stats;

    std::vector<std::thread> workers;
    std::vector<std::deque<std::pair<task_priority, task_fn>>> task_queues;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<bool> running{false};
    std::atomic<bool> stopping{false};

    // Per-thread random generators for work stealing
    std::vector<std::mt19937> thread_rngs;

    explicit impl(const thread_pool_config& cfg) : config(cfg) {
        // Determine actual max threads
        if (config.max_threads == 0) {
            config.max_threads = std::thread::hardware_concurrency();
            if (config.max_threads == 0) {
                config.max_threads = 4;  // Fallback
            }
        }

        // Ensure min <= max
        if (config.min_threads > config.max_threads) {
            config.min_threads = config.max_threads;
        }
    }

    ~impl() {
        if (running.load()) {
            stop(true, std::chrono::seconds{30});
        }
    }

    std::expected<void, performance_error> start() {
        if (running.exchange(true)) {
            return std::unexpected(performance_error::invalid_configuration);
        }

        stopping.store(false);

        // Initialize per-thread queues and RNGs
        task_queues.resize(config.min_threads);
        thread_rngs.resize(config.min_threads);
        std::random_device rd;
        for (size_t i = 0; i < config.min_threads; ++i) {
            thread_rngs[i].seed(rd());
        }

        // Create worker threads
        workers.reserve(config.min_threads);
        for (size_t i = 0; i < config.min_threads; ++i) {
            workers.emplace_back([this, i] { worker_loop(i); });
        }

        stats.total_threads.store(config.min_threads, std::memory_order_relaxed);

        return {};
    }

    std::expected<void, performance_error>
    stop(bool wait_for_tasks, std::chrono::milliseconds timeout) {
        if (!running.load()) {
            return std::unexpected(performance_error::not_initialized);
        }

        stopping.store(true);
        queue_cv.notify_all();

        auto deadline = std::chrono::steady_clock::now() + timeout;

        for (auto& worker : workers) {
            if (worker.joinable()) {
                if (wait_for_tasks) {
                    // Wait with timeout
                    auto remaining =
                        deadline - std::chrono::steady_clock::now();
                    if (remaining > std::chrono::milliseconds::zero()) {
                        // Note: std::thread doesn't support timed join,
                        // so we just join and hope for the best
                        worker.join();
                    } else {
                        worker.detach();  // Timeout exceeded
                    }
                } else {
                    worker.detach();
                }
            }
        }

        workers.clear();
        task_queues.clear();
        running.store(false);

        return {};
    }

    void worker_loop(size_t worker_id) {
        stats.active_threads.fetch_add(1, std::memory_order_relaxed);

        while (!stopping.load(std::memory_order_acquire)) {
            std::optional<task_fn> task;

            {
                std::unique_lock lock(queue_mutex);

                // Check own queue first (by priority)
                task = pop_from_queue(worker_id);

                if (!task && config.enable_work_stealing) {
                    // Try to steal from other queues
                    task = try_steal(worker_id);
                }

                if (!task) {
                    // Wait for new task
                    stats.active_threads.fetch_sub(1, std::memory_order_relaxed);
                    queue_cv.wait_for(lock, std::chrono::milliseconds{100},
                                      [this, worker_id] {
                                          return stopping.load() ||
                                                 !task_queues[worker_id].empty();
                                      });
                    stats.active_threads.fetch_add(1, std::memory_order_relaxed);

                    task = pop_from_queue(worker_id);
                }
            }

            if (task) {
                auto start = std::chrono::steady_clock::now();

                try {
                    (*task)();
                } catch (...) {
                    // Swallow exceptions from tasks
                }

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        end - start)
                        .count();

                stats.total_completed.fetch_add(1, std::memory_order_relaxed);

                // Update average (simple moving average approximation)
                auto current_avg =
                    stats.avg_task_duration_us.load(std::memory_order_relaxed);
                auto new_avg = (current_avg * 7 + duration_us) / 8;
                stats.avg_task_duration_us.store(new_avg,
                                                 std::memory_order_relaxed);

                // Update peak
                auto peak =
                    stats.peak_task_duration_us.load(std::memory_order_relaxed);
                while (static_cast<uint64_t>(duration_us) > peak &&
                       !stats.peak_task_duration_us.compare_exchange_weak(
                           peak, duration_us, std::memory_order_relaxed)) {
                }
            }
        }

        stats.active_threads.fetch_sub(1, std::memory_order_relaxed);
    }

    std::optional<task_fn> pop_from_queue(size_t worker_id) {
        if (worker_id >= task_queues.size()) return std::nullopt;

        auto& queue = task_queues[worker_id];

        // Find highest priority task
        auto it = std::min_element(
            queue.begin(), queue.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        if (it != queue.end()) {
            auto task = std::move(it->second);
            queue.erase(it);
            stats.queued_tasks.fetch_sub(1, std::memory_order_relaxed);
            return task;
        }

        return std::nullopt;
    }

    std::optional<task_fn> try_steal(size_t worker_id) {
        if (task_queues.size() <= 1) return std::nullopt;

        // Random victim selection
        std::uniform_int_distribution<size_t> dist(0, task_queues.size() - 2);
        size_t victim = dist(thread_rngs[worker_id]);
        if (victim >= worker_id) ++victim;  // Skip self

        if (!task_queues[victim].empty()) {
            // Steal from back (least recently added)
            auto task = std::move(task_queues[victim].back().second);
            task_queues[victim].pop_back();
            stats.queued_tasks.fetch_sub(1, std::memory_order_relaxed);
            stats.work_stolen.fetch_add(1, std::memory_order_relaxed);
            return task;
        }

        return std::nullopt;
    }

    bool post(task_fn task, task_priority priority) {
        if (!running.load() || stopping.load()) {
            return false;
        }

        size_t total = stats.total_submitted.fetch_add(1, std::memory_order_relaxed);

        // Round-robin to queues
        size_t queue_id = total % task_queues.size();

        {
            std::lock_guard lock(queue_mutex);

            // Check queue capacity
            if (task_queues[queue_id].size() >= config.queue_capacity) {
                stats.total_rejected.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            task_queues[queue_id].emplace_back(priority, std::move(task));

            size_t queued =
                stats.queued_tasks.fetch_add(1, std::memory_order_relaxed) + 1;
            size_t peak = stats.peak_queued.load(std::memory_order_relaxed);
            while (queued > peak &&
                   !stats.peak_queued.compare_exchange_weak(
                       peak, queued, std::memory_order_relaxed)) {
            }
        }

        queue_cv.notify_one();
        return true;
    }

    bool try_post(task_fn task, task_priority priority) noexcept {
        // Same as post, but doesn't wait
        return post(std::move(task), priority);
    }

    size_t post_batch(std::span<task_fn> tasks, task_priority priority) {
        size_t posted = 0;
        for (auto& task : tasks) {
            if (post(std::move(task), priority)) {
                ++posted;
            }
        }
        return posted;
    }
};

thread_pool_manager::thread_pool_manager(const thread_pool_config& config)
    : impl_(std::make_unique<impl>(config)) {}

thread_pool_manager::~thread_pool_manager() = default;

std::expected<void, performance_error> thread_pool_manager::start() {
    return impl_->start();
}

std::expected<void, performance_error>
thread_pool_manager::stop(bool wait_for_tasks, std::chrono::milliseconds timeout) {
    return impl_->stop(wait_for_tasks, timeout);
}

bool thread_pool_manager::is_running() const noexcept {
    return impl_->running.load(std::memory_order_relaxed);
}

bool thread_pool_manager::post(task_fn task, task_priority priority) {
    return impl_->post(std::move(task), priority);
}

bool thread_pool_manager::try_post(task_fn task, task_priority priority) noexcept {
    return impl_->try_post(std::move(task), priority);
}

size_t thread_pool_manager::post_batch(std::span<task_fn> tasks,
                                       task_priority priority) {
    return impl_->post_batch(tasks, priority);
}

std::expected<void, performance_error>
thread_pool_manager::scale(size_t thread_count) {
    // For now, just validate the count
    if (thread_count < impl_->config.min_threads ||
        thread_count > impl_->config.max_threads) {
        return std::unexpected(performance_error::invalid_configuration);
    }
    // Actual scaling would require more complex implementation
    return {};
}

size_t thread_pool_manager::thread_count() const noexcept {
    return impl_->stats.total_threads.load(std::memory_order_relaxed);
}

size_t thread_pool_manager::min_threads() const noexcept {
    return impl_->config.min_threads;
}

size_t thread_pool_manager::max_threads() const noexcept {
    return impl_->config.max_threads;
}

size_t thread_pool_manager::pending_tasks() const noexcept {
    return impl_->stats.queued_tasks.load(std::memory_order_relaxed);
}

size_t thread_pool_manager::active_tasks() const noexcept {
    return impl_->stats.active_threads.load(std::memory_order_relaxed);
}

const thread_pool_statistics& thread_pool_manager::statistics() const noexcept {
    return impl_->stats;
}

void thread_pool_manager::reset_statistics() {
    impl_->stats.reset();
}

const thread_pool_config& thread_pool_manager::config() const noexcept {
    return impl_->config;
}

// =============================================================================
// Global Instance
// =============================================================================

namespace {
std::unique_ptr<thread_pool_manager> g_instance;
std::mutex g_instance_mutex;
}  // namespace

thread_pool_manager& thread_pool_manager::instance() {
    std::lock_guard lock(g_instance_mutex);
    if (!g_instance) {
        g_instance = std::make_unique<thread_pool_manager>();
        (void)g_instance->start();
    }
    return *g_instance;
}

void thread_pool_manager::initialize(const thread_pool_config& config) {
    std::lock_guard lock(g_instance_mutex);
    if (g_instance) {
        (void)g_instance->stop(true, std::chrono::seconds{30});
    }
    g_instance = std::make_unique<thread_pool_manager>(config);
    (void)g_instance->start();
}

void thread_pool_manager::shutdown() {
    std::lock_guard lock(g_instance_mutex);
    if (g_instance) {
        (void)g_instance->stop(true, std::chrono::seconds{30});
        g_instance.reset();
    }
}

// =============================================================================
// Scoped Task Guard
// =============================================================================

scoped_task_guard::scoped_task_guard(thread_pool_statistics& stats)
    : stats_(stats), start_(std::chrono::steady_clock::now()) {
    stats_.active_threads.fetch_add(1, std::memory_order_relaxed);
}

scoped_task_guard::~scoped_task_guard() {
    stats_.active_threads.fetch_sub(1, std::memory_order_relaxed);
    auto duration = elapsed();
    stats_.total_completed.fetch_add(1, std::memory_order_relaxed);
}

std::chrono::microseconds scoped_task_guard::elapsed() const noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_);
}

}  // namespace pacs::bridge::performance
