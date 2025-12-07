/**
 * @file lockfree_queue.cpp
 * @brief Implementation of lock-free queue for high-performance message passing
 */

#include "pacs/bridge/performance/lockfree_queue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// Utility: Power of 2 rounding
// =============================================================================

namespace {

constexpr size_t next_power_of_2(size_t n) {
    if (n == 0) return 1;
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

// Backoff strategy for contention
class exponential_backoff {
public:
    explicit exponential_backoff(size_t initial_spins = 32, size_t max_spins = 4096)
        : spins_(initial_spins), max_spins_(max_spins) {}

    void backoff() {
        for (size_t i = 0; i < spins_; ++i) {
            // Pause instruction (reduces power consumption during spin)
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#elif defined(__aarch64__)
            asm volatile("yield");
#endif
        }
        spins_ = std::min(spins_ * 2, max_spins_);
    }

    void reset() { spins_ = 32; }

private:
    size_t spins_;
    size_t max_spins_;
};

}  // namespace

// =============================================================================
// Work-Stealing Queue Implementation
// =============================================================================

template <typename T>
struct work_stealing_queue<T>::impl {
    static constexpr size_t CACHE_LINE_SIZE = 64;

    struct alignas(CACHE_LINE_SIZE) padded_atomic_size {
        std::atomic<int64_t> value{0};
    };

    std::vector<std::atomic<T*>> buffer;
    size_t capacity_mask;
    padded_atomic_size top;
    padded_atomic_size bottom;

    explicit impl(size_t cap)
        : buffer(next_power_of_2(cap)),
          capacity_mask(next_power_of_2(cap) - 1) {
        for (auto& slot : buffer) {
            slot.store(nullptr, std::memory_order_relaxed);
        }
    }

    ~impl() {
        // Clean up any remaining items
        // try_pop() already handles pointer deletion internally
        while (try_pop()) {
            // Just discard the returned value
        }
    }

    void push(T item) {
        int64_t b = bottom.value.load(std::memory_order_relaxed);
        buffer[b & capacity_mask].store(new T(std::move(item)),
                                        std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        bottom.value.store(b + 1, std::memory_order_relaxed);
    }

    std::optional<T> try_pop() {
        int64_t b = bottom.value.load(std::memory_order_relaxed) - 1;
        bottom.value.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = top.value.load(std::memory_order_relaxed);

        if (t <= b) {
            // Non-empty queue
            T* item = buffer[b & capacity_mask].load(std::memory_order_relaxed);
            if (t == b) {
                // Last item - potential race with steal
                if (!top.value.compare_exchange_strong(
                        t, t + 1, std::memory_order_seq_cst,
                        std::memory_order_relaxed)) {
                    // Lost race
                    bottom.value.store(b + 1, std::memory_order_relaxed);
                    return std::nullopt;
                }
                bottom.value.store(b + 1, std::memory_order_relaxed);
            }
            T result = std::move(*item);
            delete item;
            return result;
        } else {
            // Empty queue
            bottom.value.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }
    }

    std::optional<T> try_steal() {
        int64_t t = top.value.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t b = bottom.value.load(std::memory_order_acquire);

        if (t < b) {
            T* item = buffer[t & capacity_mask].load(std::memory_order_relaxed);
            if (!top.value.compare_exchange_strong(
                    t, t + 1, std::memory_order_seq_cst,
                    std::memory_order_relaxed)) {
                // Lost race
                return std::nullopt;
            }
            T result = std::move(*item);
            delete item;
            return result;
        }
        return std::nullopt;
    }

    bool is_empty() const {
        int64_t b = bottom.value.load(std::memory_order_relaxed);
        int64_t t = top.value.load(std::memory_order_relaxed);
        return b <= t;
    }

    size_t approx_size() const {
        int64_t b = bottom.value.load(std::memory_order_relaxed);
        int64_t t = top.value.load(std::memory_order_relaxed);
        return b > t ? static_cast<size_t>(b - t) : 0;
    }
};

template <typename T>
work_stealing_queue<T>::work_stealing_queue(size_t capacity)
    : impl_(std::make_unique<impl>(capacity)) {}

template <typename T>
work_stealing_queue<T>::~work_stealing_queue() = default;

template <typename T>
void work_stealing_queue<T>::push(T item) {
    impl_->push(std::move(item));
}

template <typename T>
std::optional<T> work_stealing_queue<T>::pop() noexcept {
    return impl_->try_pop();
}

template <typename T>
std::optional<T> work_stealing_queue<T>::steal() noexcept {
    return impl_->try_steal();
}

template <typename T>
bool work_stealing_queue<T>::empty() const noexcept {
    return impl_->is_empty();
}

template <typename T>
size_t work_stealing_queue<T>::size() const noexcept {
    return impl_->approx_size();
}

// Explicit instantiations for common types
template class work_stealing_queue<std::function<void()>>;

}  // namespace pacs::bridge::performance
