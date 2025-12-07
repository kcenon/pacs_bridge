#ifndef PACS_BRIDGE_PERFORMANCE_ZERO_COPY_PARSER_H
#define PACS_BRIDGE_PERFORMANCE_ZERO_COPY_PARSER_H

/**
 * @file zero_copy_parser.h
 * @brief Zero-copy HL7 message parser for high-performance processing
 *
 * Provides efficient HL7 message parsing without copying message data.
 * Uses string_view references into the original message buffer, enabling
 * parsing latency under 1ms for typical HL7 messages.
 *
 * Key Optimizations:
 *   - String views instead of string copies
 *   - Lazy parsing (only parse requested fields)
 *   - Pre-indexed segment lookup
 *   - Segment caching for repeated access
 *   - Minimal allocations during parsing
 *
 * @see docs/reference_materials/01_hl7_message_structure.md
 * @see docs/SRS.md NFR-1.2 (Latency P95 < 50ms)
 */

#include "pacs/bridge/performance/performance_types.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// HL7 Delimiters
// =============================================================================

/**
 * @brief HL7 message encoding characters
 */
struct hl7_encoding_chars {
    char field_separator = '|';
    char component_separator = '^';
    char repetition_separator = '~';
    char escape_char = '\\';
    char subcomponent_separator = '&';

    /**
     * @brief Parse encoding characters from MSH-2
     */
    [[nodiscard]] static std::expected<hl7_encoding_chars, performance_error>
    from_msh2(std::string_view msh2);
};

// =============================================================================
// Zero-Copy Field Reference
// =============================================================================

/**
 * @brief Reference to a field within the message buffer
 *
 * Points to field data without copying. Valid only while source buffer exists.
 */
struct field_ref {
    /** View into the original message buffer */
    std::string_view value;

    /** Field is empty */
    [[nodiscard]] bool empty() const noexcept { return value.empty(); }

    /** Get field value as string view */
    [[nodiscard]] std::string_view get() const noexcept { return value; }

    /** Convert to string (copies data) */
    [[nodiscard]] std::string to_string() const { return std::string(value); }

    /** Check if field exists */
    [[nodiscard]] explicit operator bool() const noexcept { return !value.empty(); }

    /** Comparison operators */
    [[nodiscard]] bool operator==(std::string_view other) const noexcept {
        return value == other;
    }

    [[nodiscard]] bool operator!=(std::string_view other) const noexcept {
        return value != other;
    }
};

/**
 * @brief Reference to a component within a field
 */
struct component_ref {
    std::string_view value;

    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
    [[nodiscard]] std::string_view get() const noexcept { return value; }
    [[nodiscard]] std::string to_string() const { return std::string(value); }
};

// =============================================================================
// Zero-Copy Segment
// =============================================================================

/**
 * @brief Zero-copy segment reference
 *
 * Provides access to segment ID and fields without copying data.
 */
class segment_ref {
public:
    /**
     * @brief Construct from raw segment data
     */
    explicit segment_ref(std::string_view segment_data,
                         const hl7_encoding_chars& encoding);

    /**
     * @brief Get segment ID (e.g., "MSH", "PID", "OBR")
     */
    [[nodiscard]] std::string_view segment_id() const noexcept;

    /**
     * @brief Get number of fields in segment
     */
    [[nodiscard]] size_t field_count() const noexcept;

    /**
     * @brief Get field by index (1-based, HL7 convention)
     *
     * For MSH segment, field 1 is the field separator itself.
     *
     * @param index Field index (1-based)
     * @return Field reference, empty if index out of range
     */
    [[nodiscard]] field_ref field(size_t index) const noexcept;

    /**
     * @brief Get component within a field
     *
     * @param field_index Field index (1-based)
     * @param component_index Component index (1-based)
     * @return Component reference
     */
    [[nodiscard]] component_ref component(size_t field_index,
                                          size_t component_index) const noexcept;

    /**
     * @brief Get subcomponent within a component
     *
     * @param field_index Field index (1-based)
     * @param component_index Component index (1-based)
     * @param subcomponent_index Subcomponent index (1-based)
     * @return Subcomponent reference
     */
    [[nodiscard]] component_ref subcomponent(size_t field_index,
                                             size_t component_index,
                                             size_t subcomponent_index) const noexcept;

    /**
     * @brief Get raw segment data
     */
    [[nodiscard]] std::string_view raw() const noexcept;

    /**
     * @brief Check if this is an MSH segment
     */
    [[nodiscard]] bool is_msh() const noexcept;

private:
    std::string_view data_;
    hl7_encoding_chars encoding_;
    mutable std::vector<std::string_view> fields_;  // Lazy initialized
    mutable bool fields_indexed_ = false;

    void index_fields() const;
};

// =============================================================================
// Zero-Copy Parser
// =============================================================================

/**
 * @brief Zero-copy HL7 message parser
 *
 * Parses HL7 messages without copying data from the source buffer.
 * All returned references are valid only while the source buffer exists.
 *
 * Example usage:
 * @code
 *     // Parse message without copying
 *     auto parser = zero_copy_parser::parse(message_data);
 *     if (!parser) {
 *         handle_error(parser.error());
 *         return;
 *     }
 *
 *     // Access MSH segment
 *     if (auto msh = parser->segment("MSH")) {
 *         auto message_type = msh->field(9);  // MSH-9
 *         auto sending_app = msh->field(3);   // MSH-3
 *     }
 *
 *     // Access PID segment
 *     if (auto pid = parser->segment("PID")) {
 *         auto patient_id = pid->field(3);       // PID-3
 *         auto patient_name = pid->field(5);     // PID-5
 *         auto last_name = pid->component(5, 1); // PID-5.1
 *     }
 * @endcode
 */
class zero_copy_parser {
public:
    // -------------------------------------------------------------------------
    // Factory Methods
    // -------------------------------------------------------------------------

    /**
     * @brief Parse HL7 message from data
     *
     * @param data Raw HL7 message data
     * @param config Parser configuration
     * @return Parser instance or error
     */
    [[nodiscard]] static std::expected<zero_copy_parser, performance_error>
    parse(std::string_view data, const zero_copy_config& config = {});

    /**
     * @brief Parse HL7 message from byte span
     */
    [[nodiscard]] static std::expected<zero_copy_parser, performance_error>
    parse(std::span<const uint8_t> data, const zero_copy_config& config = {});

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /** Default constructor (empty parser) */
    zero_copy_parser() = default;

    /** Move constructor */
    zero_copy_parser(zero_copy_parser&& other) noexcept;

    /** Move assignment */
    zero_copy_parser& operator=(zero_copy_parser&& other) noexcept;

    /** Destructor */
    ~zero_copy_parser();

    // Non-copyable
    zero_copy_parser(const zero_copy_parser&) = delete;
    zero_copy_parser& operator=(const zero_copy_parser&) = delete;

    // -------------------------------------------------------------------------
    // Segment Access
    // -------------------------------------------------------------------------

    /**
     * @brief Get segment by ID
     *
     * @param segment_id Segment identifier (e.g., "MSH", "PID")
     * @return Segment reference, nullopt if not found
     */
    [[nodiscard]] std::optional<segment_ref> segment(std::string_view segment_id) const;

    /**
     * @brief Get segment by index (0-based)
     *
     * @param index Segment index
     * @return Segment reference, nullopt if out of range
     */
    [[nodiscard]] std::optional<segment_ref> segment(size_t index) const;

    /**
     * @brief Get all segments with given ID
     *
     * Useful for repeating segments like OBX, NTE.
     *
     * @param segment_id Segment identifier
     * @return Vector of segment references
     */
    [[nodiscard]] std::vector<segment_ref> segments(std::string_view segment_id) const;

    /**
     * @brief Get number of segments
     */
    [[nodiscard]] size_t segment_count() const noexcept;

    /**
     * @brief Check if segment exists
     */
    [[nodiscard]] bool has_segment(std::string_view segment_id) const noexcept;

    // -------------------------------------------------------------------------
    // MSH Quick Access
    // -------------------------------------------------------------------------

    /**
     * @brief Get message type (MSH-9)
     */
    [[nodiscard]] field_ref message_type() const;

    /**
     * @brief Get message control ID (MSH-10)
     */
    [[nodiscard]] field_ref message_control_id() const;

    /**
     * @brief Get sending application (MSH-3)
     */
    [[nodiscard]] field_ref sending_application() const;

    /**
     * @brief Get sending facility (MSH-4)
     */
    [[nodiscard]] field_ref sending_facility() const;

    /**
     * @brief Get receiving application (MSH-5)
     */
    [[nodiscard]] field_ref receiving_application() const;

    /**
     * @brief Get receiving facility (MSH-6)
     */
    [[nodiscard]] field_ref receiving_facility() const;

    /**
     * @brief Get message datetime (MSH-7)
     */
    [[nodiscard]] field_ref message_datetime() const;

    /**
     * @brief Get version ID (MSH-12)
     */
    [[nodiscard]] field_ref version_id() const;

    // -------------------------------------------------------------------------
    // Validation
    // -------------------------------------------------------------------------

    /**
     * @brief Check if parse was successful
     */
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Get parsing error if any
     */
    [[nodiscard]] std::optional<performance_error> error() const noexcept;

    // -------------------------------------------------------------------------
    // Raw Access
    // -------------------------------------------------------------------------

    /**
     * @brief Get raw message data
     */
    [[nodiscard]] std::string_view raw() const noexcept;

    /**
     * @brief Get encoding characters
     */
    [[nodiscard]] const hl7_encoding_chars& encoding() const noexcept;

    // -------------------------------------------------------------------------
    // Statistics
    // -------------------------------------------------------------------------

    /**
     * @brief Get parsing duration
     */
    [[nodiscard]] std::chrono::nanoseconds parse_duration() const noexcept;

    /**
     * @brief Get number of bytes parsed
     */
    [[nodiscard]] size_t bytes_parsed() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;

    explicit zero_copy_parser(std::unique_ptr<impl> impl);
};

// =============================================================================
// Batch Parser
// =============================================================================

/**
 * @brief Batch parser for multiple messages
 *
 * Parses multiple HL7 messages with shared configuration and pooled resources.
 */
class batch_parser {
public:
    /**
     * @brief Construct batch parser
     * @param config Parser configuration
     */
    explicit batch_parser(const zero_copy_config& config = {});

    /** Destructor */
    ~batch_parser();

    /**
     * @brief Parse single message
     */
    [[nodiscard]] std::expected<zero_copy_parser, performance_error>
    parse(std::string_view data);

    /**
     * @brief Parse multiple messages
     *
     * @param messages Vector of message data
     * @return Vector of parsing results
     */
    [[nodiscard]] std::vector<std::expected<zero_copy_parser, performance_error>>
    parse_batch(std::span<std::string_view> messages);

    /**
     * @brief Get statistics
     */
    struct statistics {
        uint64_t messages_parsed = 0;
        uint64_t parse_errors = 0;
        uint64_t total_bytes = 0;
        std::chrono::nanoseconds total_duration{0};

        [[nodiscard]] double avg_parse_us() const noexcept {
            if (messages_parsed == 0) return 0.0;
            return static_cast<double>(total_duration.count()) /
                   static_cast<double>(messages_parsed) / 1000.0;
        }
    };

    [[nodiscard]] const statistics& stats() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset_stats();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace pacs::bridge::performance

#endif  // PACS_BRIDGE_PERFORMANCE_ZERO_COPY_PARSER_H
