#ifndef PACS_BRIDGE_PROTOCOL_HL7_ORM_HANDLER_H
#define PACS_BRIDGE_PROTOCOL_HL7_ORM_HANDLER_H

/**
 * @file orm_handler.h
 * @brief ORM (Order Management) message handler for Modality Worklist
 *
 * Provides handlers for ORM^O01 messages to create, update, and cancel
 * Modality Worklist (MWL) entries. Supports the following Order Control codes:
 *   - NW: New Order - Create new MWL entry
 *   - XO: Change Order - Update existing MWL entry
 *   - CA: Cancel Order - Remove MWL entry
 *   - DC: Discontinue Order - Mark entry as discontinued
 *   - SC: Status Change - Update order status only
 *
 * The handler integrates with:
 *   - HL7-DICOM Mapper for ORM to MWL conversion
 *   - MWL Client for pacs_system communication
 *   - HL7 Builder for ACK response generation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/15
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include "pacs/bridge/mapping/hl7_dicom_mapper.h"
#include "pacs/bridge/pacs_adapter/mwl_client.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::hl7 {

// =============================================================================
// ORM Handler Error Codes (-860 to -869)
// =============================================================================

/**
 * @brief ORM handler specific error codes
 *
 * Allocated range: -860 to -869
 */
enum class orm_error : int {
    /** Message is not an ORM message */
    not_orm_message = -860,

    /** Unsupported order control code */
    unsupported_order_control = -861,

    /** Missing required field (Accession Number, Patient ID, etc.) */
    missing_required_field = -862,

    /** Order not found for update/cancel */
    order_not_found = -863,

    /** MWL entry creation failed */
    mwl_create_failed = -864,

    /** MWL entry update failed */
    mwl_update_failed = -865,

    /** MWL entry cancel failed */
    mwl_cancel_failed = -866,

    /** Duplicate order exists */
    duplicate_order = -867,

    /** Invalid order data */
    invalid_order_data = -868,

    /** Processing failed */
    processing_failed = -869
};

/**
 * @brief Convert orm_error to error code
 */
[[nodiscard]] constexpr int to_error_code(orm_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(orm_error error) noexcept {
    switch (error) {
        case orm_error::not_orm_message:
            return "Message is not an ORM message";
        case orm_error::unsupported_order_control:
            return "Unsupported order control code";
        case orm_error::missing_required_field:
            return "Required field missing in ORM message";
        case orm_error::order_not_found:
            return "Order not found for update/cancel operation";
        case orm_error::mwl_create_failed:
            return "Failed to create MWL entry";
        case orm_error::mwl_update_failed:
            return "Failed to update MWL entry";
        case orm_error::mwl_cancel_failed:
            return "Failed to cancel MWL entry";
        case orm_error::duplicate_order:
            return "Duplicate order already exists";
        case orm_error::invalid_order_data:
            return "Invalid order data in message";
        case orm_error::processing_failed:
            return "ORM message processing failed";
        default:
            return "Unknown ORM handler error";
    }
}

// =============================================================================
// Order Control Codes
// =============================================================================

/**
 * @brief Supported ORM order control codes (ORC-1)
 */
enum class order_control {
    /** NW - New Order: Create new MWL entry */
    new_order,

    /** XO - Change Order: Update existing MWL entry */
    change_order,

    /** CA - Cancel Order: Remove MWL entry */
    cancel_order,

    /** DC - Discontinue Order: Mark as discontinued */
    discontinue_order,

    /** SC - Status Change: Update status only */
    status_change,

    /** Unknown/unsupported order control */
    unknown
};

/**
 * @brief Parse order control from ORC-1 string
 */
[[nodiscard]] constexpr order_control parse_order_control(
    std::string_view orc1) noexcept {
    if (orc1 == "NW") return order_control::new_order;
    if (orc1 == "XO") return order_control::change_order;
    if (orc1 == "CA") return order_control::cancel_order;
    if (orc1 == "DC") return order_control::discontinue_order;
    if (orc1 == "SC") return order_control::status_change;
    return order_control::unknown;
}

/**
 * @brief Convert order control to string
 */
[[nodiscard]] constexpr const char* to_string(order_control ctrl) noexcept {
    switch (ctrl) {
        case order_control::new_order:
            return "NW";
        case order_control::change_order:
            return "XO";
        case order_control::cancel_order:
            return "CA";
        case order_control::discontinue_order:
            return "DC";
        case order_control::status_change:
            return "SC";
        default:
            return "UNKNOWN";
    }
}

// =============================================================================
// Order Status Codes
// =============================================================================

/**
 * @brief Order status codes (ORC-5)
 */
enum class order_status {
    /** SC - Scheduled */
    scheduled,

    /** IP - In Progress */
    in_progress,

    /** CM - Completed */
    completed,

    /** CA - Cancelled */
    cancelled,

    /** DC - Discontinued */
    discontinued,

    /** HD - Hold */
    hold,

    /** Unknown status */
    unknown
};

/**
 * @brief Parse order status from ORC-5 string
 */
[[nodiscard]] constexpr order_status parse_order_status(
    std::string_view orc5) noexcept {
    if (orc5 == "SC") return order_status::scheduled;
    if (orc5 == "IP") return order_status::in_progress;
    if (orc5 == "CM") return order_status::completed;
    if (orc5 == "CA") return order_status::cancelled;
    if (orc5 == "DC") return order_status::discontinued;
    if (orc5 == "HD") return order_status::hold;
    return order_status::unknown;
}

/**
 * @brief Convert order status to MWL SPS status string
 */
[[nodiscard]] constexpr const char* to_mwl_status(order_status status) noexcept {
    switch (status) {
        case order_status::scheduled:
            return "SCHEDULED";
        case order_status::in_progress:
            return "STARTED";
        case order_status::completed:
            return "COMPLETED";
        case order_status::cancelled:
        case order_status::discontinued:
            return "DISCONTINUED";
        case order_status::hold:
            return "SCHEDULED";  // Treat hold as scheduled
        default:
            return "SCHEDULED";
    }
}

// =============================================================================
// ORM Processing Result
// =============================================================================

/**
 * @brief Result of ORM message processing
 */
struct orm_result {
    /** Processing was successful */
    bool success = false;

    /** Order control code that was processed */
    order_control control = order_control::unknown;

    /** Order status from message */
    order_status status = order_status::unknown;

    /** Accession number of the affected order */
    std::string accession_number;

    /** Patient ID associated with the order */
    std::string patient_id;

    /** Placer order number (ORC-2) */
    std::string placer_order_number;

    /** Filler order number (ORC-3) */
    std::string filler_order_number;

    /** Study Instance UID (from ZDS segment or generated) */
    std::string study_instance_uid;

    /** Description of what was done */
    std::string description;

    /** ACK response message */
    hl7_message ack_message;

    /** Processing warnings (non-fatal issues) */
    std::vector<std::string> warnings;
};

// =============================================================================
// ORM Handler Configuration
// =============================================================================

/**
 * @brief ORM handler configuration
 */
struct orm_handler_config {
    /** Allow update on NW if order already exists */
    bool allow_nw_update = false;

    /** Create order on XO if not exists */
    bool allow_xo_create = false;

    /** Auto-generate Study Instance UID if not in ZDS segment */
    bool auto_generate_study_uid = true;

    /** Auto-generate Accession Number if not provided */
    bool auto_generate_accession = false;

    /** Validate order data before MWL operation */
    bool validate_order_data = true;

    /** Required fields for order validation */
    std::vector<std::string> required_fields = {
        "patient_id", "patient_name", "accession_number"};

    /** Generate detailed ACK messages */
    bool detailed_ack = true;

    /** Log all orders for audit */
    bool audit_logging = true;

    /** Custom ACK application name */
    std::string ack_sending_application = "PACS_BRIDGE";

    /** Custom ACK facility name */
    std::string ack_sending_facility = "RADIOLOGY";

    /** UID root for Study Instance UID generation */
    std::string study_uid_root = "1.2.840.10008.5.1.4";
};

// =============================================================================
// Order Information
// =============================================================================

/**
 * @brief Extracted order information from ORM message
 */
struct order_info {
    /** Order control code (ORC-1) */
    order_control control = order_control::unknown;

    /** Order status (ORC-5) */
    order_status status = order_status::unknown;

    /** Placer order number (ORC-2) */
    std::string placer_order_number;

    /** Filler order number / Accession number (ORC-3) */
    std::string filler_order_number;

    /** Patient ID (PID-3) */
    std::string patient_id;

    /** Patient name (PID-5) */
    std::string patient_name;

    /** Scheduled date/time (OBR-7) */
    std::string scheduled_datetime;

    /** Modality (OBR-24) */
    std::string modality;

    /** Procedure code (OBR-4.1) */
    std::string procedure_code;

    /** Procedure description (OBR-4.2) */
    std::string procedure_description;

    /** Ordering provider (ORC-12) */
    std::string ordering_provider;

    /** Study Instance UID (ZDS-1 or generated) */
    std::string study_instance_uid;

    /** Original HL7 message control ID */
    std::string message_control_id;
};

// =============================================================================
// ORM Handler
// =============================================================================

/**
 * @brief ORM message handler for Modality Worklist management
 *
 * Processes ORM^O01 (Order Management) messages to create, update, and cancel
 * Modality Worklist entries in pacs_system. Generates appropriate ACK responses.
 *
 * @example Basic Usage
 * ```cpp
 * // Create handler with MWL client
 * auto mwl = std::make_shared<pacs_adapter::mwl_client>(config);
 * mwl->connect();
 *
 * orm_handler handler(mwl);
 *
 * // Process ORM message
 * auto result = handler.handle(orm_message);
 * if (result) {
 *     std::cout << "Processed: " << result->description << std::endl;
 *     // Send ACK back
 *     send_response(result->ack_message);
 * }
 * ```
 *
 * @example With Configuration
 * ```cpp
 * orm_handler_config config;
 * config.auto_generate_study_uid = true;
 * config.audit_logging = true;
 *
 * orm_handler handler(mwl, config);
 * ```
 *
 * @example Custom Event Handlers
 * ```cpp
 * handler.on_order_created([](const order_info& order) {
 *     log_audit("Order created: " + order.accession_number);
 * });
 *
 * handler.on_order_cancelled([](const std::string& accession) {
 *     log_audit("Order cancelled: " + accession);
 * });
 * ```
 */
class orm_handler {
public:
    // =========================================================================
    // Callback Types
    // =========================================================================

    /** Callback for order creation */
    using order_created_callback =
        std::function<void(const order_info& order,
                           const mapping::mwl_item& mwl)>;

    /** Callback for order update */
    using order_updated_callback =
        std::function<void(const order_info& order,
                           const mapping::mwl_item& old_mwl,
                           const mapping::mwl_item& new_mwl)>;

    /** Callback for order cancellation */
    using order_cancelled_callback =
        std::function<void(const std::string& accession_number,
                           const std::string& reason)>;

    /** Callback for status change */
    using status_changed_callback =
        std::function<void(const std::string& accession_number,
                           order_status old_status, order_status new_status)>;

    // =========================================================================
    // Construction
    // =========================================================================

    /**
     * @brief Construct handler with MWL client
     *
     * @param mwl_client Shared pointer to MWL client for pacs_system
     */
    explicit orm_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client);

    /**
     * @brief Construct handler with MWL client and configuration
     *
     * @param mwl_client Shared pointer to MWL client
     * @param config Handler configuration
     */
    orm_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                const orm_handler_config& config);

    /**
     * @brief Construct handler with MWL client and HL7-DICOM mapper
     *
     * @param mwl_client Shared pointer to MWL client
     * @param mapper Shared pointer to HL7-DICOM mapper
     */
    orm_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                std::shared_ptr<mapping::hl7_dicom_mapper> mapper);

    /**
     * @brief Full constructor with all dependencies
     *
     * @param mwl_client Shared pointer to MWL client
     * @param mapper Shared pointer to HL7-DICOM mapper
     * @param config Handler configuration
     */
    orm_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                std::shared_ptr<mapping::hl7_dicom_mapper> mapper,
                const orm_handler_config& config);

    /**
     * @brief Destructor
     */
    ~orm_handler();

    // Non-copyable, movable
    orm_handler(const orm_handler&) = delete;
    orm_handler& operator=(const orm_handler&) = delete;
    orm_handler(orm_handler&&) noexcept;
    orm_handler& operator=(orm_handler&&) noexcept;

    // =========================================================================
    // Message Handling
    // =========================================================================

    /**
     * @brief Handle ORM message
     *
     * Processes the ORM message and performs the appropriate MWL operation
     * based on the order control code. Returns a result containing the
     * ACK message to send back.
     *
     * @param message HL7 ORM message
     * @return Processing result or error
     */
    [[nodiscard]] std::expected<orm_result, orm_error> handle(
        const hl7_message& message);

    /**
     * @brief Check if message can be handled
     *
     * @param message HL7 message
     * @return true if this handler can process the message
     */
    [[nodiscard]] bool can_handle(const hl7_message& message) const noexcept;

    /**
     * @brief Get supported order control codes
     *
     * @return List of supported order control code strings
     */
    [[nodiscard]] std::vector<std::string> supported_controls() const;

    // =========================================================================
    // Individual Order Control Handlers
    // =========================================================================

    /**
     * @brief Handle NW (New Order) control
     *
     * Creates a new MWL entry for the order. If the order already exists
     * and allow_nw_update is true, updates the existing entry.
     *
     * @param message HL7 ORM message with NW control
     * @return Processing result or error
     */
    [[nodiscard]] std::expected<orm_result, orm_error> handle_new_order(
        const hl7_message& message);

    /**
     * @brief Handle XO (Change Order) control
     *
     * Updates an existing MWL entry. If the order doesn't exist and
     * allow_xo_create is true, creates a new entry.
     *
     * @param message HL7 ORM message with XO control
     * @return Processing result or error
     */
    [[nodiscard]] std::expected<orm_result, orm_error> handle_change_order(
        const hl7_message& message);

    /**
     * @brief Handle CA (Cancel Order) control
     *
     * Removes the MWL entry for the order.
     *
     * @param message HL7 ORM message with CA control
     * @return Processing result or error
     */
    [[nodiscard]] std::expected<orm_result, orm_error> handle_cancel_order(
        const hl7_message& message);

    /**
     * @brief Handle DC (Discontinue Order) control
     *
     * Marks the MWL entry as discontinued but keeps it for reference.
     *
     * @param message HL7 ORM message with DC control
     * @return Processing result or error
     */
    [[nodiscard]] std::expected<orm_result, orm_error> handle_discontinue_order(
        const hl7_message& message);

    /**
     * @brief Handle SC (Status Change) control
     *
     * Updates only the status of an existing MWL entry without
     * modifying other fields.
     *
     * @param message HL7 ORM message with SC control
     * @return Processing result or error
     */
    [[nodiscard]] std::expected<orm_result, orm_error> handle_status_change(
        const hl7_message& message);

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Extract order information from ORM message
     *
     * Parses the ORM message and extracts relevant order information
     * including order control, status, accession number, patient data, etc.
     *
     * @param message HL7 ORM message
     * @return Extracted order information or error
     */
    [[nodiscard]] std::expected<order_info, orm_error> extract_order_info(
        const hl7_message& message) const;

    /**
     * @brief Generate ACK response for ORM message
     *
     * Creates an ACK^O01 response message based on the processing result.
     *
     * @param original Original ORM message
     * @param success Whether processing was successful
     * @param error_code Error code if failed (AA, AE, AR)
     * @param error_message Error description if failed
     * @return ACK message
     */
    [[nodiscard]] hl7_message generate_ack(
        const hl7_message& original, bool success,
        std::string_view error_code = "",
        std::string_view error_message = "") const;

    // =========================================================================
    // Callbacks
    // =========================================================================

    /**
     * @brief Set callback for order creation
     */
    void on_order_created(order_created_callback callback);

    /**
     * @brief Set callback for order update
     */
    void on_order_updated(order_updated_callback callback);

    /**
     * @brief Set callback for order cancellation
     */
    void on_order_cancelled(order_cancelled_callback callback);

    /**
     * @brief Set callback for status change
     */
    void on_status_changed(status_changed_callback callback);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const orm_handler_config& config() const noexcept;

    /**
     * @brief Set configuration
     */
    void set_config(const orm_handler_config& config);

    /**
     * @brief Get the MWL client
     */
    [[nodiscard]] std::shared_ptr<pacs_adapter::mwl_client>
    mwl_client() const noexcept;

    /**
     * @brief Get the HL7-DICOM mapper
     */
    [[nodiscard]] std::shared_ptr<mapping::hl7_dicom_mapper>
    mapper() const noexcept;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Handler statistics
     */
    struct statistics {
        /** Total messages processed */
        size_t total_processed = 0;

        /** Successful processing count */
        size_t success_count = 0;

        /** Failed processing count */
        size_t failure_count = 0;

        /** NW (New Order) messages processed */
        size_t nw_count = 0;

        /** XO (Change Order) messages processed */
        size_t xo_count = 0;

        /** CA (Cancel Order) messages processed */
        size_t ca_count = 0;

        /** DC (Discontinue Order) messages processed */
        size_t dc_count = 0;

        /** SC (Status Change) messages processed */
        size_t sc_count = 0;

        /** MWL entries created */
        size_t entries_created = 0;

        /** MWL entries updated */
        size_t entries_updated = 0;

        /** MWL entries cancelled */
        size_t entries_cancelled = 0;

        /** Average processing time in milliseconds */
        double avg_processing_ms = 0.0;
    };

    /**
     * @brief Get handler statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_ORM_HANDLER_H
