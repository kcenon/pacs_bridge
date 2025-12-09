#ifndef PACS_BRIDGE_INTEGRATION_THREAD_ADAPTER_H
#define PACS_BRIDGE_INTEGRATION_THREAD_ADAPTER_H

/**
 * @file thread_adapter.h
 * @brief Integration Module - Thread system adapter
 *
 * Provides worker pools for async processing.
 *
 * @see docs/SDS_COMPONENTS.md - Section 8: Integration Module (DES-INT-002)
 */

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace pacs::bridge::integration {

/**
 * @brief Task priority levels
 */
enum class task_priority {
    low,
    normal,
    high,
    critical
};

/**
 * @brief Worker pool configuration
 */
struct worker_pool_config {
    std::string name = "worker_pool";
    size_t min_threads = 2;
    size_t max_threads = 8;
    std::chrono::seconds idle_timeout{60};
    size_t queue_size = 1000;
};

/**
 * @brief Thread adapter interface
 *
 * Provides worker pools for async processing.
 * Wraps thread_system for task scheduling.
 */
class thread_adapter {
public:
    virtual ~thread_adapter() = default;

    /**
     * @brief Initialize the thread pool
     * @param config Pool configuration
     * @return true if initialized successfully
     */
    [[nodiscard]] virtual bool initialize(const worker_pool_config& config) = 0;

    /**
     * @brief Shutdown the thread pool
     * @param wait_for_completion Wait for queued tasks to complete
     */
    virtual void shutdown(bool wait_for_completion = true) = 0;

    /**
     * @brief Submit a task for execution
     * @param task Task function to execute
     * @param priority Task priority
     * @return Future for task result
     */
    template <typename F>
    [[nodiscard]] auto submit(F&& task, task_priority priority = task_priority::normal)
        -> std::future<decltype(task())>;

    /**
     * @brief Get current queue size
     */
    [[nodiscard]] virtual size_t queue_size() const noexcept = 0;

    /**
     * @brief Get active thread count
     */
    [[nodiscard]] virtual size_t active_threads() const noexcept = 0;

    /**
     * @brief Check if pool is running
     */
    [[nodiscard]] virtual bool is_running() const noexcept = 0;

protected:
    /**
     * @brief Internal task submission
     */
    virtual void submit_internal(std::function<void()> task,
                                  task_priority priority) = 0;
};

/**
 * @brief Create a thread adapter instance
 * @return Thread adapter implementation
 */
[[nodiscard]] std::unique_ptr<thread_adapter> create_thread_adapter();

} // namespace pacs::bridge::integration

#endif // PACS_BRIDGE_INTEGRATION_THREAD_ADAPTER_H
