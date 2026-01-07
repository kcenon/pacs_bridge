/**
 * @file mpps_hl7_workflow.cpp
 * @brief Implementation of MPPS to HL7 workflow coordinator
 */

#include "pacs/bridge/workflow/mpps_hl7_workflow.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <random>
#include <regex>
#include <sstream>
#include <thread>
#include <utility>

namespace pacs::bridge::workflow {

// =============================================================================
// IExecutor Job Implementations (when available)
// =============================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD

/**
 * @brief Job implementation for async workflow worker execution
 *
 * Wraps a single worker iteration for execution via IExecutor.
 */
class mpps_workflow_worker_job : public kcenon::common::interfaces::IJob {
public:
    explicit mpps_workflow_worker_job(std::function<void()> work_func)
        : work_func_(std::move(work_func)) {}

    kcenon::common::VoidResult execute() override {
        if (work_func_) {
            work_func_();
        }
        return std::monostate{};
    }

    std::string get_name() const override { return "mpps_workflow_worker"; }

private:
    std::function<void()> work_func_;
};

#endif  // PACS_BRIDGE_STANDALONE_BUILD

// =============================================================================
// workflow_result Implementation
// =============================================================================

workflow_result workflow_result::ok(std::string_view correlation_id,
                                    std::string_view destination,
                                    delivery_method method) {
    workflow_result result;
    result.success = true;
    result.correlation_id = std::string(correlation_id);
    result.destination = std::string(destination);
    result.method = method;
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

workflow_result workflow_result::error(std::string_view correlation_id,
                                       std::string_view error_message) {
    workflow_result result;
    result.success = false;
    result.correlation_id = std::string(correlation_id);
    result.error_message = std::string(error_message);
    result.timestamp = std::chrono::system_clock::now();
    return result;
}

// =============================================================================
// mpps_hl7_workflow::impl
// =============================================================================

class mpps_hl7_workflow::impl {
public:
    explicit impl(mpps_hl7_workflow_config config)
        : config_(std::move(config)),
          mapper_(config_.mapper_config),
          running_(false) {}

    ~impl() { stop(); }

    // =========================================================================
    // Component Wiring
    // =========================================================================

    void set_outbound_router(std::shared_ptr<router::outbound_router> router) {
        std::lock_guard lock(mutex_);
        outbound_router_ = std::move(router);
    }

    void set_queue_manager(std::shared_ptr<router::queue_manager> queue) {
        std::lock_guard lock(mutex_);
        queue_manager_ = std::move(queue);
    }

    void set_destination_selector(destination_selector selector) {
        std::lock_guard lock(mutex_);
        custom_selector_ = std::move(selector);
    }

    void wire_to_handler(pacs_adapter::mpps_handler& handler) {
        handler.set_callback(
            [this](pacs_adapter::mpps_event event, const pacs_adapter::mpps_dataset& mpps) {
                auto result = process(event, mpps);
                if (!result) {
                    // Log error but don't throw - callback must not throw
                }
            });
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    std::expected<void, workflow_error> start() {
        std::lock_guard lock(mutex_);

        if (running_) {
            return std::unexpected(workflow_error::already_running);
        }

        if (!config_.is_valid()) {
            return std::unexpected(workflow_error::invalid_configuration);
        }

        // Start async workers if configured
        if (config_.async_delivery) {
            stop_workers_ = false;
#ifndef PACS_BRIDGE_STANDALONE_BUILD
            if (config_.executor) {
                // Use IExecutor for worker tasks
                for (size_t i = 0; i < config_.async_workers; ++i) {
                    schedule_worker_job();
                }
            } else {
                // Fallback to std::thread
                for (size_t i = 0; i < config_.async_workers; ++i) {
                    workers_.emplace_back([this] { worker_loop(); });
                }
            }
#else
            for (size_t i = 0; i < config_.async_workers; ++i) {
                workers_.emplace_back([this] { worker_loop(); });
            }
#endif
        }

        running_ = true;
        return {};
    }

    void stop() {
        {
            std::lock_guard lock(mutex_);
            if (!running_) {
                return;
            }
            running_ = false;
        }

        // Stop async workers
        stop_workers_ = true;
        queue_cv_.notify_all();

#ifndef PACS_BRIDGE_STANDALONE_BUILD
        // Wait for executor-based worker jobs to complete
        if (config_.executor) {
            for (auto& future : worker_futures_) {
                if (future.valid()) {
                    future.wait_for(std::chrono::seconds{5});
                }
            }
            worker_futures_.clear();
        }
#endif

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

    bool is_running() const noexcept { return running_.load(); }

    // =========================================================================
    // Event Processing
    // =========================================================================

    std::expected<workflow_result, workflow_error>
    process(pacs_adapter::mpps_event event, const pacs_adapter::mpps_dataset& mpps) {
        return process(event, mpps, "");
    }

    std::expected<workflow_result, workflow_error>
    process(pacs_adapter::mpps_event event,
            const pacs_adapter::mpps_dataset& mpps,
            std::string_view correlation_id) {
        if (!running_) {
            return std::unexpected(workflow_error::not_running);
        }

        auto start_time = std::chrono::steady_clock::now();

        // Generate correlation ID if needed
        std::string corr_id = correlation_id.empty() && config_.generate_correlation_id
                                  ? generate_correlation_id()
                                  : std::string(correlation_id);

        std::string trace_id = config_.enable_tracing ? generate_trace_id() : "";

        // Update statistics
        update_event_stats(event);

        // Step 1: Map MPPS to HL7
        auto mapping_result = mapper_.mpps_to_orm(mpps, event);
        if (!mapping_result) {
            update_failure_stats(true, false, false);
            return std::unexpected(workflow_error::mapping_failed);
        }

        const auto& hl7_result = mapping_result.value();

        // Step 2: Select destination
        auto dest = select_destination(mpps);
        if (!dest) {
            update_failure_stats(false, false, false);
            return std::unexpected(workflow_error::no_destination);
        }

        // Step 3: Attempt delivery
        workflow_result result;
        result.correlation_id = corr_id;
        result.trace_id = trace_id;
        result.mpps_sop_instance_uid = mpps.sop_instance_uid;
        result.accession_number = mpps.accession_number;
        result.message_control_id = hl7_result.control_id;
        result.destination = *dest;
        result.timestamp = std::chrono::system_clock::now();

        bool delivered = false;

        // Try direct delivery first
        if (outbound_router_ && outbound_router_->is_running()) {
            auto delivery = outbound_router_->route(hl7_result.message);
            if (delivery && delivery->success) {
                delivered = true;
                result.success = true;
                result.method = delivery_method::direct;
                update_success_stats(*dest, delivery_method::direct);
            }
        }

        // Fallback to queue if direct delivery failed
        if (!delivered && config_.enable_queue_fallback && queue_manager_ &&
            queue_manager_->is_running()) {
            auto serialized = hl7_result.message.serialize();
            auto enqueue_result = queue_manager_->enqueue(
                *dest, serialized, config_.fallback_queue_priority, corr_id,
                hl7_result.message.header().full_message_type());

            if (enqueue_result) {
                delivered = true;
                result.success = true;
                result.method = delivery_method::queued;
                update_success_stats(*dest, delivery_method::queued);
            } else {
                update_failure_stats(false, false, true);
            }
        }

        if (!delivered) {
            update_failure_stats(false, true, false);
            result.success = false;
            result.error_message = "All delivery methods failed";
        }

        // Calculate processing time
        auto end_time = std::chrono::steady_clock::now();
        result.processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        // Update average processing time
        update_processing_time(result.processing_time);

        // Invoke completion callback
        if (completion_callback_) {
            completion_callback_(result);
        }

        if (result.success) {
            return result;
        }
        return std::unexpected(workflow_error::delivery_failed);
    }

    std::expected<void, workflow_error>
    process_async(pacs_adapter::mpps_event event,
                  const pacs_adapter::mpps_dataset& mpps,
                  completion_callback callback) {
        if (!running_) {
            return std::unexpected(workflow_error::not_running);
        }

        if (!config_.async_delivery) {
            // Fallback to sync processing
            auto result = process(event, mpps);
            if (callback) {
                callback(result ? *result
                                : workflow_result::error("", to_string(result.error())));
            }
            return {};
        }

        // Queue for async processing
        {
            std::lock_guard lock(queue_mutex_);
            async_queue_.push({event, mpps, std::move(callback)});
        }
        queue_cv_.notify_one();

        return {};
    }

    // =========================================================================
    // Destination Selection
    // =========================================================================

    std::optional<std::string> select_destination(const pacs_adapter::mpps_dataset& mpps) const {
        // Try custom selector first
        if (custom_selector_) {
            auto result = custom_selector_(mpps);
            if (result) {
                return result;
            }
        }

        // Evaluate routing rules in priority order
        std::vector<const destination_rule*> sorted_rules;
        for (const auto& rule : config_.routing_rules) {
            if (rule.enabled) {
                sorted_rules.push_back(&rule);
            }
        }

        std::sort(sorted_rules.begin(), sorted_rules.end(),
                  [](const destination_rule* a, const destination_rule* b) {
                      return a->priority < b->priority;
                  });

        for (const auto* rule : sorted_rules) {
            if (matches_rule(*rule, mpps)) {
                return rule->destination;
            }
        }

        // Fall back to default
        if (!config_.default_destination.empty()) {
            return config_.default_destination;
        }

        return std::nullopt;
    }

    void add_routing_rule(const destination_rule& rule) {
        std::lock_guard lock(mutex_);
        config_.routing_rules.push_back(rule);
    }

    bool remove_routing_rule(std::string_view name) {
        std::lock_guard lock(mutex_);
        auto it = std::find_if(config_.routing_rules.begin(), config_.routing_rules.end(),
                               [name](const destination_rule& r) { return r.name == name; });
        if (it != config_.routing_rules.end()) {
            config_.routing_rules.erase(it);
            return true;
        }
        return false;
    }

    std::vector<destination_rule> routing_rules() const {
        std::lock_guard lock(mutex_);
        return config_.routing_rules;
    }

    // =========================================================================
    // Correlation and Tracing
    // =========================================================================

    std::string generate_correlation_id() const {
        // Generate UUID-like correlation ID
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;

        uint64_t high = dis(gen);
        uint64_t low = dis(gen);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        oss << std::setw(8) << (high >> 32);
        oss << "-";
        oss << std::setw(4) << ((high >> 16) & 0xFFFF);
        oss << "-";
        oss << std::setw(4) << (high & 0xFFFF);
        oss << "-";
        oss << std::setw(4) << (low >> 48);
        oss << "-";
        oss << std::setw(12) << (low & 0xFFFFFFFFFFFF);

        return oss.str();
    }

    std::string generate_trace_id() const {
        // Generate OpenTelemetry compatible trace ID (32 hex chars)
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;

        uint64_t high = dis(gen);
        uint64_t low = dis(gen);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        oss << std::setw(16) << high;
        oss << std::setw(16) << low;

        return oss.str();
    }

    // =========================================================================
    // Callbacks
    // =========================================================================

    void set_completion_callback(completion_callback callback) {
        std::lock_guard lock(mutex_);
        completion_callback_ = std::move(callback);
    }

    void clear_completion_callback() {
        std::lock_guard lock(mutex_);
        completion_callback_ = nullptr;
    }

    // =========================================================================
    // Statistics
    // =========================================================================

    workflow_statistics get_statistics() const {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

    void reset_statistics() {
        std::lock_guard lock(stats_mutex_);
        stats_ = workflow_statistics{};
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    const mpps_hl7_workflow_config& config() const noexcept { return config_; }

    void set_config(const mpps_hl7_workflow_config& config) {
        std::lock_guard lock(mutex_);
        config_ = config;
        mapper_.set_config(config_.mapper_config);
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    bool matches_rule(const destination_rule& rule,
                      const pacs_adapter::mpps_dataset& mpps) const {
        switch (rule.criteria) {
            case destination_criteria::by_message_type:
                // ORM^O01 is always the message type for MPPS
                return rule.pattern == "ORM^O01" || rule.pattern == "*";

            case destination_criteria::by_modality:
                return matches_pattern(mpps.modality, rule.pattern);

            case destination_criteria::by_station:
                return matches_pattern(mpps.station_ae_title, rule.pattern);

            case destination_criteria::by_accession_pattern:
                return matches_pattern(mpps.accession_number, rule.pattern);

            case destination_criteria::custom:
                // Custom criteria should use custom_selector_
                return false;

            default:
                return false;
        }
    }

    bool matches_pattern(std::string_view value, std::string_view pattern) const {
        if (pattern == "*") {
            return true;
        }
        if (pattern.empty()) {
            return value.empty();
        }

        // Simple wildcard matching
        if (pattern.front() == '*' && pattern.back() == '*') {
            auto substr = pattern.substr(1, pattern.size() - 2);
            return value.find(substr) != std::string_view::npos;
        }
        if (pattern.front() == '*') {
            auto suffix = pattern.substr(1);
            return value.size() >= suffix.size() &&
                   value.substr(value.size() - suffix.size()) == suffix;
        }
        if (pattern.back() == '*') {
            auto prefix = pattern.substr(0, pattern.size() - 1);
            return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
        }

        return value == pattern;
    }

    void update_event_stats(pacs_adapter::mpps_event event) {
        std::lock_guard lock(stats_mutex_);
        stats_.total_events++;
        stats_.last_event_time = std::chrono::system_clock::now();

        switch (event) {
            case pacs_adapter::mpps_event::in_progress:
                stats_.in_progress_events++;
                break;
            case pacs_adapter::mpps_event::completed:
                stats_.completed_events++;
                break;
            case pacs_adapter::mpps_event::discontinued:
                stats_.discontinued_events++;
                break;
        }
    }

    void update_success_stats(std::string_view destination, delivery_method method) {
        std::lock_guard lock(stats_mutex_);
        stats_.successful_events++;

        if (method == delivery_method::direct) {
            stats_.direct_deliveries++;
        } else {
            stats_.queued_deliveries++;
        }

        stats_.destination_stats[std::string(destination)].messages_sent++;
    }

    void update_failure_stats(bool mapping_failed, bool delivery_failed, bool enqueue_failed) {
        std::lock_guard lock(stats_mutex_);
        stats_.failed_events++;

        if (mapping_failed) {
            stats_.mapping_failures++;
        }
        if (delivery_failed) {
            stats_.delivery_failures++;
        }
        if (enqueue_failed) {
            stats_.enqueue_failures++;
        }
    }

    void update_processing_time(std::chrono::milliseconds time) {
        std::lock_guard lock(stats_mutex_);
        double total = stats_.avg_processing_time_ms * static_cast<double>(stats_.total_events - 1);
        total += static_cast<double>(time.count());
        stats_.avg_processing_time_ms = total / static_cast<double>(stats_.total_events);
    }

    void worker_loop() {
        while (!stop_workers_) {
            async_work_item item;
            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { return stop_workers_ || !async_queue_.empty(); });

                if (stop_workers_ && async_queue_.empty()) {
                    break;
                }

                if (!async_queue_.empty()) {
                    item = std::move(async_queue_.front());
                    async_queue_.pop();
                }
            }

            if (item.callback || !stop_workers_) {
                auto result = process(item.event, item.mpps);
                if (item.callback) {
                    item.callback(result ? *result
                                         : workflow_result::error("", to_string(result.error())));
                }
            }
        }
    }

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    /**
     * @brief Schedule worker job using IExecutor
     *
     * Processes a single work item and reschedules itself for continuous operation.
     */
    void schedule_worker_job() {
        if (stop_workers_ || !config_.executor) {
            return;
        }

        auto job = std::make_unique<mpps_workflow_worker_job>([this]() {
            if (stop_workers_) {
                return;
            }

            // Try to get a work item
            async_work_item item;
            {
                std::unique_lock lock(queue_mutex_);
                // Use timed wait to allow periodic checks for stop signal
                queue_cv_.wait_for(lock, std::chrono::milliseconds{100},
                                   [this] { return stop_workers_ || !async_queue_.empty(); });

                if (stop_workers_) {
                    return;
                }

                if (!async_queue_.empty()) {
                    item = std::move(async_queue_.front());
                    async_queue_.pop();
                }
            }

            // Process the item if we got one
            if (item.callback || !item.mpps.sop_instance_uid.empty()) {
                auto result = process(item.event, item.mpps);
                if (item.callback) {
                    item.callback(result ? *result
                                         : workflow_result::error("", to_string(result.error())));
                }
            }

            // Reschedule for next iteration
            schedule_worker_job();
        });

        auto result = config_.executor->execute(std::move(job));
        if (result.is_ok()) {
            worker_futures_.push_back(std::move(result.value()));
        }
    }
#endif  // PACS_BRIDGE_STANDALONE_BUILD

    // =========================================================================
    // Member Variables
    // =========================================================================

    mpps_hl7_workflow_config config_;
    mapping::dicom_hl7_mapper mapper_;

    std::shared_ptr<router::outbound_router> outbound_router_;
    std::shared_ptr<router::queue_manager> queue_manager_;
    destination_selector custom_selector_;
    completion_callback completion_callback_;

    std::atomic<bool> running_;
    mutable std::mutex mutex_;

    // Statistics
    mutable std::mutex stats_mutex_;
    workflow_statistics stats_;

    // Async processing
    struct async_work_item {
        pacs_adapter::mpps_event event;
        pacs_adapter::mpps_dataset mpps;
        completion_callback callback;
    };

    std::atomic<bool> stop_workers_{false};
    std::queue<async_work_item> async_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::thread> workers_;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    // Futures for tracking executor-based worker jobs
    std::vector<std::future<void>> worker_futures_;
#endif
};

// =============================================================================
// mpps_hl7_workflow Implementation
// =============================================================================

mpps_hl7_workflow::mpps_hl7_workflow() : pimpl_(std::make_unique<impl>(mpps_hl7_workflow_config{})) {}

mpps_hl7_workflow::mpps_hl7_workflow(const mpps_hl7_workflow_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

mpps_hl7_workflow::~mpps_hl7_workflow() = default;

mpps_hl7_workflow::mpps_hl7_workflow(mpps_hl7_workflow&&) noexcept = default;
mpps_hl7_workflow& mpps_hl7_workflow::operator=(mpps_hl7_workflow&&) noexcept = default;

void mpps_hl7_workflow::set_outbound_router(std::shared_ptr<router::outbound_router> router) {
    pimpl_->set_outbound_router(std::move(router));
}

void mpps_hl7_workflow::set_queue_manager(std::shared_ptr<router::queue_manager> queue) {
    pimpl_->set_queue_manager(std::move(queue));
}

void mpps_hl7_workflow::set_destination_selector(destination_selector selector) {
    pimpl_->set_destination_selector(std::move(selector));
}

void mpps_hl7_workflow::wire_to_handler(pacs_adapter::mpps_handler& handler) {
    pimpl_->wire_to_handler(handler);
}

std::expected<void, workflow_error> mpps_hl7_workflow::start() { return pimpl_->start(); }

void mpps_hl7_workflow::stop() { pimpl_->stop(); }

bool mpps_hl7_workflow::is_running() const noexcept { return pimpl_->is_running(); }

std::expected<workflow_result, workflow_error>
mpps_hl7_workflow::process(pacs_adapter::mpps_event event, const pacs_adapter::mpps_dataset& mpps) {
    return pimpl_->process(event, mpps);
}

std::expected<workflow_result, workflow_error>
mpps_hl7_workflow::process(pacs_adapter::mpps_event event,
                           const pacs_adapter::mpps_dataset& mpps,
                           std::string_view correlation_id) {
    return pimpl_->process(event, mpps, correlation_id);
}

std::expected<void, workflow_error>
mpps_hl7_workflow::process_async(pacs_adapter::mpps_event event,
                                  const pacs_adapter::mpps_dataset& mpps,
                                  completion_callback callback) {
    return pimpl_->process_async(event, mpps, std::move(callback));
}

std::optional<std::string>
mpps_hl7_workflow::select_destination(const pacs_adapter::mpps_dataset& mpps) const {
    return pimpl_->select_destination(mpps);
}

void mpps_hl7_workflow::add_routing_rule(const destination_rule& rule) {
    pimpl_->add_routing_rule(rule);
}

bool mpps_hl7_workflow::remove_routing_rule(std::string_view name) {
    return pimpl_->remove_routing_rule(name);
}

std::vector<destination_rule> mpps_hl7_workflow::routing_rules() const {
    return pimpl_->routing_rules();
}

std::string mpps_hl7_workflow::generate_correlation_id() const {
    return pimpl_->generate_correlation_id();
}

std::string mpps_hl7_workflow::generate_trace_id() const { return pimpl_->generate_trace_id(); }

void mpps_hl7_workflow::set_completion_callback(completion_callback callback) {
    pimpl_->set_completion_callback(std::move(callback));
}

void mpps_hl7_workflow::clear_completion_callback() { pimpl_->clear_completion_callback(); }

workflow_statistics mpps_hl7_workflow::get_statistics() const { return pimpl_->get_statistics(); }

void mpps_hl7_workflow::reset_statistics() { pimpl_->reset_statistics(); }

const mpps_hl7_workflow_config& mpps_hl7_workflow::config() const noexcept {
    return pimpl_->config();
}

void mpps_hl7_workflow::set_config(const mpps_hl7_workflow_config& config) {
    pimpl_->set_config(config);
}

// =============================================================================
// workflow_config_builder Implementation
// =============================================================================

workflow_config_builder::workflow_config_builder() = default;

workflow_config_builder workflow_config_builder::create() { return workflow_config_builder{}; }

workflow_config_builder& workflow_config_builder::default_destination(std::string_view dest) {
    config_.default_destination = std::string(dest);
    return *this;
}

workflow_config_builder& workflow_config_builder::enable_queue_fallback(bool enable) {
    config_.enable_queue_fallback = enable;
    return *this;
}

workflow_config_builder& workflow_config_builder::fallback_priority(int priority) {
    config_.fallback_queue_priority = priority;
    return *this;
}

workflow_config_builder& workflow_config_builder::generate_correlation_id(bool enable) {
    config_.generate_correlation_id = enable;
    return *this;
}

workflow_config_builder& workflow_config_builder::enable_tracing(bool enable) {
    config_.enable_tracing = enable;
    return *this;
}

workflow_config_builder& workflow_config_builder::enable_metrics(bool enable) {
    config_.enable_metrics = enable;
    return *this;
}

workflow_config_builder& workflow_config_builder::add_rule(const destination_rule& rule) {
    config_.routing_rules.push_back(rule);
    return *this;
}

workflow_config_builder&
workflow_config_builder::processing_timeout(std::chrono::milliseconds timeout) {
    config_.processing_timeout = timeout;
    return *this;
}

workflow_config_builder& workflow_config_builder::async_delivery(bool enable, size_t workers) {
    config_.async_delivery = enable;
    config_.async_workers = workers;
    return *this;
}

workflow_config_builder&
workflow_config_builder::mapper_config(const mapping::dicom_hl7_mapper_config& config) {
    config_.mapper_config = config;
    return *this;
}

mpps_hl7_workflow_config workflow_config_builder::build() const { return config_; }

}  // namespace pacs::bridge::workflow
