/**
 * @file log_sanitizer.cpp
 * @brief Implementation of healthcare-specific log sanitization
 *
 * @see include/pacs/bridge/security/log_sanitizer.h
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include "pacs/bridge/security/log_sanitizer.h"

#include <algorithm>
#include <regex>
#include <sstream>

namespace pacs::bridge::security {

// =============================================================================
// PHI Detection Patterns
// =============================================================================

namespace {

// HL7 segment delimiter
constexpr char HL7_SEGMENT_DELIMITER = '\r';
constexpr char HL7_FIELD_DELIMITER = '|';

// Field positions in PID segment that contain PHI
// PID-3: Patient ID, PID-5: Patient Name, PID-7: DOB, PID-11: Address,
// PID-13: Phone, PID-19: SSN
const std::vector<int> PID_PHI_FIELDS = {3, 5, 7, 11, 13, 14, 18, 19};

// MRN pattern: digits, possibly with prefixes
const std::regex MRN_PATTERN(R"(\b(MRN|mrn|PatientID)[\s:=]*([A-Z0-9]{4,12})\b)");

// Patient name pattern (Last, First or Last^First format)
const std::regex NAME_PATTERN(R"(\b([A-Z][a-z]+)\s*[\^,]\s*([A-Z][a-z]+)\b)");

// DOB pattern (YYYYMMDD or YYYY-MM-DD or MM/DD/YYYY)
const std::regex DOB_PATTERN(R"(\b(19|20)\d{2}[-/]?\d{2}[-/]?\d{2}\b)");

}  // namespace

// =============================================================================
// Implementation Class
// =============================================================================

class healthcare_log_sanitizer::impl {
public:
    explicit impl(const healthcare_sanitization_config& config)
        : config_(config) {
        initialize_patterns();
    }

    void initialize_patterns() {
        // Add standard healthcare patterns
        custom_patterns_.clear();

        // MRN pattern
        custom_patterns_.emplace_back(
            std::regex(R"(\b(MRN|PatientID|Patient ID)[\s:=]*([A-Z0-9]{4,12})\b)",
                       std::regex::icase),
            "$1=[PATIENT_ID]");

        // Patient name pattern (Last^First HL7 format)
        custom_patterns_.emplace_back(
            std::regex(R"((\|)([A-Z][A-Za-z'-]+)\^([A-Z][A-Za-z'-]+)(\||\^))"),
            "$1[PATIENT_NAME]$4");

        // SSN pattern
        custom_patterns_.emplace_back(
            std::regex(R"(\b\d{3}[-\s]?\d{2}[-\s]?\d{4}\b)"),
            "[SSN]");

        // Phone number pattern
        custom_patterns_.emplace_back(
            std::regex(R"(\b(\+?1[-.\s]?)?\(?\d{3}\)?[-.\s]?\d{3}[-.\s]?\d{4}\b)"),
            "[PHONE]");

        // Email pattern
        custom_patterns_.emplace_back(
            std::regex(R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Z|a-z]{2,}\b)"),
            "[EMAIL]");

        // DOB in HL7 format (YYYYMMDD)
        custom_patterns_.emplace_back(
            std::regex(R"(\b(19|20)\d{6}\b)"),
            "[DOB]");

        // Address-like patterns (number + street name)
        custom_patterns_.emplace_back(
            std::regex(R"(\b\d+\s+[A-Z][a-z]+\s+(St|Street|Ave|Avenue|Rd|Road|Blvd|Dr|Drive|Ln|Lane)\b)",
                       std::regex::icase),
            "[ADDRESS]");
    }

    healthcare_sanitization_config config_;
    std::vector<std::pair<std::regex, std::string>> custom_patterns_;
};

// Forward declaration
std::string sanitize_hl7_segment(std::string_view segment, const std::string& segment_type);

// =============================================================================
// Constructor / Destructor
// =============================================================================

healthcare_log_sanitizer::healthcare_log_sanitizer(
    const healthcare_sanitization_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

healthcare_log_sanitizer::~healthcare_log_sanitizer() = default;

healthcare_log_sanitizer::healthcare_log_sanitizer(healthcare_log_sanitizer&&) noexcept = default;
healthcare_log_sanitizer& healthcare_log_sanitizer::operator=(healthcare_log_sanitizer&&) noexcept = default;

// =============================================================================
// Sanitization Methods
// =============================================================================

std::string healthcare_log_sanitizer::sanitize(std::string_view content) const {
    if (!pimpl_->config_.enabled || content.empty()) {
        return std::string(content);
    }

    std::string result(content);

    // Apply all custom patterns
    for (const auto& [pattern, replacement] : pimpl_->custom_patterns_) {
        result = std::regex_replace(result, pattern, replacement);
    }

    return result;
}

std::string healthcare_log_sanitizer::sanitize_hl7(std::string_view hl7_message) const {
    if (!pimpl_->config_.enabled || hl7_message.empty()) {
        return std::string(hl7_message);
    }

    std::string result;
    result.reserve(hl7_message.size());

    // Process each segment
    size_t start = 0;
    size_t pos = 0;

    while (pos <= hl7_message.size()) {
        if (pos == hl7_message.size() || hl7_message[pos] == HL7_SEGMENT_DELIMITER) {
            std::string_view segment = hl7_message.substr(start, pos - start);

            // Check if this is a PHI segment
            std::string segment_type;
            if (segment.size() >= 3) {
                segment_type = std::string(segment.substr(0, 3));
            }

            if (pimpl_->config_.phi_segments.count(segment_type) > 0) {
                // Sanitize the segment field by field
                result += sanitize_hl7_segment(segment, segment_type);
            } else {
                result += segment;
            }

            if (pos < hl7_message.size()) {
                result += HL7_SEGMENT_DELIMITER;
            }
            start = pos + 1;
        }
        ++pos;
    }

    return result;
}

std::pair<std::string, std::vector<phi_detection>>
healthcare_log_sanitizer::sanitize_with_detections(std::string_view content) const {
    std::vector<phi_detection> detections;
    std::string sanitized = sanitize(content);

    // Detect PHI in original content
    detections = detect_phi(content);

    return {std::move(sanitized), std::move(detections)};
}

// =============================================================================
// Detection Methods
// =============================================================================

bool healthcare_log_sanitizer::contains_phi(std::string_view content) const {
    for (const auto& [pattern, _] : pimpl_->custom_patterns_) {
        if (std::regex_search(content.begin(), content.end(), pattern)) {
            return true;
        }
    }
    return false;
}

std::vector<phi_detection>
healthcare_log_sanitizer::detect_phi(std::string_view content) const {
    std::vector<phi_detection> detections;

    // Detect MRN patterns
    std::smatch match;
    std::string content_str(content);
    auto it = content_str.cbegin();

    while (std::regex_search(it, content_str.cend(), match, MRN_PATTERN)) {
        phi_detection detection;
        detection.type = phi_field_type::patient_id;
        detection.position = static_cast<size_t>(match.position() + (it - content_str.cbegin()));
        detection.length = match.length();
        detection.context = "[MRN detected]";
        detections.push_back(detection);
        it = match.suffix().first;
    }

    // Detect name patterns
    it = content_str.cbegin();
    while (std::regex_search(it, content_str.cend(), match, NAME_PATTERN)) {
        phi_detection detection;
        detection.type = phi_field_type::patient_name;
        detection.position = static_cast<size_t>(match.position() + (it - content_str.cbegin()));
        detection.length = match.length();
        detection.context = "[Name detected]";
        detections.push_back(detection);
        it = match.suffix().first;
    }

    return detections;
}

// =============================================================================
// Masking Methods
// =============================================================================

std::string healthcare_log_sanitizer::mask(std::string_view value,
                                           phi_field_type type) const {
    switch (pimpl_->config_.style) {
        case masking_style::asterisks:
            return std::string(value.size(), '*');

        case masking_style::type_label:
            return make_type_label(type);

        case masking_style::x_characters:
            return std::string(value.size(), 'X');

        case masking_style::partial:
            if (value.size() <= pimpl_->config_.partial_show_prefix +
                               pimpl_->config_.partial_show_suffix) {
                return std::string(value.size(), '*');
            }
            return std::string(value.substr(0, pimpl_->config_.partial_show_prefix)) +
                   std::string(value.size() - pimpl_->config_.partial_show_prefix -
                               pimpl_->config_.partial_show_suffix, '*') +
                   std::string(value.substr(value.size() - pimpl_->config_.partial_show_suffix));

        case masking_style::remove:
            return "";

        default:
            return make_type_label(type);
    }
}

std::string healthcare_log_sanitizer::make_type_label(phi_field_type type) {
    return std::string("[") + to_string(type) + "]";
}

// =============================================================================
// Configuration Methods
// =============================================================================

void healthcare_log_sanitizer::set_config(const healthcare_sanitization_config& config) {
    pimpl_->config_ = config;
    pimpl_->initialize_patterns();
}

const healthcare_sanitization_config& healthcare_log_sanitizer::config() const noexcept {
    return pimpl_->config_;
}

void healthcare_log_sanitizer::set_enabled(bool enabled) {
    pimpl_->config_.enabled = enabled;
}

bool healthcare_log_sanitizer::is_enabled() const noexcept {
    return pimpl_->config_.enabled;
}

void healthcare_log_sanitizer::add_custom_pattern(std::string_view pattern,
                                                   std::string_view replacement) {
    pimpl_->custom_patterns_.emplace_back(
        std::regex(std::string(pattern)),
        std::string(replacement));
}

// =============================================================================
// Private Helper Methods
// =============================================================================

// Note: This function is in pacs::bridge::security namespace to match forward declaration
std::string sanitize_hl7_segment(std::string_view segment, const std::string& segment_type) {
    std::string result;
    result.reserve(segment.size());

    // Split segment into fields
    std::vector<std::string> fields;
    size_t start = 0;
    size_t pos = 0;

    while (pos <= segment.size()) {
        if (pos == segment.size() || segment[pos] == HL7_FIELD_DELIMITER) {
            fields.emplace_back(segment.substr(start, pos - start));
            start = pos + 1;
        }
        ++pos;
    }

    // Determine which fields to mask based on segment type
    std::vector<int> phi_fields;
    if (segment_type == "PID") {
        phi_fields = PID_PHI_FIELDS;
    }

    // Rebuild segment with masked fields
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            result += HL7_FIELD_DELIMITER;
        }

        // Field index is 1-based in HL7, but fields[0] is segment name
        int field_num = static_cast<int>(i);
        if (std::find(phi_fields.begin(), phi_fields.end(), field_num) != phi_fields.end()) {
            // Mask this field
            if (!fields[i].empty()) {
                result += "[REDACTED]";
            }
        } else {
            result += fields[i];
        }
    }

    return result;
}

// =============================================================================
// Utility Functions
// =============================================================================

std::string make_safe_hl7_summary(std::string_view hl7_message) {
    std::ostringstream oss;

    if (hl7_message.empty() || hl7_message.size() < 8) {
        return "[Invalid HL7 message]";
    }

    // Extract MSH segment
    auto msh_end = hl7_message.find(HL7_SEGMENT_DELIMITER);
    std::string_view msh = (msh_end != std::string_view::npos)
        ? hl7_message.substr(0, msh_end)
        : hl7_message;

    // Parse MSH fields
    std::vector<std::string_view> fields;
    size_t start = 0;
    size_t pos = 0;

    while (pos <= msh.size()) {
        if (pos == msh.size() || msh[pos] == HL7_FIELD_DELIMITER) {
            fields.push_back(msh.substr(start, pos - start));
            start = pos + 1;
        }
        ++pos;
    }

    // Build summary
    oss << "HL7[";

    // MSH-9: Message Type
    if (fields.size() > 8 && !fields[8].empty()) {
        oss << "type=" << fields[8];
    }

    // MSH-10: Message Control ID
    if (fields.size() > 9 && !fields[9].empty()) {
        oss << ", ctrl=" << fields[9];
    }

    // MSH-3: Sending Application
    if (fields.size() > 2 && !fields[2].empty()) {
        oss << ", from=" << fields[2];
    }

    oss << ", size=" << hl7_message.size() << "]";

    return oss.str();
}

std::string make_safe_session_desc(std::string_view remote_address,
                                   uint16_t remote_port,
                                   uint64_t session_id,
                                   bool mask_ip) {
    std::ostringstream oss;

    oss << "session=" << session_id << ", peer=";

    if (mask_ip && !remote_address.empty()) {
        // Mask last two octets of IP
        std::string ip(remote_address);
        size_t last_dot = ip.rfind('.');
        if (last_dot != std::string::npos) {
            size_t second_last_dot = ip.rfind('.', last_dot - 1);
            if (second_last_dot != std::string::npos) {
                ip = ip.substr(0, second_last_dot) + ".x.x";
            }
        }
        oss << ip;
    } else {
        oss << remote_address;
    }

    oss << ":" << remote_port;

    return oss.str();
}

}  // namespace pacs::bridge::security
