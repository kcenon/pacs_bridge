/**
 * @file hl7_parser.cpp
 * @brief HL7 message parser implementation
 */

#include "pacs/bridge/protocol/hl7/hl7_parser.h"

#include "pacs/bridge/tracing/trace_manager.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// hl7_parser::impl
// =============================================================================

class hl7_parser::impl {
public:
    parser_options options_;

    explicit impl(const parser_options& options) : options_(options) {}
};

// =============================================================================
// hl7_parser Implementation
// =============================================================================

hl7_parser::hl7_parser() : pimpl_(std::make_unique<impl>(parser_options{})) {}

hl7_parser::hl7_parser(const parser_options& options)
    : pimpl_(std::make_unique<impl>(options)) {}

hl7_parser::~hl7_parser() = default;

hl7_parser::hl7_parser(hl7_parser&&) noexcept = default;
hl7_parser& hl7_parser::operator=(hl7_parser&&) noexcept = default;

std::expected<hl7_message, hl7_error> hl7_parser::parse(
    std::string_view data, parse_details* details) const {
    // Start tracing span
    auto span = tracing::trace_manager::instance().start_span(
        "hl7_parse", tracing::span_kind::internal);
    span.set_attribute("hl7.message_size", static_cast<int64_t>(data.size()));

    auto start = std::chrono::steady_clock::now();

    // Preprocessing
    std::string_view processed = data;

    // Strip trailing whitespace if enabled
    if (pimpl_->options_.strip_trailing_whitespace) {
        while (!processed.empty() &&
               (processed.back() == '\r' || processed.back() == '\n' ||
                processed.back() == ' ')) {
            processed = processed.substr(0, processed.size() - 1);
        }
    }

    // Size checks
    if (processed.empty()) {
        return std::unexpected(hl7_error::empty_message);
    }

    if (processed.size() > pimpl_->options_.max_message_size) {
        return std::unexpected(hl7_error::message_too_large);
    }

    // Extract encoding characters
    auto encoding_result = extract_encoding(processed);
    if (!encoding_result) {
        return std::unexpected(encoding_result.error());
    }

    auto result = parse(processed, *encoding_result, details);

    if (details) {
        auto end = std::chrono::steady_clock::now();
        details->parse_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                     end - start)
                                     .count();
        details->original_size = data.size();

        // Add tracing attributes
        span.set_attribute("hl7.parse_time_us", details->parse_time_us);
        span.set_attribute("hl7.segment_count", static_cast<int64_t>(details->segment_count));
        span.set_attribute("hl7.field_count", static_cast<int64_t>(details->field_count));
        if (!details->detected_message_type.empty()) {
            span.set_attribute("hl7.message_type", details->detected_message_type);
        }
        if (!details->detected_version.empty()) {
            span.set_attribute("hl7.version", details->detected_version);
        }
    }

    if (!result) {
        span.set_error("HL7 parsing failed");
    }

    return result;
}

std::expected<hl7_message, hl7_error> hl7_parser::parse(
    std::span<const uint8_t> data, parse_details* details) const {
    std::string_view sv(reinterpret_cast<const char*>(data.data()), data.size());
    return parse(sv, details);
}

std::expected<hl7_message, hl7_error> hl7_parser::parse(
    std::string_view data, const hl7_encoding_characters& encoding,
    parse_details* details) const {
    auto result = hl7_message::parse(data, encoding);

    if (result && details) {
        details->segment_count = result->segment_count();

        // Count total fields
        size_t field_count = 0;
        for (size_t i = 0; i < result->segment_count(); ++i) {
            field_count += result->segment_at(i).field_count();
        }
        details->field_count = field_count;

        // Extract header info
        auto header = result->header();
        details->detected_version = header.version_id;
        details->detected_message_type = header.full_message_type();
    }

    // Validate if requested
    if (result && pimpl_->options_.validate_structure) {
        auto validation = result->validate();
        if (!validation.valid) {
            // Add warnings to details
            if (details) {
                for (const auto& issue : validation.issues) {
                    details->warnings.push_back(issue.message);
                }
            }

            // In strict mode, fail on validation errors
            if (!pimpl_->options_.lenient_mode) {
                return std::unexpected(hl7_error::validation_failed);
            }
        }
    }

    return result;
}

std::expected<hl7_encoding_characters, hl7_error> hl7_parser::extract_encoding(
    std::string_view data) {
    if (data.length() < 8) {
        return std::unexpected(hl7_error::invalid_msh);
    }

    if (data.substr(0, 3) != "MSH") {
        return std::unexpected(hl7_error::missing_msh);
    }

    hl7_encoding_characters encoding;
    encoding.field_separator = data[3];

    if (data.length() >= 8) {
        encoding.component_separator = data[4];
        encoding.repetition_separator = data[5];
        encoding.escape_character = data[6];
        encoding.subcomponent_separator = data[7];
    }

    return encoding;
}

std::expected<hl7_message_header, hl7_error> hl7_parser::extract_header(
    std::string_view data) {
    // Parse just enough to get the header
    auto encoding_result = extract_encoding(data);
    if (!encoding_result) {
        return std::unexpected(encoding_result.error());
    }

    // Find end of MSH segment
    size_t msh_end = data.find(HL7_SEGMENT_TERMINATOR);
    if (msh_end == std::string_view::npos) {
        msh_end = data.length();
    }

    // Parse MSH segment
    auto msh_result = hl7_segment::parse(data.substr(0, msh_end), *encoding_result);
    if (!msh_result) {
        return std::unexpected(msh_result.error());
    }

    // Build header from MSH
    hl7_message_header header;
    const auto& msh = *msh_result;

    header.encoding = *encoding_result;
    header.sending_application = std::string(msh.field_value(3));
    header.sending_facility = std::string(msh.field_value(4));
    header.receiving_application = std::string(msh.field_value(5));
    header.receiving_facility = std::string(msh.field_value(6));

    if (auto ts = hl7_timestamp::parse(msh.field_value(7))) {
        header.timestamp = *ts;
    }

    header.security = std::string(msh.field_value(8));

    const auto& type_field = msh.field(9);
    header.type_string = std::string(type_field.component(1).value());
    header.type = parse_message_type(header.type_string);
    header.trigger_event = std::string(type_field.component(2).value());
    header.message_structure = std::string(type_field.component(3).value());

    header.message_control_id = std::string(msh.field_value(10));
    header.processing_id = std::string(msh.field_value(11));
    header.version_id = std::string(msh.field_value(12));

    return header;
}

bool hl7_parser::looks_like_hl7(std::string_view data) noexcept {
    if (data.length() < 8) {
        return false;
    }

    // Check for MSH segment
    if (data.substr(0, 3) != "MSH") {
        return false;
    }

    // Check for typical field separator
    char fs = data[3];
    if (fs != '|' && !std::ispunct(static_cast<unsigned char>(fs))) {
        return false;
    }

    return true;
}

std::string hl7_parser::unescape(std::string_view data,
                                  const hl7_encoding_characters& encoding) {
    std::string result;
    result.reserve(data.length());

    for (size_t i = 0; i < data.length(); ++i) {
        if (data[i] == encoding.escape_character && i + 2 < data.length()) {
            char next = data[i + 1];
            size_t end_escape = data.find(encoding.escape_character, i + 1);

            if (end_escape != std::string_view::npos) {
                std::string_view escape_seq = data.substr(i + 1, end_escape - i - 1);

                if (escape_seq == "F") {
                    result += encoding.field_separator;
                    i = end_escape;
                    continue;
                } else if (escape_seq == "S") {
                    result += encoding.component_separator;
                    i = end_escape;
                    continue;
                } else if (escape_seq == "T") {
                    result += encoding.subcomponent_separator;
                    i = end_escape;
                    continue;
                } else if (escape_seq == "R") {
                    result += encoding.repetition_separator;
                    i = end_escape;
                    continue;
                } else if (escape_seq == "E") {
                    result += encoding.escape_character;
                    i = end_escape;
                    continue;
                } else if (escape_seq == ".br") {
                    result += '\n';
                    i = end_escape;
                    continue;
                } else if (escape_seq.length() >= 2 && escape_seq[0] == 'X') {
                    // Hex encoding: \Xhh\ format (e.g., \X0D\ for CR)
                    std::string hex_str(escape_seq.substr(1));
                    try {
                        int value = std::stoi(hex_str, nullptr, 16);
                        result += static_cast<char>(value);
                        i = end_escape;
                        continue;
                    } catch (...) {
                        // Invalid hex, keep as-is
                    }
                }
            }
        }
        result += data[i];
    }

    return result;
}

std::string hl7_parser::escape(std::string_view data,
                                const hl7_encoding_characters& encoding) {
    std::string result;
    result.reserve(data.length() * 2);

    for (char c : data) {
        if (c == encoding.field_separator) {
            result += encoding.escape_character;
            result += 'F';
            result += encoding.escape_character;
        } else if (c == encoding.component_separator) {
            result += encoding.escape_character;
            result += 'S';
            result += encoding.escape_character;
        } else if (c == encoding.subcomponent_separator) {
            result += encoding.escape_character;
            result += 'T';
            result += encoding.escape_character;
        } else if (c == encoding.repetition_separator) {
            result += encoding.escape_character;
            result += 'R';
            result += encoding.escape_character;
        } else if (c == encoding.escape_character) {
            result += encoding.escape_character;
            result += 'E';
            result += encoding.escape_character;
        } else if (c == '\n') {
            result += encoding.escape_character;
            result += ".br";
            result += encoding.escape_character;
        } else {
            result += c;
        }
    }

    return result;
}

const parser_options& hl7_parser::options() const noexcept {
    return pimpl_->options_;
}

void hl7_parser::set_options(const parser_options& options) {
    pimpl_->options_ = options;
}

std::expected<hl7_segment, hl7_error> hl7_parser::parse_segment(
    std::string_view segment_data, const hl7_encoding_characters& encoding) {
    return hl7_segment::parse(segment_data, encoding);
}

std::vector<std::string_view> hl7_parser::split_segments(std::string_view data) {
    std::vector<std::string_view> segments;

    size_t start = 0;
    size_t pos = 0;

    while (pos <= data.length()) {
        if (pos == data.length() || data[pos] == HL7_SEGMENT_TERMINATOR ||
            data[pos] == HL7_LINE_FEED) {
            if (pos > start) {
                std::string_view seg = data.substr(start, pos - start);

                // Trim trailing CR/LF
                while (!seg.empty() &&
                       (seg.back() == '\r' || seg.back() == '\n')) {
                    seg = seg.substr(0, seg.length() - 1);
                }

                if (!seg.empty()) {
                    segments.push_back(seg);
                }
            }
            start = pos + 1;
        }
        ++pos;
    }

    return segments;
}

// =============================================================================
// hl7_streaming_parser::impl
// =============================================================================

class hl7_streaming_parser::impl {
public:
    parser_options options_;
    segment_callback callback_;
    std::string buffer_;
    std::optional<hl7_encoding_characters> encoding_;
    std::optional<hl7_message_header> header_;
    std::vector<hl7_segment> segments_;
    size_t segment_index_ = 0;

    explicit impl(const parser_options& options) : options_(options) {}
};

// =============================================================================
// hl7_streaming_parser Implementation
// =============================================================================

hl7_streaming_parser::hl7_streaming_parser(const parser_options& options)
    : pimpl_(std::make_unique<impl>(options)) {}

hl7_streaming_parser::~hl7_streaming_parser() = default;

std::expected<void, hl7_error> hl7_streaming_parser::feed(std::string_view data) {
    pimpl_->buffer_.append(data);

    // Process complete segments
    while (true) {
        size_t seg_end = pimpl_->buffer_.find(HL7_SEGMENT_TERMINATOR);
        if (seg_end == std::string::npos) {
            // Also check for LF
            seg_end = pimpl_->buffer_.find(HL7_LINE_FEED);
            if (seg_end == std::string::npos) {
                break;
            }
        }

        std::string_view segment_data(pimpl_->buffer_.data(), seg_end);

        // Trim trailing whitespace
        while (!segment_data.empty() &&
               (segment_data.back() == '\r' || segment_data.back() == '\n')) {
            segment_data = segment_data.substr(0, segment_data.length() - 1);
        }

        if (!segment_data.empty()) {
            // First segment should be MSH
            if (!pimpl_->encoding_) {
                auto enc_result = hl7_parser::extract_encoding(segment_data);
                if (!enc_result) {
                    return std::unexpected(enc_result.error());
                }
                pimpl_->encoding_ = *enc_result;

                auto header_result = hl7_parser::extract_header(segment_data);
                if (header_result) {
                    pimpl_->header_ = *header_result;
                }
            }

            // Parse segment
            auto seg_result =
                hl7_segment::parse(segment_data, *pimpl_->encoding_);
            if (!seg_result) {
                if (!pimpl_->options_.lenient_mode) {
                    return std::unexpected(seg_result.error());
                }
            } else {
                // Invoke callback
                if (pimpl_->callback_) {
                    if (!pimpl_->callback_(*seg_result, pimpl_->segment_index_)) {
                        // Callback requested stop
                        pimpl_->buffer_.erase(0, seg_end + 1);
                        break;
                    }
                }

                pimpl_->segments_.push_back(std::move(*seg_result));
                ++pimpl_->segment_index_;
            }
        }

        pimpl_->buffer_.erase(0, seg_end + 1);
    }

    return {};
}

void hl7_streaming_parser::set_segment_callback(segment_callback callback) {
    pimpl_->callback_ = std::move(callback);
}

std::expected<std::optional<hl7_message>, hl7_error>
hl7_streaming_parser::finish() {
    // Process any remaining data
    if (!pimpl_->buffer_.empty()) {
        // Final segment without terminator
        std::string_view segment_data = pimpl_->buffer_;

        // Trim trailing whitespace
        while (!segment_data.empty() &&
               (segment_data.back() == '\r' || segment_data.back() == '\n' ||
                segment_data.back() == ' ')) {
            segment_data = segment_data.substr(0, segment_data.length() - 1);
        }

        if (!segment_data.empty() && pimpl_->encoding_) {
            auto seg_result =
                hl7_segment::parse(segment_data, *pimpl_->encoding_);
            if (seg_result) {
                if (pimpl_->callback_) {
                    pimpl_->callback_(*seg_result, pimpl_->segment_index_);
                }
                pimpl_->segments_.push_back(std::move(*seg_result));
            }
        }

        pimpl_->buffer_.clear();
    }

    if (pimpl_->segments_.empty()) {
        return std::nullopt;
    }

    // Build message from collected segments
    hl7_message msg;
    msg.set_encoding(*pimpl_->encoding_);

    for (auto& seg : pimpl_->segments_) {
        msg.insert_segment(msg.segment_count(), std::move(seg));
    }

    pimpl_->segments_.clear();
    pimpl_->segment_index_ = 0;

    return msg;
}

void hl7_streaming_parser::reset() {
    pimpl_->buffer_.clear();
    pimpl_->encoding_.reset();
    pimpl_->header_.reset();
    pimpl_->segments_.clear();
    pimpl_->segment_index_ = 0;
}

std::optional<hl7_encoding_characters> hl7_streaming_parser::encoding() const {
    return pimpl_->encoding_;
}

std::optional<hl7_message_header> hl7_streaming_parser::header() const {
    return pimpl_->header_;
}

}  // namespace pacs::bridge::hl7
