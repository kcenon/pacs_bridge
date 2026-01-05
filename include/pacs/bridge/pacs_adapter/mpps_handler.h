#ifndef PACS_BRIDGE_PACS_ADAPTER_MPPS_HANDLER_H
#define PACS_BRIDGE_PACS_ADAPTER_MPPS_HANDLER_H

/**
 * @file mpps_handler.h
 * @brief MPPS (Modality Performed Procedure Step) event handler for pacs_system integration
 *
 * Provides a handler implementation for receiving and processing MPPS events
 * from pacs_system. Supports registration as an MPPS event listener and
 * invokes callbacks when N-CREATE or N-SET operations occur.
 *
 * Features:
 *   - Register as MPPS event listener with pacs_system
 *   - Handle N-CREATE events (IN PROGRESS status)
 *   - Handle N-SET events (COMPLETED/DISCONTINUED status)
 *   - Extract timing information from MPPS datasets
 *   - Parse MPPS status and attributes
 *   - Thread-safe callback invocation
 *   - Persist MPPS records to database for recovery
 *   - Query MPPS records by various criteria
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/23
 * @see https://github.com/kcenon/pacs_bridge/issues/186
 * @see docs/reference_materials/06_ihe_swf_profile.md
 */

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// IExecutor interface for task execution (when available)
#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include <kcenon/common/interfaces/executor_interface.h>
#endif

namespace pacs::bridge::pacs_adapter {

// =============================================================================
// Error Codes (-970 to -979)
// =============================================================================

/**
 * @brief MPPS handler specific error codes
 *
 * Allocated range: -970 to -979
 */
enum class mpps_error : int {
    /** Cannot connect to pacs_system MPPS SCP */
    connection_failed = -970,

    /** Registration with MPPS SCP failed */
    registration_failed = -971,

    /** Invalid MPPS dataset received */
    invalid_dataset = -972,

    /** MPPS status parsing failed */
    status_parse_failed = -973,

    /** Missing required attribute in MPPS */
    missing_attribute = -974,

    /** Callback invocation failed */
    callback_failed = -975,

    /** Handler not registered */
    not_registered = -976,

    /** Handler already registered */
    already_registered = -977,

    /** Invalid MPPS SOP Instance UID */
    invalid_sop_instance = -978,

    /** Unexpected MPPS operation */
    unexpected_operation = -979,

    /** Database operation failed */
    database_error = -980,

    /** MPPS record not found in database */
    record_not_found = -981,

    /** Invalid state transition (e.g., updating final state) */
    invalid_state_transition = -982,

    /** Persistence is disabled */
    persistence_disabled = -983
};

/**
 * @brief Convert mpps_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(mpps_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of MPPS error
 */
[[nodiscard]] constexpr const char* to_string(mpps_error error) noexcept {
    switch (error) {
        case mpps_error::connection_failed:
            return "Cannot connect to pacs_system MPPS SCP";
        case mpps_error::registration_failed:
            return "Registration with MPPS SCP failed";
        case mpps_error::invalid_dataset:
            return "Invalid MPPS dataset received";
        case mpps_error::status_parse_failed:
            return "MPPS status parsing failed";
        case mpps_error::missing_attribute:
            return "Missing required attribute in MPPS";
        case mpps_error::callback_failed:
            return "Callback invocation failed";
        case mpps_error::not_registered:
            return "Handler not registered with MPPS SCP";
        case mpps_error::already_registered:
            return "Handler already registered with MPPS SCP";
        case mpps_error::invalid_sop_instance:
            return "Invalid MPPS SOP Instance UID";
        case mpps_error::unexpected_operation:
            return "Unexpected MPPS operation";
        case mpps_error::database_error:
            return "Database operation failed";
        case mpps_error::record_not_found:
            return "MPPS record not found in database";
        case mpps_error::invalid_state_transition:
            return "Invalid MPPS state transition";
        case mpps_error::persistence_disabled:
            return "MPPS persistence is disabled";
        default:
            return "Unknown MPPS error";
    }
}

// =============================================================================
// MPPS Event Types
// =============================================================================

/**
 * @brief MPPS event type indicating the procedure step status
 *
 * Maps to the Performed Procedure Step Status (0040,0252) values:
 *   - IN PROGRESS: Procedure has started
 *   - COMPLETED: Procedure completed successfully
 *   - DISCONTINUED: Procedure was stopped before completion
 */
enum class mpps_event {
    /** Procedure step started (N-CREATE with IN PROGRESS) */
    in_progress,

    /** Procedure step completed successfully (N-SET with COMPLETED) */
    completed,

    /** Procedure step discontinued/cancelled (N-SET with DISCONTINUED) */
    discontinued
};

/**
 * @brief Convert mpps_event to string representation
 */
[[nodiscard]] constexpr const char* to_string(mpps_event event) noexcept {
    switch (event) {
        case mpps_event::in_progress:
            return "IN PROGRESS";
        case mpps_event::completed:
            return "COMPLETED";
        case mpps_event::discontinued:
            return "DISCONTINUED";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Parse MPPS status string to event type
 *
 * @param status DICOM Performed Procedure Step Status value
 * @return Corresponding mpps_event or nullopt if unknown
 */
[[nodiscard]] inline std::optional<mpps_event>
parse_mpps_status(std::string_view status) noexcept {
    if (status == "IN PROGRESS") {
        return mpps_event::in_progress;
    }
    if (status == "COMPLETED") {
        return mpps_event::completed;
    }
    if (status == "DISCONTINUED") {
        return mpps_event::discontinued;
    }
    return std::nullopt;
}

// =============================================================================
// MPPS Data Structures
// =============================================================================

/**
 * @brief Performed series information from MPPS
 *
 * Corresponds to the Performed Series Sequence (0040,0340) item.
 */
struct mpps_performed_series {
    /** Series Instance UID (0020,000E) */
    std::string series_instance_uid;

    /** Series Description (0008,103E) */
    std::string series_description;

    /** Protocol Name (0018,1030) */
    std::string protocol_name;

    /** Modality (0008,0060) */
    std::string modality;

    /** Number of instances in series */
    size_t number_of_instances = 0;

    /** Performing Physician's Name (0008,1050) */
    std::string performing_physician;
};

/**
 * @brief MPPS dataset containing all relevant attributes
 *
 * Contains parsed attributes from MPPS N-CREATE or N-SET operations.
 * Includes patient, procedure, and timing information.
 */
struct mpps_dataset {
    // =========================================================================
    // SOP Instance Identification
    // =========================================================================

    /** MPPS SOP Instance UID (0008,0018) */
    std::string sop_instance_uid;

    // =========================================================================
    // Performed Procedure Step Relationship
    // =========================================================================

    /** Study Instance UID (0020,000D) */
    std::string study_instance_uid;

    /** Accession Number (0008,0050) */
    std::string accession_number;

    /** Scheduled Procedure Step ID (0040,0009) */
    std::string scheduled_procedure_step_id;

    /** Performed Procedure Step ID (0040,0253) */
    std::string performed_procedure_step_id;

    // =========================================================================
    // Patient Information
    // =========================================================================

    /** Patient ID (0010,0020) */
    std::string patient_id;

    /** Patient Name (0010,0010) */
    std::string patient_name;

    // =========================================================================
    // Procedure Step Status
    // =========================================================================

    /** Performed Procedure Step Status (0040,0252) */
    mpps_event status = mpps_event::in_progress;

    /** Performed Procedure Step Description (0040,0254) */
    std::string performed_procedure_description;

    // =========================================================================
    // Timing Information
    // =========================================================================

    /** Performed Procedure Step Start Date (0040,0244) - YYYYMMDD */
    std::string start_date;

    /** Performed Procedure Step Start Time (0040,0245) - HHMMSS */
    std::string start_time;

    /** Performed Procedure Step End Date (0040,0250) - YYYYMMDD */
    std::string end_date;

    /** Performed Procedure Step End Time (0040,0251) - HHMMSS */
    std::string end_time;

    // =========================================================================
    // Modality and Station
    // =========================================================================

    /** Modality (0008,0060) */
    std::string modality;

    /** Station AE Title (0040,0241) */
    std::string station_ae_title;

    /** Station Name (0008,1010) */
    std::string station_name;

    // =========================================================================
    // Performed Series
    // =========================================================================

    /** Performed Series Sequence (0040,0340) */
    std::vector<mpps_performed_series> performed_series;

    // =========================================================================
    // Additional Information
    // =========================================================================

    /** Referring Physician's Name (0008,0090) */
    std::string referring_physician;

    /** Requested Procedure ID (0040,1001) */
    std::string requested_procedure_id;

    /** Discontinuation Reason Code Sequence description (for discontinued) */
    std::string discontinuation_reason;

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Get total number of instances across all series
     */
    [[nodiscard]] size_t total_instances() const noexcept {
        size_t total = 0;
        for (const auto& series : performed_series) {
            total += series.number_of_instances;
        }
        return total;
    }

    /**
     * @brief Check if this MPPS represents a completed procedure
     */
    [[nodiscard]] bool is_completed() const noexcept {
        return status == mpps_event::completed;
    }

    /**
     * @brief Check if this MPPS represents a discontinued procedure
     */
    [[nodiscard]] bool is_discontinued() const noexcept {
        return status == mpps_event::discontinued;
    }

    /**
     * @brief Check if timing information is complete
     */
    [[nodiscard]] bool has_complete_timing() const noexcept {
        return !start_date.empty() && !start_time.empty() &&
               (status == mpps_event::in_progress ||
                (!end_date.empty() && !end_time.empty()));
    }
};

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief MPPS handler configuration
 *
 * Configuration for connecting to and receiving events from pacs_system's
 * MPPS SCP service.
 */
struct mpps_handler_config {
    /** pacs_system host address */
    std::string pacs_host = "localhost";

    /** pacs_system MPPS SCP port */
    uint16_t pacs_port = 11113;

    /** Our Application Entity title */
    std::string our_ae_title = "PACS_BRIDGE";

    /** pacs_system MPPS SCP AE title */
    std::string pacs_ae_title = "MPPS_SCP";

    /** Enable automatic reconnection on disconnection */
    bool auto_reconnect = true;

    /** Reconnection delay on disconnection */
    std::chrono::seconds reconnect_delay{5};

    /** Maximum reconnection attempts (0 = unlimited) */
    size_t max_reconnect_attempts = 0;

    /** Event processing timeout */
    std::chrono::seconds event_timeout{30};

    /** Enable verbose logging of MPPS events */
    bool verbose_logging = false;

    // =========================================================================
    // Persistence Options
    // =========================================================================

    /** Enable MPPS record persistence to database */
    bool enable_persistence = true;

    /** Database path for MPPS persistence (empty = use shared index_database) */
    std::string database_path;

    /** Recover pending MPPS records on startup */
    bool recover_on_startup = true;

    /** Maximum age for recovering pending MPPS (0 = no limit) */
    std::chrono::hours max_recovery_age{24};

    // =========================================================================
    // Executor Options (IExecutor integration)
    // =========================================================================

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    /** Optional executor for task execution (nullptr = use internal std::thread) */
    std::shared_ptr<kcenon::common::interfaces::IExecutor> executor;
#endif
};

// =============================================================================
// MPPS Handler
// =============================================================================

/**
 * @brief Callback type for MPPS events
 *
 * Invoked when an MPPS event (N-CREATE or N-SET) is received from pacs_system.
 *
 * @param event The type of MPPS event (in_progress, completed, discontinued)
 * @param mpps The parsed MPPS dataset containing all relevant attributes
 */
using mpps_callback = std::function<void(mpps_event event, const mpps_dataset& mpps)>;

/**
 * @brief MPPS event handler for pacs_system integration
 *
 * Receives MPPS N-CREATE and N-SET notifications from pacs_system and
 * triggers callbacks for downstream processing (e.g., generating HL7 messages).
 *
 * The handler registers as an MPPS event listener with pacs_system's MPPS SCP
 * service and receives notifications whenever a modality performs procedure
 * step operations.
 *
 * @example Basic Usage
 * ```cpp
 * mpps_handler_config config;
 * config.pacs_host = "pacs.hospital.local";
 * config.pacs_port = 11113;
 *
 * auto handler = mpps_handler::create(config);
 *
 * handler->set_callback([](mpps_event event, const mpps_dataset& mpps) {
 *     switch (event) {
 *         case mpps_event::in_progress:
 *             std::cout << "Procedure started: " << mpps.accession_number << "\n";
 *             break;
 *         case mpps_event::completed:
 *             std::cout << "Procedure completed: " << mpps.accession_number << "\n";
 *             // Generate ORU message...
 *             break;
 *         case mpps_event::discontinued:
 *             std::cout << "Procedure discontinued: " << mpps.accession_number << "\n";
 *             // Generate cancellation message...
 *             break;
 *     }
 * });
 *
 * auto result = handler->start();
 * if (!result) {
 *     std::cerr << "Failed to start: " << to_string(result.error()) << "\n";
 * }
 * ```
 *
 * @example With HL7 Message Generation
 * ```cpp
 * auto hl7_mapper = std::make_shared<mpps_hl7_mapper>(mapper_config);
 *
 * handler->set_callback([hl7_mapper](mpps_event event, const mpps_dataset& mpps) {
 *     auto hl7_result = hl7_mapper->map(event, mpps);
 *     if (hl7_result) {
 *         message_router.route(hl7_result.value());
 *     }
 * });
 * ```
 */
class mpps_handler {
public:
    /**
     * @brief Create an MPPS handler instance
     *
     * @param config Handler configuration
     * @return Unique pointer to the handler instance
     */
    [[nodiscard]] static std::unique_ptr<mpps_handler>
    create(const mpps_handler_config& config);

    /**
     * @brief Destructor - stops handler and releases resources
     */
    virtual ~mpps_handler() = default;

    // Non-copyable
    mpps_handler(const mpps_handler&) = delete;
    mpps_handler& operator=(const mpps_handler&) = delete;

    // Non-movable (due to potential callback references)
    mpps_handler(mpps_handler&&) = delete;
    mpps_handler& operator=(mpps_handler&&) = delete;

    // =========================================================================
    // Callback Management
    // =========================================================================

    /**
     * @brief Set the callback for MPPS events
     *
     * The callback will be invoked for each MPPS event received from
     * pacs_system. Only one callback can be registered at a time.
     *
     * @param callback The callback function to invoke on events
     */
    virtual void set_callback(mpps_callback callback) = 0;

    /**
     * @brief Clear the registered callback
     */
    virtual void clear_callback() = 0;

    /**
     * @brief Check if a callback is registered
     */
    [[nodiscard]] virtual bool has_callback() const noexcept = 0;

    // =========================================================================
    // Lifecycle Management
    // =========================================================================

    /**
     * @brief Start the MPPS handler
     *
     * Connects to pacs_system and registers as an MPPS event listener.
     * After successful start, the handler will receive MPPS notifications.
     *
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, mpps_error> start() = 0;

    /**
     * @brief Stop the MPPS handler
     *
     * Unregisters from pacs_system and stops receiving events.
     * Any pending events will be processed before stopping.
     *
     * @param graceful If true, wait for pending events to complete
     */
    virtual void stop(bool graceful = true) = 0;

    /**
     * @brief Check if the handler is running
     */
    [[nodiscard]] virtual bool is_running() const noexcept = 0;

    /**
     * @brief Check if connected to pacs_system
     */
    [[nodiscard]] virtual bool is_connected() const noexcept = 0;

    // =========================================================================
    // Event Handlers (for manual invocation or testing)
    // =========================================================================

    /**
     * @brief Handle an N-CREATE operation
     *
     * Called when an MPPS N-CREATE is received. Parses the dataset
     * and invokes the registered callback with an in_progress event.
     *
     * @param dataset The MPPS dataset from N-CREATE
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, mpps_error>
    on_n_create(const mpps_dataset& dataset) = 0;

    /**
     * @brief Handle an N-SET operation
     *
     * Called when an MPPS N-SET is received. Parses the dataset,
     * determines the status (completed/discontinued), and invokes
     * the registered callback.
     *
     * @param dataset The MPPS dataset from N-SET
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, mpps_error>
    on_n_set(const mpps_dataset& dataset) = 0;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Handler statistics
     */
    struct statistics {
        /** Total N-CREATE events received */
        size_t n_create_count = 0;

        /** Total N-SET events received */
        size_t n_set_count = 0;

        /** Events with IN PROGRESS status */
        size_t in_progress_count = 0;

        /** Events with COMPLETED status */
        size_t completed_count = 0;

        /** Events with DISCONTINUED status */
        size_t discontinued_count = 0;

        /** Events with parsing errors */
        size_t parse_error_count = 0;

        /** Callback invocation failures */
        size_t callback_error_count = 0;

        /** Connection attempts */
        size_t connect_attempts = 0;

        /** Successful connections */
        size_t connect_successes = 0;

        /** Reconnection count */
        size_t reconnections = 0;

        /** Last event timestamp */
        std::chrono::system_clock::time_point last_event_time;

        /** Handler uptime since last start */
        std::chrono::seconds uptime{0};
    };

    /**
     * @brief Get handler statistics
     */
    [[nodiscard]] virtual statistics get_statistics() const = 0;

    /**
     * @brief Reset statistics
     */
    virtual void reset_statistics() = 0;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] virtual const mpps_handler_config& config() const noexcept = 0;

    // =========================================================================
    // Persistence Operations
    // =========================================================================

    /**
     * @brief Query parameters for MPPS search
     */
    struct mpps_query_params {
        /** MPPS SOP Instance UID (exact match) */
        std::optional<std::string> sop_instance_uid;

        /** Status filter (exact match) */
        std::optional<mpps_event> status;

        /** Station AE Title filter (exact match) */
        std::optional<std::string> station_ae_title;

        /** Modality filter (exact match) */
        std::optional<std::string> modality;

        /** Study Instance UID filter (exact match) */
        std::optional<std::string> study_instance_uid;

        /** Accession number filter (exact match) */
        std::optional<std::string> accession_number;

        /** Maximum number of results to return (0 = unlimited) */
        size_t limit = 0;
    };

    /**
     * @brief Check if persistence is enabled and available
     *
     * @return true if MPPS records are being persisted to database
     */
    [[nodiscard]] virtual bool is_persistence_enabled() const noexcept = 0;

    /**
     * @brief Query MPPS record by SOP Instance UID
     *
     * Retrieves a persisted MPPS record from the database.
     *
     * @param sop_instance_uid The MPPS SOP Instance UID to look up
     * @return The MPPS dataset if found, nullopt otherwise, or error
     */
    [[nodiscard]] virtual std::expected<std::optional<mpps_dataset>, mpps_error>
    query_mpps(std::string_view sop_instance_uid) const = 0;

    /**
     * @brief Query MPPS records with filter criteria
     *
     * Retrieves persisted MPPS records matching the query parameters.
     *
     * @param params Query parameters for filtering
     * @return Vector of matching MPPS datasets, or error
     */
    [[nodiscard]] virtual std::expected<std::vector<mpps_dataset>, mpps_error>
    query_mpps(const mpps_query_params& params) const = 0;

    /**
     * @brief Get all active (IN PROGRESS) MPPS records
     *
     * Retrieves all MPPS records that are currently in progress.
     * Useful for recovery and monitoring.
     *
     * @return Vector of active MPPS datasets, or error
     */
    [[nodiscard]] virtual std::expected<std::vector<mpps_dataset>, mpps_error>
    get_active_mpps() const = 0;

    /**
     * @brief Get pending MPPS records for a station
     *
     * Retrieves all IN PROGRESS MPPS records for a specific station.
     *
     * @param station_ae_title The station AE Title to filter by
     * @return Vector of pending MPPS datasets for the station, or error
     */
    [[nodiscard]] virtual std::expected<std::vector<mpps_dataset>, mpps_error>
    get_pending_mpps_for_station(std::string_view station_ae_title) const = 0;

    /**
     * @brief Persistence statistics
     */
    struct persistence_stats {
        /** Total MPPS records persisted */
        size_t total_persisted = 0;

        /** Records with IN PROGRESS status */
        size_t in_progress_count = 0;

        /** Records with COMPLETED status */
        size_t completed_count = 0;

        /** Records with DISCONTINUED status */
        size_t discontinued_count = 0;

        /** Persistence failures */
        size_t persistence_failures = 0;

        /** Records recovered on startup */
        size_t recovered_count = 0;
    };

    /**
     * @brief Get persistence statistics
     */
    [[nodiscard]] virtual persistence_stats get_persistence_stats() const = 0;

protected:
    mpps_handler() = default;
};

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Validate an MPPS dataset for required attributes
 *
 * Checks that the dataset contains all required DICOM attributes
 * for proper MPPS processing.
 *
 * @param dataset The dataset to validate
 * @return Success if valid, or error describing the missing attribute
 */
[[nodiscard]] std::expected<void, mpps_error>
validate_mpps_dataset(const mpps_dataset& dataset);

/**
 * @brief Extract timing duration from MPPS start/end times
 *
 * Calculates the duration between start and end times if both are available.
 *
 * @param dataset The MPPS dataset with timing information
 * @return Duration in seconds, or nullopt if timing is incomplete
 */
[[nodiscard]] std::optional<std::chrono::seconds>
calculate_procedure_duration(const mpps_dataset& dataset);

}  // namespace pacs::bridge::pacs_adapter

#endif  // PACS_BRIDGE_PACS_ADAPTER_MPPS_HANDLER_H
