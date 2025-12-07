/**
 * @file object_pool.cpp
 * @brief Implementation of object pooling for memory optimization
 */

#include "pacs/bridge/performance/object_pool.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <shared_mutex>

namespace pacs::bridge::performance {

// =============================================================================
// Message Buffer Pool Implementation
// =============================================================================

struct message_buffer_pool::impl {
    memory_config config;
    pool_statistics stats;
    mutable std::shared_mutex mutex;

    // Buffer pools organized by size class
    // Size classes: 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
    static constexpr size_t SIZE_CLASSES[] = {
        256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
    static constexpr size_t NUM_SIZE_CLASSES = 9;

    struct buffer_entry {
        std::unique_ptr<uint8_t[]> data;
        size_t capacity;
    };

    std::deque<buffer_entry> pools[NUM_SIZE_CLASSES];
    std::atomic<size_t> total_memory{0};
    std::atomic<uint32_t> next_pool_id{1};

    explicit impl(const memory_config& cfg) : config(cfg) {
        // Pre-allocate buffers
        preallocate();
    }

    void preallocate() {
        // Pre-allocate default size buffers
        size_t default_class = find_size_class(config.default_buffer_size);
        size_t alloc_size = SIZE_CLASSES[default_class];

        for (size_t i = 0; i < config.message_buffer_pool_size; ++i) {
            buffer_entry entry;
            entry.data = std::make_unique<uint8_t[]>(alloc_size);
            entry.capacity = alloc_size;
            pools[default_class].push_back(std::move(entry));
            total_memory.fetch_add(alloc_size, std::memory_order_relaxed);
            stats.total_created.fetch_add(1, std::memory_order_relaxed);
        }
        stats.current_size.store(config.message_buffer_pool_size,
                                 std::memory_order_relaxed);
    }

    static size_t find_size_class(size_t requested_size) {
        for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
            if (SIZE_CLASSES[i] >= requested_size) {
                return i;
            }
        }
        return NUM_SIZE_CLASSES - 1;  // Return largest
    }

    std::expected<buffer_handle, performance_error> acquire(size_t min_size) {
        size_t size_class = find_size_class(min_size);
        size_t alloc_size = SIZE_CLASSES[size_class];

        // Check memory limit
        if (config.max_memory_bytes > 0 &&
            total_memory.load(std::memory_order_relaxed) + alloc_size >
                config.max_memory_bytes) {
            return std::unexpected(performance_error::memory_limit_exceeded);
        }

        stats.total_acquires.fetch_add(1, std::memory_order_relaxed);

        buffer_handle handle;
        handle.pool_id = next_pool_id.fetch_add(1, std::memory_order_relaxed);

        {
            std::unique_lock lock(mutex);

            // Try to get from pool
            if (!pools[size_class].empty()) {
                auto& entry = pools[size_class].back();
                handle.data = entry.data.release();
                handle.capacity = entry.capacity;
                pools[size_class].pop_back();

                stats.cache_hits.fetch_add(1, std::memory_order_relaxed);
            } else {
                // Allocate new buffer
                handle.data = new uint8_t[alloc_size];
                handle.capacity = alloc_size;
                total_memory.fetch_add(alloc_size, std::memory_order_relaxed);
                stats.total_created.fetch_add(1, std::memory_order_relaxed);
                stats.cache_misses.fetch_add(1, std::memory_order_relaxed);
            }
        }

        handle.size = 0;
        stats.objects_in_use.fetch_add(1, std::memory_order_relaxed);
        size_t current = stats.objects_in_use.load(std::memory_order_relaxed);
        size_t peak = stats.peak_in_use.load(std::memory_order_relaxed);
        while (current > peak &&
               !stats.peak_in_use.compare_exchange_weak(
                   peak, current, std::memory_order_relaxed)) {
        }

        return handle;
    }

    void release(buffer_handle& handle) {
        if (!handle.data) return;

        stats.total_releases.fetch_add(1, std::memory_order_relaxed);
        stats.objects_in_use.fetch_sub(1, std::memory_order_relaxed);

        size_t size_class = find_size_class(handle.capacity);

        {
            std::unique_lock lock(mutex);

            buffer_entry entry;
            entry.data.reset(handle.data);
            entry.capacity = handle.capacity;
            pools[size_class].push_back(std::move(entry));
        }

        handle.data = nullptr;
        handle.capacity = 0;
        handle.size = 0;
    }

    size_t memory_usage() const {
        return total_memory.load(std::memory_order_relaxed);
    }
};

message_buffer_pool::message_buffer_pool(const memory_config& config)
    : impl_(std::make_unique<impl>(config)) {}

message_buffer_pool::~message_buffer_pool() = default;

std::expected<message_buffer_pool::buffer_handle, performance_error>
message_buffer_pool::acquire(size_t min_size) {
    return impl_->acquire(min_size);
}

void message_buffer_pool::release(buffer_handle& buffer) noexcept {
    impl_->release(buffer);
}

const pool_statistics& message_buffer_pool::statistics() const noexcept {
    return impl_->stats;
}

size_t message_buffer_pool::memory_usage() const noexcept {
    return impl_->memory_usage();
}

void message_buffer_pool::buffer_handle::clear() noexcept {
    if (data && capacity > 0) {
        std::memset(data, 0, capacity);
    }
    size = 0;
}

// =============================================================================
// Scoped Buffer Implementation
// =============================================================================

scoped_buffer::scoped_buffer(message_buffer_pool& pool,
                             message_buffer_pool::buffer_handle handle)
    : pool_(&pool), handle_(std::move(handle)) {}

scoped_buffer::scoped_buffer(scoped_buffer&& other) noexcept
    : pool_(other.pool_), handle_(std::move(other.handle_)) {
    other.pool_ = nullptr;
    other.handle_ = {};
}

scoped_buffer& scoped_buffer::operator=(scoped_buffer&& other) noexcept {
    if (this != &other) {
        if (pool_ && handle_.valid()) {
            pool_->release(handle_);
        }
        pool_ = other.pool_;
        handle_ = std::move(other.handle_);
        other.pool_ = nullptr;
        other.handle_ = {};
    }
    return *this;
}

scoped_buffer::~scoped_buffer() {
    if (pool_ && handle_.valid()) {
        pool_->release(handle_);
    }
}

uint8_t* scoped_buffer::data() noexcept {
    return handle_.data;
}

const uint8_t* scoped_buffer::data() const noexcept {
    return handle_.data;
}

size_t scoped_buffer::capacity() const noexcept {
    return handle_.capacity;
}

size_t scoped_buffer::size() const noexcept {
    return handle_.size;
}

void scoped_buffer::set_size(size_t new_size) noexcept {
    if (new_size <= handle_.capacity) {
        handle_.size = new_size;
    }
}

bool scoped_buffer::valid() const noexcept {
    return handle_.valid();
}

message_buffer_pool::buffer_handle scoped_buffer::release() noexcept {
    pool_ = nullptr;
    auto h = std::move(handle_);
    handle_ = {};
    return h;
}

}  // namespace pacs::bridge::performance
