#ifndef PACS_BRIDGE_MESSAGING_HL7_PIPELINE_H
#define PACS_BRIDGE_MESSAGING_HL7_PIPELINE_H

/**
 * @file hl7_pipeline.h
 * @brief HL7 message processing pipeline
 *
 * Provides a configurable message processing pipeline for HL7 messages.
 * Supports sequential processing stages with:
 *   - Parse, validate, route, map, send stages
 *   - Error handling and recovery
 *   - Stage metrics and logging
 *   - Conditional stage execution
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 * @see https://github.com/kcenon/pacs_bridge/issues/155
 */

#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::messaging {

// =============================================================================
// Pipeline Error Codes (-820 to -829)
// =============================================================================

/**
 * @brief Pipeline specific error codes
 *
 * Allocated range: -820 to -829
 */
enum class pipeline_error : int {
    /** Pipeline not started */
    not_started = -820,

    /** Stage processing failed */
    stage_failed = -821,

    /** Invalid stage configuration */
    invalid_stage = -822,

    /** Stage not found */
    stage_not_found = -823,

    /** Pipeline execution timeout */
    timeout = -824,

    /** Message transformation failed */
    transform_failed = -825,

    /** Stage filter rejected message */
    filtered = -826,

    /** Maximum retries exceeded */
    max_retries_exceeded = -827,

    /** Pipeline already running */
    already_running = -828
};

/**
 * @brief Convert pipeline_error to error code
 */
[[nodiscard]] constexpr int to_error_code(pipeline_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(pipeline_error error) noexcept {
    switch (error) {
        case pipeline_error::not_started:
            return "Pipeline not started";
        case pipeline_error::stage_failed:
            return "Stage processing failed";
        case pipeline_error::invalid_stage:
            return "Invalid stage configuration";
        case pipeline_error::stage_not_found:
            return "Stage not found";
        case pipeline_error::timeout:
            return "Pipeline execution timeout";
        case pipeline_error::transform_failed:
            return "Message transformation failed";
        case pipeline_error::filtered:
            return "Message was filtered out";
        case pipeline_error::max_retries_exceeded:
            return "Maximum retry attempts exceeded";
        case pipeline_error::already_running:
            return "Pipeline is already running";
        default:
            return "Unknown pipeline error";
    }
}

// =============================================================================
// Pipeline Stage Types
// =============================================================================

/**
 * @brief Result of a pipeline stage
 */
struct stage_result {
    /** Stage completed successfully */
    bool success = true;

    /** Transformed message (if any) */
    std::optional<hl7::hl7_message> message;

    /** Error message if not successful */
    std::string error_message;

    /** Skip remaining stages */
    bool skip_remaining = false;

    /** Processing time for this stage */
    std::chrono::microseconds processing_time{0};

    /**
     * @brief Create success result
     */
    [[nodiscard]] static stage_result ok() {
        return {true, std::nullopt, {}, false, {}};
    }

    /**
     * @brief Create success result with transformed message
     */
    [[nodiscard]] static stage_result ok(hl7::hl7_message msg) {
        return {true, std::move(msg), {}, false, {}};
    }

    /**
     * @brief Create error result
     */
    [[nodiscard]] static stage_result error(std::string_view msg) {
        return {false, std::nullopt, std::string(msg), false, {}};
    }

    /**
     * @brief Create skip result
     */
    [[nodiscard]] static stage_result skip(std::string_view reason = "") {
        return {true, std::nullopt, std::string(reason), true, {}};
    }
};

/**
 * @brief Stage processor function type
 */
using stage_processor = std::function<stage_result(const hl7::hl7_message& message)>;

/**
 * @brief Stage filter function type - returns true to continue processing
 */
using stage_filter = std::function<bool(const hl7::hl7_message& message)>;

/**
 * @brief Message transformer function type
 */
using message_transformer = std::function<hl7::hl7_message(const hl7::hl7_message& message)>;

// =============================================================================
// Pipeline Stage Definition
// =============================================================================

/**
 * @brief Pipeline stage configuration
 */
struct pipeline_stage {
    /** Stage identifier */
    std::string id;

    /** Stage name for logging */
    std::string name;

    /** Stage processor function */
    stage_processor processor;

    /** Optional pre-filter */
    stage_filter filter;

    /** Is stage optional (failure doesn't stop pipeline) */
    bool optional = false;

    /** Enable stage (default: true) */
    bool enabled = true;

    /** Maximum retry count for this stage */
    size_t max_retries = 0;

    /** Retry delay */
    std::chrono::milliseconds retry_delay{100};
};

// =============================================================================
// Pipeline Configuration
// =============================================================================

/**
 * @brief Pipeline configuration
 */
struct pipeline_config {
    /** Pipeline name */
    std::string name = "hl7_pipeline";

    /** Input topic for message bus integration */
    std::string input_topic;

    /** Output topic for processed messages */
    std::string output_topic;

    /** Enable statistics collection */
    bool enable_statistics = true;

    /** Enable detailed logging */
    bool enable_logging = true;

    /** Pipeline execution timeout */
    std::chrono::milliseconds timeout{30000};

    /** Stop on first error */
    bool stop_on_error = true;
};

// =============================================================================
// HL7 Pipeline
// =============================================================================

/**
 * @brief HL7 message processing pipeline
 *
 * Processes HL7 messages through a series of configurable stages.
 * Common stages include:
 *   - Parse: Convert raw data to HL7 message
 *   - Validate: Verify message structure and content
 *   - Route: Determine message destination
 *   - Map: Transform HL7 to DICOM or other formats
 *   - Send: Deliver to target system
 *
 * @example Basic Usage
 * ```cpp
 * hl7_pipeline pipeline;
 *
 * // Add stages
 * pipeline.add_stage({
 *     .id = "validate",
 *     .name = "Validate Message",
 *     .processor = [](const hl7::hl7_message& msg) {
 *         if (!msg.has_segment("MSH")) {
 *             return stage_result::error("Missing MSH segment");
 *         }
 *         return stage_result::ok();
 *     }
 * });
 *
 * pipeline.add_stage({
 *     .id = "route",
 *     .name = "Route Message",
 *     .processor = [&router](const hl7::hl7_message& msg) {
 *         auto result = router.route(msg);
 *         if (!result) {
 *             return stage_result::error("Routing failed");
 *         }
 *         return stage_result::ok();
 *     }
 * });
 *
 * // Process message
 * auto result = pipeline.process(message);
 * ```
 *
 * @example Using Pipeline Builder
 * ```cpp
 * auto pipeline = hl7_pipeline_builder::create("adt_pipeline")
 *     .add_validator([](const hl7::hl7_message& msg) {
 *         return msg.has_segment("PID");
 *     })
 *     .add_transformer("enrich", [](const hl7::hl7_message& msg) {
 *         auto enriched = msg;
 *         enriched.set_value("ZPI.1", "ENRICHED");
 *         return enriched;
 *     })
 *     .add_processor("send", send_to_pacs)
 *     .build();
 * ```
 */
class hl7_pipeline {
public:
    /**
     * @brief Default constructor
     */
    hl7_pipeline();

    /**
     * @brief Constructor with configuration
     */
    explicit hl7_pipeline(const pipeline_config& config);

    /**
     * @brief Destructor
     */
    ~hl7_pipeline();

    // Non-copyable, movable
    hl7_pipeline(const hl7_pipeline&) = delete;
    hl7_pipeline& operator=(const hl7_pipeline&) = delete;
    hl7_pipeline(hl7_pipeline&&) noexcept;
    hl7_pipeline& operator=(hl7_pipeline&&) noexcept;

    // =========================================================================
    // Stage Management
    // =========================================================================

    /**
     * @brief Add a stage to the pipeline
     *
     * @param stage Stage configuration
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, pipeline_error> add_stage(
        const pipeline_stage& stage);

    /**
     * @brief Add a simple processor stage
     *
     * @param id Stage identifier
     * @param name Stage name
     * @param processor Processing function
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, pipeline_error> add_stage(
        std::string_view id,
        std::string_view name,
        stage_processor processor);

    /**
     * @brief Remove a stage
     *
     * @param stage_id Stage identifier
     * @return true if removed
     */
    bool remove_stage(std::string_view stage_id);

    /**
     * @brief Enable or disable a stage
     *
     * @param stage_id Stage identifier
     * @param enabled Enable state
     */
    void set_stage_enabled(std::string_view stage_id, bool enabled);

    /**
     * @brief Get stage by ID
     */
    [[nodiscard]] const pipeline_stage* get_stage(std::string_view stage_id) const;

    /**
     * @brief Get number of stages
     */
    [[nodiscard]] size_t stage_count() const noexcept;

    /**
     * @brief Get ordered list of stage names
     */
    [[nodiscard]] std::vector<std::string> stage_names() const;

    /**
     * @brief Clear all stages
     */
    void clear_stages();

    // =========================================================================
    // Processing
    // =========================================================================

    /**
     * @brief Process a message through the pipeline
     *
     * @param message HL7 message to process
     * @return Processed message or error
     */
    [[nodiscard]] std::expected<hl7::hl7_message, pipeline_error> process(
        const hl7::hl7_message& message);

    /**
     * @brief Process raw HL7 data through the pipeline
     *
     * Automatically parses the raw data before processing.
     *
     * @param raw_data Raw HL7 message data
     * @return Processed message or error
     */
    [[nodiscard]] std::expected<hl7::hl7_message, pipeline_error> process(
        std::string_view raw_data);

    // =========================================================================
    // Message Bus Integration
    // =========================================================================

    /**
     * @brief Start pipeline with message bus integration
     *
     * Automatically subscribes to input topic and publishes to output topic.
     *
     * @param bus Message bus to integrate with
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, pipeline_error> start(
        std::shared_ptr<class hl7_message_bus> bus);

    /**
     * @brief Stop message bus integration
     */
    void stop();

    /**
     * @brief Check if pipeline is running with message bus
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Pipeline statistics
     */
    struct statistics {
        /** Total messages processed */
        uint64_t messages_processed = 0;

        /** Messages successfully completed */
        uint64_t messages_succeeded = 0;

        /** Messages that failed */
        uint64_t messages_failed = 0;

        /** Messages filtered out */
        uint64_t messages_filtered = 0;

        /** Per-stage statistics */
        struct stage_stats {
            std::string stage_id;
            uint64_t invocations = 0;
            uint64_t successes = 0;
            uint64_t failures = 0;
            double avg_time_us = 0.0;
        };
        std::vector<stage_stats> stage_statistics;

        /** Average total pipeline time in microseconds */
        double avg_pipeline_time_us = 0.0;
    };

    /**
     * @brief Get pipeline statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const pipeline_config& config() const noexcept;

    /**
     * @brief Set configuration
     */
    void set_config(const pipeline_config& config);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Pipeline Builder
// =============================================================================

/**
 * @brief Fluent builder for HL7 pipeline construction
 *
 * @example
 * ```cpp
 * auto pipeline = hl7_pipeline_builder::create("order_pipeline")
 *     .from_topic("hl7.orm.*")
 *     .to_topic("hl7.processed.orm")
 *     .add_validator([](const auto& msg) {
 *         return msg.has_segment("ORC");
 *     })
 *     .add_processor("extract_order", extract_order_data)
 *     .add_processor("update_mwl", update_worklist)
 *     .with_retry(3, std::chrono::milliseconds{500})
 *     .build();
 * ```
 */
class hl7_pipeline_builder {
public:
    /**
     * @brief Create a new pipeline builder
     *
     * @param name Pipeline name
     */
    [[nodiscard]] static hl7_pipeline_builder create(std::string_view name);

    /**
     * @brief Set input topic for message bus integration
     */
    hl7_pipeline_builder& from_topic(std::string_view topic);

    /**
     * @brief Set output topic for message bus integration
     */
    hl7_pipeline_builder& to_topic(std::string_view topic);

    /**
     * @brief Add a validator stage
     *
     * @param validator Function that returns true if message is valid
     */
    hl7_pipeline_builder& add_validator(stage_filter validator);

    /**
     * @brief Add a named validator stage
     */
    hl7_pipeline_builder& add_validator(std::string_view name, stage_filter validator);

    /**
     * @brief Add a filter stage
     *
     * Messages that don't pass the filter are skipped (not failed).
     */
    hl7_pipeline_builder& add_filter(std::string_view name, stage_filter filter);

    /**
     * @brief Add a transformer stage
     *
     * @param name Stage name
     * @param transformer Function to transform the message
     */
    hl7_pipeline_builder& add_transformer(std::string_view name,
                                           message_transformer transformer);

    /**
     * @brief Add a processor stage
     *
     * @param name Stage name
     * @param processor Processing function
     */
    hl7_pipeline_builder& add_processor(std::string_view name,
                                         stage_processor processor);

    /**
     * @brief Add an optional stage
     *
     * Stage failure won't stop the pipeline.
     */
    hl7_pipeline_builder& add_optional(std::string_view name,
                                        stage_processor processor);

    /**
     * @brief Configure retry for the last added stage
     *
     * @param max_retries Maximum retry attempts
     * @param delay Delay between retries
     */
    hl7_pipeline_builder& with_retry(size_t max_retries,
                                      std::chrono::milliseconds delay);

    /**
     * @brief Set pipeline timeout
     */
    hl7_pipeline_builder& timeout(std::chrono::milliseconds timeout);

    /**
     * @brief Enable statistics collection
     */
    hl7_pipeline_builder& with_statistics(bool enable = true);

    /**
     * @brief Stop pipeline on first error
     */
    hl7_pipeline_builder& stop_on_error(bool stop = true);

    /**
     * @brief Build the pipeline
     *
     * @return Configured pipeline
     */
    [[nodiscard]] hl7_pipeline build();

private:
    explicit hl7_pipeline_builder(std::string_view name);

    pipeline_config config_;
    std::vector<pipeline_stage> stages_;
};

// =============================================================================
// Standard Pipeline Stages
// =============================================================================

/**
 * @brief Pre-built pipeline stages for common operations
 */
namespace pipeline_stages {

/**
 * @brief Create a logging stage
 *
 * Logs message details without modifying the message.
 *
 * @param stage_name Name for logging
 * @param log_func Logging function
 * @return Stage processor
 */
[[nodiscard]] stage_processor create_logging_stage(
    std::string_view stage_name,
    std::function<void(std::string_view)> log_func = nullptr);

/**
 * @brief Create a validation stage
 *
 * @param validator Validation function
 * @param error_message Error message on validation failure
 * @return Stage processor
 */
[[nodiscard]] stage_processor create_validation_stage(
    stage_filter validator,
    std::string_view error_message = "Validation failed");

/**
 * @brief Create an enrichment stage
 *
 * Modifies the message in place.
 *
 * @param enricher Function to enrich the message
 * @return Stage processor
 */
[[nodiscard]] stage_processor create_enrichment_stage(
    std::function<void(hl7::hl7_message&)> enricher);

/**
 * @brief Create a retry wrapper stage
 *
 * @param processor Original processor
 * @param max_retries Maximum retry attempts
 * @param retry_delay Delay between retries
 * @return Stage processor with retry logic
 */
[[nodiscard]] stage_processor create_retry_stage(
    stage_processor processor,
    size_t max_retries = 3,
    std::chrono::milliseconds retry_delay = std::chrono::milliseconds{100});

/**
 * @brief Create a conditional stage
 *
 * Only executes processor if condition is met.
 *
 * @param condition Condition to check
 * @param processor Processor to execute if condition is true
 * @return Stage processor
 */
[[nodiscard]] stage_processor create_conditional_stage(
    stage_filter condition,
    stage_processor processor);

/**
 * @brief Create a branching stage
 *
 * Executes different processors based on conditions.
 *
 * @param branches Vector of (condition, processor) pairs
 * @param default_processor Default processor if no condition matches
 * @return Stage processor
 */
[[nodiscard]] stage_processor create_branching_stage(
    std::vector<std::pair<stage_filter, stage_processor>> branches,
    stage_processor default_processor = nullptr);

}  // namespace pipeline_stages

}  // namespace pacs::bridge::messaging

#endif  // PACS_BRIDGE_MESSAGING_HL7_PIPELINE_H
