#ifndef PACS_BRIDGE_PERFORMANCE_LOCKFREE_QUEUE_H
#define PACS_BRIDGE_PERFORMANCE_LOCKFREE_QUEUE_H

/**
 * @file lockfree_queue.h
 * @brief Lock-free queue for high-performance message passing
 *
 * Provides MPMC (Multi-Producer Multi-Consumer) lock-free queue
 * implementations integrated with thread_system from kcenon ecosystem.
 * Designed for low-latency message passing in hot paths.
 *
 * Key Features:
 *   - Lock-free operations using atomic CAS
 *   - Bounded and unbounded variants
 *   - Configurable backoff on contention
 *   - Wait-free try_push/try_pop operations
 *   - Batch operations for throughput
 *
 * @see docs/SRS.md NFR-1.1 (Throughput >= 500 msg/s)
 */

#include "pacs/bridge/performance/performance_types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// Queue Statistics
// =============================================================================

/**
 * @brief Statistics for queue monitoring
 */
struct queue_statistics {
    /** Total items pushed */
    std::atomic<uint64_t> total_pushed{0};

    /** Total items popped */
    std::atomic<uint64_t> total_popped{0};

    /** Push operations that failed (queue full) */
    std::atomic<uint64_t> push_failures{0};

    /** Pop operations that failed (queue empty) */
    std::atomic<uint64_t> pop_failures{0};

    /** Contention events (CAS retries) */
    std::atomic<uint64_t> contentions{0};

    /** Current queue depth */
    std::atomic<size_t> current_depth{0};

    /** Peak queue depth observed */
    std::atomic<size_t> peak_depth{0};

    /**
     * @brief Reset statistics
     */
    void reset() noexcept {
        total_pushed.store(0, std::memory_order_relaxed);
        total_popped.store(0, std::memory_order_relaxed);
        push_failures.store(0, std::memory_order_relaxed);
        pop_failures.store(0, std::memory_order_relaxed);
        contentions.store(0, std::memory_order_relaxed);
        // Note: current_depth is not reset as it reflects actual state
    }
};

// =============================================================================
// Lock-Free Queue Interface
// =============================================================================

/**
 * @brief MPMC lock-free queue
 *
 * Thread-safe queue using lock-free atomic operations for high-performance
 * message passing between producers and consumers.
 *
 * @tparam T Element type (should be move-constructible)
 *
 * Example usage:
 * @code
 *     lockfree_queue_config config;
 *     config.capacity = 4096;
 *
 *     lockfree_queue<hl7_message> queue(config);
 *
 *     // Producer
 *     queue.push(std::move(message));
 *
 *     // Consumer
 *     if (auto msg = queue.pop()) {
 *         process(*msg);
 *     }
 * @endcode
 */
template <typename T>
class lockfree_queue {
public:
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /**
     * @brief Construct queue with configuration
     * @param config Queue configuration
     */
    explicit lockfree_queue(const lockfree_queue_config& config = {});

    /** Destructor */
    ~lockfree_queue();

    // Non-copyable, non-movable
    lockfree_queue(const lockfree_queue&) = delete;
    lockfree_queue& operator=(const lockfree_queue&) = delete;
    lockfree_queue(lockfree_queue&&) = delete;
    lockfree_queue& operator=(lockfree_queue&&) = delete;

    // -------------------------------------------------------------------------
    // Push Operations
    // -------------------------------------------------------------------------

    /**
     * @brief Push an item to the queue (blocking if bounded and full)
     *
     * For bounded queues, blocks until space is available or timeout.
     * For unbounded queues, always succeeds (unless OOM).
     *
     * @param item Item to push
     * @param timeout Maximum time to wait (bounded only)
     * @return true if pushed, false on timeout/failure
     */
    bool push(T item,
              std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

    /**
     * @brief Try to push without blocking
     *
     * @param item Item to push
     * @return true if pushed, false if queue is full
     */
    [[nodiscard]] bool try_push(T item) noexcept;

    /**
     * @brief Push multiple items atomically (batch operation)
     *
     * @param items Items to push
     * @return Number of items successfully pushed
     */
    size_t push_batch(std::span<T> items);

    /**
     * @brief Emplace an item in the queue
     *
     * @tparam Args Constructor argument types
     * @param args Arguments forwarded to T constructor
     * @return true if emplaced, false if queue is full
     */
    template <typename... Args>
    bool emplace(Args&&... args);

    // -------------------------------------------------------------------------
    // Pop Operations
    // -------------------------------------------------------------------------

    /**
     * @brief Pop an item from the queue (blocking if empty)
     *
     * @param timeout Maximum time to wait
     * @return Item if available, nullopt on timeout
     */
    std::optional<T> pop(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

    /**
     * @brief Try to pop without blocking
     *
     * @return Item if available, nullopt if queue is empty
     */
    [[nodiscard]] std::optional<T> try_pop() noexcept;

    /**
     * @brief Pop multiple items (batch operation)
     *
     * @param max_items Maximum items to pop
     * @return Vector of popped items
     */
    [[nodiscard]] std::vector<T> pop_batch(size_t max_items);

    /**
     * @brief Pop all available items
     *
     * @return Vector of all items currently in queue
     */
    [[nodiscard]] std::vector<T> pop_all();

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /**
     * @brief Check if queue is empty
     *
     * Note: This is a snapshot and may change immediately after return.
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Check if queue is full (bounded queues only)
     */
    [[nodiscard]] bool full() const noexcept;

    /**
     * @brief Get current queue size (approximate)
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * @brief Get queue capacity
     */
    [[nodiscard]] size_t capacity() const noexcept;

    /**
     * @brief Get statistics
     */
    [[nodiscard]] const queue_statistics& statistics() const noexcept;

    /**
     * @brief Clear the queue
     */
    void clear();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// Priority Queue
// =============================================================================

/**
 * @brief Lock-free priority queue for prioritized message handling
 *
 * Supports multiple priority levels with lock-free operations per level.
 *
 * @tparam T Element type
 * @tparam Priorities Number of priority levels (default 4: high, normal, low, background)
 */
template <typename T, size_t Priorities = 4>
class priority_lockfree_queue {
public:
    // -------------------------------------------------------------------------
    // Types
    // -------------------------------------------------------------------------

    /** Priority levels (0 = highest) */
    enum class priority : uint8_t {
        high = 0,
        normal = 1,
        low = 2,
        background = 3
    };

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /**
     * @brief Construct with configuration per priority level
     * @param config Base configuration (applied to each level)
     */
    explicit priority_lockfree_queue(const lockfree_queue_config& config = {});

    /** Destructor */
    ~priority_lockfree_queue();

    // Non-copyable, non-movable
    priority_lockfree_queue(const priority_lockfree_queue&) = delete;
    priority_lockfree_queue& operator=(const priority_lockfree_queue&) = delete;

    // -------------------------------------------------------------------------
    // Push Operations
    // -------------------------------------------------------------------------

    /**
     * @brief Push with priority
     */
    bool push(T item, priority prio = priority::normal);

    /**
     * @brief Try to push without blocking
     */
    [[nodiscard]] bool try_push(T item, priority prio = priority::normal) noexcept;

    // -------------------------------------------------------------------------
    // Pop Operations
    // -------------------------------------------------------------------------

    /**
     * @brief Pop highest priority available item
     *
     * Checks queues from highest to lowest priority.
     */
    std::optional<T> pop(std::chrono::milliseconds timeout =
                             std::chrono::milliseconds::max());

    /**
     * @brief Try to pop without blocking
     */
    [[nodiscard]] std::optional<T> try_pop() noexcept;

    /**
     * @brief Pop from specific priority level
     */
    [[nodiscard]] std::optional<T> pop_priority(priority prio) noexcept;

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /**
     * @brief Check if all queues are empty
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get total size across all priority levels
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * @brief Get size of specific priority level
     */
    [[nodiscard]] size_t size(priority prio) const noexcept;

    /**
     * @brief Get statistics for specific priority level
     */
    [[nodiscard]] const queue_statistics& statistics(priority prio) const noexcept;

    /**
     * @brief Clear all queues
     */
    void clear();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// Work-Stealing Queue
// =============================================================================

/**
 * @brief Work-stealing deque for thread pool implementation
 *
 * Single-producer (owner thread) can push/pop from bottom.
 * Multiple consumers (steal threads) can steal from top.
 * Optimized for locality - owner works on recently added items.
 */
template <typename T>
class work_stealing_queue {
public:
    /**
     * @brief Construct with capacity
     * @param capacity Queue capacity (must be power of 2)
     */
    explicit work_stealing_queue(size_t capacity = 4096);

    /** Destructor */
    ~work_stealing_queue();

    // Non-copyable, non-movable
    work_stealing_queue(const work_stealing_queue&) = delete;
    work_stealing_queue& operator=(const work_stealing_queue&) = delete;

    // -------------------------------------------------------------------------
    // Owner Operations (single producer)
    // -------------------------------------------------------------------------

    /**
     * @brief Push item to bottom (owner only)
     */
    void push(T item);

    /**
     * @brief Pop item from bottom (owner only)
     * @return Item if available
     */
    [[nodiscard]] std::optional<T> pop() noexcept;

    // -------------------------------------------------------------------------
    // Stealer Operations (multiple consumers)
    // -------------------------------------------------------------------------

    /**
     * @brief Steal item from top (any thread)
     * @return Item if available
     */
    [[nodiscard]] std::optional<T> steal() noexcept;

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /**
     * @brief Check if queue is empty
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get approximate size
     */
    [[nodiscard]] size_t size() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace pacs::bridge::performance

#endif  // PACS_BRIDGE_PERFORMANCE_LOCKFREE_QUEUE_H
