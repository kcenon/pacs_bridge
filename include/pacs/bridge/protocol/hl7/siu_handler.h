#ifndef PACS_BRIDGE_PROTOCOL_HL7_SIU_HANDLER_H
#define PACS_BRIDGE_PROTOCOL_HL7_SIU_HANDLER_H

/**
 * @file siu_handler.h
 * @brief SIU (Scheduling Information Unsolicited) message handler
 *
 * Provides handlers for SIU messages to support appointment-based scheduling
 * updates. Supports the following trigger events:
 *   - S12: New Appointment - Create MWL entry with appointment info
 *   - S13: Rescheduled - Update MWL timing
 *   - S14: Modification - Update MWL details
 *   - S15: Cancellation - Cancel MWL entry
 *
 * The handler integrates with:
 *   - HL7-DICOM Mapper for SIU to MWL conversion
 *   - MWL Client for pacs_system communication
 *   - HL7 Builder for ACK response generation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/26
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include "pacs/bridge/mapping/hl7_dicom_mapper.h"
#include "pacs/bridge/pacs_adapter/mwl_client.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::hl7 {

// =============================================================================
// SIU Handler Error Codes (-870 to -879)
// =============================================================================

/**
 * @brief SIU handler specific error codes
 *
 * Allocated range: -870 to -879
 */
enum class siu_error : int {
    /** Message is not an SIU message */
    not_siu_message = -870,

    /** Unsupported trigger event */
    unsupported_trigger_event = -871,

    /** Missing required field (Appointment ID, Patient ID, etc.) */
    missing_required_field = -872,

    /** Appointment not found for update/cancel */
    appointment_not_found = -873,

    /** MWL entry creation failed */
    mwl_create_failed = -874,

    /** MWL entry update failed */
    mwl_update_failed = -875,

    /** MWL entry cancel failed */
    mwl_cancel_failed = -876,

    /** Duplicate appointment exists */
    duplicate_appointment = -877,

    /** Invalid appointment data */
    invalid_appointment_data = -878,

    /** Processing failed */
    processing_failed = -879
};

/**
 * @brief Convert siu_error to error code
 */
[[nodiscard]] constexpr int to_error_code(siu_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(siu_error error) noexcept {
    switch (error) {
        case siu_error::not_siu_message:
            return "Message is not an SIU message";
        case siu_error::unsupported_trigger_event:
            return "Unsupported SIU trigger event";
        case siu_error::missing_required_field:
            return "Required field missing in SIU message";
        case siu_error::appointment_not_found:
            return "Appointment not found for update/cancel operation";
        case siu_error::mwl_create_failed:
            return "Failed to create MWL entry from appointment";
        case siu_error::mwl_update_failed:
            return "Failed to update MWL entry from appointment";
        case siu_error::mwl_cancel_failed:
            return "Failed to cancel MWL entry from appointment";
        case siu_error::duplicate_appointment:
            return "Duplicate appointment already exists";
        case siu_error::invalid_appointment_data:
            return "Invalid appointment data in message";
        case siu_error::processing_failed:
            return "SIU message processing failed";
        default:
            return "Unknown SIU handler error";
    }
}

/**
 * @brief Convert siu_error to error_info for Result<T>
 *
 * @param error SIU error code
 * @param details Optional additional details
 * @return error_info for use with Result<T>
 */
[[nodiscard]] inline error_info to_error_info(
    siu_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "hl7::siu",
        details
    };
}

// =============================================================================
// SIU Trigger Events
// =============================================================================

/**
 * @brief Supported SIU trigger events
 */
enum class siu_trigger_event {
    /** S12 - New Appointment: Create MWL entry */
    s12_new_appointment,

    /** S13 - Rescheduled: Update MWL timing */
    s13_rescheduled,

    /** S14 - Modification: Update MWL details */
    s14_modification,

    /** S15 - Cancellation: Cancel MWL entry */
    s15_cancellation,

    /** Unknown/unsupported trigger event */
    unknown
};

/**
 * @brief Parse trigger event from MSH-9.2 string
 */
[[nodiscard]] constexpr siu_trigger_event parse_siu_trigger_event(
    std::string_view trigger) noexcept {
    if (trigger == "S12") return siu_trigger_event::s12_new_appointment;
    if (trigger == "S13") return siu_trigger_event::s13_rescheduled;
    if (trigger == "S14") return siu_trigger_event::s14_modification;
    if (trigger == "S15") return siu_trigger_event::s15_cancellation;
    return siu_trigger_event::unknown;
}

/**
 * @brief Convert trigger event to string
 */
[[nodiscard]] constexpr const char* to_string(siu_trigger_event event) noexcept {
    switch (event) {
        case siu_trigger_event::s12_new_appointment:
            return "S12";
        case siu_trigger_event::s13_rescheduled:
            return "S13";
        case siu_trigger_event::s14_modification:
            return "S14";
        case siu_trigger_event::s15_cancellation:
            return "S15";
        default:
            return "UNKNOWN";
    }
}

// =============================================================================
// Appointment Status Codes
// =============================================================================

/**
 * @brief Appointment filler status codes (SCH-25)
 */
enum class appointment_status {
    /** Pending - Not yet confirmed */
    pending,

    /** Booked - Confirmed appointment */
    booked,

    /** Arrived - Patient has arrived */
    arrived,

    /** Started - Procedure has started */
    started,

    /** Complete - Procedure completed */
    complete,

    /** Cancelled - Appointment cancelled */
    cancelled,

    /** No-Show - Patient did not appear */
    no_show,

    /** Unknown status */
    unknown
};

/**
 * @brief Parse appointment status from SCH-25 string
 */
[[nodiscard]] constexpr appointment_status parse_appointment_status(
    std::string_view sch25) noexcept {
    if (sch25 == "Pending") return appointment_status::pending;
    if (sch25 == "Booked") return appointment_status::booked;
    if (sch25 == "Arrived") return appointment_status::arrived;
    if (sch25 == "Started") return appointment_status::started;
    if (sch25 == "Complete") return appointment_status::complete;
    if (sch25 == "Cancelled") return appointment_status::cancelled;
    if (sch25 == "No-Show" || sch25 == "NoShow")
        return appointment_status::no_show;
    return appointment_status::unknown;
}

/**
 * @brief Convert appointment status to MWL SPS status string
 */
[[nodiscard]] constexpr const char* to_mwl_status(
    appointment_status status) noexcept {
    switch (status) {
        case appointment_status::pending:
        case appointment_status::booked:
            return "SCHEDULED";
        case appointment_status::arrived:
        case appointment_status::started:
            return "STARTED";
        case appointment_status::complete:
            return "COMPLETED";
        case appointment_status::cancelled:
        case appointment_status::no_show:
            return "DISCONTINUED";
        default:
            return "SCHEDULED";
    }
}

// =============================================================================
// SIU Processing Result
// =============================================================================

/**
 * @brief Result of SIU message processing
 */
struct siu_result {
    /** Processing was successful */
    bool success = false;

    /** Trigger event that was processed */
    siu_trigger_event trigger = siu_trigger_event::unknown;

    /** Appointment status from message */
    appointment_status status = appointment_status::unknown;

    /** Placer appointment ID (SCH-1) */
    std::string placer_appointment_id;

    /** Filler appointment ID (SCH-2) */
    std::string filler_appointment_id;

    /** Patient ID associated with the appointment */
    std::string patient_id;

    /** Scheduled start date/time */
    std::string scheduled_datetime;

    /** Study Instance UID (from mapping or generated) */
    std::string study_instance_uid;

    /** Description of what was done */
    std::string description;

    /** ACK response message */
    hl7_message ack_message;

    /** Processing warnings (non-fatal issues) */
    std::vector<std::string> warnings;
};

// =============================================================================
// SIU Handler Configuration
// =============================================================================

/**
 * @brief SIU handler configuration
 */
struct siu_handler_config {
    /** Allow update on S12 if appointment already exists */
    bool allow_s12_update = false;

    /** Create appointment on S13/S14 if not exists */
    bool allow_reschedule_create = false;

    /** Auto-generate Study Instance UID */
    bool auto_generate_study_uid = true;

    /** Validate appointment data before MWL operation */
    bool validate_appointment_data = true;

    /** Required fields for appointment validation */
    std::vector<std::string> required_fields = {
        "patient_id", "patient_name", "appointment_id"};

    /** Generate detailed ACK messages */
    bool detailed_ack = true;

    /** Log all appointments for audit */
    bool audit_logging = true;

    /** Custom ACK application name */
    std::string ack_sending_application = "PACS_BRIDGE";

    /** Custom ACK facility name */
    std::string ack_sending_facility = "RADIOLOGY";

    /** UID root for Study Instance UID generation */
    std::string study_uid_root = "1.2.840.10008.5.1.4";
};

// =============================================================================
// Appointment Information
// =============================================================================

/**
 * @brief Extracted appointment information from SIU message
 */
struct appointment_info {
    /** Trigger event */
    siu_trigger_event trigger = siu_trigger_event::unknown;

    /** Filler status (SCH-25) */
    appointment_status status = appointment_status::unknown;

    /** Placer Appointment ID (SCH-1) */
    std::string placer_appointment_id;

    /** Filler Appointment ID (SCH-2) */
    std::string filler_appointment_id;

    /** Appointment timing quantity - duration (SCH-11) */
    std::string duration;

    /** Requested Start Date/Time (SCH-16) */
    std::string requested_start_datetime;

    /** Appointment timing - scheduled date/time (SCH-11.4) */
    std::string scheduled_datetime;

    /** Patient ID (PID-3) */
    std::string patient_id;

    /** Patient name (PID-5) */
    std::string patient_name;

    /** Resource identifier (AIS-3) */
    std::string resource_id;

    /** Resource type (AIS-4) */
    std::string resource_type;

    /** Start date/time from AIS (AIS-4) */
    std::string ais_start_datetime;

    /** Procedure code */
    std::string procedure_code;

    /** Procedure description */
    std::string procedure_description;

    /** Study Instance UID (mapped or generated) */
    std::string study_instance_uid;

    /** Original HL7 message control ID */
    std::string message_control_id;
};

// =============================================================================
// SIU Handler
// =============================================================================

/**
 * @brief SIU message handler for appointment-based MWL management
 *
 * Processes SIU (Scheduling Information Unsolicited) messages to create,
 * update, and cancel Modality Worklist entries based on appointment data.
 * Generates appropriate ACK responses.
 *
 * @example Basic Usage
 * ```cpp
 * // Create handler with MWL client
 * auto mwl = std::make_shared<pacs_adapter::mwl_client>(config);
 * mwl->connect();
 *
 * siu_handler handler(mwl);
 *
 * // Process SIU message
 * auto result = handler.handle(siu_message);
 * if (result) {
 *     std::cout << "Processed: " << result->description << std::endl;
 *     // Send ACK back
 *     send_response(result->ack_message);
 * }
 * ```
 *
 * @example With Configuration
 * ```cpp
 * siu_handler_config config;
 * config.auto_generate_study_uid = true;
 * config.audit_logging = true;
 *
 * siu_handler handler(mwl, config);
 * ```
 *
 * @example Custom Event Handlers
 * ```cpp
 * handler.on_appointment_created([](const appointment_info& appt) {
 *     log_audit("Appointment created: " + appt.placer_appointment_id);
 * });
 *
 * handler.on_appointment_cancelled([](const std::string& appt_id) {
 *     log_audit("Appointment cancelled: " + appt_id);
 * });
 * ```
 */
class siu_handler {
public:
    // =========================================================================
    // Callback Types
    // =========================================================================

    /** Callback for appointment creation */
    using appointment_created_callback =
        std::function<void(const appointment_info& appointment,
                           const mapping::mwl_item& mwl)>;

    /** Callback for appointment update */
    using appointment_updated_callback =
        std::function<void(const appointment_info& appointment,
                           const mapping::mwl_item& old_mwl,
                           const mapping::mwl_item& new_mwl)>;

    /** Callback for appointment cancellation */
    using appointment_cancelled_callback =
        std::function<void(const std::string& appointment_id,
                           const std::string& reason)>;

    /** Callback for status change */
    using status_changed_callback =
        std::function<void(const std::string& appointment_id,
                           appointment_status old_status,
                           appointment_status new_status)>;

    // =========================================================================
    // Construction
    // =========================================================================

    /**
     * @brief Construct handler with MWL client
     *
     * @param mwl_client Shared pointer to MWL client for pacs_system
     */
    explicit siu_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client);

    /**
     * @brief Construct handler with MWL client and configuration
     *
     * @param mwl_client Shared pointer to MWL client
     * @param config Handler configuration
     */
    siu_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                const siu_handler_config& config);

    /**
     * @brief Construct handler with MWL client and HL7-DICOM mapper
     *
     * @param mwl_client Shared pointer to MWL client
     * @param mapper Shared pointer to HL7-DICOM mapper
     */
    siu_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                std::shared_ptr<mapping::hl7_dicom_mapper> mapper);

    /**
     * @brief Full constructor with all dependencies
     *
     * @param mwl_client Shared pointer to MWL client
     * @param mapper Shared pointer to HL7-DICOM mapper
     * @param config Handler configuration
     */
    siu_handler(std::shared_ptr<pacs_adapter::mwl_client> mwl_client,
                std::shared_ptr<mapping::hl7_dicom_mapper> mapper,
                const siu_handler_config& config);

    /**
     * @brief Destructor
     */
    ~siu_handler();

    // Non-copyable, movable
    siu_handler(const siu_handler&) = delete;
    siu_handler& operator=(const siu_handler&) = delete;
    siu_handler(siu_handler&&) noexcept;
    siu_handler& operator=(siu_handler&&) noexcept;

    // =========================================================================
    // Message Handling
    // =========================================================================

    /**
     * @brief Handle SIU message
     *
     * Processes the SIU message and performs the appropriate MWL operation
     * based on the trigger event. Returns a result containing the
     * ACK message to send back.
     *
     * @param message HL7 SIU message
     * @return Processing result or error
     */
    [[nodiscard]] Result<siu_result> handle(
        const hl7_message& message);

    /**
     * @brief Check if message can be handled
     *
     * @param message HL7 message
     * @return true if this handler can process the message
     */
    [[nodiscard]] bool can_handle(const hl7_message& message) const noexcept;

    /**
     * @brief Get supported trigger events
     *
     * @return List of supported trigger event strings
     */
    [[nodiscard]] std::vector<std::string> supported_triggers() const;

    // =========================================================================
    // Individual Trigger Event Handlers
    // =========================================================================

    /**
     * @brief Handle S12 (New Appointment) trigger
     *
     * Creates a new MWL entry for the appointment. If the appointment already
     * exists and allow_s12_update is true, updates the existing entry.
     *
     * @param message HL7 SIU^S12 message
     * @return Processing result or error
     */
    [[nodiscard]] Result<siu_result> handle_s12(
        const hl7_message& message);

    /**
     * @brief Handle S13 (Rescheduled) trigger
     *
     * Updates the timing information of an existing MWL entry.
     * If the appointment doesn't exist and allow_reschedule_create is true,
     * creates a new entry.
     *
     * @param message HL7 SIU^S13 message
     * @return Processing result or error
     */
    [[nodiscard]] Result<siu_result> handle_s13(
        const hl7_message& message);

    /**
     * @brief Handle S14 (Modification) trigger
     *
     * Updates details of an existing MWL entry.
     *
     * @param message HL7 SIU^S14 message
     * @return Processing result or error
     */
    [[nodiscard]] Result<siu_result> handle_s14(
        const hl7_message& message);

    /**
     * @brief Handle S15 (Cancellation) trigger
     *
     * Removes or marks as cancelled the MWL entry for the appointment.
     *
     * @param message HL7 SIU^S15 message
     * @return Processing result or error
     */
    [[nodiscard]] Result<siu_result> handle_s15(
        const hl7_message& message);

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Extract appointment information from SIU message
     *
     * Parses the SIU message and extracts relevant appointment information
     * including trigger event, status, appointment IDs, patient data, etc.
     *
     * @param message HL7 SIU message
     * @return Extracted appointment information or error
     */
    [[nodiscard]] Result<appointment_info>
    extract_appointment_info(const hl7_message& message) const;

    /**
     * @brief Generate ACK response for SIU message
     *
     * Creates an ACK response message based on the processing result.
     *
     * @param original Original SIU message
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
     * @brief Set callback for appointment creation
     */
    void on_appointment_created(appointment_created_callback callback);

    /**
     * @brief Set callback for appointment update
     */
    void on_appointment_updated(appointment_updated_callback callback);

    /**
     * @brief Set callback for appointment cancellation
     */
    void on_appointment_cancelled(appointment_cancelled_callback callback);

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
    [[nodiscard]] const siu_handler_config& config() const noexcept;

    /**
     * @brief Set configuration
     */
    void set_config(const siu_handler_config& config);

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

        /** S12 (New Appointment) messages processed */
        size_t s12_count = 0;

        /** S13 (Rescheduled) messages processed */
        size_t s13_count = 0;

        /** S14 (Modification) messages processed */
        size_t s14_count = 0;

        /** S15 (Cancellation) messages processed */
        size_t s15_count = 0;

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

#endif  // PACS_BRIDGE_PROTOCOL_HL7_SIU_HANDLER_H
