/**
 * @file exporter_factory.cpp
 * @brief Implementation of trace exporter factory
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/208
 */

#include "pacs/bridge/tracing/exporter_factory.h"

#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include "pacs/bridge/integration/executor_adapter.h"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

namespace pacs::bridge::tracing {

// =============================================================================
// No-Op Exporter (for disabled tracing)
// =============================================================================

namespace {

class noop_exporter : public trace_exporter {
public:
    std::expected<void, exporter_error> export_spans(
        const std::vector<span_data>& /*spans*/) override {
        return {};
    }

    std::expected<void, exporter_error> flush(
        std::chrono::milliseconds /*timeout*/) override {
        return {};
    }

    void shutdown() override {}

    bool is_healthy() const noexcept override { return true; }

    std::string name() const override { return "noop"; }
};

// =============================================================================
// Console Exporter (for debugging)
// =============================================================================

class console_exporter : public trace_exporter {
public:
    std::expected<void, exporter_error> export_spans(
        const std::vector<span_data>& spans) override {
        // In a real implementation, this would format and print spans
        // For now, just count them
        spans_exported_ += spans.size();
        return {};
    }

    std::expected<void, exporter_error> flush(
        std::chrono::milliseconds /*timeout*/) override {
        return {};
    }

    void shutdown() override {}

    bool is_healthy() const noexcept override { return true; }

    std::string name() const override { return "console"; }

private:
    std::atomic<size_t> spans_exported_{0};
};

// =============================================================================
// HTTP Exporter Base (for Jaeger/Zipkin/OTLP HTTP)
// =============================================================================

class http_exporter : public trace_exporter {
public:
    explicit http_exporter(std::string endpoint, std::string service_name)
        : endpoint_(std::move(endpoint))
        , service_name_(std::move(service_name)) {}

    std::expected<void, exporter_error> export_spans(
        const std::vector<span_data>& spans) override {
        if (spans.empty()) {
            return {};
        }

        // In a real implementation, this would:
        // 1. Serialize spans to the appropriate format (Thrift, JSON, etc.)
        // 2. Send HTTP request to the endpoint
        // 3. Handle response and errors

        // For standalone builds, we just simulate success
        ++export_count_;
        spans_exported_ += spans.size();

        return {};
    }

    std::expected<void, exporter_error> flush(
        std::chrono::milliseconds /*timeout*/) override {
        return {};
    }

    void shutdown() override {
        healthy_ = false;
    }

    bool is_healthy() const noexcept override { return healthy_; }

    std::string name() const override { return "http:" + endpoint_; }

protected:
    std::string endpoint_;
    std::string service_name_;
    std::atomic<bool> healthy_{true};
    std::atomic<size_t> export_count_{0};
    std::atomic<size_t> spans_exported_{0};
};

// =============================================================================
// Factory Registry
// =============================================================================

std::unordered_map<trace_export_format, exporter_factory::factory_function>&
get_factory_registry() {
    static std::unordered_map<trace_export_format, exporter_factory::factory_function>
        registry;
    return registry;
}

std::mutex& get_registry_mutex() {
    static std::mutex mtx;
    return mtx;
}

}  // namespace

// =============================================================================
// Exporter Factory Implementation
// =============================================================================

std::expected<std::unique_ptr<trace_exporter>, exporter_error>
exporter_factory::create(const tracing_config& config) {
    if (!config.enabled) {
        return create_noop();
    }

    if (config.endpoint.empty()) {
        return std::unexpected(exporter_error::invalid_config);
    }

    // Check for custom factory first
    {
        std::lock_guard lock(get_registry_mutex());
        auto& registry = get_factory_registry();
        auto it = registry.find(config.format);
        if (it != registry.end()) {
            return it->second(config);
        }
    }

    // Use built-in exporters
    switch (config.format) {
        case trace_export_format::jaeger_thrift:
        case trace_export_format::jaeger_grpc:
        case trace_export_format::zipkin_json:
        case trace_export_format::otlp_grpc:
        case trace_export_format::otlp_http_json:
            // For standalone builds, use HTTP exporter stub
            return std::make_unique<http_exporter>(
                config.endpoint, config.service_name);

        default:
            return std::unexpected(exporter_error::invalid_config);
    }
}

std::unique_ptr<trace_exporter> exporter_factory::create_noop() {
    return std::make_unique<noop_exporter>();
}

void exporter_factory::register_factory(trace_export_format format,
                                         factory_function factory) {
    std::lock_guard lock(get_registry_mutex());
    get_factory_registry()[format] = std::move(factory);
}

// =============================================================================
// Batch Exporter Implementation
// =============================================================================

class batch_exporter::impl {
public:
    impl(std::unique_ptr<trace_exporter> inner, const batch_config& config)
        : inner_(std::move(inner))
        , config_(config)
        , running_(true)
        , use_executor_(false)
        , export_thread_(&impl::export_loop, this) {}

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    impl(std::unique_ptr<trace_exporter> inner,
         std::shared_ptr<kcenon::common::interfaces::IExecutor> executor,
         const batch_config& config)
        : inner_(std::move(inner))
        , config_(config)
        , executor_(std::move(executor))
        , running_(true)
        , use_executor_(executor_ && executor_->is_running()) {
        if (use_executor_) {
            schedule_export_task();
        } else {
            export_thread_ = std::thread(&impl::export_loop, this);
        }
    }
#endif

    ~impl() {
        shutdown();
    }

    std::expected<void, exporter_error> export_spans(
        const std::vector<span_data>& spans) {
        if (!running_) {
            return std::unexpected(exporter_error::not_initialized);
        }

        std::lock_guard lock(queue_mutex_);

        for (const auto& span : spans) {
            if (queue_.size() >= config_.max_queue_size) {
                ++stats_.spans_dropped;
                continue;
            }
            queue_.push(span);
        }

        // Notify export thread if batch is ready
        if (queue_.size() >= config_.max_batch_size) {
            queue_cv_.notify_one();
        }

        return {};
    }

    std::expected<void, exporter_error> flush(std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        // Signal export thread to flush
        {
            std::lock_guard lock(queue_mutex_);
            flush_requested_ = true;
            queue_cv_.notify_one();
        }

        // Wait for queue to empty
        std::unique_lock lock(queue_mutex_);
        while (!queue_.empty() && std::chrono::steady_clock::now() < deadline) {
            flush_complete_cv_.wait_until(lock, deadline);
        }

        flush_requested_ = false;

        if (!queue_.empty()) {
            return std::unexpected(exporter_error::timeout);
        }

        return {};
    }

    void shutdown() {
        if (!running_.exchange(false)) {
            return;  // Already shutdown
        }

        // Flush remaining spans
        flush(std::chrono::milliseconds{5000});

        // Stop export thread (only if using std::thread)
        queue_cv_.notify_all();
        if (!use_executor_ && export_thread_.joinable()) {
            export_thread_.join();
        }

        inner_->shutdown();
    }

    bool is_healthy() const noexcept {
        return running_ && inner_->is_healthy();
    }

    std::string name() const {
        return "batch:" + inner_->name();
    }

    size_t queue_size() const noexcept {
        std::lock_guard lock(queue_mutex_);
        return queue_.size();
    }

    exporter_statistics statistics() const {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

    void reset_statistics() {
        std::lock_guard lock(stats_mutex_);
        stats_ = exporter_statistics{};
    }

private:
    void export_loop() {
        while (running_) {
            std::vector<span_data> batch;

            // Wait for spans or timeout
            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait_for(
                    lock, config_.max_export_delay,
                    [this] {
                        return !running_ ||
                               queue_.size() >= config_.max_batch_size ||
                               flush_requested_;
                    });

                // Extract batch
                while (!queue_.empty() && batch.size() < config_.max_batch_size) {
                    batch.push_back(std::move(queue_.front()));
                    queue_.pop();
                }
            }

            // Export batch
            if (!batch.empty()) {
                export_batch_with_retry(batch);
            }

            // Notify flush completion
            if (flush_requested_) {
                flush_complete_cv_.notify_all();
            }
        }
    }

    void export_batch_with_retry(const std::vector<span_data>& batch) {
        auto start = std::chrono::steady_clock::now();

        for (size_t attempt = 0; attempt <= config_.retry_count; ++attempt) {
            auto result = inner_->export_spans(batch);

            if (result) {
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start);

                std::lock_guard lock(stats_mutex_);
                stats_.spans_exported += batch.size();
                ++stats_.export_requests;
                stats_.total_export_time_us += static_cast<size_t>(duration.count());
                stats_.retry_attempts += attempt;
                return;
            }

            // Retry
            if (attempt < config_.retry_count) {
                std::this_thread::sleep_for(config_.retry_delay);
            }
        }

        // All retries failed
        {
            std::lock_guard lock(stats_mutex_);
            ++stats_.export_failures;
            stats_.spans_dropped += batch.size();
        }
    }

    std::unique_ptr<trace_exporter> inner_;
    batch_config config_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    std::shared_ptr<kcenon::common::interfaces::IExecutor> executor_;
#endif

    std::atomic<bool> running_;
    bool use_executor_ = false;
    std::thread export_thread_;

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable flush_complete_cv_;
    std::queue<span_data> queue_;
    bool flush_requested_ = false;

    mutable std::mutex stats_mutex_;
    exporter_statistics stats_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    void schedule_export_task() {
        if (!running_ || !executor_ || !executor_->is_running()) {
            return;
        }

        auto job = std::make_unique<integration::lambda_job>(
            [this]() -> kcenon::common::VoidResult {
                if (!running_) {
                    return kcenon::common::VoidResult(std::monostate{});
                }

                std::vector<span_data> batch;

                // Extract batch
                {
                    std::unique_lock lock(queue_mutex_);

                    while (!queue_.empty() && batch.size() < config_.max_batch_size) {
                        batch.push_back(std::move(queue_.front()));
                        queue_.pop();
                    }
                }

                // Export batch
                if (!batch.empty()) {
                    export_batch_with_retry(batch);
                }

                // Notify flush completion
                if (flush_requested_) {
                    flush_complete_cv_.notify_all();
                }

                // Schedule next export task
                schedule_export_task();

                return kcenon::common::VoidResult(std::monostate{});
            },
            "batch_exporter",
            0);

        executor_->execute_delayed(std::move(job), config_.max_export_delay);
    }
#endif
};

batch_exporter::batch_exporter(std::unique_ptr<trace_exporter> inner,
                               const batch_config& config)
    : pimpl_(std::make_unique<impl>(std::move(inner), config)) {}

#ifndef PACS_BRIDGE_STANDALONE_BUILD
batch_exporter::batch_exporter(
    std::unique_ptr<trace_exporter> inner,
    std::shared_ptr<kcenon::common::interfaces::IExecutor> executor,
    const batch_config& config)
    : pimpl_(std::make_unique<impl>(std::move(inner), std::move(executor), config)) {}
#endif

batch_exporter::~batch_exporter() = default;

std::expected<void, exporter_error> batch_exporter::export_spans(
    const std::vector<span_data>& spans) {
    return pimpl_->export_spans(spans);
}

std::expected<void, exporter_error> batch_exporter::flush(
    std::chrono::milliseconds timeout) {
    return pimpl_->flush(timeout);
}

void batch_exporter::shutdown() {
    pimpl_->shutdown();
}

bool batch_exporter::is_healthy() const noexcept {
    return pimpl_->is_healthy();
}

std::string batch_exporter::name() const {
    return pimpl_->name();
}

size_t batch_exporter::queue_size() const noexcept {
    return pimpl_->queue_size();
}

exporter_statistics batch_exporter::statistics() const {
    return pimpl_->statistics();
}

void batch_exporter::reset_statistics() {
    pimpl_->reset_statistics();
}

}  // namespace pacs::bridge::tracing
