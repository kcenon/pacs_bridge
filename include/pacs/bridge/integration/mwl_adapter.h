#ifndef PACS_BRIDGE_INTEGRATION_MWL_ADAPTER_H
#define PACS_BRIDGE_INTEGRATION_MWL_ADAPTER_H

/**
 * @file mwl_adapter.h
 * @brief Modality Worklist adapter interface for abstracting MWL storage
 *
 * Provides a common interface for MWL operations, supporting both:
 * - In-memory storage (standalone mode)
 * - pacs_system index_database (full integration mode)
 *
 * This adapter pattern enables:
 * - Easy testing with mock implementations
 * - Switching between storage backends without code changes
 * - Consistent interface across different build configurations
 */

#include "pacs/bridge/mapping/hl7_dicom_mapper.h"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::integration {

// =============================================================================
// Error Codes
// =============================================================================

/**
 * @brief MWL adapter error codes
 *
 * Allocated range: -870 to -879
 */
enum class mwl_adapter_error : int {
    /** Storage initialization failed */
    init_failed = -870,

    /** Entry not found */
    not_found = -871,

    /** Duplicate entry exists */
    duplicate = -872,

    /** Invalid data provided */
    invalid_data = -873,

    /** Query failed */
    query_failed = -874,

    /** Add operation failed */
    add_failed = -875,

    /** Update operation failed */
    update_failed = -876,

    /** Delete operation failed */
    delete_failed = -877,

    /** Storage not accessible */
    storage_unavailable = -878
};

/**
 * @brief Convert mwl_adapter_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(mwl_adapter_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of MWL adapter error
 */
[[nodiscard]] constexpr const char* to_string(mwl_adapter_error error) noexcept {
    switch (error) {
        case mwl_adapter_error::init_failed:
            return "MWL storage initialization failed";
        case mwl_adapter_error::not_found:
            return "MWL entry not found";
        case mwl_adapter_error::duplicate:
            return "Duplicate MWL entry";
        case mwl_adapter_error::invalid_data:
            return "Invalid MWL data";
        case mwl_adapter_error::query_failed:
            return "MWL query failed";
        case mwl_adapter_error::add_failed:
            return "MWL add failed";
        case mwl_adapter_error::update_failed:
            return "MWL update failed";
        case mwl_adapter_error::delete_failed:
            return "MWL delete failed";
        case mwl_adapter_error::storage_unavailable:
            return "MWL storage unavailable";
        default:
            return "Unknown MWL adapter error";
    }
}

// =============================================================================
// Query Filter
// =============================================================================

/**
 * @brief MWL query filter criteria
 */
struct mwl_query_filter {
    std::optional<std::string> patient_id;
    std::optional<std::string> accession_number;
    std::optional<std::string> patient_name;
    std::optional<std::string> modality;
    std::optional<std::string> scheduled_station_ae;
    std::optional<std::string> scheduled_date;
    std::optional<std::string> scheduled_date_from;
    std::optional<std::string> scheduled_date_to;
    std::optional<std::string> referring_physician;
    std::optional<std::string> sps_status;
    size_t max_results = 0;  // 0 = unlimited
};

// =============================================================================
// MWL Adapter Interface
// =============================================================================

/**
 * @brief Abstract interface for Modality Worklist storage
 *
 * Defines the contract for MWL storage backends. Implementations must
 * provide thread-safe operations for managing worklist entries.
 *
 * Implementations:
 * - memory_mwl_adapter: In-memory storage for standalone/testing
 * - pacs_mwl_adapter: pacs_system index_database integration
 *
 * @example
 * ```cpp
 * auto adapter = std::make_shared<memory_mwl_adapter>();
 *
 * mapping::mwl_item item;
 * item.imaging_service_request.accession_number = "ACC001";
 * item.patient.patient_id = "PAT001";
 *
 * auto result = adapter->add_item(item);
 * if (!result) {
 *     std::cerr << "Add failed: " << to_string(result.error()) << std::endl;
 * }
 * ```
 */
class mwl_adapter {
public:
    virtual ~mwl_adapter() = default;

    /**
     * @brief Add a new MWL entry
     *
     * @param item MWL item to add
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, mwl_adapter_error>
    add_item(const mapping::mwl_item& item) = 0;

    /**
     * @brief Update an existing MWL entry
     *
     * Updates the entry identified by accession number.
     * Only non-empty fields in the item are updated.
     *
     * @param accession_number Accession number of entry to update
     * @param item Updated MWL item data
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, mwl_adapter_error>
    update_item(std::string_view accession_number,
                const mapping::mwl_item& item) = 0;

    /**
     * @brief Delete an MWL entry
     *
     * @param accession_number Accession number of entry to delete
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, mwl_adapter_error>
    delete_item(std::string_view accession_number) = 0;

    /**
     * @brief Query MWL entries with filter
     *
     * @param filter Query filter criteria
     * @return Vector of matching items, or error
     */
    [[nodiscard]] virtual std::expected<std::vector<mapping::mwl_item>,
                                        mwl_adapter_error>
    query_items(const mwl_query_filter& filter) = 0;

    /**
     * @brief Get a specific MWL entry by accession number
     *
     * @param accession_number Accession number of entry to retrieve
     * @return MWL item or error
     */
    [[nodiscard]] virtual std::expected<mapping::mwl_item, mwl_adapter_error>
    get_item(std::string_view accession_number) = 0;

    /**
     * @brief Check if an entry exists
     *
     * @param accession_number Accession number to check
     * @return true if entry exists
     */
    [[nodiscard]] virtual bool exists(std::string_view accession_number) = 0;

    /**
     * @brief Delete entries scheduled before a specific date
     *
     * Useful for cleaning up old/expired entries.
     *
     * @param before_date Date in YYYYMMDD format
     * @return Number of deleted entries, or error
     */
    [[nodiscard]] virtual std::expected<size_t, mwl_adapter_error>
    delete_items_before(std::string_view before_date) = 0;

    /**
     * @brief Get adapter type name (for debugging)
     *
     * @return Adapter type name (e.g., "memory", "pacs_system")
     */
    [[nodiscard]] virtual const char* adapter_type() const noexcept = 0;
};

// =============================================================================
// Factory Function
// =============================================================================

/**
 * @brief Create appropriate MWL adapter based on build configuration
 *
 * Returns:
 * - memory_mwl_adapter if PACS_BRIDGE_STANDALONE_BUILD is defined
 * - pacs_mwl_adapter if PACS_BRIDGE_HAS_PACS_SYSTEM is defined
 *
 * @param database_path Path to pacs_system database (used only for pacs_mwl_adapter)
 * @return Shared pointer to mwl_adapter implementation
 */
[[nodiscard]] std::shared_ptr<mwl_adapter>
create_mwl_adapter(const std::string& database_path = "");

}  // namespace pacs::bridge::integration

#endif  // PACS_BRIDGE_INTEGRATION_MWL_ADAPTER_H
