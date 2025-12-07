#ifndef PACS_BRIDGE_PROTOCOL_HL7_HL7_PARSER_H
#define PACS_BRIDGE_PROTOCOL_HL7_HL7_PARSER_H

/**
 * @file hl7_parser.h
 * @brief HL7 v2.x message parser
 *
 * Provides parsing functionality for HL7 v2.x messages. Converts raw
 * HL7 message strings into structured hl7_message objects with full
 * support for:
 *   - Standard and non-standard delimiters
 *   - Escape sequence handling
 *   - Character set conversions
 *   - Partial message recovery
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/9
 * @see docs/reference_materials/02_hl7_message_types.md
 */

#include "hl7_message.h"
#include "hl7_types.h"

#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace pacs::bridge::hl7 {

// =============================================================================
// Parser Options
// =============================================================================

/**
 * @brief Parser configuration options
 */
struct parser_options {
    /** Maximum message size to accept */
    size_t max_message_size = HL7_MAX_MESSAGE_SIZE;

    /** Maximum segment length to accept */
    size_t max_segment_length = HL7_MAX_SEGMENT_LENGTH;

    /** Allow LF as segment terminator (in addition to CR) */
    bool allow_lf_terminator = true;

    /** Strip CR/LF from end of message */
    bool strip_trailing_whitespace = true;

    /** Parse in lenient mode (try to recover from errors) */
    bool lenient_mode = true;

    /** Validate message structure after parsing */
    bool validate_structure = false;

    /** Character set for decoding (empty = assume ASCII/UTF-8) */
    std::string character_set;
};

// =============================================================================
// Parser Result Details
// =============================================================================

/**
 * @brief Detailed information about a parsed message
 */
struct parse_details {
    /** Number of segments parsed */
    size_t segment_count = 0;

    /** Total number of fields parsed */
    size_t field_count = 0;

    /** Parse time in microseconds */
    int64_t parse_time_us = 0;

    /** Any non-fatal warnings during parsing */
    std::vector<std::string> warnings;

    /** Original message size in bytes */
    size_t original_size = 0;

    /** Detected HL7 version */
    std::string detected_version;

    /** Detected message type */
    std::string detected_message_type;
};

// =============================================================================
// HL7 Parser
// =============================================================================

/**
 * @brief HL7 v2.x message parser
 *
 * Parses raw HL7 message data into structured hl7_message objects.
 * Supports various HL7 v2.x versions and handles common message format
 * variations.
 *
 * The parser can operate in strict or lenient mode:
 *   - Strict: Fails on any structural violation
 *   - Lenient: Attempts to recover from common issues
 *
 * @example Basic Parsing
 * ```cpp
 * hl7_parser parser;
 *
 * std::string raw = "MSH|^~\\&|APP|FAC|...";
 * auto result = parser.parse(raw);
 *
 * if (result) {
 *     hl7_message& msg = *result;
 *     // Process message
 * } else {
 *     std::cerr << "Parse error: " << to_string(result.error()) << std::endl;
 * }
 * ```
 *
 * @example With Options
 * ```cpp
 * parser_options opts;
 * opts.lenient_mode = false;
 * opts.validate_structure = true;
 *
 * hl7_parser parser(opts);
 * auto result = parser.parse(raw);
 * ```
 *
 * @example With Parse Details
 * ```cpp
 * parse_details details;
 * auto result = parser.parse(raw, &details);
 *
 * if (result) {
 *     std::cout << "Parsed " << details.segment_count << " segments in "
 *               << details.parse_time_us << "µs" << std::endl;
 * }
 * ```
 */
class hl7_parser {
public:
    /**
     * @brief Default constructor with default options
     */
    hl7_parser();

    /**
     * @brief Constructor with custom options
     * @param options Parser configuration
     */
    explicit hl7_parser(const parser_options& options);

    /**
     * @brief Destructor
     */
    ~hl7_parser();

    // Non-copyable, movable
    hl7_parser(const hl7_parser&) = delete;
    hl7_parser& operator=(const hl7_parser&) = delete;
    hl7_parser(hl7_parser&&) noexcept;
    hl7_parser& operator=(hl7_parser&&) noexcept;

    // =========================================================================
    // Parsing Methods
    // =========================================================================

    /**
     * @brief Parse HL7 message from string
     *
     * @param data Raw HL7 message data
     * @param details Optional pointer to receive parse details
     * @return Parsed message or error
     */
    [[nodiscard]] std::expected<hl7_message, hl7_error> parse(
        std::string_view data, parse_details* details = nullptr) const;

    /**
     * @brief Parse HL7 message from byte span
     *
     * @param data Raw message bytes
     * @param details Optional pointer to receive parse details
     * @return Parsed message or error
     */
    [[nodiscard]] std::expected<hl7_message, hl7_error> parse(
        std::span<const uint8_t> data, parse_details* details = nullptr) const;

    /**
     * @brief Parse with explicit encoding characters
     *
     * Use when encoding characters are known beforehand or differ
     * from what's in the MSH segment.
     *
     * @param data Raw HL7 message data
     * @param encoding Encoding characters to use
     * @param details Optional pointer to receive parse details
     * @return Parsed message or error
     */
    [[nodiscard]] std::expected<hl7_message, hl7_error> parse(
        std::string_view data, const hl7_encoding_characters& encoding,
        parse_details* details = nullptr) const;

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Extract encoding characters from raw message
     *
     * Parses just the MSH segment to extract encoding characters without
     * parsing the entire message.
     *
     * @param data Raw HL7 message data
     * @return Encoding characters or error
     */
    [[nodiscard]] static std::expected<hl7_encoding_characters, hl7_error>
    extract_encoding(std::string_view data);

    /**
     * @brief Extract message header from raw message
     *
     * Parses just the MSH segment to extract header information without
     * parsing the entire message. Useful for routing decisions.
     *
     * @param data Raw HL7 message data
     * @return Message header or error
     */
    [[nodiscard]] static std::expected<hl7_message_header, hl7_error>
    extract_header(std::string_view data);

    /**
     * @brief Check if data looks like an HL7 message
     *
     * Performs quick validation without full parsing.
     *
     * @param data Data to check
     * @return true if data appears to be HL7
     */
    [[nodiscard]] static bool looks_like_hl7(std::string_view data) noexcept;

    /**
     * @brief Unescape HL7 escape sequences
     *
     * Converts HL7 escape sequences to their actual characters:
     *   \F\ -> |
     *   \S\ -> ^
     *   \T\ -> &
     *   \R\ -> ~
     *   \E\ -> \
     *   \Xhh\ -> hex character
     *   \.br\ -> line break
     *
     * @param data Escaped string
     * @param encoding Encoding characters
     * @return Unescaped string
     */
    [[nodiscard]] static std::string unescape(
        std::string_view data, const hl7_encoding_characters& encoding);

    /**
     * @brief Escape special characters for HL7
     *
     * Converts special characters to HL7 escape sequences.
     *
     * @param data Raw string
     * @param encoding Encoding characters
     * @return Escaped string
     */
    [[nodiscard]] static std::string escape(
        std::string_view data, const hl7_encoding_characters& encoding);

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current parser options
     */
    [[nodiscard]] const parser_options& options() const noexcept;

    /**
     * @brief Set parser options
     */
    void set_options(const parser_options& options);

    // =========================================================================
    // Segment Parsing
    // =========================================================================

    /**
     * @brief Parse a single segment
     *
     * @param segment_data Segment string (single line)
     * @param encoding Encoding characters
     * @return Parsed segment or error
     */
    [[nodiscard]] static std::expected<hl7_segment, hl7_error> parse_segment(
        std::string_view segment_data, const hl7_encoding_characters& encoding);

    /**
     * @brief Split message into segment strings
     *
     * @param data Raw message data
     * @return Vector of segment strings
     */
    [[nodiscard]] static std::vector<std::string_view> split_segments(
        std::string_view data);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Streaming Parser (for large messages)
// =============================================================================

/**
 * @brief Streaming HL7 parser for large messages
 *
 * Parses HL7 messages incrementally, useful for very large messages
 * or streaming scenarios where the full message isn't available at once.
 */
class hl7_streaming_parser {
public:
    /**
     * @brief Segment callback type
     *
     * Called for each segment as it's parsed.
     * Return false to stop parsing.
     */
    using segment_callback =
        std::function<bool(const hl7_segment& segment, size_t index)>;

    /**
     * @brief Constructor
     * @param options Parser options
     */
    explicit hl7_streaming_parser(const parser_options& options = {});

    /**
     * @brief Destructor
     */
    ~hl7_streaming_parser();

    /**
     * @brief Feed data to the parser
     *
     * @param data Data chunk
     * @return Error if parsing failed
     */
    [[nodiscard]] std::expected<void, hl7_error> feed(std::string_view data);

    /**
     * @brief Set callback for parsed segments
     */
    void set_segment_callback(segment_callback callback);

    /**
     * @brief Finish parsing and get any remaining message
     *
     * @return Complete message if one was fully parsed
     */
    [[nodiscard]] std::expected<std::optional<hl7_message>, hl7_error> finish();

    /**
     * @brief Reset parser state
     */
    void reset();

    /**
     * @brief Get encoding characters (available after MSH is parsed)
     */
    [[nodiscard]] std::optional<hl7_encoding_characters> encoding() const;

    /**
     * @brief Get message header (available after MSH is parsed)
     */
    [[nodiscard]] std::optional<hl7_message_header> header() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

}  // namespace pacs::bridge::hl7

#endif  // PACS_BRIDGE_PROTOCOL_HL7_HL7_PARSER_H
