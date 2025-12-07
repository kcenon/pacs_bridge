#ifndef PACS_BRIDGE_PERFORMANCE_THREAD_POOL_MANAGER_H
#define PACS_BRIDGE_PERFORMANCE_THREAD_POOL_MANAGER_H

/**
 * @file thread_pool_manager.h
 * @brief Thread pool management integrating with thread_system
 *
 * Provides thread pool configuration and management for optimal performance.
 * Integrates with the kcenon thread_system for work-stealing, job scheduling,
 * and dynamic thread scaling.
 *
 * Key Features:
 *   - Work-stealing scheduler for load balancing
 *   - Dynamic thread scaling based on load
 *   - Priority-based task scheduling
 *   - CPU affinity for cache optimization
 *   - Comprehensive statistics tracking
 *
 * @see docs/SRS.md NFR-1.4 (Concurrent connections >= 50)
 */

#include "pacs/bridge/performance/performance_types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// Thread Pool Statistics
// =============================================================================

/**
 * @brief Thread pool statistics
 */
struct thread_pool_statistics {
    /** Current number of active threads */
    std::atomic<size_t> active_threads{0};

    /** Total threads in pool */
    std::atomic<size_t> total_threads{0};

    /** Tasks queued waiting for execution */
    std::atomic<size_t> queued_tasks{0};

    /** Peak queued tasks */
    std::atomic<size_t> peak_queued{0};

    /** Total tasks submitted */
    std::atomic<uint64_t> total_submitted{0};

    /** Total tasks completed */
    std::atomic<uint64_t> total_completed{0};

    /** Total tasks rejected (queue full) */
    std::atomic<uint64_t> total_rejected{0};

    /** Total work stolen by other threads */
    std::atomic<uint64_t> work_stolen{0};

    /** Average task duration in microseconds */
    std::atomic<uint64_t> avg_task_duration_us{0};

    /** Peak task duration in microseconds */
    std::atomic<uint64_t> peak_task_duration_us{0};

    /**
     * @brief Get thread utilization percentage
     */
    [[nodiscard]] double utilization() const noexcept {
        size_t total = total_threads.load(std::memory_order_relaxed);
        if (total == 0) return 0.0;
        size_t active = active_threads.load(std::memory_order_relaxed);
        return (static_cast<double>(active) / static_cast<double>(total)) * 100.0;
    }

    /**
     * @brief Get completion rate
     */
    [[nodiscard]] double completion_rate() const noexcept {
        uint64_t submitted = total_submitted.load(std::memory_order_relaxed);
        if (submitted == 0) return 0.0;
        uint64_t completed = total_completed.load(std::memory_order_relaxed);
        return (static_cast<double>(completed) / static_cast<double>(submitted)) *
               100.0;
    }

    /**
     * @brief Reset statistics
     */
    void reset() noexcept {
        total_submitted.store(0, std::memory_order_relaxed);
        total_completed.store(0, std::memory_order_relaxed);
        total_rejected.store(0, std::memory_order_relaxed);
        work_stolen.store(0, std::memory_order_relaxed);
        peak_queued.store(0, std::memory_order_relaxed);
        avg_task_duration_us.store(0, std::memory_order_relaxed);
        peak_task_duration_us.store(0, std::memory_order_relaxed);
    }
};

// =============================================================================
// Task Priority
// =============================================================================

/**
 * @brief Task priority levels
 */
enum class task_priority : uint8_t {
    /** Critical tasks - immediate execution */
    critical = 0,

    /** High priority - ACK responses, health checks */
    high = 1,

    /** Normal priority - regular message processing */
    normal = 2,

    /** Low priority - background tasks */
    low = 3,

    /** Background - maintenance, cleanup */
    background = 4
};

// =============================================================================
// Thread Pool Manager
// =============================================================================

/**
 * @brief Thread pool manager for PACS Bridge
 *
 * Manages worker threads for message processing with integration to
 * thread_system's work-stealing scheduler.
 *
 * Example usage:
 * @code
 *     thread_pool_config config;
 *     config.min_threads = 4;
 *     config.enable_work_stealing = true;
 *
 *     thread_pool_manager pool(config);
 *     if (auto result = pool.start(); !result) {
 *         handle_error(result.error());
 *         return;
 *     }
 *
 *     // Submit task
 *     auto future = pool.submit([](hl7_message& msg) {
 *         return process_message(msg);
 *     }, task_priority::high);
 *
 *     // Wait for result
 *     auto result = future.get();
 * @endcode
 */
class thread_pool_manager {
public:
    // -------------------------------------------------------------------------
    // Types
    // -------------------------------------------------------------------------

    /** Task function type */
    using task_fn = std::function<void()>;

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /**
     * @brief Construct thread pool manager
     * @param config Thread pool configuration
     */
    explicit thread_pool_manager(const thread_pool_config& config = {});

    /** Destructor */
    ~thread_pool_manager();

    // Non-copyable, non-movable
    thread_pool_manager(const thread_pool_manager&) = delete;
    thread_pool_manager& operator=(const thread_pool_manager&) = delete;
    thread_pool_manager(thread_pool_manager&&) = delete;
    thread_pool_manager& operator=(thread_pool_manager&&) = delete;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Start the thread pool
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, performance_error> start();

    /**
     * @brief Stop the thread pool
     *
     * @param wait_for_tasks Wait for pending tasks to complete
     * @param timeout Maximum time to wait
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, performance_error>
    stop(bool wait_for_tasks = true,
         std::chrono::milliseconds timeout = std::chrono::seconds{30});

    /**
     * @brief Check if pool is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // -------------------------------------------------------------------------
    // Task Submission
    // -------------------------------------------------------------------------

    /**
     * @brief Submit a task for execution
     *
     * @tparam F Callable type
     * @tparam Args Argument types
     * @param f Callable to execute
     * @param args Arguments to pass
     * @return Future for the result
     */
    template <typename F, typename... Args>
    [[nodiscard]] auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /**
     * @brief Submit a task with priority
     *
     * @tparam F Callable type
     * @tparam Args Argument types
     * @param priority Task priority
     * @param f Callable to execute
     * @param args Arguments to pass
     * @return Future for the result
     */
    template <typename F, typename... Args>
    [[nodiscard]] auto submit_priority(task_priority priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /**
     * @brief Submit a task without waiting for result
     *
     * @param task Task to execute
     * @param priority Task priority
     * @return true if submitted, false if rejected
     */
    bool post(task_fn task, task_priority priority = task_priority::normal);

    /**
     * @brief Try to submit without blocking
     *
     * @param task Task to execute
     * @param priority Task priority
     * @return true if submitted, false if queue full
     */
    [[nodiscard]] bool try_post(task_fn task,
                                task_priority priority = task_priority::normal) noexcept;

    /**
     * @brief Submit batch of tasks
     *
     * @param tasks Tasks to execute
     * @param priority Task priority
     * @return Number of tasks submitted
     */
    size_t post_batch(std::span<task_fn> tasks,
                      task_priority priority = task_priority::normal);

    // -------------------------------------------------------------------------
    // Thread Management
    // -------------------------------------------------------------------------

    /**
     * @brief Scale thread count
     *
     * @param thread_count Target thread count
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, performance_error> scale(size_t thread_count);

    /**
     * @brief Get current thread count
     */
    [[nodiscard]] size_t thread_count() const noexcept;

    /**
     * @brief Get minimum thread count
     */
    [[nodiscard]] size_t min_threads() const noexcept;

    /**
     * @brief Get maximum thread count
     */
    [[nodiscard]] size_t max_threads() const noexcept;

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /**
     * @brief Get pending task count
     */
    [[nodiscard]] size_t pending_tasks() const noexcept;

    /**
     * @brief Get active task count
     */
    [[nodiscard]] size_t active_tasks() const noexcept;

    /**
     * @brief Get statistics
     */
    [[nodiscard]] const thread_pool_statistics& statistics() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    /**
     * @brief Get configuration
     */
    [[nodiscard]] const thread_pool_config& config() const noexcept;

    // -------------------------------------------------------------------------
    // Global Instance
    // -------------------------------------------------------------------------

    /**
     * @brief Get global thread pool instance
     *
     * Creates with default configuration if not exists.
     */
    [[nodiscard]] static thread_pool_manager& instance();

    /**
     * @brief Initialize global instance with custom configuration
     *
     * Must be called before instance() if custom config is needed.
     */
    static void initialize(const thread_pool_config& config);

    /**
     * @brief Shutdown global instance
     */
    static void shutdown();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// Scoped Task Guard
// =============================================================================

/**
 * @brief RAII guard for task execution tracking
 */
class scoped_task_guard {
public:
    /**
     * @brief Start tracking task execution
     */
    explicit scoped_task_guard(thread_pool_statistics& stats);

    /** Destructor - records task completion */
    ~scoped_task_guard();

    // Non-copyable, non-movable
    scoped_task_guard(const scoped_task_guard&) = delete;
    scoped_task_guard& operator=(const scoped_task_guard&) = delete;

    /**
     * @brief Get task duration so far
     */
    [[nodiscard]] std::chrono::microseconds elapsed() const noexcept;

private:
    thread_pool_statistics& stats_;
    std::chrono::steady_clock::time_point start_;
};

// =============================================================================
// Template Implementations
// =============================================================================

template <typename F, typename... Args>
auto thread_pool_manager::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    return submit_priority(task_priority::normal, std::forward<F>(f),
                           std::forward<Args>(args)...);
}

template <typename F, typename... Args>
auto thread_pool_manager::submit_priority(task_priority priority, F&& f,
                                          Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    auto future = task->get_future();

    post([task = std::move(task)]() { (*task)(); }, priority);

    return future;
}

}  // namespace pacs::bridge::performance

#endif  // PACS_BRIDGE_PERFORMANCE_THREAD_POOL_MANAGER_H
