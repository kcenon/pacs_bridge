#ifndef PACS_BRIDGE_PACS_ADAPTER_MWL_CLIENT_H
#define PACS_BRIDGE_PACS_ADAPTER_MWL_CLIENT_H

/**
 * @file mwl_client.h
 * @brief Modality Worklist client for pacs_system integration
 *
 * Provides a client implementation for managing Modality Worklist (MWL)
 * entries in pacs_system. Supports DICOM-based communication with the
 * worklist_scp service for creating, updating, querying, and canceling
 * scheduled procedure steps.
 *
 * Features:
 *   - Add new worklist entries from HL7 orders
 *   - Update existing entries by accession number
 *   - Cancel (remove) entries on order cancellation
 *   - Query entries with flexible criteria
 *   - Connection pooling for performance
 *   - Automatic reconnection on failure
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/17
 * @see docs/reference_materials/05_mwl_mapping.md
 */

#include "pacs/bridge/mapping/hl7_dicom_mapper.h"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::pacs_adapter {

// =============================================================================
// Error Codes (-980 to -989)
// =============================================================================

/**
 * @brief MWL client specific error codes
 *
 * Allocated range: -980 to -989
 */
enum class mwl_error : int {
    /** Cannot connect to pacs_system */
    connection_failed = -980,

    /** MWL add operation failed */
    add_failed = -981,

    /** MWL update operation failed */
    update_failed = -982,

    /** MWL cancel operation failed */
    cancel_failed = -983,

    /** MWL query operation failed */
    query_failed = -984,

    /** Entry not found */
    entry_not_found = -985,

    /** Duplicate entry exists */
    duplicate_entry = -986,

    /** Invalid MWL data */
    invalid_data = -987,

    /** Connection timeout */
    timeout = -988,

    /** DICOM association rejected */
    association_rejected = -989
};

/**
 * @brief Convert mwl_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(mwl_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of MWL error
 */
[[nodiscard]] constexpr const char* to_string(mwl_error error) noexcept {
    switch (error) {
        case mwl_error::connection_failed:
            return "Cannot connect to pacs_system";
        case mwl_error::add_failed:
            return "MWL add operation failed";
        case mwl_error::update_failed:
            return "MWL update operation failed";
        case mwl_error::cancel_failed:
            return "MWL cancel operation failed";
        case mwl_error::query_failed:
            return "MWL query operation failed";
        case mwl_error::entry_not_found:
            return "MWL entry not found";
        case mwl_error::duplicate_entry:
            return "Duplicate MWL entry exists";
        case mwl_error::invalid_data:
            return "Invalid MWL data";
        case mwl_error::timeout:
            return "Connection timeout";
        case mwl_error::association_rejected:
            return "DICOM association rejected";
        default:
            return "Unknown MWL error";
    }
}

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief MWL client configuration
 *
 * Configuration for connecting to pacs_system's worklist_scp service.
 */
struct mwl_client_config {
    /** pacs_system host address */
    std::string pacs_host = "localhost";

    /** pacs_system worklist port */
    uint16_t pacs_port = 11112;

    /** Our Application Entity title */
    std::string our_ae_title = "PACS_BRIDGE";

    /** pacs_system AE title */
    std::string pacs_ae_title = "PACS_SCP";

    /** Connection timeout */
    std::chrono::seconds connect_timeout{10};

    /** Operation timeout (for queries, etc.) */
    std::chrono::seconds operation_timeout{30};

    /** Maximum retry attempts on failure */
    size_t max_retries = 3;

    /** Delay between retry attempts */
    std::chrono::milliseconds retry_delay{1000};

    /** Enable connection keep-alive */
    bool keep_alive = true;

    /** Keep-alive ping interval */
    std::chrono::seconds keep_alive_interval{30};
};

// =============================================================================
// Query Filter
// =============================================================================

/**
 * @brief MWL query filter criteria
 *
 * Used to filter MWL query results. Empty fields are not used in filtering.
 */
struct mwl_query_filter {
    /** Filter by patient ID */
    std::optional<std::string> patient_id;

    /** Filter by accession number */
    std::optional<std::string> accession_number;

    /** Filter by scheduled date (YYYYMMDD) */
    std::optional<std::string> scheduled_date;

    /** Filter by scheduled date range start (YYYYMMDD) */
    std::optional<std::string> scheduled_date_from;

    /** Filter by scheduled date range end (YYYYMMDD) */
    std::optional<std::string> scheduled_date_to;

    /** Filter by modality (CT, MR, US, etc.) */
    std::optional<std::string> modality;

    /** Filter by scheduled station AE title */
    std::optional<std::string> scheduled_station_ae;

    /** Filter by referring physician name */
    std::optional<std::string> referring_physician;

    /** Filter by patient name (supports wildcards) */
    std::optional<std::string> patient_name;

    /** Filter by scheduled procedure step status */
    std::optional<std::string> sps_status;

    /** Maximum number of results to return (0 = unlimited) */
    size_t max_results = 0;
};

// =============================================================================
// MWL Client
// =============================================================================

/**
 * @brief Modality Worklist client for pacs_system integration
 *
 * Manages MWL entries in pacs_system's worklist_scp service. Provides
 * operations to add, update, query, and cancel worklist entries.
 *
 * @example Basic Usage
 * ```cpp
 * mwl_client_config config;
 * config.pacs_host = "pacs.hospital.local";
 * config.pacs_port = 11112;
 *
 * mwl_client client(config);
 *
 * auto connect_result = client.connect();
 * if (connect_result) {
 *     // Add a new MWL entry
 *     mapping::mwl_item item;
 *     item.patient.patient_id = "PAT001";
 *     item.patient.patient_name = "DOE^JOHN";
 *     item.imaging_service_request.accession_number = "ACC001";
 *
 *     auto add_result = client.add_entry(item);
 *     if (add_result) {
 *         std::cout << "Entry added successfully" << std::endl;
 *     }
 * }
 * ```
 *
 * @example Query with Filter
 * ```cpp
 * mwl_query_filter filter;
 * filter.modality = "CT";
 * filter.scheduled_date = "20241201";
 *
 * auto results = client.query(filter);
 * if (results) {
 *     for (const auto& item : results.value()) {
 *         std::cout << "Found: " << item.patient.patient_name << std::endl;
 *     }
 * }
 * ```
 */
class mwl_client {
public:
    /**
     * @brief Operation result with timing information
     */
    struct operation_result {
        /** Operation execution time */
        std::chrono::milliseconds elapsed_time;

        /** Number of retry attempts needed */
        size_t retry_count = 0;

        /** DICOM status code (if applicable) */
        uint16_t dicom_status = 0;
    };

    /**
     * @brief Query result containing items and metadata
     */
    struct query_result {
        /** Matching MWL items */
        std::vector<mapping::mwl_item> items;

        /** Query execution time */
        std::chrono::milliseconds elapsed_time;

        /** Whether more results are available (pagination) */
        bool has_more = false;

        /** Total matching count (if known) */
        std::optional<size_t> total_count;
    };

    /**
     * @brief Constructor
     * @param config Client configuration
     */
    explicit mwl_client(const mwl_client_config& config);

    /**
     * @brief Destructor - closes connection
     */
    ~mwl_client();

    // Non-copyable
    mwl_client(const mwl_client&) = delete;
    mwl_client& operator=(const mwl_client&) = delete;

    // Movable
    mwl_client(mwl_client&&) noexcept;
    mwl_client& operator=(mwl_client&&) noexcept;

    // =========================================================================
    // Connection Management
    // =========================================================================

    /**
     * @brief Establish connection to pacs_system
     *
     * Connects to the configured pacs_system host and establishes
     * a DICOM association with the worklist SCP.
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, mwl_error> connect();

    /**
     * @brief Close the connection
     *
     * @param graceful If true, send association release before closing
     */
    void disconnect(bool graceful = true);

    /**
     * @brief Check if connected
     */
    [[nodiscard]] bool is_connected() const noexcept;

    /**
     * @brief Reconnect after disconnection
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, mwl_error> reconnect();

    // =========================================================================
    // MWL Operations
    // =========================================================================

    /**
     * @brief Add a new worklist entry
     *
     * Creates a new Modality Worklist entry in pacs_system from the
     * provided MWL item data.
     *
     * @param item MWL item to add
     * @return Operation result or error
     */
    [[nodiscard]] std::expected<operation_result, mwl_error>
    add_entry(const mapping::mwl_item& item);

    /**
     * @brief Update an existing worklist entry
     *
     * Updates the worklist entry identified by accession number with
     * the provided data. Only non-empty fields in the item are updated.
     *
     * @param accession_number Accession number of entry to update
     * @param item Updated MWL item data
     * @return Operation result or error
     */
    [[nodiscard]] std::expected<operation_result, mwl_error>
    update_entry(std::string_view accession_number,
                 const mapping::mwl_item& item);

    /**
     * @brief Cancel (remove) a worklist entry
     *
     * Removes the worklist entry identified by accession number.
     * This is typically called when an order is cancelled.
     *
     * @param accession_number Accession number of entry to cancel
     * @return Operation result or error
     */
    [[nodiscard]] std::expected<operation_result, mwl_error>
    cancel_entry(std::string_view accession_number);

    /**
     * @brief Query worklist entries
     *
     * Queries pacs_system for MWL entries matching the filter criteria.
     * Uses DICOM C-FIND with Modality Worklist Information Model.
     *
     * @param filter Query filter criteria
     * @return Query result with matching items, or error
     */
    [[nodiscard]] std::expected<query_result, mwl_error>
    query(const mwl_query_filter& filter = {});

    /**
     * @brief Query worklist entries with MWL item as filter
     *
     * Uses the provided MWL item as a query template. Non-empty fields
     * in the item are used as filter criteria.
     *
     * @param query_item MWL item as query template
     * @return Query result with matching items, or error
     */
    [[nodiscard]] std::expected<query_result, mwl_error>
    query(const mapping::mwl_item& query_item);

    /**
     * @brief Check if an entry exists
     *
     * @param accession_number Accession number to check
     * @return true if entry exists
     */
    [[nodiscard]] bool exists(std::string_view accession_number);

    /**
     * @brief Get a specific entry by accession number
     *
     * @param accession_number Accession number of entry to retrieve
     * @return MWL item or error
     */
    [[nodiscard]] std::expected<mapping::mwl_item, mwl_error>
    get_entry(std::string_view accession_number);

    // =========================================================================
    // Bulk Operations
    // =========================================================================

    /**
     * @brief Bulk add multiple entries
     *
     * Adds multiple MWL entries efficiently. Continues on individual
     * failures if continue_on_error is true.
     *
     * @param items MWL items to add
     * @param continue_on_error Continue adding remaining items on failure
     * @return Number of successfully added items, or error
     */
    [[nodiscard]] std::expected<size_t, mwl_error>
    add_entries(const std::vector<mapping::mwl_item>& items,
                bool continue_on_error = true);

    /**
     * @brief Bulk cancel entries by scheduled date
     *
     * Cancels all MWL entries scheduled for the specified date.
     * Useful for clearing old entries.
     *
     * @param before_date Cancel entries scheduled before this date (YYYYMMDD)
     * @return Number of cancelled entries, or error
     */
    [[nodiscard]] std::expected<size_t, mwl_error>
    cancel_entries_before(std::string_view before_date);

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Client statistics
     */
    struct statistics {
        /** Total add operations */
        size_t add_count = 0;

        /** Total update operations */
        size_t update_count = 0;

        /** Total cancel operations */
        size_t cancel_count = 0;

        /** Total query operations */
        size_t query_count = 0;

        /** Failed operations */
        size_t error_count = 0;

        /** Connection attempts */
        size_t connect_attempts = 0;

        /** Successful connections */
        size_t connect_successes = 0;

        /** Reconnection count */
        size_t reconnections = 0;

        /** Average operation time in milliseconds */
        double avg_operation_ms = 0.0;
    };

    /**
     * @brief Get client statistics
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
    [[nodiscard]] const mwl_client_config& config() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::pacs_adapter

#endif  // PACS_BRIDGE_PACS_ADAPTER_MWL_CLIENT_H
