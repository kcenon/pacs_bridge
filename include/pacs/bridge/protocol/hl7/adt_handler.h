#ifndef PACS_BRIDGE_PROTOCOL_HL7_ADT_HANDLER_H
#define PACS_BRIDGE_PROTOCOL_HL7_ADT_HANDLER_H

/**
 * @file adt_handler.h
 * @brief ADT (Admission, Discharge, Transfer) message handler
 *
 * Provides handlers for ADT messages to maintain the patient demographics
 * cache. Supports the following trigger events:
 *   - A01: Admit/Visit Notification
 *   - A04: Register a Patient
 *   - A08: Update Patient Information
 *   - A40: Merge Patient - Patient Identifier List
 *
 * The handler integrates with:
 *   - HL7 Parser for message parsing
 *   - HL7-DICOM Mapper for patient data extraction
 *   - Patient Cache for demographic storage
 *   - C++20 Concepts for callback type safety (Issue #70)
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/14
 * @see https://github.com/kcenon/pacs_bridge/issues/70
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include "pacs/bridge/cache/patient_cache.h"
#include "pacs/bridge/concepts/bridge_concepts.h"
#include "pacs/bridge/mapping/hl7_dicom_mapper.h"
#include "pacs/bridge/protocol/hl7/hl7_handler_base.h"
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::hl7 {

// =============================================================================
// ADT Handler Error Codes (-850 to -859)
// =============================================================================

/**
 * @brief ADT handler specific error codes
 *
 * Allocated range: -850 to -859
 */
enum class adt_error : int {
    /** Message is not an ADT message */
    not_adt_message = -850,

    /** Unsupported ADT trigger event */
    unsupported_trigger_event = -851,

    /** Patient ID not found in message */
    missing_patient_id = -852,

    /** Patient not found for update/merge */
    patient_not_found = -853,

    /** Merge operation failed */
    merge_failed = -854,

    /** Cache operation failed */
    cache_operation_failed = -855,

    /** Invalid patient data */
    invalid_patient_data = -856,

    /** Duplicate patient */
    duplicate_patient = -857,

    /** Handler not registered */
    handler_not_registered = -858,

    /** Processing failed */
    processing_failed = -859
};

/**
 * @brief Convert adt_error to error code
 */
[[nodiscard]] constexpr int to_error_code(adt_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(adt_error error) noexcept {
    switch (error) {
        case adt_error::not_adt_message:
            return "Message is not an ADT message";
        case adt_error::unsupported_trigger_event:
            return "Unsupported ADT trigger event";
        case adt_error::missing_patient_id:
            return "Patient ID not found in message";
        case adt_error::patient_not_found:
            return "Patient not found for update/merge";
        case adt_error::merge_failed:
            return "Patient merge operation failed";
        case adt_error::cache_operation_failed:
            return "Cache operation failed";
        case adt_error::invalid_patient_data:
            return "Invalid patient data in message";
        case adt_error::duplicate_patient:
            return "Duplicate patient record exists";
        case adt_error::handler_not_registered:
            return "Handler not registered for trigger event";
        case adt_error::processing_failed:
            return "ADT message processing failed";
        default:
            return "Unknown ADT handler error";
    }
}

/**
 * @brief Convert adt_error to error_info for Result<T>
 *
 * @param error ADT error code
 * @param details Optional additional details
 * @return error_info for use with Result<T>
 */
[[nodiscard]] inline error_info to_error_info(
    adt_error error,
    const std::string& details = "") {
    return error_info{
        static_cast<int>(error),
        to_string(error),
        "hl7::adt",
        details
    };
}

// =============================================================================
// ADT Trigger Events
// =============================================================================

/**
 * @brief Supported ADT trigger events
 */
enum class adt_trigger_event {
    /** A01 - Admit/Visit Notification */
    A01,

    /** A04 - Register a Patient */
    A04,

    /** A08 - Update Patient Information */
    A08,

    /** A40 - Merge Patient - Patient Identifier List */
    A40,

    /** Unknown/unsupported trigger event */
    unknown
};

/**
 * @brief Parse trigger event from string
 */
[[nodiscard]] constexpr adt_trigger_event parse_adt_trigger(
    std::string_view trigger) noexcept {
    if (trigger == "A01") return adt_trigger_event::A01;
    if (trigger == "A04") return adt_trigger_event::A04;
    if (trigger == "A08") return adt_trigger_event::A08;
    if (trigger == "A40") return adt_trigger_event::A40;
    return adt_trigger_event::unknown;
}

/**
 * @brief Convert trigger event to string
 */
[[nodiscard]] constexpr const char* to_string(adt_trigger_event event) noexcept {
    switch (event) {
        case adt_trigger_event::A01:
            return "A01";
        case adt_trigger_event::A04:
            return "A04";
        case adt_trigger_event::A08:
            return "A08";
        case adt_trigger_event::A40:
            return "A40";
        default:
            return "UNKNOWN";
    }
}

// =============================================================================
// ADT Processing Result
// =============================================================================

/**
 * @brief Result of ADT message processing
 */
struct adt_result {
    /** Processing was successful */
    bool success = false;

    /** Trigger event that was processed */
    adt_trigger_event trigger = adt_trigger_event::unknown;

    /** Patient ID that was affected */
    std::string patient_id;

    /** Secondary patient ID (for merges) */
    std::string merged_patient_id;

    /** Description of what was done */
    std::string description;

    /** ACK response message */
    hl7_message ack_message;

    /** Processing warnings (non-fatal issues) */
    std::vector<std::string> warnings;
};

// =============================================================================
// ADT Handler Configuration
// =============================================================================

/**
 * @brief ADT handler configuration
 */
struct adt_handler_config {
    /** Update existing patient on A01 if already exists */
    bool allow_a01_update = true;

    /** Create patient on A08 if not exists */
    bool allow_a08_create = false;

    /** Validate patient data before caching */
    bool validate_patient_data = true;

    /** Required fields for patient validation */
    std::vector<std::string> required_fields = {"patient_id", "patient_name"};

    /** Generate detailed ACK messages */
    bool detailed_ack = true;

    /** Log all patient updates for audit */
    bool audit_logging = true;

    /** Custom ACK application name */
    std::string ack_sending_application = "PACS_BRIDGE";

    /** Custom ACK facility name */
    std::string ack_sending_facility = "RADIOLOGY";
};

// =============================================================================
// Merge Information
// =============================================================================

/**
 * @brief Information about patient merge operation
 */
struct merge_info {
    /** Primary (surviving) patient ID */
    std::string primary_patient_id;

    /** Secondary (merged) patient ID */
    std::string secondary_patient_id;

    /** Primary patient issuer */
    std::string primary_issuer;

    /** Secondary patient issuer */
    std::string secondary_issuer;

    /** Merge timestamp */
    std::string merge_datetime;
};

// =============================================================================
// ADT Handler
// =============================================================================

/**
 * @brief ADT message handler
 *
 * Processes ADT (Admission, Discharge, Transfer) messages to maintain
 * patient demographics in the cache. Generates appropriate ACK responses.
 *
 * @example Basic Usage
 * ```cpp
 * // Create handler with patient cache
 * auto cache = std::make_shared<cache::patient_cache>();
 * adt_handler handler(cache);
 *
 * // Process ADT message
 * auto result = handler.handle(adt_message);
 * if (result) {
 *     std::cout << "Processed: " << result->description << std::endl;
 *     // Send ACK back
 *     send_response(result->ack_message);
 * }
 * ```
 *
 * @example With Configuration
 * ```cpp
 * adt_handler_config config;
 * config.allow_a08_create = true;
 * config.audit_logging = true;
 *
 * adt_handler handler(cache, config);
 * ```
 *
 * @example Custom Event Handler
 * ```cpp
 * handler.on_patient_created([](const mapping::dicom_patient& patient) {
 *     log_audit("Patient created: " + patient.patient_id);
 * });
 *
 * handler.on_patient_updated([](const mapping::dicom_patient& old_patient,
 *                               const mapping::dicom_patient& new_patient) {
 *     log_audit("Patient updated: " + new_patient.patient_id);
 * });
 * ```
 */
class adt_handler : public HL7HandlerBase<adt_handler> {
    friend class HL7HandlerBase<adt_handler>;

public:
    // =========================================================================
    // CRTP Static Members
    // =========================================================================

    /** Handler type identifier for CRTP */
    static constexpr std::string_view type_name = "ADT";

    // =========================================================================
    // Callback Types
    // =========================================================================

    /** Callback for patient creation */
    using patient_created_callback =
        std::function<void(const mapping::dicom_patient& patient)>;

    /** Callback for patient update */
    using patient_updated_callback =
        std::function<void(const mapping::dicom_patient& old_patient,
                           const mapping::dicom_patient& new_patient)>;

    /** Callback for patient merge */
    using patient_merged_callback =
        std::function<void(const merge_info& merge)>;

    // =========================================================================
    // Construction
    // =========================================================================

    /**
     * @brief Construct handler with patient cache
     *
     * @param cache Shared pointer to patient cache
     */
    explicit adt_handler(std::shared_ptr<cache::patient_cache> cache);

    /**
     * @brief Construct handler with cache and configuration
     *
     * @param cache Shared pointer to patient cache
     * @param config Handler configuration
     */
    adt_handler(std::shared_ptr<cache::patient_cache> cache,
                const adt_handler_config& config);

    /**
     * @brief Destructor
     */
    ~adt_handler();

    // Non-copyable, movable
    adt_handler(const adt_handler&) = delete;
    adt_handler& operator=(const adt_handler&) = delete;
    adt_handler(adt_handler&&) noexcept;
    adt_handler& operator=(adt_handler&&) noexcept;

    // =========================================================================
    // Message Handling
    // =========================================================================

    /**
     * @brief Handle ADT message
     *
     * Processes the ADT message and updates the patient cache accordingly.
     * Returns a result containing the ACK message to send back.
     *
     * @param message HL7 ADT message
     * @return Processing result or error
     */
    [[nodiscard]] Result<adt_result> handle(
        const hl7_message& message);

    // Note: can_handle() is provided by HL7HandlerBase via CRTP

    /**
     * @brief Get supported trigger events
     *
     * @return List of supported trigger event strings
     */
    [[nodiscard]] std::vector<std::string> supported_triggers() const;

    // =========================================================================
    // Individual Event Handlers
    // =========================================================================

    /**
     * @brief Handle A01 (Admit) event
     *
     * Creates a new patient record in the cache, or updates if configured
     * to allow updates on admit.
     *
     * @param message HL7 ADT^A01 message
     * @return Processing result or error
     */
    [[nodiscard]] Result<adt_result> handle_admit(
        const hl7_message& message);

    /**
     * @brief Handle A04 (Register) event
     *
     * Creates a new outpatient record in the cache.
     *
     * @param message HL7 ADT^A04 message
     * @return Processing result or error
     */
    [[nodiscard]] Result<adt_result> handle_register(
        const hl7_message& message);

    /**
     * @brief Handle A08 (Update) event
     *
     * Updates existing patient demographics in the cache.
     *
     * @param message HL7 ADT^A08 message
     * @return Processing result or error
     */
    [[nodiscard]] Result<adt_result> handle_update(
        const hl7_message& message);

    /**
     * @brief Handle A40 (Merge) event
     *
     * Merges patient records in the cache, transferring data from
     * the secondary patient to the primary patient.
     *
     * @param message HL7 ADT^A40 message
     * @return Processing result or error
     */
    [[nodiscard]] Result<adt_result> handle_merge(
        const hl7_message& message);

    // =========================================================================
    // Callbacks
    // =========================================================================

    /**
     * @brief Set callback for patient creation
     */
    void on_patient_created(patient_created_callback callback);

    /**
     * @brief Set callback for patient update
     */
    void on_patient_updated(patient_updated_callback callback);

    /**
     * @brief Set callback for patient merge
     */
    void on_patient_merged(patient_merged_callback callback);

    /**
     * @brief Set callback for patient creation (concept-constrained)
     *
     * Template version using C++20 Concepts for compile-time validation.
     *
     * @tparam Callback Type satisfying concepts::EventCallback<dicom_patient>
     * @param callback Function to call when patient is created
     *
     * @see concepts::EventCallback
     */
    template <concepts::EventCallback<mapping::dicom_patient> Callback>
    void on_patient_created_v2(Callback&& callback) {
        on_patient_created(
            patient_created_callback(std::forward<Callback>(callback)));
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const adt_handler_config& config() const noexcept;

    /**
     * @brief Set configuration
     */
    void set_config(const adt_handler_config& config);

    /**
     * @brief Get the patient cache
     */
    [[nodiscard]] std::shared_ptr<cache::patient_cache> cache() const noexcept;

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

        /** A01 messages processed */
        size_t a01_count = 0;

        /** A04 messages processed */
        size_t a04_count = 0;

        /** A08 messages processed */
        size_t a08_count = 0;

        /** A40 messages processed */
        size_t a40_count = 0;

        /** Patients created */
        size_t patients_created = 0;

        /** Patients updated */
        size_t patients_updated = 0;

        /** Patients merged */
        size_t patients_merged = 0;
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
    // =========================================================================
    // CRTP Implementation
    // =========================================================================

    /**
     * @brief CRTP implementation for can_handle
     *
     * Called by HL7HandlerBase::can_handle() via static dispatch.
     *
     * @param message HL7 message to check
     * @return true if this handler can process the message
     */
    [[nodiscard]] bool can_handle_impl(
        const hl7_message& message) const noexcept;

    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_ADT_HANDLER_H
