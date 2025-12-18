#ifndef PACS_BRIDGE_WORKFLOW_MPPS_HL7_WORKFLOW_H
#define PACS_BRIDGE_WORKFLOW_MPPS_HL7_WORKFLOW_H

/**
 * @file mpps_hl7_workflow.h
 * @brief MPPS to HL7 workflow coordinator
 *
 * Orchestrates the complete workflow from MPPS events to HL7 message delivery:
 *   1. Receive MPPS event from mpps_handler
 *   2. Map MPPS to HL7 ORM^O01 via dicom_hl7_mapper
 *   3. Route HL7 message via outbound_router
 *   4. On failure, enqueue to queue_manager for reliable delivery
 *
 * Features:
 *   - Destination selection based on message type and rules
 *   - Correlation and trace ID propagation
 *   - Automatic failover to queue-based delivery
 *   - Metrics collection for monitoring
 *   - Configurable retry policies
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/173
 * @see docs/reference_materials/07_dicom_hl7_mapping.md
 */

#include "pacs/bridge/mapping/dicom_hl7_mapper.h"
#include "pacs/bridge/pacs_adapter/mpps_handler.h"
#include "pacs/bridge/router/outbound_router.h"
#include "pacs/bridge/router/queue_manager.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::workflow {

// =============================================================================
// Error Codes (-900 to -909)
// =============================================================================

/**
 * @brief MPPS to HL7 workflow specific error codes
 *
 * Allocated range: -900 to -909
 */
enum class workflow_error : int {
    /** Workflow is not running */
    not_running = -900,

    /** Workflow is already running */
    already_running = -901,

    /** MPPS to HL7 mapping failed */
    mapping_failed = -902,

    /** Outbound delivery failed */
    delivery_failed = -903,

    /** Queue enqueue failed */
    enqueue_failed = -904,

    /** No destination configured for message type */
    no_destination = -905,

    /** Invalid workflow configuration */
    invalid_configuration = -906,

    /** Correlation ID generation failed */
    correlation_failed = -907,

    /** Destination selection failed */
    destination_selection_failed = -908,

    /** Component initialization failed */
    initialization_failed = -909
};

/**
 * @brief Convert workflow_error to error code
 */
[[nodiscard]] constexpr int to_error_code(workflow_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(workflow_error error) noexcept {
    switch (error) {
        case workflow_error::not_running:
            return "Workflow is not running";
        case workflow_error::already_running:
            return "Workflow is already running";
        case workflow_error::mapping_failed:
            return "MPPS to HL7 mapping failed";
        case workflow_error::delivery_failed:
            return "Outbound delivery failed";
        case workflow_error::enqueue_failed:
            return "Queue enqueue failed";
        case workflow_error::no_destination:
            return "No destination configured for message type";
        case workflow_error::invalid_configuration:
            return "Invalid workflow configuration";
        case workflow_error::correlation_failed:
            return "Correlation ID generation failed";
        case workflow_error::destination_selection_failed:
            return "Destination selection failed";
        case workflow_error::initialization_failed:
            return "Component initialization failed";
        default:
            return "Unknown workflow error";
    }
}

// =============================================================================
// Destination Selection Rules
// =============================================================================

/**
 * @brief Destination selection criteria
 */
enum class destination_criteria {
    /** Route by message type (default) */
    by_message_type,

    /** Route by modality */
    by_modality,

    /** Route by station AE title */
    by_station,

    /** Route by accession number pattern */
    by_accession_pattern,

    /** Custom rule-based routing */
    custom
};

/**
 * @brief Get string representation of criteria
 */
[[nodiscard]] constexpr const char* to_string(destination_criteria criteria) noexcept {
    switch (criteria) {
        case destination_criteria::by_message_type:
            return "by_message_type";
        case destination_criteria::by_modality:
            return "by_modality";
        case destination_criteria::by_station:
            return "by_station";
        case destination_criteria::by_accession_pattern:
            return "by_accession_pattern";
        case destination_criteria::custom:
            return "custom";
        default:
            return "unknown";
    }
}

/**
 * @brief Destination selection rule
 */
struct destination_rule {
    /** Rule name for identification */
    std::string name;

    /** Selection criteria type */
    destination_criteria criteria = destination_criteria::by_message_type;

    /** Pattern to match (message type, modality, etc.) */
    std::string pattern;

    /** Target destination name */
    std::string destination;

    /** Rule priority (lower = higher priority) */
    int priority = 100;

    /** Is rule enabled */
    bool enabled = true;
};

// =============================================================================
// Workflow Result
// =============================================================================

/**
 * @brief Delivery method used for message
 */
enum class delivery_method {
    /** Direct delivery via outbound_router */
    direct,

    /** Queued delivery via queue_manager */
    queued,

    /** Async delivery (fire-and-forget) */
    async
};

/**
 * @brief Get string representation of delivery method
 */
[[nodiscard]] constexpr const char* to_string(delivery_method method) noexcept {
    switch (method) {
        case delivery_method::direct:
            return "direct";
        case delivery_method::queued:
            return "queued";
        case delivery_method::async:
            return "async";
        default:
            return "unknown";
    }
}

/**
 * @brief Result of workflow execution
 */
struct workflow_result {
    /** Workflow execution was successful */
    bool success = false;

    /** Correlation ID for tracking */
    std::string correlation_id;

    /** Trace ID for distributed tracing */
    std::string trace_id;

    /** MPPS SOP Instance UID */
    std::string mpps_sop_instance_uid;

    /** Accession number */
    std::string accession_number;

    /** HL7 message control ID */
    std::string message_control_id;

    /** Destination that received the message */
    std::string destination;

    /** Delivery method used */
    delivery_method method = delivery_method::direct;

    /** Processing time in milliseconds */
    std::chrono::milliseconds processing_time{0};

    /** Error message if failed */
    std::string error_message;

    /** Timestamp of execution */
    std::chrono::system_clock::time_point timestamp;

    /**
     * @brief Create success result
     */
    [[nodiscard]] static workflow_result ok(
        std::string_view correlation_id,
        std::string_view destination,
        delivery_method method = delivery_method::direct);

    /**
     * @brief Create failure result
     */
    [[nodiscard]] static workflow_result error(
        std::string_view correlation_id,
        std::string_view error_message);
};

// =============================================================================
// Workflow Configuration
// =============================================================================

/**
 * @brief MPPS to HL7 workflow configuration
 */
struct mpps_hl7_workflow_config {
    /** Enable automatic queue fallback on delivery failure */
    bool enable_queue_fallback = true;

    /** Queue priority for fallback messages (lower = higher priority) */
    int fallback_queue_priority = 0;

    /** Generate unique correlation ID for each workflow execution */
    bool generate_correlation_id = true;

    /** Include trace ID from incoming context or generate new */
    bool enable_tracing = true;

    /** Enable metrics collection */
    bool enable_metrics = true;

    /** Destination selection rules */
    std::vector<destination_rule> routing_rules;

    /** Default destination if no rule matches */
    std::string default_destination;

    /** Processing timeout for workflow execution */
    std::chrono::milliseconds processing_timeout{30000};

    /** Enable async delivery mode */
    bool async_delivery = false;

    /** Number of async worker threads */
    size_t async_workers = 4;

    /** DICOM to HL7 mapper configuration */
    mapping::dicom_hl7_mapper_config mapper_config;

    /**
     * @brief Validate configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (default_destination.empty() && routing_rules.empty()) {
            return false;
        }
        return true;
    }
};

// =============================================================================
// Workflow Statistics
// =============================================================================

/**
 * @brief Workflow execution statistics
 */
struct workflow_statistics {
    /** Total MPPS events processed */
    size_t total_events = 0;

    /** Successfully processed events */
    size_t successful_events = 0;

    /** Failed events */
    size_t failed_events = 0;

    /** Events delivered directly */
    size_t direct_deliveries = 0;

    /** Events queued for later delivery */
    size_t queued_deliveries = 0;

    /** Mapping failures */
    size_t mapping_failures = 0;

    /** Delivery failures (before queue fallback) */
    size_t delivery_failures = 0;

    /** Queue enqueue failures */
    size_t enqueue_failures = 0;

    /** Events by MPPS status */
    size_t in_progress_events = 0;
    size_t completed_events = 0;
    size_t discontinued_events = 0;

    /** Average processing time in milliseconds */
    double avg_processing_time_ms = 0.0;

    /** Per-destination statistics */
    struct destination_stats {
        size_t messages_sent = 0;
        size_t messages_failed = 0;
        double avg_delivery_time_ms = 0.0;
    };
    std::unordered_map<std::string, destination_stats> destination_stats;

    /** Last event timestamp */
    std::chrono::system_clock::time_point last_event_time;
};

// =============================================================================
// MPPS to HL7 Workflow
// =============================================================================

/**
 * @brief MPPS to HL7 workflow coordinator
 *
 * Orchestrates the complete workflow from MPPS events to HL7 delivery.
 * Wires together mpps_handler, dicom_hl7_mapper, outbound_router, and
 * queue_manager to provide reliable end-to-end processing.
 *
 * @example Basic Usage
 * ```cpp
 * // Create components
 * auto mpps_handler = mpps_handler::create(mpps_config);
 * auto router = std::make_shared<outbound_router>(router_config);
 * auto queue = std::make_shared<queue_manager>(queue_config);
 *
 * // Configure workflow
 * mpps_hl7_workflow_config config;
 * config.default_destination = "HIS_PRIMARY";
 * config.enable_queue_fallback = true;
 *
 * // Create workflow
 * mpps_hl7_workflow workflow(config);
 * workflow.set_outbound_router(router);
 * workflow.set_queue_manager(queue);
 *
 * // Wire up and start
 * workflow.start();
 * workflow.wire_to_handler(*mpps_handler);
 * mpps_handler->start();
 * ```
 *
 * @example With Custom Routing Rules
 * ```cpp
 * mpps_hl7_workflow_config config;
 *
 * // Route CT modality to specific destination
 * destination_rule ct_rule;
 * ct_rule.name = "CT_ROUTE";
 * ct_rule.criteria = destination_criteria::by_modality;
 * ct_rule.pattern = "CT";
 * ct_rule.destination = "CT_HIS";
 * ct_rule.priority = 1;
 * config.routing_rules.push_back(ct_rule);
 *
 * // Default for other modalities
 * config.default_destination = "GENERAL_HIS";
 *
 * mpps_hl7_workflow workflow(config);
 * ```
 *
 * @example Manual Processing
 * ```cpp
 * mpps_hl7_workflow workflow(config);
 * workflow.start();
 *
 * // Process MPPS event manually
 * auto result = workflow.process(mpps_event::completed, mpps_data);
 * if (result) {
 *     std::cout << "Correlation: " << result->correlation_id << std::endl;
 *     std::cout << "Destination: " << result->destination << std::endl;
 * }
 * ```
 */
class mpps_hl7_workflow {
public:
    /**
     * @brief Custom destination selector function type
     *
     * Takes MPPS data and returns destination name.
     */
    using destination_selector =
        std::function<std::optional<std::string>(const pacs_adapter::mpps_dataset& mpps)>;

    /**
     * @brief Workflow completion callback
     */
    using completion_callback = std::function<void(const workflow_result& result)>;

    /**
     * @brief Default constructor with default configuration
     */
    mpps_hl7_workflow();

    /**
     * @brief Constructor with configuration
     */
    explicit mpps_hl7_workflow(const mpps_hl7_workflow_config& config);

    /**
     * @brief Destructor - stops workflow if running
     */
    ~mpps_hl7_workflow();

    // Non-copyable, movable
    mpps_hl7_workflow(const mpps_hl7_workflow&) = delete;
    mpps_hl7_workflow& operator=(const mpps_hl7_workflow&) = delete;
    mpps_hl7_workflow(mpps_hl7_workflow&&) noexcept;
    mpps_hl7_workflow& operator=(mpps_hl7_workflow&&) noexcept;

    // =========================================================================
    // Component Wiring
    // =========================================================================

    /**
     * @brief Set the outbound router for direct delivery
     *
     * @param router Shared pointer to outbound router
     */
    void set_outbound_router(std::shared_ptr<router::outbound_router> router);

    /**
     * @brief Set the queue manager for fallback delivery
     *
     * @param queue Shared pointer to queue manager
     */
    void set_queue_manager(std::shared_ptr<router::queue_manager> queue);

    /**
     * @brief Set custom destination selector
     *
     * @param selector Custom selector function
     */
    void set_destination_selector(destination_selector selector);

    /**
     * @brief Wire this workflow as callback to MPPS handler
     *
     * Registers this workflow's process method as the MPPS event callback.
     *
     * @param handler MPPS handler to wire to
     */
    void wire_to_handler(pacs_adapter::mpps_handler& handler);

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Start the workflow
     *
     * Initializes internal components and prepares for event processing.
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, workflow_error> start();

    /**
     * @brief Stop the workflow
     *
     * Stops async workers and flushes pending operations.
     */
    void stop();

    /**
     * @brief Check if workflow is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Event Processing
    // =========================================================================

    /**
     * @brief Process an MPPS event
     *
     * Main entry point for workflow execution. Maps MPPS to HL7 and
     * delivers to configured destination.
     *
     * @param event MPPS event type
     * @param mpps MPPS dataset
     * @return Workflow result or error
     */
    [[nodiscard]] std::expected<workflow_result, workflow_error>
    process(pacs_adapter::mpps_event event, const pacs_adapter::mpps_dataset& mpps);

    /**
     * @brief Process an MPPS event with custom correlation ID
     *
     * @param event MPPS event type
     * @param mpps MPPS dataset
     * @param correlation_id Custom correlation ID
     * @return Workflow result or error
     */
    [[nodiscard]] std::expected<workflow_result, workflow_error>
    process(pacs_adapter::mpps_event event,
            const pacs_adapter::mpps_dataset& mpps,
            std::string_view correlation_id);

    /**
     * @brief Process an MPPS event asynchronously
     *
     * @param event MPPS event type
     * @param mpps MPPS dataset
     * @param callback Callback invoked when processing completes
     * @return Success if queued, error if failed
     */
    [[nodiscard]] std::expected<void, workflow_error>
    process_async(pacs_adapter::mpps_event event,
                  const pacs_adapter::mpps_dataset& mpps,
                  completion_callback callback = nullptr);

    // =========================================================================
    // Destination Selection
    // =========================================================================

    /**
     * @brief Select destination for MPPS data
     *
     * Evaluates routing rules and returns matching destination.
     *
     * @param mpps MPPS dataset
     * @return Destination name or nullopt if no match
     */
    [[nodiscard]] std::optional<std::string>
    select_destination(const pacs_adapter::mpps_dataset& mpps) const;

    /**
     * @brief Add a routing rule
     *
     * @param rule Routing rule to add
     */
    void add_routing_rule(const destination_rule& rule);

    /**
     * @brief Remove a routing rule by name
     *
     * @param name Rule name
     * @return true if removed
     */
    bool remove_routing_rule(std::string_view name);

    /**
     * @brief Get all routing rules
     */
    [[nodiscard]] std::vector<destination_rule> routing_rules() const;

    // =========================================================================
    // Correlation and Tracing
    // =========================================================================

    /**
     * @brief Generate a new correlation ID
     *
     * @return Unique correlation ID
     */
    [[nodiscard]] std::string generate_correlation_id() const;

    /**
     * @brief Generate a new trace ID
     *
     * @return Unique trace ID compatible with OpenTelemetry
     */
    [[nodiscard]] std::string generate_trace_id() const;

    // =========================================================================
    // Callbacks
    // =========================================================================

    /**
     * @brief Set callback for workflow completion
     *
     * @param callback Callback invoked after each workflow execution
     */
    void set_completion_callback(completion_callback callback);

    /**
     * @brief Clear completion callback
     */
    void clear_completion_callback();

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Get workflow statistics
     */
    [[nodiscard]] workflow_statistics get_statistics() const;

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
    [[nodiscard]] const mpps_hl7_workflow_config& config() const noexcept;

    /**
     * @brief Update configuration
     *
     * Note: Some changes may require restart to take effect.
     */
    void set_config(const mpps_hl7_workflow_config& config);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Workflow Config Builder (Fluent API)
// =============================================================================

/**
 * @brief Fluent builder for workflow configuration
 *
 * @example
 * ```cpp
 * auto config = workflow_config_builder::create()
 *     .default_destination("HIS_PRIMARY")
 *     .enable_queue_fallback(true)
 *     .add_rule(destination_rule{"CT_ROUTE", destination_criteria::by_modality, "CT", "CT_HIS"})
 *     .add_rule(destination_rule{"MR_ROUTE", destination_criteria::by_modality, "MR", "MR_HIS"})
 *     .enable_tracing(true)
 *     .enable_metrics(true)
 *     .build();
 * ```
 */
class workflow_config_builder {
public:
    /**
     * @brief Create new builder with defaults
     */
    [[nodiscard]] static workflow_config_builder create();

    /** Set default destination */
    workflow_config_builder& default_destination(std::string_view dest);

    /** Enable/disable queue fallback */
    workflow_config_builder& enable_queue_fallback(bool enable = true);

    /** Set fallback queue priority */
    workflow_config_builder& fallback_priority(int priority);

    /** Enable/disable correlation ID generation */
    workflow_config_builder& generate_correlation_id(bool enable = true);

    /** Enable/disable tracing */
    workflow_config_builder& enable_tracing(bool enable = true);

    /** Enable/disable metrics */
    workflow_config_builder& enable_metrics(bool enable = true);

    /** Add a routing rule */
    workflow_config_builder& add_rule(const destination_rule& rule);

    /** Set processing timeout */
    workflow_config_builder& processing_timeout(std::chrono::milliseconds timeout);

    /** Enable async delivery */
    workflow_config_builder& async_delivery(bool enable = true, size_t workers = 4);

    /** Set mapper configuration */
    workflow_config_builder& mapper_config(const mapping::dicom_hl7_mapper_config& config);

    /** Build the configuration */
    [[nodiscard]] mpps_hl7_workflow_config build() const;

private:
    workflow_config_builder();
    mpps_hl7_workflow_config config_;
};

}  // namespace pacs::bridge::workflow

#endif  // PACS_BRIDGE_WORKFLOW_MPPS_HL7_WORKFLOW_H
