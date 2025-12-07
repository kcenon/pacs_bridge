#ifndef PACS_BRIDGE_PROTOCOL_HL7_HL7_MESSAGE_H
#define PACS_BRIDGE_PROTOCOL_HL7_HL7_MESSAGE_H

/**
 * @file hl7_message.h
 * @brief HL7 v2.x message data model
 *
 * Provides the core data structures for representing HL7 v2.x messages
 * with full support for segments, fields, components, and repetitions.
 *
 * The message model supports:
 *   - Hierarchical access: segment.field.component.subcomponent
 *   - Path-based access: "PID.5.1" for patient family name
 *   - Iteration over segments and fields
 *   - Modification and serialization
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/8
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include "hl7_types.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pacs::bridge::hl7 {

// Forward declarations
class hl7_segment;
class hl7_field;

// =============================================================================
// HL7 Subcomponent
// =============================================================================

/**
 * @brief HL7 subcomponent (atomic value unit)
 *
 * The smallest unit of data in an HL7 message. Contains a single
 * string value.
 */
class hl7_subcomponent {
public:
    /** Default constructor */
    hl7_subcomponent() = default;

    /** Construct from value */
    explicit hl7_subcomponent(std::string value) : value_(std::move(value)) {}

    /** Get the value */
    [[nodiscard]] const std::string& value() const noexcept { return value_; }

    /** Set the value */
    void set_value(std::string value) { value_ = std::move(value); }

    /** Check if empty */
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

    /** Implicit conversion to string_view */
    [[nodiscard]] operator std::string_view() const noexcept { return value_; }

    /** Comparison operators */
    [[nodiscard]] bool operator==(const hl7_subcomponent& other) const noexcept {
        return value_ == other.value_;
    }

    [[nodiscard]] bool operator==(std::string_view sv) const noexcept {
        return value_ == sv;
    }

private:
    std::string value_;
};

// =============================================================================
// HL7 Component
// =============================================================================

/**
 * @brief HL7 component (contains subcomponents)
 *
 * A component within a field, which may contain subcomponents
 * separated by the subcomponent separator (&).
 */
class hl7_component {
public:
    /** Default constructor */
    hl7_component() = default;

    /** Construct from simple value (no subcomponents) */
    explicit hl7_component(std::string value);

    /** Get the number of subcomponents */
    [[nodiscard]] size_t subcomponent_count() const noexcept {
        return subcomponents_.size();
    }

    /** Check if component is empty */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get subcomponent by index (1-based per HL7 convention)
     * @param index 1-based subcomponent index
     * @return Reference to subcomponent or empty subcomponent if out of range
     */
    [[nodiscard]] const hl7_subcomponent& subcomponent(size_t index) const;

    /**
     * @brief Get mutable subcomponent, creating if needed
     * @param index 1-based subcomponent index
     * @return Reference to subcomponent
     */
    hl7_subcomponent& subcomponent(size_t index);

    /**
     * @brief Get the simple value (first subcomponent's value)
     * @return The value or empty string if no subcomponents
     */
    [[nodiscard]] std::string_view value() const noexcept;

    /**
     * @brief Set simple value (replaces all subcomponents with single value)
     */
    void set_value(std::string value);

    /**
     * @brief Serialize to HL7 format
     * @param encoding Encoding characters
     * @return Serialized component string
     */
    [[nodiscard]] std::string serialize(
        const hl7_encoding_characters& encoding) const;

    /**
     * @brief Parse from HL7 string
     * @param data Component data
     * @param encoding Encoding characters
     * @return Parsed component
     */
    [[nodiscard]] static hl7_component parse(
        std::string_view data, const hl7_encoding_characters& encoding);

    /** Implicit conversion to string_view (gets first subcomponent value) */
    [[nodiscard]] operator std::string_view() const noexcept { return value(); }

    /** Comparison */
    [[nodiscard]] bool operator==(std::string_view sv) const noexcept {
        return value() == sv;
    }

private:
    std::vector<hl7_subcomponent> subcomponents_;
    static const hl7_subcomponent empty_subcomponent_;
};

// =============================================================================
// HL7 Field
// =============================================================================

/**
 * @brief HL7 field (contains components, may repeat)
 *
 * A field within a segment. May contain multiple components separated
 * by the component separator (^), and may repeat using the repetition
 * separator (~).
 */
class hl7_field {
public:
    /** Default constructor */
    hl7_field() = default;

    /** Construct from simple value */
    explicit hl7_field(std::string value);

    /** Get number of repetitions */
    [[nodiscard]] size_t repetition_count() const noexcept {
        return repetitions_.size();
    }

    /** Get number of components in first repetition */
    [[nodiscard]] size_t component_count() const noexcept;

    /** Check if field is empty */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get component by index (1-based) from first repetition
     * @param index 1-based component index
     */
    [[nodiscard]] const hl7_component& component(size_t index) const;

    /**
     * @brief Get mutable component from first repetition
     * @param index 1-based component index
     */
    hl7_component& component(size_t index);

    /**
     * @brief Get component from specific repetition
     * @param rep_index 1-based repetition index
     * @param comp_index 1-based component index
     */
    [[nodiscard]] const hl7_component& component(size_t rep_index,
                                                  size_t comp_index) const;

    /**
     * @brief Get the simple value (first component of first repetition)
     */
    [[nodiscard]] std::string_view value() const noexcept;

    /**
     * @brief Set simple value (replaces entire field)
     */
    void set_value(std::string value);

    /**
     * @brief Get all repetitions as strings
     */
    [[nodiscard]] std::vector<std::string> repetitions(
        const hl7_encoding_characters& encoding) const;

    /**
     * @brief Add a repetition
     */
    void add_repetition();

    /**
     * @brief Serialize to HL7 format
     */
    [[nodiscard]] std::string serialize(
        const hl7_encoding_characters& encoding) const;

    /**
     * @brief Parse from HL7 string
     */
    [[nodiscard]] static hl7_field parse(std::string_view data,
                                          const hl7_encoding_characters& encoding);

    /** Implicit conversion to string_view */
    [[nodiscard]] operator std::string_view() const noexcept { return value(); }

    /** Comparison */
    [[nodiscard]] bool operator==(std::string_view sv) const noexcept {
        return value() == sv;
    }

private:
    // Each repetition is a vector of components
    std::vector<std::vector<hl7_component>> repetitions_;
    static const hl7_component empty_component_;
};

// =============================================================================
// HL7 Segment
// =============================================================================

/**
 * @brief HL7 segment (a line of fields)
 *
 * A segment in an HL7 message, consisting of a segment ID and fields.
 * Examples: MSH, PID, ORC, OBR, etc.
 */
class hl7_segment {
public:
    /** Default constructor */
    hl7_segment() = default;

    /** Construct with segment ID */
    explicit hl7_segment(std::string segment_id);

    /** Get segment ID (e.g., "MSH", "PID") */
    [[nodiscard]] const std::string& segment_id() const noexcept {
        return segment_id_;
    }

    /** Set segment ID */
    void set_segment_id(std::string id) { segment_id_ = std::move(id); }

    /** Get number of fields (excluding segment ID) */
    [[nodiscard]] size_t field_count() const noexcept { return fields_.size(); }

    /**
     * @brief Get field by index (1-based per HL7 convention)
     *
     * Note: For MSH segment, field 1 is the field separator and field 2
     * is the encoding characters. Use field(3) to get MSH-3.
     *
     * @param index 1-based field index
     * @return Reference to field or empty field if out of range
     */
    [[nodiscard]] const hl7_field& field(size_t index) const;

    /**
     * @brief Get mutable field, creating intermediate fields if needed
     * @param index 1-based field index
     */
    hl7_field& field(size_t index);

    /**
     * @brief Get field value as string
     * @param index 1-based field index
     */
    [[nodiscard]] std::string_view field_value(size_t index) const;

    /**
     * @brief Set field value
     * @param index 1-based field index
     * @param value New field value
     */
    void set_field(size_t index, std::string value);

    /**
     * @brief Get value by path (e.g., "5.1.2" for component 1, subcomponent 2 of
     * field 5)
     * @param path Dot-separated path
     */
    [[nodiscard]] std::string_view get_value(std::string_view path) const;

    /**
     * @brief Set value by path
     * @param path Dot-separated path
     * @param value New value
     */
    void set_value(std::string_view path, std::string value);

    /**
     * @brief Serialize to HL7 format
     * @param encoding Encoding characters
     * @return Serialized segment string (without segment terminator)
     */
    [[nodiscard]] std::string serialize(
        const hl7_encoding_characters& encoding) const;

    /**
     * @brief Parse from HL7 string
     * @param data Segment data (single line)
     * @param encoding Encoding characters
     * @return Parsed segment
     */
    [[nodiscard]] static std::expected<hl7_segment, hl7_error> parse(
        std::string_view data, const hl7_encoding_characters& encoding);

    /**
     * @brief Check if this is an MSH segment
     */
    [[nodiscard]] bool is_msh() const noexcept { return segment_id_ == "MSH"; }

    /**
     * @brief Iterator support for fields
     */
    [[nodiscard]] auto begin() const noexcept { return fields_.begin(); }
    [[nodiscard]] auto end() const noexcept { return fields_.end(); }
    [[nodiscard]] auto begin() noexcept { return fields_.begin(); }
    [[nodiscard]] auto end() noexcept { return fields_.end(); }

private:
    std::string segment_id_;
    std::vector<hl7_field> fields_;
    static const hl7_field empty_field_;
};

// =============================================================================
// HL7 Message
// =============================================================================

/**
 * @brief HL7 v2.x message container
 *
 * Complete HL7 message representation with full support for parsing,
 * modification, and serialization. Provides multiple access patterns:
 *   - By segment ID and index: message.segment("PID", 0)
 *   - By path: message.get_value("PID.5.1")
 *
 * @example Basic Usage
 * ```cpp
 * // Parse a message
 * auto result = hl7_message::parse(raw_message);
 * if (result) {
 *     auto& msg = *result;
 *
 *     // Get patient name
 *     std::string_view name = msg.get_value("PID.5.1");
 *
 *     // Get message type
 *     auto header = msg.header();
 *     std::cout << "Message: " << header.full_message_type() << std::endl;
 * }
 * ```
 *
 * @example Building a Message
 * ```cpp
 * hl7_message msg;
 *
 * // Set MSH fields
 * msg.set_value("MSH.3", "SENDING_APP");
 * msg.set_value("MSH.4", "SENDING_FACILITY");
 * msg.set_value("MSH.9.1", "ADT");
 * msg.set_value("MSH.9.2", "A01");
 *
 * // Add PID segment
 * auto& pid = msg.add_segment("PID");
 * pid.set_field(3, "12345");
 * pid.set_value("5.1", "DOE");
 * pid.set_value("5.2", "JOHN");
 *
 * // Serialize
 * std::string raw = msg.serialize();
 * ```
 */
class hl7_message {
public:
    /**
     * @brief Default constructor
     *
     * Creates an empty message with default encoding characters.
     */
    hl7_message();

    /**
     * @brief Copy constructor
     */
    hl7_message(const hl7_message& other);

    /**
     * @brief Move constructor
     */
    hl7_message(hl7_message&& other) noexcept;

    /**
     * @brief Copy assignment
     */
    hl7_message& operator=(const hl7_message& other);

    /**
     * @brief Move assignment
     */
    hl7_message& operator=(hl7_message&& other) noexcept;

    /**
     * @brief Destructor
     */
    ~hl7_message();

    // =========================================================================
    // Parsing
    // =========================================================================

    /**
     * @brief Parse HL7 message from string
     *
     * Parses raw HL7 v2.x message data into structured format.
     * Automatically detects encoding characters from MSH segment.
     *
     * @param data Raw HL7 message data
     * @return Parsed message or error
     */
    [[nodiscard]] static std::expected<hl7_message, hl7_error> parse(
        std::string_view data);

    /**
     * @brief Parse with specific encoding
     *
     * @param data Raw HL7 message data
     * @param encoding Encoding characters to use
     * @return Parsed message or error
     */
    [[nodiscard]] static std::expected<hl7_message, hl7_error> parse(
        std::string_view data, const hl7_encoding_characters& encoding);

    // =========================================================================
    // Serialization
    // =========================================================================

    /**
     * @brief Serialize message to HL7 format
     *
     * @return HL7 formatted message string
     */
    [[nodiscard]] std::string serialize() const;

    /**
     * @brief Get raw message size estimate
     */
    [[nodiscard]] size_t estimated_size() const noexcept;

    // =========================================================================
    // Message Information
    // =========================================================================

    /**
     * @brief Get message header information
     *
     * Returns parsed MSH segment information. If message has no MSH
     * or MSH is invalid, returns default-initialized header.
     */
    [[nodiscard]] hl7_message_header header() const;

    /**
     * @brief Get encoding characters
     */
    [[nodiscard]] const hl7_encoding_characters& encoding() const noexcept;

    /**
     * @brief Set encoding characters
     */
    void set_encoding(const hl7_encoding_characters& encoding);

    /**
     * @brief Check if message is empty (no segments)
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get message type
     */
    [[nodiscard]] message_type type() const noexcept;

    /**
     * @brief Get trigger event (e.g., "A01" for ADT^A01)
     */
    [[nodiscard]] std::string_view trigger_event() const noexcept;

    /**
     * @brief Get message control ID
     */
    [[nodiscard]] std::string_view control_id() const noexcept;

    // =========================================================================
    // Segment Access
    // =========================================================================

    /**
     * @brief Get total number of segments
     */
    [[nodiscard]] size_t segment_count() const noexcept;

    /**
     * @brief Get count of segments with specific ID
     * @param segment_id Segment ID (e.g., "OBX")
     */
    [[nodiscard]] size_t segment_count(std::string_view segment_id) const noexcept;

    /**
     * @brief Get segment by index (0-based)
     * @param index 0-based segment index
     * @return Reference to segment
     * @throws std::out_of_range if index is invalid
     */
    [[nodiscard]] const hl7_segment& segment_at(size_t index) const;

    /**
     * @brief Get mutable segment by index
     */
    hl7_segment& segment_at(size_t index);

    /**
     * @brief Get first segment with specific ID
     * @param segment_id Segment ID
     * @return Pointer to segment or nullptr if not found
     */
    [[nodiscard]] const hl7_segment* segment(std::string_view segment_id) const;

    /**
     * @brief Get mutable segment by ID
     */
    hl7_segment* segment(std::string_view segment_id);

    /**
     * @brief Get segment by ID and occurrence index
     * @param segment_id Segment ID
     * @param occurrence 0-based occurrence index
     * @return Pointer to segment or nullptr if not found
     */
    [[nodiscard]] const hl7_segment* segment(std::string_view segment_id,
                                              size_t occurrence) const;

    /**
     * @brief Get mutable segment by ID and occurrence
     */
    hl7_segment* segment(std::string_view segment_id, size_t occurrence);

    /**
     * @brief Get all segments with specific ID
     * @param segment_id Segment ID
     * @return Vector of pointers to matching segments
     */
    [[nodiscard]] std::vector<const hl7_segment*> segments(
        std::string_view segment_id) const;

    /**
     * @brief Check if segment exists
     * @param segment_id Segment ID
     */
    [[nodiscard]] bool has_segment(std::string_view segment_id) const noexcept;

    /**
     * @brief Add a new segment
     * @param segment_id Segment ID
     * @return Reference to new segment
     */
    hl7_segment& add_segment(std::string_view segment_id);

    /**
     * @brief Insert segment at specific position
     * @param index 0-based position
     * @param segment Segment to insert
     */
    void insert_segment(size_t index, hl7_segment segment);

    /**
     * @brief Remove segment at index
     * @param index 0-based segment index
     */
    void remove_segment(size_t index);

    /**
     * @brief Remove all segments with specific ID
     * @param segment_id Segment ID
     * @return Number of segments removed
     */
    size_t remove_segments(std::string_view segment_id);

    // =========================================================================
    // Path-based Access
    // =========================================================================

    /**
     * @brief Get value by path
     *
     * Path format: "SEGMENT[.occurrence].field[.component[.subcomponent]]"
     * Examples:
     *   - "PID.5" - PID field 5
     *   - "PID.5.1" - First component of PID field 5
     *   - "OBX[1].5" - Field 5 of second OBX segment
     *
     * @param path Path string
     * @return Value at path or empty string if not found
     */
    [[nodiscard]] std::string_view get_value(std::string_view path) const;

    /**
     * @brief Set value by path
     *
     * Creates segments and fields as needed.
     *
     * @param path Path string
     * @param value New value
     */
    void set_value(std::string_view path, std::string value);

    // =========================================================================
    // Validation
    // =========================================================================

    /**
     * @brief Validate message structure
     *
     * Performs basic structural validation:
     *   - MSH segment present and valid
     *   - Required fields present
     *   - Encoding consistent
     *
     * @return Validation result with any issues
     */
    [[nodiscard]] validation_result validate() const;

    /**
     * @brief Quick check if message is valid
     */
    [[nodiscard]] bool is_valid() const;

    // =========================================================================
    // Iteration
    // =========================================================================

    /**
     * @brief Iterate over all segments
     */
    [[nodiscard]] auto begin() const noexcept;
    [[nodiscard]] auto end() const noexcept;
    [[nodiscard]] auto begin() noexcept;
    [[nodiscard]] auto end() noexcept;

    // =========================================================================
    // Utility
    // =========================================================================

    /**
     * @brief Create acknowledgment message for this message
     *
     * @param code Acknowledgment code
     * @param text Acknowledgment text (optional)
     * @return ACK message
     */
    [[nodiscard]] hl7_message create_ack(
        ack_code code, std::string_view text = "") const;

    /**
     * @brief Clone the message
     */
    [[nodiscard]] hl7_message clone() const;

    /**
     * @brief Clear all segments
     */
    void clear();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_HL7_MESSAGE_H
