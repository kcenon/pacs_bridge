/**
 * @file zero_copy_parser.cpp
 * @brief Implementation of zero-copy HL7 message parser
 */

#include "pacs/bridge/performance/zero_copy_parser.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace pacs::bridge::performance {

// =============================================================================
// HL7 Encoding Characters
// =============================================================================

std::expected<hl7_encoding_chars, performance_error>
hl7_encoding_chars::from_msh2(std::string_view msh2) {
    if (msh2.size() < 4) {
        return std::unexpected(performance_error::parser_error);
    }

    hl7_encoding_chars chars;
    chars.component_separator = msh2[0];
    chars.repetition_separator = msh2[1];
    chars.escape_char = msh2[2];
    chars.subcomponent_separator = msh2[3];
    return chars;
}

// =============================================================================
// Segment Reference Implementation
// =============================================================================

segment_ref::segment_ref(std::string_view segment_data,
                         const hl7_encoding_chars& encoding)
    : data_(segment_data), encoding_(encoding) {}

std::string_view segment_ref::segment_id() const noexcept {
    // Segment ID is the first 3 characters
    if (data_.size() < 3) {
        return {};
    }
    return data_.substr(0, 3);
}

size_t segment_ref::field_count() const noexcept {
    index_fields();
    return fields_.size();
}

void segment_ref::index_fields() const {
    if (fields_indexed_) return;

    fields_.clear();

    // For MSH segment, field 1 is the field separator itself
    bool is_msh = (data_.size() >= 3 && data_.substr(0, 3) == "MSH");

    if (is_msh && data_.size() > 3) {
        // MSH-1 is the field separator character (position 3)
        fields_.push_back(data_.substr(3, 1));

        // Start after "MSH|"
        size_t start = 4;
        size_t end;

        while (start < data_.size()) {
            end = data_.find(encoding_.field_separator, start);
            if (end == std::string_view::npos) {
                end = data_.size();
            }
            fields_.push_back(data_.substr(start, end - start));
            start = end + 1;
        }
    } else {
        // Non-MSH segments: skip segment ID
        size_t start = 0;
        if (data_.size() > 3 && data_[3] == encoding_.field_separator) {
            start = 4;  // Skip "XXX|"
        } else {
            start = data_.find(encoding_.field_separator);
            if (start != std::string_view::npos) {
                ++start;
            } else {
                start = data_.size();
            }
        }

        size_t field_start = start;
        while (field_start < data_.size()) {
            size_t field_end = data_.find(encoding_.field_separator, field_start);
            if (field_end == std::string_view::npos) {
                field_end = data_.size();
            }
            fields_.push_back(data_.substr(field_start, field_end - field_start));
            field_start = field_end + 1;
        }
    }

    fields_indexed_ = true;
}

field_ref segment_ref::field(size_t index) const noexcept {
    if (index == 0) {
        return field_ref{};  // HL7 is 1-indexed
    }

    index_fields();

    if (index > fields_.size()) {
        return field_ref{};
    }

    return field_ref{fields_[index - 1]};
}

component_ref segment_ref::component(size_t field_index,
                                     size_t component_index) const noexcept {
    auto f = field(field_index);
    if (f.empty() || component_index == 0) {
        return component_ref{};
    }

    size_t comp_start = 0;
    size_t current_component = 1;

    while (comp_start < f.value.size()) {
        size_t comp_end = f.value.find(encoding_.component_separator, comp_start);
        if (comp_end == std::string_view::npos) {
            comp_end = f.value.size();
        }

        if (current_component == component_index) {
            return component_ref{f.value.substr(comp_start, comp_end - comp_start)};
        }

        comp_start = comp_end + 1;
        ++current_component;
    }

    return component_ref{};
}

component_ref segment_ref::subcomponent(size_t field_index,
                                        size_t component_index,
                                        size_t subcomponent_index) const noexcept {
    auto comp = component(field_index, component_index);
    if (comp.empty() || subcomponent_index == 0) {
        return component_ref{};
    }

    size_t sub_start = 0;
    size_t current_sub = 1;

    while (sub_start < comp.value.size()) {
        size_t sub_end =
            comp.value.find(encoding_.subcomponent_separator, sub_start);
        if (sub_end == std::string_view::npos) {
            sub_end = comp.value.size();
        }

        if (current_sub == subcomponent_index) {
            return component_ref{comp.value.substr(sub_start, sub_end - sub_start)};
        }

        sub_start = sub_end + 1;
        ++current_sub;
    }

    return component_ref{};
}

std::string_view segment_ref::raw() const noexcept {
    return data_;
}

bool segment_ref::is_msh() const noexcept {
    return segment_id() == "MSH";
}

// =============================================================================
// Zero-Copy Parser Implementation
// =============================================================================

struct zero_copy_parser::impl {
    std::string_view data;
    hl7_encoding_chars encoding;
    zero_copy_config config;
    std::vector<std::string_view> segment_data;
    std::unordered_map<std::string, std::vector<size_t>> segment_index;
    std::chrono::nanoseconds parse_duration{0};
    bool parsed = false;

    std::expected<void, performance_error> parse() {
        auto start = std::chrono::steady_clock::now();

        if (data.empty()) {
            return std::unexpected(performance_error::parser_error);
        }

        // Find MSH segment
        if (data.size() < 8 || data.substr(0, 3) != "MSH") {
            return std::unexpected(performance_error::parser_error);
        }

        // Get field separator from MSH-1 (position 3)
        encoding.field_separator = data[3];

        // Get encoding characters from MSH-2 (position 4-7)
        if (data.size() >= 8) {
            auto enc_result = hl7_encoding_chars::from_msh2(data.substr(4, 4));
            if (enc_result) {
                encoding = *enc_result;
                encoding.field_separator = data[3];
            }
        }

        // Split into segments
        segment_data.clear();
        segment_data.reserve(config.segment_index_capacity);
        segment_index.clear();

        // HL7 uses \r as segment delimiter, but also handle \n and \r\n
        size_t seg_start = 0;
        while (seg_start < data.size()) {
            size_t seg_end = seg_start;

            // Find end of segment
            while (seg_end < data.size() && data[seg_end] != '\r' &&
                   data[seg_end] != '\n') {
                ++seg_end;
            }

            if (seg_end > seg_start) {
                auto segment = data.substr(seg_start, seg_end - seg_start);
                if (segment.size() >= 3) {
                    std::string seg_id(segment.substr(0, 3));
                    segment_index[seg_id].push_back(segment_data.size());
                    segment_data.push_back(segment);
                }
            }

            // Skip delimiters
            seg_start = seg_end;
            while (seg_start < data.size() &&
                   (data[seg_start] == '\r' || data[seg_start] == '\n')) {
                ++seg_start;
            }
        }

        parsed = true;
        auto end = std::chrono::steady_clock::now();
        parse_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start);

        return {};
    }

    std::optional<segment_ref> get_segment(std::string_view segment_id) const {
        auto it = segment_index.find(std::string(segment_id));
        if (it == segment_index.end() || it->second.empty()) {
            return std::nullopt;
        }
        return segment_ref(segment_data[it->second[0]], encoding);
    }

    std::optional<segment_ref> get_segment_by_index(size_t index) const {
        if (index >= segment_data.size()) {
            return std::nullopt;
        }
        return segment_ref(segment_data[index], encoding);
    }

    std::vector<segment_ref> get_segments(std::string_view segment_id) const {
        std::vector<segment_ref> result;
        auto it = segment_index.find(std::string(segment_id));
        if (it != segment_index.end()) {
            result.reserve(it->second.size());
            for (size_t idx : it->second) {
                result.emplace_back(segment_data[idx], encoding);
            }
        }
        return result;
    }
};

zero_copy_parser::zero_copy_parser(std::unique_ptr<impl> impl_ptr)
    : impl_(std::move(impl_ptr)) {}

zero_copy_parser::zero_copy_parser(zero_copy_parser&& other) noexcept = default;
zero_copy_parser& zero_copy_parser::operator=(zero_copy_parser&& other) noexcept =
    default;
zero_copy_parser::~zero_copy_parser() = default;

std::expected<zero_copy_parser, performance_error>
zero_copy_parser::parse(std::string_view data, const zero_copy_config& config) {
    auto impl_ptr = std::make_unique<impl>();
    impl_ptr->data = data;
    impl_ptr->config = config;

    auto result = impl_ptr->parse();
    if (!result) {
        return std::unexpected(result.error());
    }

    return zero_copy_parser(std::move(impl_ptr));
}

std::expected<zero_copy_parser, performance_error>
zero_copy_parser::parse(std::span<const uint8_t> data,
                        const zero_copy_config& config) {
    return parse(std::string_view(reinterpret_cast<const char*>(data.data()),
                                  data.size()),
                 config);
}

std::optional<segment_ref> zero_copy_parser::segment(
    std::string_view segment_id) const {
    if (!impl_) return std::nullopt;
    return impl_->get_segment(segment_id);
}

std::optional<segment_ref> zero_copy_parser::segment(size_t index) const {
    if (!impl_) return std::nullopt;
    return impl_->get_segment_by_index(index);
}

std::vector<segment_ref> zero_copy_parser::segments(
    std::string_view segment_id) const {
    if (!impl_) return {};
    return impl_->get_segments(segment_id);
}

size_t zero_copy_parser::segment_count() const noexcept {
    if (!impl_) return 0;
    return impl_->segment_data.size();
}

bool zero_copy_parser::has_segment(std::string_view segment_id) const noexcept {
    if (!impl_) return false;
    return impl_->segment_index.contains(std::string(segment_id));
}

field_ref zero_copy_parser::message_type() const {
    if (auto msh = segment("MSH")) {
        return msh->field(9);
    }
    return field_ref{};
}

field_ref zero_copy_parser::message_control_id() const {
    if (auto msh = segment("MSH")) {
        return msh->field(10);
    }
    return field_ref{};
}

field_ref zero_copy_parser::sending_application() const {
    if (auto msh = segment("MSH")) {
        return msh->field(3);
    }
    return field_ref{};
}

field_ref zero_copy_parser::sending_facility() const {
    if (auto msh = segment("MSH")) {
        return msh->field(4);
    }
    return field_ref{};
}

field_ref zero_copy_parser::receiving_application() const {
    if (auto msh = segment("MSH")) {
        return msh->field(5);
    }
    return field_ref{};
}

field_ref zero_copy_parser::receiving_facility() const {
    if (auto msh = segment("MSH")) {
        return msh->field(6);
    }
    return field_ref{};
}

field_ref zero_copy_parser::message_datetime() const {
    if (auto msh = segment("MSH")) {
        return msh->field(7);
    }
    return field_ref{};
}

field_ref zero_copy_parser::version_id() const {
    if (auto msh = segment("MSH")) {
        return msh->field(12);
    }
    return field_ref{};
}

bool zero_copy_parser::valid() const noexcept {
    return impl_ && impl_->parsed;
}

std::optional<performance_error> zero_copy_parser::error() const noexcept {
    if (impl_ && impl_->parsed) {
        return std::nullopt;
    }
    return performance_error::parser_error;
}

std::string_view zero_copy_parser::raw() const noexcept {
    if (!impl_) return {};
    return impl_->data;
}

const hl7_encoding_chars& zero_copy_parser::encoding() const noexcept {
    static const hl7_encoding_chars default_encoding{};
    if (!impl_) return default_encoding;
    return impl_->encoding;
}

std::chrono::nanoseconds zero_copy_parser::parse_duration() const noexcept {
    if (!impl_) return std::chrono::nanoseconds{0};
    return impl_->parse_duration;
}

size_t zero_copy_parser::bytes_parsed() const noexcept {
    if (!impl_) return 0;
    return impl_->data.size();
}

// =============================================================================
// Batch Parser Implementation
// =============================================================================

struct batch_parser::impl {
    zero_copy_config config;
    statistics stats;
};

batch_parser::batch_parser(const zero_copy_config& config)
    : impl_(std::make_unique<impl>()) {
    impl_->config = config;
}

batch_parser::~batch_parser() = default;

std::expected<zero_copy_parser, performance_error>
batch_parser::parse(std::string_view data) {
    auto result = zero_copy_parser::parse(data, impl_->config);

    if (result) {
        impl_->stats.messages_parsed++;
        impl_->stats.total_bytes += data.size();
        impl_->stats.total_duration += result->parse_duration();
    } else {
        impl_->stats.parse_errors++;
    }

    return result;
}

std::vector<std::expected<zero_copy_parser, performance_error>>
batch_parser::parse_batch(std::span<std::string_view> messages) {
    std::vector<std::expected<zero_copy_parser, performance_error>> results;
    results.reserve(messages.size());

    for (auto msg : messages) {
        results.push_back(parse(msg));
    }

    return results;
}

const batch_parser::statistics& batch_parser::stats() const noexcept {
    return impl_->stats;
}

void batch_parser::reset_stats() {
    impl_->stats = statistics{};
}

}  // namespace pacs::bridge::performance
