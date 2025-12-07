/**
 * @file hl7_message.cpp
 * @brief HL7 message data model implementation
 */

#include "pacs/bridge/protocol/hl7/hl7_message.h"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace pacs::bridge::hl7 {

// =============================================================================
// Static Empty Instances
// =============================================================================

const hl7_subcomponent hl7_component::empty_subcomponent_;
const hl7_component hl7_field::empty_component_;
const hl7_field hl7_segment::empty_field_;

// =============================================================================
// hl7_component Implementation
// =============================================================================

hl7_component::hl7_component(std::string value) {
    subcomponents_.emplace_back(std::move(value));
}

bool hl7_component::empty() const noexcept {
    return subcomponents_.empty() ||
           (subcomponents_.size() == 1 && subcomponents_[0].empty());
}

const hl7_subcomponent& hl7_component::subcomponent(size_t index) const {
    if (index == 0 || index > subcomponents_.size()) {
        return empty_subcomponent_;
    }
    return subcomponents_[index - 1];
}

hl7_subcomponent& hl7_component::subcomponent(size_t index) {
    if (index == 0) {
        index = 1;
    }
    while (subcomponents_.size() < index) {
        subcomponents_.emplace_back();
    }
    return subcomponents_[index - 1];
}

std::string_view hl7_component::value() const noexcept {
    if (subcomponents_.empty()) {
        return {};
    }
    return subcomponents_[0].value();
}

void hl7_component::set_value(std::string value) {
    subcomponents_.clear();
    subcomponents_.emplace_back(std::move(value));
}

std::string hl7_component::serialize(
    const hl7_encoding_characters& encoding) const {
    if (subcomponents_.empty()) {
        return {};
    }

    std::string result;
    bool first = true;
    for (const auto& sc : subcomponents_) {
        if (!first) {
            result += encoding.subcomponent_separator;
        }
        result += sc.value();
        first = false;
    }

    // Trim trailing separators
    while (!result.empty() && result.back() == encoding.subcomponent_separator) {
        result.pop_back();
    }

    return result;
}

hl7_component hl7_component::parse(std::string_view data,
                                    const hl7_encoding_characters& encoding) {
    hl7_component comp;

    if (data.empty()) {
        return comp;
    }

    size_t start = 0;
    size_t pos = 0;

    while (pos <= data.length()) {
        if (pos == data.length() ||
            data[pos] == encoding.subcomponent_separator) {
            comp.subcomponents_.emplace_back(
                std::string(data.substr(start, pos - start)));
            start = pos + 1;
        }
        ++pos;
    }

    return comp;
}

// =============================================================================
// hl7_field Implementation
// =============================================================================

hl7_field::hl7_field(std::string value) {
    repetitions_.emplace_back();
    repetitions_[0].emplace_back(std::move(value));
}

size_t hl7_field::component_count() const noexcept {
    if (repetitions_.empty()) {
        return 0;
    }
    return repetitions_[0].size();
}

bool hl7_field::empty() const noexcept {
    if (repetitions_.empty()) {
        return true;
    }
    for (const auto& rep : repetitions_) {
        for (const auto& comp : rep) {
            if (!comp.empty()) {
                return false;
            }
        }
    }
    return true;
}

const hl7_component& hl7_field::component(size_t index) const {
    if (repetitions_.empty() || index == 0 ||
        index > repetitions_[0].size()) {
        return empty_component_;
    }
    return repetitions_[0][index - 1];
}

hl7_component& hl7_field::component(size_t index) {
    if (repetitions_.empty()) {
        repetitions_.emplace_back();
    }
    if (index == 0) {
        index = 1;
    }
    while (repetitions_[0].size() < index) {
        repetitions_[0].emplace_back();
    }
    return repetitions_[0][index - 1];
}

const hl7_component& hl7_field::component(size_t rep_index,
                                           size_t comp_index) const {
    if (rep_index == 0 || rep_index > repetitions_.size()) {
        return empty_component_;
    }
    const auto& rep = repetitions_[rep_index - 1];
    if (comp_index == 0 || comp_index > rep.size()) {
        return empty_component_;
    }
    return rep[comp_index - 1];
}

std::string_view hl7_field::value() const noexcept {
    if (repetitions_.empty() || repetitions_[0].empty()) {
        return {};
    }
    return repetitions_[0][0].value();
}

void hl7_field::set_value(std::string value) {
    repetitions_.clear();
    repetitions_.emplace_back();
    repetitions_[0].emplace_back(std::move(value));
}

std::vector<std::string> hl7_field::repetitions(
    const hl7_encoding_characters& encoding) const {
    std::vector<std::string> result;
    for (const auto& rep : repetitions_) {
        std::string rep_str;
        bool first = true;
        for (const auto& comp : rep) {
            if (!first) {
                rep_str += encoding.component_separator;
            }
            rep_str += comp.serialize(encoding);
            first = false;
        }
        result.push_back(std::move(rep_str));
    }
    return result;
}

void hl7_field::add_repetition() {
    repetitions_.emplace_back();
}

std::string hl7_field::serialize(
    const hl7_encoding_characters& encoding) const {
    if (repetitions_.empty()) {
        return {};
    }

    std::string result;
    bool first_rep = true;

    for (const auto& rep : repetitions_) {
        if (!first_rep) {
            result += encoding.repetition_separator;
        }

        bool first_comp = true;
        for (const auto& comp : rep) {
            if (!first_comp) {
                result += encoding.component_separator;
            }
            result += comp.serialize(encoding);
            first_comp = false;
        }

        first_rep = false;
    }

    // Trim trailing separators
    while (!result.empty() &&
           (result.back() == encoding.component_separator ||
            result.back() == encoding.repetition_separator)) {
        result.pop_back();
    }

    return result;
}

hl7_field hl7_field::parse(std::string_view data,
                            const hl7_encoding_characters& encoding) {
    hl7_field field;

    if (data.empty()) {
        return field;
    }

    // Split by repetition separator
    size_t rep_start = 0;
    size_t pos = 0;

    while (pos <= data.length()) {
        if (pos == data.length() || data[pos] == encoding.repetition_separator) {
            std::string_view rep_data = data.substr(rep_start, pos - rep_start);

            std::vector<hl7_component> components;

            // Split by component separator
            size_t comp_start = 0;
            size_t comp_pos = 0;

            while (comp_pos <= rep_data.length()) {
                if (comp_pos == rep_data.length() ||
                    rep_data[comp_pos] == encoding.component_separator) {
                    components.push_back(hl7_component::parse(
                        rep_data.substr(comp_start, comp_pos - comp_start),
                        encoding));
                    comp_start = comp_pos + 1;
                }
                ++comp_pos;
            }

            field.repetitions_.push_back(std::move(components));
            rep_start = pos + 1;
        }
        ++pos;
    }

    return field;
}

// =============================================================================
// hl7_segment Implementation
// =============================================================================

hl7_segment::hl7_segment(std::string segment_id)
    : segment_id_(std::move(segment_id)) {}

const hl7_field& hl7_segment::field(size_t index) const {
    if (index == 0 || index > fields_.size()) {
        return empty_field_;
    }
    return fields_[index - 1];
}

hl7_field& hl7_segment::field(size_t index) {
    if (index == 0) {
        index = 1;
    }
    while (fields_.size() < index) {
        fields_.emplace_back();
    }
    return fields_[index - 1];
}

std::string_view hl7_segment::field_value(size_t index) const {
    return field(index).value();
}

void hl7_segment::set_field(size_t index, std::string value) {
    field(index).set_value(std::move(value));
}

std::string_view hl7_segment::get_value(std::string_view path) const {
    // Parse path: field[.component[.subcomponent]]
    size_t dot1 = path.find('.');
    size_t field_idx = 0;

    if (auto result = std::from_chars(path.data(),
                                       path.data() + (dot1 == std::string_view::npos
                                                          ? path.size()
                                                          : dot1),
                                       field_idx);
        result.ec != std::errc{}) {
        return {};
    }

    const auto& f = field(field_idx);

    if (dot1 == std::string_view::npos) {
        return f.value();
    }

    // Parse component
    path = path.substr(dot1 + 1);
    size_t dot2 = path.find('.');
    size_t comp_idx = 0;

    if (auto result = std::from_chars(path.data(),
                                       path.data() + (dot2 == std::string_view::npos
                                                          ? path.size()
                                                          : dot2),
                                       comp_idx);
        result.ec != std::errc{}) {
        return {};
    }

    const auto& c = f.component(comp_idx);

    if (dot2 == std::string_view::npos) {
        return c.value();
    }

    // Parse subcomponent
    path = path.substr(dot2 + 1);
    size_t subcomp_idx = 0;

    if (auto result = std::from_chars(path.data(), path.data() + path.size(),
                                       subcomp_idx);
        result.ec != std::errc{}) {
        return {};
    }

    return c.subcomponent(subcomp_idx).value();
}

void hl7_segment::set_value(std::string_view path, std::string value) {
    // Parse path: field[.component[.subcomponent]]
    size_t dot1 = path.find('.');
    size_t field_idx = 0;

    if (auto result = std::from_chars(path.data(),
                                       path.data() + (dot1 == std::string_view::npos
                                                          ? path.size()
                                                          : dot1),
                                       field_idx);
        result.ec != std::errc{}) {
        return;
    }

    auto& f = field(field_idx);

    if (dot1 == std::string_view::npos) {
        f.set_value(std::move(value));
        return;
    }

    // Parse component
    path = path.substr(dot1 + 1);
    size_t dot2 = path.find('.');
    size_t comp_idx = 0;

    if (auto result = std::from_chars(path.data(),
                                       path.data() + (dot2 == std::string_view::npos
                                                          ? path.size()
                                                          : dot2),
                                       comp_idx);
        result.ec != std::errc{}) {
        return;
    }

    auto& c = f.component(comp_idx);

    if (dot2 == std::string_view::npos) {
        c.set_value(std::move(value));
        return;
    }

    // Parse subcomponent
    path = path.substr(dot2 + 1);
    size_t subcomp_idx = 0;

    if (auto result = std::from_chars(path.data(), path.data() + path.size(),
                                       subcomp_idx);
        result.ec != std::errc{}) {
        return;
    }

    c.subcomponent(subcomp_idx).set_value(std::move(value));
}

std::string hl7_segment::serialize(
    const hl7_encoding_characters& encoding) const {
    std::string result = segment_id_;

    // Special handling for MSH segment
    if (is_msh()) {
        result += encoding.field_separator;
        result += encoding.to_msh2();

        // MSH fields start at index 3 (after encoding characters)
        for (size_t i = 2; i < fields_.size(); ++i) {
            result += encoding.field_separator;
            result += fields_[i].serialize(encoding);
        }
    } else {
        for (const auto& f : fields_) {
            result += encoding.field_separator;
            result += f.serialize(encoding);
        }
    }

    // Trim trailing field separators
    while (!result.empty() && result.back() == encoding.field_separator) {
        result.pop_back();
    }

    return result;
}

std::expected<hl7_segment, hl7_error> hl7_segment::parse(
    std::string_view data, const hl7_encoding_characters& encoding) {
    if (data.empty()) {
        return std::unexpected(hl7_error::invalid_segment);
    }

    // Extract segment ID (first 3 characters)
    if (data.length() < 3) {
        return std::unexpected(hl7_error::invalid_segment);
    }

    hl7_segment segment(std::string(data.substr(0, 3)));

    // Handle MSH segment specially
    if (segment.is_msh()) {
        if (data.length() < 4) {
            return std::unexpected(hl7_error::invalid_msh);
        }

        // MSH-1 is the field separator (position 3)
        // MSH-2 is the encoding characters (positions 4-7)
        // We already have encoding, skip to MSH-3

        data = data.substr(3);  // Skip "MSH"

        if (data.empty() || data[0] != encoding.field_separator) {
            return std::unexpected(hl7_error::invalid_msh);
        }

        data = data.substr(1);  // Skip field separator

        // Find MSH-2 (encoding characters)
        size_t msh2_end = data.find(encoding.field_separator);
        if (msh2_end == std::string_view::npos) {
            msh2_end = data.length();
        }

        // Add placeholder fields for MSH-1 and MSH-2
        segment.fields_.emplace_back(std::string(1, encoding.field_separator));
        segment.fields_.emplace_back(std::string(data.substr(0, msh2_end)));

        // Parse remaining fields
        if (msh2_end < data.length()) {
            data = data.substr(msh2_end + 1);

            size_t field_start = 0;
            size_t pos = 0;

            while (pos <= data.length()) {
                if (pos == data.length() ||
                    data[pos] == encoding.field_separator) {
                    segment.fields_.push_back(hl7_field::parse(
                        data.substr(field_start, pos - field_start), encoding));
                    field_start = pos + 1;
                }
                ++pos;
            }
        }
    } else {
        // Regular segment
        if (data.length() > 3 && data[3] == encoding.field_separator) {
            data = data.substr(4);  // Skip "XXX|"

            size_t field_start = 0;
            size_t pos = 0;

            while (pos <= data.length()) {
                if (pos == data.length() ||
                    data[pos] == encoding.field_separator) {
                    segment.fields_.push_back(hl7_field::parse(
                        data.substr(field_start, pos - field_start), encoding));
                    field_start = pos + 1;
                }
                ++pos;
            }
        }
    }

    return segment;
}

// =============================================================================
// hl7_message::impl
// =============================================================================

class hl7_message::impl {
public:
    hl7_encoding_characters encoding_;
    std::vector<hl7_segment> segments_;

    impl() = default;

    impl(const impl& other) = default;
    impl(impl&&) noexcept = default;
    impl& operator=(const impl&) = default;
    impl& operator=(impl&&) noexcept = default;
};

// =============================================================================
// hl7_message Implementation
// =============================================================================

hl7_message::hl7_message() : pimpl_(std::make_unique<impl>()) {}

hl7_message::hl7_message(const hl7_message& other)
    : pimpl_(std::make_unique<impl>(*other.pimpl_)) {}

hl7_message::hl7_message(hl7_message&& other) noexcept = default;

hl7_message& hl7_message::operator=(const hl7_message& other) {
    if (this != &other) {
        pimpl_ = std::make_unique<impl>(*other.pimpl_);
    }
    return *this;
}

hl7_message& hl7_message::operator=(hl7_message&& other) noexcept = default;

hl7_message::~hl7_message() = default;

std::expected<hl7_message, hl7_error> hl7_message::parse(std::string_view data) {
    hl7_encoding_characters encoding;

    // Extract encoding from MSH segment
    if (data.length() < 8) {
        return std::unexpected(hl7_error::empty_message);
    }

    if (data.substr(0, 3) != "MSH") {
        return std::unexpected(hl7_error::missing_msh);
    }

    encoding.field_separator = data[3];
    encoding = hl7_encoding_characters::from_msh2(data.substr(4, 4));
    encoding.field_separator = data[3];

    return parse(data, encoding);
}

std::expected<hl7_message, hl7_error> hl7_message::parse(
    std::string_view data, const hl7_encoding_characters& encoding) {
    if (data.empty()) {
        return std::unexpected(hl7_error::empty_message);
    }

    if (data.length() > HL7_MAX_MESSAGE_SIZE) {
        return std::unexpected(hl7_error::message_too_large);
    }

    hl7_message msg;
    msg.pimpl_->encoding_ = encoding;

    // Split into segments
    size_t seg_start = 0;
    size_t pos = 0;

    while (pos <= data.length()) {
        if (pos == data.length() || data[pos] == HL7_SEGMENT_TERMINATOR ||
            data[pos] == HL7_LINE_FEED) {
            if (pos > seg_start) {
                std::string_view seg_data = data.substr(seg_start, pos - seg_start);

                // Trim trailing whitespace
                while (!seg_data.empty() &&
                       (seg_data.back() == '\r' || seg_data.back() == '\n' ||
                        seg_data.back() == ' ')) {
                    seg_data = seg_data.substr(0, seg_data.length() - 1);
                }

                if (!seg_data.empty()) {
                    auto seg_result = hl7_segment::parse(seg_data, encoding);
                    if (!seg_result) {
                        return std::unexpected(seg_result.error());
                    }
                    msg.pimpl_->segments_.push_back(std::move(*seg_result));
                }
            }
            seg_start = pos + 1;
        }
        ++pos;
    }

    // Validate MSH is present
    if (msg.pimpl_->segments_.empty() ||
        msg.pimpl_->segments_[0].segment_id() != "MSH") {
        return std::unexpected(hl7_error::missing_msh);
    }

    return msg;
}

std::string hl7_message::serialize() const {
    std::string result;

    for (size_t i = 0; i < pimpl_->segments_.size(); ++i) {
        if (i > 0) {
            result += HL7_SEGMENT_TERMINATOR;
        }
        result += pimpl_->segments_[i].serialize(pimpl_->encoding_);
    }

    result += HL7_SEGMENT_TERMINATOR;
    return result;
}

size_t hl7_message::estimated_size() const noexcept {
    size_t size = 0;
    for (const auto& seg : pimpl_->segments_) {
        size += seg.segment_id().length() + seg.field_count() * 20;
    }
    return size;
}

hl7_message_header hl7_message::header() const {
    hl7_message_header hdr;

    const auto* msh = segment("MSH");
    if (!msh) {
        return hdr;
    }

    hdr.encoding = pimpl_->encoding_;
    hdr.sending_application = std::string(msh->field_value(3));
    hdr.sending_facility = std::string(msh->field_value(4));
    hdr.receiving_application = std::string(msh->field_value(5));
    hdr.receiving_facility = std::string(msh->field_value(6));

    // Parse timestamp
    if (auto ts = hl7_timestamp::parse(msh->field_value(7))) {
        hdr.timestamp = *ts;
    }

    hdr.security = std::string(msh->field_value(8));

    // Parse message type (field 9)
    const auto& type_field = msh->field(9);
    hdr.type_string = std::string(type_field.component(1).value());
    hdr.type = parse_message_type(hdr.type_string);
    hdr.trigger_event = std::string(type_field.component(2).value());
    hdr.message_structure = std::string(type_field.component(3).value());

    hdr.message_control_id = std::string(msh->field_value(10));
    hdr.processing_id = std::string(msh->field_value(11));
    hdr.version_id = std::string(msh->field_value(12));

    // Sequence number (field 13)
    auto seq_str = msh->field_value(13);
    if (!seq_str.empty()) {
        int64_t seq = 0;
        if (auto result = std::from_chars(seq_str.data(),
                                           seq_str.data() + seq_str.size(), seq);
            result.ec == std::errc{}) {
            hdr.sequence_number = seq;
        }
    }

    hdr.accept_ack_type = std::string(msh->field_value(15));
    hdr.app_ack_type = std::string(msh->field_value(16));
    hdr.country_code = std::string(msh->field_value(17));
    hdr.character_set = std::string(msh->field_value(18));

    return hdr;
}

const hl7_encoding_characters& hl7_message::encoding() const noexcept {
    return pimpl_->encoding_;
}

void hl7_message::set_encoding(const hl7_encoding_characters& encoding) {
    pimpl_->encoding_ = encoding;
}

bool hl7_message::empty() const noexcept {
    return pimpl_->segments_.empty();
}

message_type hl7_message::type() const noexcept {
    return header().type;
}

std::string_view hl7_message::trigger_event() const noexcept {
    const auto* msh = segment("MSH");
    if (!msh) return {};
    return msh->field(9).component(2).value();
}

std::string_view hl7_message::control_id() const noexcept {
    const auto* msh = segment("MSH");
    if (!msh) return {};
    return msh->field_value(10);
}

size_t hl7_message::segment_count() const noexcept {
    return pimpl_->segments_.size();
}

size_t hl7_message::segment_count(std::string_view segment_id) const noexcept {
    size_t count = 0;
    for (const auto& seg : pimpl_->segments_) {
        if (seg.segment_id() == segment_id) {
            ++count;
        }
    }
    return count;
}

const hl7_segment& hl7_message::segment_at(size_t index) const {
    return pimpl_->segments_.at(index);
}

hl7_segment& hl7_message::segment_at(size_t index) {
    return pimpl_->segments_.at(index);
}

const hl7_segment* hl7_message::segment(std::string_view segment_id) const {
    for (const auto& seg : pimpl_->segments_) {
        if (seg.segment_id() == segment_id) {
            return &seg;
        }
    }
    return nullptr;
}

hl7_segment* hl7_message::segment(std::string_view segment_id) {
    for (auto& seg : pimpl_->segments_) {
        if (seg.segment_id() == segment_id) {
            return &seg;
        }
    }
    return nullptr;
}

const hl7_segment* hl7_message::segment(std::string_view segment_id,
                                         size_t occurrence) const {
    size_t count = 0;
    for (const auto& seg : pimpl_->segments_) {
        if (seg.segment_id() == segment_id) {
            if (count == occurrence) {
                return &seg;
            }
            ++count;
        }
    }
    return nullptr;
}

hl7_segment* hl7_message::segment(std::string_view segment_id,
                                   size_t occurrence) {
    size_t count = 0;
    for (auto& seg : pimpl_->segments_) {
        if (seg.segment_id() == segment_id) {
            if (count == occurrence) {
                return &seg;
            }
            ++count;
        }
    }
    return nullptr;
}

std::vector<const hl7_segment*> hl7_message::segments(
    std::string_view segment_id) const {
    std::vector<const hl7_segment*> result;
    for (const auto& seg : pimpl_->segments_) {
        if (seg.segment_id() == segment_id) {
            result.push_back(&seg);
        }
    }
    return result;
}

bool hl7_message::has_segment(std::string_view segment_id) const noexcept {
    return segment(segment_id) != nullptr;
}

hl7_segment& hl7_message::add_segment(std::string_view segment_id) {
    pimpl_->segments_.emplace_back(std::string(segment_id));
    return pimpl_->segments_.back();
}

void hl7_message::insert_segment(size_t index, hl7_segment seg) {
    if (index >= pimpl_->segments_.size()) {
        pimpl_->segments_.push_back(std::move(seg));
    } else {
        pimpl_->segments_.insert(pimpl_->segments_.begin() + static_cast<ptrdiff_t>(index),
                                  std::move(seg));
    }
}

void hl7_message::remove_segment(size_t index) {
    if (index < pimpl_->segments_.size()) {
        pimpl_->segments_.erase(pimpl_->segments_.begin() + static_cast<ptrdiff_t>(index));
    }
}

size_t hl7_message::remove_segments(std::string_view segment_id) {
    size_t removed = 0;
    auto it = pimpl_->segments_.begin();
    while (it != pimpl_->segments_.end()) {
        if (it->segment_id() == segment_id) {
            it = pimpl_->segments_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::string_view hl7_message::get_value(std::string_view path) const {
    // Parse path: SEGMENT[occurrence].field[.component[.subcomponent]]
    size_t dot1 = path.find('.');
    if (dot1 == std::string_view::npos) {
        return {};
    }

    std::string_view seg_part = path.substr(0, dot1);
    std::string_view field_path = path.substr(dot1 + 1);

    // Check for occurrence index
    size_t bracket = seg_part.find('[');
    std::string_view seg_id;
    size_t occurrence = 0;

    if (bracket != std::string_view::npos) {
        seg_id = seg_part.substr(0, bracket);
        size_t end_bracket = seg_part.find(']', bracket);
        if (end_bracket != std::string_view::npos) {
            std::from_chars(seg_part.data() + bracket + 1,
                            seg_part.data() + end_bracket, occurrence);
        }
    } else {
        seg_id = seg_part;
    }

    const auto* seg = segment(seg_id, occurrence);
    if (!seg) {
        return {};
    }

    return seg->get_value(field_path);
}

void hl7_message::set_value(std::string_view path, std::string value) {
    // Parse path: SEGMENT[occurrence].field[.component[.subcomponent]]
    size_t dot1 = path.find('.');
    if (dot1 == std::string_view::npos) {
        return;
    }

    std::string_view seg_part = path.substr(0, dot1);
    std::string_view field_path = path.substr(dot1 + 1);

    // Check for occurrence index
    size_t bracket = seg_part.find('[');
    std::string_view seg_id;
    size_t occurrence = 0;

    if (bracket != std::string_view::npos) {
        seg_id = seg_part.substr(0, bracket);
        size_t end_bracket = seg_part.find(']', bracket);
        if (end_bracket != std::string_view::npos) {
            std::from_chars(seg_part.data() + bracket + 1,
                            seg_part.data() + end_bracket, occurrence);
        }
    } else {
        seg_id = seg_part;
    }

    // Find or create segment
    auto* seg = segment(seg_id, occurrence);
    if (!seg) {
        // Create segments as needed
        while (segment_count(seg_id) <= occurrence) {
            add_segment(seg_id);
        }
        seg = segment(seg_id, occurrence);
    }

    if (seg) {
        seg->set_value(field_path, std::move(value));
    }
}

validation_result hl7_message::validate() const {
    validation_result result;

    // Check MSH segment exists
    const auto* msh = segment("MSH");
    if (!msh) {
        result.add_error(hl7_error::missing_msh, "MSH", "MSH segment is required");
        return result;
    }

    // Check required MSH fields
    if (msh->field_value(9).empty()) {
        result.add_error(hl7_error::missing_required_field, "MSH.9",
                         "Message type is required");
    }

    if (msh->field_value(10).empty()) {
        result.add_error(hl7_error::missing_required_field, "MSH.10",
                         "Message control ID is required");
    }

    if (msh->field_value(11).empty()) {
        result.add_error(hl7_error::missing_required_field, "MSH.11",
                         "Processing ID is required");
    }

    if (msh->field_value(12).empty()) {
        result.add_error(hl7_error::missing_required_field, "MSH.12",
                         "Version ID is required");
    }

    return result;
}

bool hl7_message::is_valid() const {
    return validate().valid;
}

auto hl7_message::begin() const noexcept {
    return pimpl_->segments_.begin();
}

auto hl7_message::end() const noexcept {
    return pimpl_->segments_.end();
}

auto hl7_message::begin() noexcept {
    return pimpl_->segments_.begin();
}

auto hl7_message::end() noexcept {
    return pimpl_->segments_.end();
}

hl7_message hl7_message::create_ack(ack_code code, std::string_view text) const {
    hl7_message ack;
    ack.pimpl_->encoding_ = pimpl_->encoding_;

    auto& msh = ack.add_segment("MSH");

    // Swap sending/receiving
    auto hdr = header();

    msh.set_field(1, std::string(1, pimpl_->encoding_.field_separator));
    msh.set_field(2, pimpl_->encoding_.to_msh2());
    msh.set_field(3, hdr.receiving_application);
    msh.set_field(4, hdr.receiving_facility);
    msh.set_field(5, hdr.sending_application);
    msh.set_field(6, hdr.sending_facility);
    msh.set_field(7, hl7_timestamp::now().to_string());
    msh.set_value("9.1", "ACK");
    msh.set_value("9.2", hdr.trigger_event);
    msh.set_field(10, std::string(control_id()) + "_ACK");
    msh.set_field(11, hdr.processing_id);
    msh.set_field(12, hdr.version_id);

    // Add MSA segment
    auto& msa = ack.add_segment("MSA");
    msa.set_field(1, to_string(code));
    msa.set_field(2, std::string(control_id()));
    if (!text.empty()) {
        msa.set_field(3, std::string(text));
    }

    return ack;
}

hl7_message hl7_message::clone() const {
    return *this;
}

void hl7_message::clear() {
    pimpl_->segments_.clear();
    pimpl_->encoding_ = hl7_encoding_characters{};
}

}  // namespace pacs::bridge::hl7
