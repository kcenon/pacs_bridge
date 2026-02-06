#ifndef PACS_BRIDGE_INTEGRATION_PACS_ADAPTER_H
#define PACS_BRIDGE_INTEGRATION_PACS_ADAPTER_H

/**
 * @file pacs_adapter.h
 * @brief Integration Module - PACS system adapter
 *
 * Provides adapters that abstract DICOM operations and enable integration
 * with pacs_system while maintaining standalone capability. This adapter
 * consolidates PACS-related functionality scattered across multiple files
 * (mpps_handler.cpp, mwl_client.cpp) into a consistent interface.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/283
 * @see https://github.com/kcenon/pacs_bridge/issues/273
 * @see docs/SDS_COMPONENTS.md - Section 8: Integration Module
 */

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::integration {

// =============================================================================
// Error Codes (-850 to -899)
// =============================================================================

/**
 * @brief PACS adapter specific error codes
 *
 * Allocated range: -850 to -899
 */
enum class pacs_error : int {
    /** Connection to PACS server failed */
    connection_failed = -850,

    /** Query execution failed */
    query_failed = -851,

    /** Store operation failed */
    store_failed = -852,

    /** Invalid or malformed DICOM dataset */
    invalid_dataset = -853,

    /** DICOM association failed */
    association_failed = -854,

    /** Operation timeout */
    timeout = -855,

    /** Resource not found */
    not_found = -856,

    /** Duplicate entry detected */
    duplicate_entry = -857,

    /** Validation failed */
    validation_failed = -858,

    /** MPPS N-CREATE failed */
    mpps_create_failed = -859,

    /** MPPS N-SET failed */
    mpps_update_failed = -860,

    /** MWL query failed */
    mwl_query_failed = -861,

    /** DICOM storage failed */
    storage_failed = -862,

    /** Invalid SOP Instance UID */
    invalid_sop_uid = -863
};

/**
 * @brief Convert pacs_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(pacs_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable error message for pacs_error
 */
[[nodiscard]] std::string_view to_string(pacs_error error) noexcept;

// =============================================================================
// DICOM Dataset Abstraction
// =============================================================================

/**
 * @brief DICOM dataset representation
 *
 * Provides a simplified abstraction over DICOM attributes,
 * using tag numbers (e.g., 0x00100020 for Patient ID).
 */
struct dicom_dataset {
    /** SOP Class UID (0008,0016) */
    std::string sop_class_uid;

    /** SOP Instance UID (0008,0018) */
    std::string sop_instance_uid;

    /** DICOM attributes: Tag -> Value */
    std::map<uint32_t, std::string> attributes;

    /**
     * @brief Get string value for a DICOM tag
     *
     * @param tag DICOM tag (e.g., 0x00100020 for Patient ID)
     * @return String value if present, std::nullopt otherwise
     */
    [[nodiscard]] std::optional<std::string> get_string(uint32_t tag) const;

    /**
     * @brief Set string value for a DICOM tag
     *
     * @param tag DICOM tag
     * @param value String value to set
     */
    void set_string(uint32_t tag, std::string_view value);

    /**
     * @brief Check if a tag exists in the dataset
     */
    [[nodiscard]] bool has_tag(uint32_t tag) const;

    /**
     * @brief Remove a tag from the dataset
     */
    void remove_tag(uint32_t tag);

    /**
     * @brief Clear all attributes
     */
    void clear();
};

// =============================================================================
// MPPS Record Abstraction
// =============================================================================

/**
 * @brief Modality Performed Procedure Step (MPPS) record
 *
 * Represents an MPPS record with essential DICOM attributes.
 */
struct mpps_record {
    /** Affected SOP Instance UID */
    std::string sop_instance_uid;

    /** Scheduled Procedure Step ID */
    std::string scheduled_procedure_step_id;

    /** Performed Procedure Step ID */
    std::string performed_procedure_step_id;

    /** Performed Station AE Title */
    std::string performed_station_ae_title;

    /** Performed Station Name */
    std::string performed_station_name;

    /** Performed Location */
    std::string performed_location;

    /** Procedure Step Start Date/Time */
    std::chrono::system_clock::time_point start_datetime;

    /** Procedure Step End Date/Time (optional) */
    std::optional<std::chrono::system_clock::time_point> end_datetime;

    /** Performed Procedure Step Status: "IN PROGRESS", "COMPLETED", "DISCONTINUED" */
    std::string status;

    /** Study Instance UID */
    std::string study_instance_uid;

    /** Patient ID */
    std::string patient_id;

    /** Patient Name */
    std::string patient_name;

    /** Series Instance UIDs */
    std::vector<std::string> series_instance_uids;

    /** Accession Number (0008,0050) */
    std::string accession_number;

    /** Modality (0008,0060) */
    std::string modality;

    /** Performed Procedure Step Description (0040,0254) */
    std::string performed_procedure_description;

    /** Referring Physician's Name (0008,0090) */
    std::string referring_physician;

    /** Requested Procedure ID (0040,1001) */
    std::string requested_procedure_id;

    /** Discontinuation Reason */
    std::string discontinuation_reason;

    /**
     * @brief Validate MPPS record fields
     *
     * @return true if all required fields are present and valid
     */
    [[nodiscard]] bool is_valid() const;
};

// =============================================================================
// MWL Item Abstraction
// =============================================================================

/**
 * @brief Modality Worklist (MWL) item
 *
 * Represents a worklist item from a DICOM Worklist query.
 */
struct mwl_item {
    /** Accession Number */
    std::string accession_number;

    /** Scheduled Procedure Step ID */
    std::string scheduled_procedure_step_id;

    /** Requested Procedure ID */
    std::string requested_procedure_id;

    /** Scheduled Station AE Title */
    std::string scheduled_station_ae_title;

    /** Scheduled Procedure Step Start Date/Time */
    std::chrono::system_clock::time_point scheduled_datetime;

    /** Modality */
    std::string modality;

    /** Patient ID */
    std::string patient_id;

    /** Patient Name */
    std::string patient_name;

    /** Study Instance UID */
    std::string study_instance_uid;

    /**
     * @brief Validate MWL item fields
     *
     * @return true if all required fields are present and valid
     */
    [[nodiscard]] bool is_valid() const;
};

// =============================================================================
// Query Parameters
// =============================================================================

/**
 * @brief Query parameters for MPPS records
 */
struct mpps_query_params {
    /** Patient ID filter (optional) */
    std::optional<std::string> patient_id;

    /** Study Instance UID filter (optional) */
    std::optional<std::string> study_instance_uid;

    /** Status filter: "IN PROGRESS", "COMPLETED", "DISCONTINUED" (optional) */
    std::optional<std::string> status;

    /** Station AE Title filter (optional) */
    std::optional<std::string> station_ae_title;

    /** Modality filter (optional) */
    std::optional<std::string> modality;

    /** Accession Number filter (optional) */
    std::optional<std::string> accession_number;

    /** Start datetime range (from) */
    std::optional<std::chrono::system_clock::time_point> from_datetime;

    /** Start datetime range (to) */
    std::optional<std::chrono::system_clock::time_point> to_datetime;

    /** Maximum number of results */
    std::size_t max_results = 100;
};

/**
 * @brief Query parameters for MWL items
 */
struct mwl_query_params {
    /** Patient ID filter (optional) */
    std::optional<std::string> patient_id;

    /** Accession Number filter (optional) */
    std::optional<std::string> accession_number;

    /** Modality filter (optional) */
    std::optional<std::string> modality;

    /** Scheduled date filter (optional) */
    std::optional<std::chrono::system_clock::time_point> scheduled_date;

    /** Maximum number of results */
    std::size_t max_results = 100;
};

// =============================================================================
// MPPS Service Adapter
// =============================================================================

/**
 * @brief MPPS service adapter interface
 *
 * Provides abstraction for DICOM Modality Performed Procedure Step operations.
 */
class mpps_adapter {
public:
    virtual ~mpps_adapter() = default;

    /**
     * @brief Create new MPPS record (DICOM N-CREATE)
     *
     * @param record MPPS record to create
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, pacs_error>
        create_mpps(const mpps_record& record) = 0;

    /**
     * @brief Update existing MPPS record (DICOM N-SET)
     *
     * @param record MPPS record with updated fields
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, pacs_error>
        update_mpps(const mpps_record& record) = 0;

    /**
     * @brief Query MPPS records
     *
     * @param params Query parameters
     * @return Vector of matching MPPS records or error
     */
    [[nodiscard]] virtual std::expected<std::vector<mpps_record>, pacs_error>
        query_mpps(const mpps_query_params& params) = 0;

    /**
     * @brief Get single MPPS record by SOP Instance UID
     *
     * @param sop_instance_uid SOP Instance UID
     * @return MPPS record or error
     */
    [[nodiscard]] virtual std::expected<mpps_record, pacs_error>
        get_mpps(std::string_view sop_instance_uid) = 0;
};

// =============================================================================
// MWL Service Adapter
// =============================================================================

/**
 * @brief Modality Worklist (MWL) service adapter interface
 *
 * Provides abstraction for DICOM Worklist Query/Retrieve operations.
 */
class mwl_adapter {
public:
    virtual ~mwl_adapter() = default;

    /**
     * @brief Query worklist
     *
     * @param params Query parameters
     * @return Vector of matching MWL items or error
     */
    [[nodiscard]] virtual std::expected<std::vector<mwl_item>, pacs_error>
        query_mwl(const mwl_query_params& params) = 0;

    /**
     * @brief Get single MWL item by accession number
     *
     * @param accession_number Accession Number
     * @return MWL item or error
     */
    [[nodiscard]] virtual std::expected<mwl_item, pacs_error>
        get_mwl_item(std::string_view accession_number) = 0;
};

// =============================================================================
// Storage Service Adapter
// =============================================================================

/**
 * @brief DICOM storage service adapter interface
 *
 * Provides abstraction for DICOM C-STORE operations.
 */
class storage_adapter {
public:
    virtual ~storage_adapter() = default;

    /**
     * @brief Store DICOM dataset
     *
     * @param dataset DICOM dataset to store
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, pacs_error>
        store(const dicom_dataset& dataset) = 0;

    /**
     * @brief Retrieve DICOM dataset by SOP Instance UID
     *
     * @param sop_instance_uid SOP Instance UID
     * @return DICOM dataset or error
     */
    [[nodiscard]] virtual std::expected<dicom_dataset, pacs_error>
        retrieve(std::string_view sop_instance_uid) = 0;

    /**
     * @brief Check if dataset exists
     *
     * @param sop_instance_uid SOP Instance UID
     * @return true if exists, false otherwise
     */
    [[nodiscard]] virtual bool exists(std::string_view sop_instance_uid) const = 0;
};

// =============================================================================
// Combined PACS Adapter
// =============================================================================

/**
 * @brief Combined PACS adapter interface
 *
 * Provides unified access to all PACS services (MPPS, MWL, Storage).
 */
class pacs_adapter {
public:
    virtual ~pacs_adapter() = default;

    /**
     * @brief Get MPPS service adapter
     */
    [[nodiscard]] virtual std::shared_ptr<mpps_adapter> get_mpps_adapter() = 0;

    /**
     * @brief Get MWL service adapter
     */
    [[nodiscard]] virtual std::shared_ptr<mwl_adapter> get_mwl_adapter() = 0;

    /**
     * @brief Get storage service adapter
     */
    [[nodiscard]] virtual std::shared_ptr<storage_adapter> get_storage_adapter() = 0;

    /**
     * @brief Connect to PACS server
     *
     * @return Success or error
     */
    [[nodiscard]] virtual std::expected<void, pacs_error> connect() = 0;

    /**
     * @brief Disconnect from PACS server
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if connected to PACS server
     */
    [[nodiscard]] virtual bool is_connected() const = 0;

    /**
     * @brief Check if PACS adapter is healthy
     */
    [[nodiscard]] virtual bool is_healthy() const = 0;
};

// =============================================================================
// Configuration
// =============================================================================

/**
 * @brief PACS adapter configuration
 */
struct pacs_config {
    /** PACS server AE Title */
    std::string server_ae_title = "PACS_SERVER";

    /** PACS server hostname */
    std::string server_hostname = "localhost";

    /** PACS server port */
    uint16_t server_port = 11112;

    /** Calling AE Title */
    std::string calling_ae_title = "PACS_BRIDGE";

    /** Connection timeout */
    std::chrono::seconds connection_timeout{30};

    /** Query timeout */
    std::chrono::seconds query_timeout{60};
};

// =============================================================================
// Factory Functions
// =============================================================================

/**
 * @brief Create PACS adapter (standalone mode with stub implementation)
 *
 * @param config PACS configuration
 * @return Shared pointer to PACS adapter
 */
[[nodiscard]] std::shared_ptr<pacs_adapter>
create_pacs_adapter(const pacs_config& config);

#ifdef PACS_BRIDGE_HAS_PACS_SYSTEM
// Forward declaration for pacs_system integration
namespace kcenon::pacs::services {
class pacs_server;
}

/**
 * @brief Create PACS adapter using pacs_system (full integration mode)
 *
 * @param server Shared pointer to pacs_server
 * @return Shared pointer to PACS adapter
 */
[[nodiscard]] std::shared_ptr<pacs_adapter> create_pacs_adapter(
    std::shared_ptr<kcenon::pacs::services::pacs_server> server);
#endif

}  // namespace pacs::bridge::integration

#endif  // PACS_BRIDGE_INTEGRATION_PACS_ADAPTER_H
