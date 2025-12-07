#ifndef PACS_BRIDGE_PERFORMANCE_OBJECT_POOL_H
#define PACS_BRIDGE_PERFORMANCE_OBJECT_POOL_H

/**
 * @file object_pool.h
 * @brief Object pooling for memory optimization
 *
 * Provides thread-safe object pools to reduce allocation overhead.
 * Pre-allocates objects and recycles them for reuse, significantly
 * reducing GC pressure and allocation latency in hot paths.
 *
 * Key Features:
 *   - Pre-allocation of objects during initialization
 *   - Automatic pool growth when exhausted
 *   - Thread-safe acquire/release operations
 *   - Pool shrinking when usage is low
 *   - Statistics tracking for optimization
 *
 * @see docs/SRS.md NFR-1.5 (Memory < 200MB)
 */

#include "pacs/bridge/performance/performance_types.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>

namespace pacs::bridge::performance {

// =============================================================================
// Pool Statistics
// =============================================================================

/**
 * @brief Statistics for object pool monitoring
 */
struct pool_statistics {
    /** Total objects created */
    std::atomic<uint64_t> total_created{0};

    /** Total acquire calls */
    std::atomic<uint64_t> total_acquires{0};

    /** Total release calls */
    std::atomic<uint64_t> total_releases{0};

    /** Cache hits (object reused from pool) */
    std::atomic<uint64_t> cache_hits{0};

    /** Cache misses (new object created) */
    std::atomic<uint64_t> cache_misses{0};

    /** Current pool size */
    std::atomic<size_t> current_size{0};

    /** Current objects in use */
    std::atomic<size_t> objects_in_use{0};

    /** Peak objects in use */
    std::atomic<size_t> peak_in_use{0};

    /**
     * @brief Calculate hit rate
     * @return Hit rate as percentage (0-100)
     */
    [[nodiscard]] double hit_rate() const noexcept {
        uint64_t hits = cache_hits.load(std::memory_order_relaxed);
        uint64_t total = total_acquires.load(std::memory_order_relaxed);
        if (total == 0) return 0.0;
        return (static_cast<double>(hits) / static_cast<double>(total)) * 100.0;
    }

    /**
     * @brief Reset statistics
     */
    void reset() noexcept {
        total_created.store(0, std::memory_order_relaxed);
        total_acquires.store(0, std::memory_order_relaxed);
        total_releases.store(0, std::memory_order_relaxed);
        cache_hits.store(0, std::memory_order_relaxed);
        cache_misses.store(0, std::memory_order_relaxed);
    }
};

// =============================================================================
// Object Pool Template
// =============================================================================

/**
 * @brief Thread-safe object pool for type T
 *
 * Manages a pool of pre-allocated objects that can be acquired and
 * released without dynamic allocation overhead.
 *
 * @tparam T Object type to pool (must be default constructible)
 *
 * Example usage:
 * @code
 *     object_pool_config config;
 *     config.initial_size = 128;
 *     config.max_size = 1024;
 *
 *     object_pool<hl7_message> pool(config);
 *
 *     // Acquire object from pool
 *     auto obj = pool.acquire();
 *     if (obj) {
 *         // Use the object
 *         obj->parse(data);
 *
 *         // Object automatically returned to pool when out of scope
 *     }
 * @endcode
 */
template <typename T>
class object_pool {
public:
    // -------------------------------------------------------------------------
    // Types
    // -------------------------------------------------------------------------

    /** Unique pointer with custom deleter that returns object to pool */
    using pooled_ptr = std::unique_ptr<T, std::function<void(T*)>>;

    /** Factory function for creating new objects */
    using factory_fn = std::function<std::unique_ptr<T>()>;

    /** Reset function called when object is released back to pool */
    using reset_fn = std::function<void(T&)>;

    // -------------------------------------------------------------------------
    // Construction / Destruction
    // -------------------------------------------------------------------------

    /**
     * @brief Construct object pool with configuration
     * @param config Pool configuration
     * @param factory Optional custom factory function
     * @param reset Optional reset function for reused objects
     */
    explicit object_pool(
        const object_pool_config& config = {},
        factory_fn factory = nullptr,
        reset_fn reset = nullptr);

    /** Destructor */
    ~object_pool();

    // Non-copyable, non-movable (due to thread-safety requirements)
    object_pool(const object_pool&) = delete;
    object_pool& operator=(const object_pool&) = delete;
    object_pool(object_pool&&) = delete;
    object_pool& operator=(object_pool&&) = delete;

    // -------------------------------------------------------------------------
    // Pool Operations
    // -------------------------------------------------------------------------

    /**
     * @brief Acquire an object from the pool
     *
     * Returns a pre-allocated object if available, otherwise creates
     * a new one (unless pool is at max capacity).
     *
     * @return Pooled pointer to object, or error if pool exhausted
     */
    [[nodiscard]] std::expected<pooled_ptr, performance_error> acquire();

    /**
     * @brief Try to acquire without blocking
     *
     * Non-blocking version that returns immediately if no object available.
     *
     * @return Pooled pointer if available, nullopt otherwise
     */
    [[nodiscard]] std::optional<pooled_ptr> try_acquire() noexcept;

    /**
     * @brief Release an object back to the pool manually
     *
     * Normally objects are returned automatically via the pooled_ptr
     * deleter, but this allows manual release if needed.
     *
     * @param obj Object to return to pool
     */
    void release(T* obj) noexcept;

    /**
     * @brief Pre-warm the pool by creating objects up to initial_size
     *
     * @return Number of objects created
     */
    size_t prewarm();

    /**
     * @brief Shrink the pool by releasing excess idle objects
     *
     * @param target_size Target pool size after shrinking
     * @return Number of objects released
     */
    size_t shrink(size_t target_size = 0);

    /**
     * @brief Clear all objects from the pool
     */
    void clear();

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /**
     * @brief Get current number of available objects in pool
     */
    [[nodiscard]] size_t available() const noexcept;

    /**
     * @brief Get current number of objects in use
     */
    [[nodiscard]] size_t in_use() const noexcept;

    /**
     * @brief Get total pool capacity (available + in_use)
     */
    [[nodiscard]] size_t capacity() const noexcept;

    /**
     * @brief Check if pool is empty (no available objects)
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get pool statistics
     */
    [[nodiscard]] const pool_statistics& statistics() const noexcept;

    /**
     * @brief Get configuration
     */
    [[nodiscard]] const object_pool_config& config() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// Message Buffer Pool
// =============================================================================

/**
 * @brief Specialized pool for message buffers
 *
 * Optimized for HL7 message processing with pre-sized buffers.
 */
class message_buffer_pool {
public:
    /**
     * @brief Buffer handle returned by acquire
     */
    struct buffer_handle {
        /** Pointer to buffer data */
        uint8_t* data = nullptr;

        /** Buffer capacity */
        size_t capacity = 0;

        /** Current data size */
        size_t size = 0;

        /** Pool identifier for return */
        uint32_t pool_id = 0;

        /** Check if buffer is valid */
        [[nodiscard]] bool valid() const noexcept { return data != nullptr; }

        /** Reset size without clearing data */
        void reset() noexcept { size = 0; }

        /** Clear buffer data */
        void clear() noexcept;
    };

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /**
     * @brief Construct buffer pool
     * @param config Memory configuration
     */
    explicit message_buffer_pool(const memory_config& config = {});

    /** Destructor */
    ~message_buffer_pool();

    // Non-copyable, non-movable
    message_buffer_pool(const message_buffer_pool&) = delete;
    message_buffer_pool& operator=(const message_buffer_pool&) = delete;

    // -------------------------------------------------------------------------
    // Buffer Operations
    // -------------------------------------------------------------------------

    /**
     * @brief Acquire a buffer of at least the specified size
     *
     * @param min_size Minimum buffer size required
     * @return Buffer handle, or error if allocation fails
     */
    [[nodiscard]] std::expected<buffer_handle, performance_error>
    acquire(size_t min_size);

    /**
     * @brief Release a buffer back to the pool
     *
     * @param buffer Buffer handle to release
     */
    void release(buffer_handle& buffer) noexcept;

    /**
     * @brief Get statistics
     */
    [[nodiscard]] const pool_statistics& statistics() const noexcept;

    /**
     * @brief Get current memory usage in bytes
     */
    [[nodiscard]] size_t memory_usage() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// =============================================================================
// RAII Buffer Wrapper
// =============================================================================

/**
 * @brief RAII wrapper for message buffer handles
 *
 * Automatically releases buffer back to pool when destroyed.
 */
class scoped_buffer {
public:
    /**
     * @brief Construct with buffer from pool
     */
    scoped_buffer(message_buffer_pool& pool,
                  message_buffer_pool::buffer_handle handle);

    /** Move constructor */
    scoped_buffer(scoped_buffer&& other) noexcept;

    /** Move assignment */
    scoped_buffer& operator=(scoped_buffer&& other) noexcept;

    /** Destructor - returns buffer to pool */
    ~scoped_buffer();

    // Non-copyable
    scoped_buffer(const scoped_buffer&) = delete;
    scoped_buffer& operator=(const scoped_buffer&) = delete;

    /**
     * @brief Access buffer data
     */
    [[nodiscard]] uint8_t* data() noexcept;
    [[nodiscard]] const uint8_t* data() const noexcept;

    /**
     * @brief Get buffer capacity
     */
    [[nodiscard]] size_t capacity() const noexcept;

    /**
     * @brief Get current data size
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * @brief Set current data size
     */
    void set_size(size_t new_size) noexcept;

    /**
     * @brief Check if buffer is valid
     */
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Release ownership and return raw handle
     */
    [[nodiscard]] message_buffer_pool::buffer_handle release() noexcept;

private:
    message_buffer_pool* pool_;
    message_buffer_pool::buffer_handle handle_;
};

}  // namespace pacs::bridge::performance

#endif  // PACS_BRIDGE_PERFORMANCE_OBJECT_POOL_H
