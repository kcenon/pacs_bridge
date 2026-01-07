/**
 * @file hl7_pipeline.cpp
 * @brief Implementation of HL7 message processing pipeline
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 * @see https://github.com/kcenon/pacs_bridge/issues/155
 */

#include "pacs/bridge/messaging/hl7_pipeline.h"
#include "pacs/bridge/messaging/hl7_message_bus.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace pacs::bridge::messaging {

// =============================================================================
// hl7_pipeline Implementation
// =============================================================================

class hl7_pipeline::impl {
public:
    explicit impl(const pipeline_config& config)
        : config_(config)
        , running_(false) {
    }

    ~impl() {
        stop();
    }

    std::expected<void, pipeline_error> add_stage(const pipeline_stage& stage) {
        if (stage.id.empty()) {
            return std::unexpected(pipeline_error::invalid_stage);
        }

        if (!stage.processor) {
            return std::unexpected(pipeline_error::invalid_stage);
        }

        std::unique_lock lock(mutex_);

        // Check for duplicate ID
        for (const auto& s : stages_) {
            if (s.id == stage.id) {
                return std::unexpected(pipeline_error::invalid_stage);
            }
        }

        stages_.push_back(stage);
        return {};
    }

    std::expected<void, pipeline_error> add_stage(
        std::string_view id,
        std::string_view name,
        stage_processor processor) {

        pipeline_stage stage;
        stage.id = std::string(id);
        stage.name = std::string(name);
        stage.processor = std::move(processor);
        return add_stage(stage);
    }

    bool remove_stage(std::string_view stage_id) {
        std::unique_lock lock(mutex_);

        auto it = std::find_if(stages_.begin(), stages_.end(),
            [&](const pipeline_stage& s) { return s.id == stage_id; });

        if (it == stages_.end()) {
            return false;
        }

        stages_.erase(it);
        return true;
    }

    void set_stage_enabled(std::string_view stage_id, bool enabled) {
        std::unique_lock lock(mutex_);

        for (auto& stage : stages_) {
            if (stage.id == stage_id) {
                stage.enabled = enabled;
                return;
            }
        }
    }

    const pipeline_stage* get_stage(std::string_view stage_id) const {
        std::shared_lock lock(mutex_);

        for (const auto& stage : stages_) {
            if (stage.id == stage_id) {
                return &stage;
            }
        }
        return nullptr;
    }

    size_t stage_count() const noexcept {
        std::shared_lock lock(mutex_);
        return stages_.size();
    }

    std::vector<std::string> stage_names() const {
        std::shared_lock lock(mutex_);

        std::vector<std::string> names;
        names.reserve(stages_.size());
        for (const auto& stage : stages_) {
            names.push_back(stage.name.empty() ? stage.id : stage.name);
        }
        return names;
    }

    void clear_stages() {
        std::unique_lock lock(mutex_);
        stages_.clear();
    }

    std::expected<hl7::hl7_message, pipeline_error> process(
        const hl7::hl7_message& message) {

        auto start_time = std::chrono::steady_clock::now();
        hl7::hl7_message current_message = message;

        std::shared_lock lock(mutex_);

        for (size_t i = 0; i < stages_.size(); ++i) {
            const auto& stage = stages_[i];

            if (!stage.enabled) {
                continue;
            }

            // Apply filter if present
            if (stage.filter && !stage.filter(current_message)) {
                if (config_.enable_statistics) {
                    update_stage_stats(stage.id, true, 0);
                }
                continue;
            }

            auto stage_start = std::chrono::steady_clock::now();
            stage_result result;

            // Execute with retry logic
            size_t attempts = 0;
            bool success = false;

            while (attempts <= stage.max_retries) {
                try {
                    result = stage.processor(current_message);
                    success = result.success;

                    if (success) break;

                    if (attempts < stage.max_retries) {
                        std::this_thread::sleep_for(stage.retry_delay);
                    }
                } catch (const std::exception& e) {
                    result.success = false;
                    result.error_message = e.what();
                }

                attempts++;
            }

            auto stage_end = std::chrono::steady_clock::now();
            auto stage_time = std::chrono::duration_cast<std::chrono::microseconds>(
                stage_end - stage_start);

            if (config_.enable_statistics) {
                update_stage_stats(stage.id, result.success, stage_time.count());
            }

            if (!result.success) {
                if (stage.optional) {
                    // Continue to next stage
                    continue;
                }

                if (config_.stop_on_error) {
                    if (config_.enable_statistics) {
                        std::unique_lock stats_lock(stats_mutex_);
                        stats_.messages_failed++;
                    }
                    return std::unexpected(pipeline_error::stage_failed);
                }
            }

            // Update message if transformed
            if (result.message) {
                current_message = std::move(*result.message);
            }

            // Check if we should skip remaining stages
            if (result.skip_remaining) {
                break;
            }
        }

        auto end_time = std::chrono::steady_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);

        if (config_.enable_statistics) {
            std::unique_lock stats_lock(stats_mutex_);
            stats_.messages_processed++;
            stats_.messages_succeeded++;
            stats_.total_pipeline_time_us += total_time.count();
        }

        return current_message;
    }

    std::expected<hl7::hl7_message, pipeline_error> process(
        std::string_view raw_data) {

        hl7::hl7_parser parser;
        auto parse_result = parser.parse(raw_data);

        if (!parse_result.is_ok()) {
            return std::unexpected(pipeline_error::stage_failed);
        }

        return process(parse_result.value());
    }

    std::expected<void, pipeline_error> start(
        std::shared_ptr<hl7_message_bus> bus) {

        if (running_.load()) {
            return std::unexpected(pipeline_error::already_running);
        }

        if (!bus || !bus->is_running()) {
            return std::unexpected(pipeline_error::not_started);
        }

        bus_ = bus;

        // Subscribe to input topic
        if (!config_.input_topic.empty()) {
            auto sub_result = bus_->subscribe(config_.input_topic,
                [this](const hl7::hl7_message& msg) {
                    auto result = process(msg);

                    if (result && !config_.output_topic.empty()) {
                        (void)bus_->publish(config_.output_topic, *result);
                    }

                    return subscription_result::ok();
                });

            if (!sub_result) {
                return std::unexpected(pipeline_error::not_started);
            }

            subscription_ = *sub_result;
        }

        running_.store(true);
        return {};
    }

    void stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);

        if (bus_) {
            (void)bus_->unsubscribe(subscription_);
            bus_.reset();
        }
    }

    bool is_running() const noexcept {
        return running_.load();
    }

    statistics get_statistics() const {
        std::shared_lock lock(stats_mutex_);

        statistics stats;
        stats.messages_processed = stats_.messages_processed;
        stats.messages_succeeded = stats_.messages_succeeded;
        stats.messages_failed = stats_.messages_failed;
        stats.messages_filtered = stats_.messages_filtered;

        if (stats_.messages_processed > 0) {
            stats.avg_pipeline_time_us =
                static_cast<double>(stats_.total_pipeline_time_us) /
                static_cast<double>(stats_.messages_processed);
        }

        // Stage statistics
        for (const auto& [stage_id, stage_stats] : stage_stats_) {
            statistics::stage_stats ss;
            ss.stage_id = stage_id;
            ss.invocations = stage_stats.invocations;
            ss.successes = stage_stats.successes;
            ss.failures = stage_stats.failures;

            if (stage_stats.invocations > 0) {
                ss.avg_time_us =
                    static_cast<double>(stage_stats.total_time_us) /
                    static_cast<double>(stage_stats.invocations);
            }

            stats.stage_statistics.push_back(ss);
        }

        return stats;
    }

    void reset_statistics() {
        std::unique_lock lock(stats_mutex_);
        stats_ = internal_stats{};
        stage_stats_.clear();
    }

    const pipeline_config& config() const noexcept {
        return config_;
    }

    void set_config(const pipeline_config& config) {
        std::unique_lock lock(mutex_);
        config_ = config;
    }

private:
    struct internal_stats {
        uint64_t messages_processed = 0;
        uint64_t messages_succeeded = 0;
        uint64_t messages_failed = 0;
        uint64_t messages_filtered = 0;
        uint64_t total_pipeline_time_us = 0;
    };

    struct stage_internal_stats {
        uint64_t invocations = 0;
        uint64_t successes = 0;
        uint64_t failures = 0;
        uint64_t total_time_us = 0;
    };

    void update_stage_stats(const std::string& stage_id, bool success,
                             uint64_t time_us) {
        std::unique_lock lock(stats_mutex_);

        auto& stats = stage_stats_[stage_id];
        stats.invocations++;
        stats.total_time_us += time_us;

        if (success) {
            stats.successes++;
        } else {
            stats.failures++;
        }
    }

    pipeline_config config_;
    std::atomic<bool> running_;

    mutable std::shared_mutex mutex_;
    std::vector<pipeline_stage> stages_;

    std::shared_ptr<hl7_message_bus> bus_;
    subscription_handle subscription_;

    mutable std::shared_mutex stats_mutex_;
    internal_stats stats_;
    std::unordered_map<std::string, stage_internal_stats> stage_stats_;
};

// =============================================================================
// hl7_pipeline Public Interface
// =============================================================================

hl7_pipeline::hl7_pipeline()
    : pimpl_(std::make_unique<impl>(pipeline_config{})) {
}

hl7_pipeline::hl7_pipeline(const pipeline_config& config)
    : pimpl_(std::make_unique<impl>(config)) {
}

hl7_pipeline::~hl7_pipeline() = default;
hl7_pipeline::hl7_pipeline(hl7_pipeline&&) noexcept = default;
hl7_pipeline& hl7_pipeline::operator=(hl7_pipeline&&) noexcept = default;

std::expected<void, pipeline_error> hl7_pipeline::add_stage(
    const pipeline_stage& stage) {
    return pimpl_->add_stage(stage);
}

std::expected<void, pipeline_error> hl7_pipeline::add_stage(
    std::string_view id,
    std::string_view name,
    stage_processor processor) {
    return pimpl_->add_stage(id, name, std::move(processor));
}

bool hl7_pipeline::remove_stage(std::string_view stage_id) {
    return pimpl_->remove_stage(stage_id);
}

void hl7_pipeline::set_stage_enabled(std::string_view stage_id, bool enabled) {
    pimpl_->set_stage_enabled(stage_id, enabled);
}

const pipeline_stage* hl7_pipeline::get_stage(std::string_view stage_id) const {
    return pimpl_->get_stage(stage_id);
}

size_t hl7_pipeline::stage_count() const noexcept {
    return pimpl_->stage_count();
}

std::vector<std::string> hl7_pipeline::stage_names() const {
    return pimpl_->stage_names();
}

void hl7_pipeline::clear_stages() {
    pimpl_->clear_stages();
}

std::expected<hl7::hl7_message, pipeline_error> hl7_pipeline::process(
    const hl7::hl7_message& message) {
    return pimpl_->process(message);
}

std::expected<hl7::hl7_message, pipeline_error> hl7_pipeline::process(
    std::string_view raw_data) {
    return pimpl_->process(raw_data);
}

std::expected<void, pipeline_error> hl7_pipeline::start(
    std::shared_ptr<hl7_message_bus> bus) {
    return pimpl_->start(std::move(bus));
}

void hl7_pipeline::stop() {
    pimpl_->stop();
}

bool hl7_pipeline::is_running() const noexcept {
    return pimpl_->is_running();
}

hl7_pipeline::statistics hl7_pipeline::get_statistics() const {
    return pimpl_->get_statistics();
}

void hl7_pipeline::reset_statistics() {
    pimpl_->reset_statistics();
}

const pipeline_config& hl7_pipeline::config() const noexcept {
    return pimpl_->config();
}

void hl7_pipeline::set_config(const pipeline_config& config) {
    pimpl_->set_config(config);
}

// =============================================================================
// hl7_pipeline_builder Implementation
// =============================================================================

hl7_pipeline_builder::hl7_pipeline_builder(std::string_view name) {
    config_.name = std::string(name);
}

hl7_pipeline_builder hl7_pipeline_builder::create(std::string_view name) {
    return hl7_pipeline_builder(name);
}

hl7_pipeline_builder& hl7_pipeline_builder::from_topic(std::string_view topic) {
    config_.input_topic = std::string(topic);
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::to_topic(std::string_view topic) {
    config_.output_topic = std::string(topic);
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::add_validator(stage_filter validator) {
    return add_validator("validator_" + std::to_string(stages_.size()), validator);
}

hl7_pipeline_builder& hl7_pipeline_builder::add_validator(
    std::string_view name, stage_filter validator) {

    pipeline_stage stage;
    stage.id = "validator_" + std::to_string(stages_.size());
    stage.name = std::string(name);
    stage.processor = [validator = std::move(validator)](
        const hl7::hl7_message& msg) -> stage_result {
        if (validator(msg)) {
            return stage_result::ok();
        }
        return stage_result::error("Validation failed");
    };

    stages_.push_back(std::move(stage));
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::add_filter(
    std::string_view name, stage_filter filter) {

    pipeline_stage stage;
    stage.id = "filter_" + std::to_string(stages_.size());
    stage.name = std::string(name);
    stage.filter = std::move(filter);
    stage.processor = [](const hl7::hl7_message&) -> stage_result {
        return stage_result::ok();
    };

    stages_.push_back(std::move(stage));
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::add_transformer(
    std::string_view name, message_transformer transformer) {

    pipeline_stage stage;
    stage.id = "transformer_" + std::to_string(stages_.size());
    stage.name = std::string(name);
    stage.processor = [transformer = std::move(transformer)](
        const hl7::hl7_message& msg) -> stage_result {
        return stage_result::ok(transformer(msg));
    };

    stages_.push_back(std::move(stage));
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::add_processor(
    std::string_view name, stage_processor processor) {

    pipeline_stage stage;
    stage.id = "processor_" + std::to_string(stages_.size());
    stage.name = std::string(name);
    stage.processor = std::move(processor);

    stages_.push_back(std::move(stage));
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::add_optional(
    std::string_view name, stage_processor processor) {

    pipeline_stage stage;
    stage.id = "optional_" + std::to_string(stages_.size());
    stage.name = std::string(name);
    stage.processor = std::move(processor);
    stage.optional = true;

    stages_.push_back(std::move(stage));
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::with_retry(
    size_t max_retries, std::chrono::milliseconds delay) {

    if (!stages_.empty()) {
        stages_.back().max_retries = max_retries;
        stages_.back().retry_delay = delay;
    }
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::timeout(
    std::chrono::milliseconds timeout) {
    config_.timeout = timeout;
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::with_statistics(bool enable) {
    config_.enable_statistics = enable;
    return *this;
}

hl7_pipeline_builder& hl7_pipeline_builder::stop_on_error(bool stop) {
    config_.stop_on_error = stop;
    return *this;
}

hl7_pipeline hl7_pipeline_builder::build() {
    hl7_pipeline pipeline(config_);

    for (const auto& stage : stages_) {
        (void)pipeline.add_stage(stage);
    }

    return pipeline;
}

// =============================================================================
// Pipeline Stages Utilities
// =============================================================================

namespace pipeline_stages {

stage_processor create_logging_stage(
    std::string_view stage_name,
    std::function<void(std::string_view)> log_func) {

    std::string name(stage_name);

    return [name, log_func = std::move(log_func)](
        const hl7::hl7_message& msg) -> stage_result {

        std::string log_msg = "[" + name + "] Processing: " +
            std::string(msg.get_value("MSH.9.1")) + "^" +
            std::string(msg.trigger_event()) +
            " (ID: " + std::string(msg.control_id()) + ")";

        if (log_func) {
            log_func(log_msg);
        }

        return stage_result::ok();
    };
}

stage_processor create_validation_stage(
    stage_filter validator,
    std::string_view error_message) {

    std::string error_msg(error_message);

    return [validator = std::move(validator), error_msg](
        const hl7::hl7_message& msg) -> stage_result {

        if (validator(msg)) {
            return stage_result::ok();
        }
        return stage_result::error(error_msg);
    };
}

stage_processor create_enrichment_stage(
    std::function<void(hl7::hl7_message&)> enricher) {

    return [enricher = std::move(enricher)](
        const hl7::hl7_message& msg) -> stage_result {

        hl7::hl7_message enriched = msg;
        enricher(enriched);
        return stage_result::ok(std::move(enriched));
    };
}

stage_processor create_retry_stage(
    stage_processor processor,
    size_t max_retries,
    std::chrono::milliseconds retry_delay) {

    return [processor = std::move(processor), max_retries, retry_delay](
        const hl7::hl7_message& msg) -> stage_result {

        size_t attempts = 0;

        while (attempts <= max_retries) {
            auto result = processor(msg);

            if (result.success) {
                return result;
            }

            if (attempts < max_retries) {
                std::this_thread::sleep_for(retry_delay);
            }

            attempts++;
        }

        return stage_result::error("Max retries exceeded");
    };
}

stage_processor create_conditional_stage(
    stage_filter condition,
    stage_processor processor) {

    return [condition = std::move(condition),
            processor = std::move(processor)](
        const hl7::hl7_message& msg) -> stage_result {

        if (condition(msg)) {
            return processor(msg);
        }
        return stage_result::ok();
    };
}

stage_processor create_branching_stage(
    std::vector<std::pair<stage_filter, stage_processor>> branches,
    stage_processor default_processor) {

    return [branches = std::move(branches),
            default_processor = std::move(default_processor)](
        const hl7::hl7_message& msg) -> stage_result {

        for (const auto& [condition, processor] : branches) {
            if (condition(msg)) {
                return processor(msg);
            }
        }

        if (default_processor) {
            return default_processor(msg);
        }

        return stage_result::ok();
    };
}

}  // namespace pipeline_stages

}  // namespace pacs::bridge::messaging
