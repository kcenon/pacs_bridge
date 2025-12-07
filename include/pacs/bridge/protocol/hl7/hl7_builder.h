#ifndef PACS_BRIDGE_PROTOCOL_HL7_HL7_BUILDER_H
#define PACS_BRIDGE_PROTOCOL_HL7_HL7_BUILDER_H

/**
 * @file hl7_builder.h
 * @brief HL7 v2.x message builder with fluent API
 *
 * Provides a fluent builder interface for constructing HL7 v2.x messages.
 * Supports common message types (ADT, ORM, ORU, ACK) with type-safe
 * field setting and automatic MSH generation.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/10
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include "hl7_message.h"
#include "hl7_types.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::hl7 {

// =============================================================================
// Builder Options
// =============================================================================

/**
 * @brief Message builder configuration
 */
struct builder_options {
    /** Sending application name (MSH-3) */
    std::string sending_application;

    /** Sending facility (MSH-4) */
    std::string sending_facility;

    /** Receiving application name (MSH-5) */
    std::string receiving_application;

    /** Receiving facility (MSH-6) */
    std::string receiving_facility;

    /** HL7 version to use (default: 2.5.1) */
    std::string version = "2.5.1";

    /** Processing ID (P=Production, T=Training, D=Debug) */
    std::string processing_id = "P";

    /** Character set (default: UNICODE UTF-8) */
    std::string character_set = "UNICODE UTF-8";

    /** Country code */
    std::string country_code = "USA";

    /** Encoding characters (default: ^~\&) */
    hl7_encoding_characters encoding;

    /** Auto-generate message control ID if not set */
    bool auto_generate_control_id = true;

    /** Auto-set timestamp to current time if not set */
    bool auto_timestamp = true;

    /** Validate message before building */
    bool validate_before_build = false;
};

// =============================================================================
// HL7 Message Builder
// =============================================================================

/**
 * @brief Fluent builder for HL7 messages
 *
 * Provides a convenient fluent API for constructing HL7 messages with
 * proper MSH segment initialization and common field helpers.
 *
 * @example Building an ADT^A01 Message
 * ```cpp
 * auto result = hl7_builder::create()
 *     .sending_app("HIS")
 *     .sending_facility("HOSPITAL")
 *     .receiving_app("PACS")
 *     .receiving_facility("IMAGING")
 *     .message_type("ADT", "A01")
 *     .patient_id("12345", "MRN")
 *     .patient_name("DOE", "JOHN", "M")
 *     .patient_birth_date(1980, 5, 15)
 *     .patient_sex("M")
 *     .build();
 *
 * if (result) {
 *     std::string msg = result->serialize();
 * }
 * ```
 *
 * @example Building an ACK Message
 * ```cpp
 * auto ack = hl7_builder::create_ack(
 *     original_message, ack_code::AA, "Message processed");
 * ```
 *
 * @example Building an ORM^O01 Order Message
 * ```cpp
 * auto result = hl7_builder::create()
 *     .message_type("ORM", "O01")
 *     .patient_id("12345")
 *     .patient_name("DOE", "JOHN")
 *     .order_control("NW")  // New order
 *     .placer_order_number("ORD001")
 *     .procedure_code("71020", "CHEST XRAY PA AND LAT")
 *     .build();
 * ```
 */
class hl7_builder {
public:
    /**
     * @brief Create a new builder with default options
     */
    [[nodiscard]] static hl7_builder create();

    /**
     * @brief Create a builder with custom options
     */
    [[nodiscard]] static hl7_builder create(const builder_options& options);

    /**
     * @brief Create an ACK message for a received message
     *
     * @param original Original message to acknowledge
     * @param code Acknowledgment code
     * @param text Acknowledgment text (optional)
     * @return ACK message
     */
    [[nodiscard]] static hl7_message create_ack(const hl7_message& original,
                                                 ack_code code,
                                                 std::string_view text = "");

    /**
     * @brief Create an ACK message from header info
     *
     * @param original_header Header of original message
     * @param code Acknowledgment code
     * @param text Acknowledgment text (optional)
     * @return ACK message
     */
    [[nodiscard]] static hl7_message create_ack(
        const hl7_message_header& original_header, ack_code code,
        std::string_view text = "");

    // Constructors/destructors
    hl7_builder();
    ~hl7_builder();
    hl7_builder(hl7_builder&&) noexcept;
    hl7_builder& operator=(hl7_builder&&) noexcept;

    // =========================================================================
    // MSH Segment Fields
    // =========================================================================

    /** Set sending application (MSH-3) */
    hl7_builder& sending_app(std::string_view app);

    /** Set sending facility (MSH-4) */
    hl7_builder& sending_facility(std::string_view facility);

    /** Set receiving application (MSH-5) */
    hl7_builder& receiving_app(std::string_view app);

    /** Set receiving facility (MSH-6) */
    hl7_builder& receiving_facility(std::string_view facility);

    /** Set message timestamp (MSH-7) - defaults to now */
    hl7_builder& timestamp(const hl7_timestamp& ts);

    /** Set message type and trigger event (MSH-9) */
    hl7_builder& message_type(std::string_view type, std::string_view trigger);

    /** Set message control ID (MSH-10) - auto-generated if not set */
    hl7_builder& control_id(std::string_view id);

    /** Set processing ID (MSH-11) */
    hl7_builder& processing_id(std::string_view id);

    /** Set HL7 version (MSH-12) */
    hl7_builder& version(std::string_view ver);

    /** Set security field (MSH-8) */
    hl7_builder& security(std::string_view sec);

    // =========================================================================
    // PID Segment Fields (Patient Identification)
    // =========================================================================

    /** Set patient ID (PID-3) */
    hl7_builder& patient_id(std::string_view id,
                            std::string_view assigning_authority = "",
                            std::string_view id_type = "MR");

    /** Set patient name (PID-5) */
    hl7_builder& patient_name(std::string_view family, std::string_view given,
                              std::string_view middle = "",
                              std::string_view suffix = "",
                              std::string_view prefix = "");

    /** Set patient name from hl7_person_name */
    hl7_builder& patient_name(const hl7_person_name& name);

    /** Set patient birth date (PID-7) */
    hl7_builder& patient_birth_date(int year, int month, int day);

    /** Set patient birth date from timestamp */
    hl7_builder& patient_birth_date(const hl7_timestamp& ts);

    /** Set patient sex (PID-8) - M, F, O, U, A, N */
    hl7_builder& patient_sex(std::string_view sex);

    /** Set patient address (PID-11) */
    hl7_builder& patient_address(const hl7_address& addr);

    /** Set patient phone (PID-13) */
    hl7_builder& patient_phone(std::string_view phone);

    /** Set patient SSN (PID-19) */
    hl7_builder& patient_ssn(std::string_view ssn);

    /** Set patient account number (PID-18) */
    hl7_builder& patient_account(std::string_view account);

    // =========================================================================
    // PV1 Segment Fields (Patient Visit)
    // =========================================================================

    /** Set patient class (PV1-2) - I=Inpatient, O=Outpatient, E=Emergency, etc. */
    hl7_builder& patient_class(std::string_view pclass);

    /** Set assigned patient location (PV1-3) */
    hl7_builder& patient_location(std::string_view point_of_care,
                                   std::string_view room = "",
                                   std::string_view bed = "");

    /** Set admission type (PV1-4) */
    hl7_builder& admission_type(std::string_view type);

    /** Set attending doctor (PV1-7) */
    hl7_builder& attending_doctor(std::string_view id, std::string_view family,
                                   std::string_view given);

    /** Set referring doctor (PV1-8) */
    hl7_builder& referring_doctor(std::string_view id, std::string_view family,
                                   std::string_view given);

    /** Set visit number (PV1-19) */
    hl7_builder& visit_number(std::string_view number);

    /** Set admit date/time (PV1-44) */
    hl7_builder& admit_datetime(const hl7_timestamp& ts);

    // =========================================================================
    // ORC Segment Fields (Common Order)
    // =========================================================================

    /** Set order control (ORC-1) - NW=New, SC=Status Changed, CA=Cancel, etc. */
    hl7_builder& order_control(std::string_view control);

    /** Set placer order number (ORC-2) */
    hl7_builder& placer_order_number(std::string_view number,
                                      std::string_view namespace_id = "");

    /** Set filler order number (ORC-3) */
    hl7_builder& filler_order_number(std::string_view number,
                                      std::string_view namespace_id = "");

    /** Set order status (ORC-5) */
    hl7_builder& order_status(std::string_view status);

    /** Set ordering provider (ORC-12) */
    hl7_builder& ordering_provider(std::string_view id, std::string_view family,
                                    std::string_view given);

    /** Set entered by (ORC-10) */
    hl7_builder& entered_by(std::string_view id, std::string_view family,
                            std::string_view given);

    /** Set order effective date/time (ORC-15) */
    hl7_builder& order_effective_datetime(const hl7_timestamp& ts);

    // =========================================================================
    // OBR Segment Fields (Observation Request)
    // =========================================================================

    /** Set procedure/universal service ID (OBR-4) */
    hl7_builder& procedure_code(std::string_view code,
                                 std::string_view description,
                                 std::string_view coding_system = "");

    /** Set priority (OBR-5) */
    hl7_builder& priority(std::string_view priority);

    /** Set requested date/time (OBR-6) */
    hl7_builder& requested_datetime(const hl7_timestamp& ts);

    /** Set observation date/time (OBR-7) */
    hl7_builder& observation_datetime(const hl7_timestamp& ts);

    /** Set clinical info/reason for study (OBR-13) */
    hl7_builder& clinical_info(std::string_view info);

    /** Set specimen source (OBR-15) */
    hl7_builder& specimen_source(std::string_view source);

    /** Set result status (OBR-25) */
    hl7_builder& result_status(std::string_view status);

    // =========================================================================
    // OBX Segment Fields (Observation/Result)
    // =========================================================================

    /**
     * @brief Add an OBX segment
     *
     * @param value_type Value type (ST, NM, TX, etc.)
     * @param observation_id Observation identifier
     * @param value Observation value
     * @param units Units (optional)
     */
    hl7_builder& add_observation(std::string_view value_type,
                                  std::string_view observation_id,
                                  std::string_view value,
                                  std::string_view units = "");

    // =========================================================================
    // Generic Segment/Field Access
    // =========================================================================

    /**
     * @brief Set a field value by path
     *
     * @param path Path like "PID.5.1" or "OBR.4"
     * @param value Field value
     */
    hl7_builder& set_field(std::string_view path, std::string_view value);

    /**
     * @brief Add a custom segment
     *
     * @param segment_id Segment ID
     * @return Reference to the added segment for direct manipulation
     */
    hl7_segment& add_segment(std::string_view segment_id);

    /**
     * @brief Get a segment for modification
     *
     * @param segment_id Segment ID
     * @param occurrence Occurrence index (0-based)
     * @return Pointer to segment or nullptr if not found
     */
    hl7_segment* get_segment(std::string_view segment_id,
                             size_t occurrence = 0);

    // =========================================================================
    // Build
    // =========================================================================

    /**
     * @brief Build the message
     *
     * @return Built message or error if validation fails
     */
    [[nodiscard]] std::expected<hl7_message, hl7_error> build();

    /**
     * @brief Build and serialize to string
     *
     * @return Serialized message or error
     */
    [[nodiscard]] std::expected<std::string, hl7_error> build_string();

    /**
     * @brief Get current message being built (for inspection)
     *
     * The message is not validated and may be incomplete.
     */
    [[nodiscard]] const hl7_message& message() const;

    /**
     * @brief Reset builder to initial state
     */
    void reset();

private:
    explicit hl7_builder(const builder_options& options);

    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Specialized Message Builders
// =============================================================================

/**
 * @brief ADT (Admission, Discharge, Transfer) message builder
 *
 * Specialized builder for ADT messages with convenient helpers
 * for common trigger events.
 */
class adt_builder {
public:
    /**
     * @brief Create ADT^A01 (Admit) message builder
     */
    [[nodiscard]] static hl7_builder admit();

    /**
     * @brief Create ADT^A02 (Transfer) message builder
     */
    [[nodiscard]] static hl7_builder transfer();

    /**
     * @brief Create ADT^A03 (Discharge) message builder
     */
    [[nodiscard]] static hl7_builder discharge();

    /**
     * @brief Create ADT^A04 (Register) message builder
     */
    [[nodiscard]] static hl7_builder register_patient();

    /**
     * @brief Create ADT^A08 (Update) message builder
     */
    [[nodiscard]] static hl7_builder update();

    /**
     * @brief Create ADT^A40 (Merge) message builder
     */
    [[nodiscard]] static hl7_builder merge();
};

/**
 * @brief ORM (Order) message builder
 *
 * Specialized builder for ORM messages with convenient helpers
 * for order operations.
 */
class orm_builder {
public:
    /**
     * @brief Create ORM^O01 new order message builder
     */
    [[nodiscard]] static hl7_builder new_order();

    /**
     * @brief Create ORM^O01 cancel order message builder
     */
    [[nodiscard]] static hl7_builder cancel_order();

    /**
     * @brief Create ORM^O01 modify order message builder
     */
    [[nodiscard]] static hl7_builder modify_order();

    /**
     * @brief Create ORM^O01 status request message builder
     */
    [[nodiscard]] static hl7_builder status_request();
};

/**
 * @brief ORU (Observation Result) message builder
 *
 * Specialized builder for ORU messages.
 */
class oru_builder {
public:
    /**
     * @brief Create ORU^R01 result message builder
     */
    [[nodiscard]] static hl7_builder result();
};

// =============================================================================
// Message ID Generator
// =============================================================================

/**
 * @brief Generate unique message control IDs
 */
class message_id_generator {
public:
    /**
     * @brief Generate a unique message control ID
     *
     * Format: YYYYMMDDHHMMSS_NNNNNN
     *
     * @return Unique ID string
     */
    [[nodiscard]] static std::string generate();

    /**
     * @brief Generate a UUID-based control ID
     *
     * @return UUID string
     */
    [[nodiscard]] static std::string generate_uuid();

    /**
     * @brief Generate control ID with custom prefix
     *
     * @param prefix Prefix string
     * @return Prefixed unique ID
     */
    [[nodiscard]] static std::string generate_with_prefix(
        std::string_view prefix);
};

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_HL7_BUILDER_H
