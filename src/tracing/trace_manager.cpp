/**
 * @file trace_manager.cpp
 * @brief Implementation of centralized trace management
 */

#include "pacs/bridge/tracing/trace_manager.h"
#include "pacs/bridge/tracing/span_wrapper.h"
#include "span_impl.h"

#include <atomic>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
#include <kcenon/monitoring/tracing/trace_context.h>
#include <kcenon/monitoring/tracing/distributed_tracer.h>
#include <kcenon/monitoring/exporters/trace_exporters.h>
#endif

namespace pacs::bridge::tracing {

namespace {

/**
 * @brief Generate a random trace ID (32 hex chars)
 */
std::string generate_trace_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char hex_chars[] = "0123456789abcdef";

    std::string result;
    result.reserve(32);

    std::uniform_int_distribution<int> dist(0, 15);
    for (size_t i = 0; i < 32; ++i) {
        result += hex_chars[dist(rng)];
    }

    return result;
}

/**
 * @brief Generate a random span ID (16 hex chars)
 */
std::string generate_span_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char hex_chars[] = "0123456789abcdef";

    std::string result;
    result.reserve(16);

    std::uniform_int_distribution<int> dist(0, 15);
    for (size_t i = 0; i < 16; ++i) {
        result += hex_chars[dist(rng)];
    }

    return result;
}

}  // namespace

// =============================================================================
// trace_manager::impl
// =============================================================================

class trace_manager::impl {
public:
    impl() = default;

    std::expected<void, trace_error> initialize(const tracing_config& config) {
        std::lock_guard<std::mutex> lock(mutex_);

        config_ = config;
        initialized_ = true;

        if (!config.enabled) {
            return {};
        }

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
        // Initialize monitoring_system exporter if available
        try {
            kcenon::monitoring::trace_export_config export_config;
            export_config.endpoint = config.endpoint;
            export_config.service_name = config.service_name;
            export_config.max_batch_size = config.max_batch_size;
            export_config.batch_timeout = config.batch_timeout;

            // Map format
            switch (config.format) {
                case trace_export_format::jaeger_thrift:
                    export_config.format = kcenon::monitoring::trace_export_format::jaeger_thrift;
                    break;
                case trace_export_format::jaeger_grpc:
                    export_config.format = kcenon::monitoring::trace_export_format::jaeger_grpc;
                    break;
                case trace_export_format::zipkin_json:
                    export_config.format = kcenon::monitoring::trace_export_format::zipkin_json;
                    break;
                case trace_export_format::otlp_grpc:
                    export_config.format = kcenon::monitoring::trace_export_format::otlp_grpc;
                    break;
                case trace_export_format::otlp_http_json:
                    export_config.format = kcenon::monitoring::trace_export_format::otlp_http_json;
                    break;
            }

            exporter_ = kcenon::monitoring::trace_exporter_factory::create_exporter(export_config);

        } catch (const std::exception& e) {
            return std::unexpected(trace_error::exporter_failed);
        }
#endif

        return {};
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
        if (exporter_) {
            exporter_->flush();
            exporter_->shutdown();
            exporter_.reset();
        }
#endif

        initialized_ = false;
    }

    bool is_enabled() const noexcept {
        return initialized_ && config_.enabled;
    }

    const tracing_config& config() const noexcept {
        return config_;
    }

    span_wrapper start_span(std::string_view name, span_kind kind) {
        if (!is_enabled()) {
            return span_wrapper{};  // No-op span
        }

        trace_context ctx;
        ctx.trace_id = generate_trace_id();
        ctx.span_id = generate_span_id();
        ctx.parent_span_id = std::nullopt;
        ctx.trace_flags = should_sample() ? 0x01 : 0x00;

        stats_.spans_created++;

        return span_wrapper::create_internal(name, ctx, kind, config_.service_name);
    }

    span_wrapper start_span_with_parent(std::string_view name,
                                        const trace_context& parent,
                                        span_kind kind) {
        if (!is_enabled()) {
            return span_wrapper{};
        }

        trace_context ctx;
        ctx.trace_id = parent.trace_id;
        ctx.span_id = generate_span_id();
        ctx.parent_span_id = parent.span_id;
        ctx.trace_flags = parent.trace_flags;

        stats_.spans_created++;

        return span_wrapper::create_internal(name, ctx, kind, config_.service_name);
    }

    span_wrapper start_span_from_traceparent(std::string_view name,
                                             std::string_view traceparent,
                                             span_kind kind) {
        auto parent_ctx = trace_context::from_traceparent(traceparent);
        if (parent_ctx) {
            return start_span_with_parent(name, *parent_ctx, kind);
        }

        // Fall back to new root span
        return start_span(name, kind);
    }

    std::optional<trace_context> current_context() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_context_;
    }

    void set_current_context(const trace_context& context) {
        std::lock_guard<std::mutex> lock(mutex_);
        current_context_ = context;
    }

    void clear_current_context() {
        std::lock_guard<std::mutex> lock(mutex_);
        current_context_ = std::nullopt;
    }

    std::expected<void, trace_error> flush(std::chrono::milliseconds timeout) {
        (void)timeout;  // Suppress unused warning

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
        if (exporter_) {
            auto result = exporter_->flush();
            if (result.is_err()) {
                return std::unexpected(trace_error::exporter_failed);
            }
        }
#endif

        return {};
    }

    statistics get_statistics() const {
        return stats_;
    }

    void reset_statistics() {
        stats_ = statistics{};
    }

private:
    bool should_sample() const {
        if (config_.sampling_rate >= 1.0) {
            return true;
        }
        if (config_.sampling_rate <= 0.0) {
            return false;
        }

        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng) < config_.sampling_rate;
    }

    mutable std::mutex mutex_;
    bool initialized_ = false;
    tracing_config config_;
    std::optional<trace_context> current_context_;
    statistics stats_;

#ifdef PACS_BRIDGE_HAS_MONITORING_SYSTEM
    std::unique_ptr<kcenon::monitoring::trace_exporter_interface> exporter_;
#endif
};

// =============================================================================
// trace_manager Implementation
// =============================================================================

trace_manager& trace_manager::instance() {
    static trace_manager instance;
    return instance;
}

trace_manager::trace_manager() : pimpl_(std::make_unique<impl>()) {}

trace_manager::~trace_manager() {
    shutdown();
}

std::expected<void, trace_error> trace_manager::initialize(
    const tracing_config& config) {
    return pimpl_->initialize(config);
}

void trace_manager::shutdown() {
    pimpl_->shutdown();
}

bool trace_manager::is_enabled() const noexcept {
    return pimpl_->is_enabled();
}

const tracing_config& trace_manager::config() const noexcept {
    return pimpl_->config();
}

span_wrapper trace_manager::start_span(std::string_view name, span_kind kind) {
    return pimpl_->start_span(name, kind);
}

span_wrapper trace_manager::start_span_with_parent(
    std::string_view name,
    const trace_context& parent,
    span_kind kind) {
    return pimpl_->start_span_with_parent(name, parent, kind);
}

span_wrapper trace_manager::start_span_from_traceparent(
    std::string_view name,
    std::string_view traceparent,
    span_kind kind) {
    return pimpl_->start_span_from_traceparent(name, traceparent, kind);
}

std::optional<trace_context> trace_manager::current_context() const {
    return pimpl_->current_context();
}

void trace_manager::set_current_context(const trace_context& context) {
    pimpl_->set_current_context(context);
}

void trace_manager::clear_current_context() {
    pimpl_->clear_current_context();
}

std::expected<void, trace_error> trace_manager::flush(
    std::chrono::milliseconds timeout) {
    return pimpl_->flush(timeout);
}

trace_manager::statistics trace_manager::get_statistics() const {
    return pimpl_->get_statistics();
}

void trace_manager::reset_statistics() {
    pimpl_->reset_statistics();
}

}  // namespace pacs::bridge::tracing
