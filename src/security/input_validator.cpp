/**
 * @file input_validator.cpp
 * @brief Implementation of HL7 message input validation
 *
 * @see include/pacs/bridge/security/input_validator.h
 * @see https://github.com/kcenon/pacs_bridge/issues/43
 */

#include "pacs/bridge/security/input_validator.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <regex>
#include <sstream>

namespace pacs::bridge::security {

// =============================================================================
// SQL/Command Injection Patterns
// =============================================================================

namespace {

// SQL injection patterns to detect
const std::array<std::regex, 8> sql_injection_patterns = {
    std::regex(R"((\b(SELECT|INSERT|UPDATE|DELETE|DROP|UNION|ALTER)\b))", std::regex::icase),
    std::regex(R"(--\s*$)", std::regex::icase),
    std::regex(R"(;\s*(SELECT|INSERT|UPDATE|DELETE|DROP))", std::regex::icase),
    std::regex(R"((['"])\s*OR\s+\1\s*=\s*\1)", std::regex::icase),
    std::regex(R"(\bEXEC(UTE)?\b)", std::regex::icase),
    std::regex(R"(\bxp_\w+)", std::regex::icase),
    std::regex(R"(0x[0-9a-fA-F]+)", std::regex::icase),
    std::regex(R"(\bCHAR\s*\(\s*\d+\s*\))", std::regex::icase)
};

// Command injection patterns to detect
// Note: HL7 uses | as field delimiter, ^ as component delimiter, ~ as repetition
// separator, \ as escape character, and & as subcomponent delimiter (MSH-2).
// We must exclude these HL7 encoding characters from the command injection check.
const std::array<std::regex, 6> command_injection_patterns = {
    std::regex(R"([;`$])", std::regex::icase),  // Exclude | & (HL7 delimiters)
    std::regex(R"(\$\(|\$\{)", std::regex::icase),
    std::regex(R"(\b(cat|ls|rm|mv|cp|chmod|chown|wget|curl)\b)", std::regex::icase),
    std::regex(R"(\.\.\/)", std::regex::icase),
    std::regex(R"(\/etc\/|\/bin\/|\/usr\/)", std::regex::icase),
    std::regex(R"(\n)", std::regex::icase)  // Only newline, not CR (HL7 uses CR)
};

// HL7 segment delimiter
constexpr char HL7_SEGMENT_DELIMITER = '\r';
constexpr char HL7_FIELD_DELIMITER = '|';
constexpr char HL7_COMPONENT_DELIMITER = '^';

}  // namespace

// =============================================================================
// Implementation Class
// =============================================================================

class input_validator::impl {
public:
    explicit impl(const validation_config& config) : config_(config) {}

    validation_config config_;
};

// =============================================================================
// Constructor / Destructor
// =============================================================================

input_validator::input_validator(const validation_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

input_validator::~input_validator() = default;

input_validator::input_validator(input_validator&&) noexcept = default;
input_validator& input_validator::operator=(input_validator&&) noexcept = default;

// =============================================================================
// Validation Methods
// =============================================================================

validation_result input_validator::validate(std::string_view message) const {
    // Check for empty message
    if (message.empty()) {
        return validation_result::failure(
            validation_error::empty_message,
            "Message is empty");
    }

    // Check message size
    if (auto error = check_size(message)) {
        return validation_result::failure(
            *error,
            "Message size exceeds limit: " + std::to_string(message.size()) +
            " > " + std::to_string(pimpl_->config_.max_message_size));
    }

    // Validate HL7 structure
    auto structure_result = validate_structure(message);
    if (!structure_result.valid) {
        return structure_result;
    }

    // Extract and validate MSH segment
    if (pimpl_->config_.validate_msh) {
        auto msh_end = message.find(HL7_SEGMENT_DELIMITER);
        std::string_view msh_segment = (msh_end != std::string_view::npos)
            ? message.substr(0, msh_end)
            : message;

        auto msh_result = validate_msh(msh_segment);
        if (!msh_result.valid) {
            return msh_result;
        }

        // Copy extracted fields to result
        structure_result.sending_app = msh_result.sending_app;
        structure_result.sending_facility = msh_result.sending_facility;
        structure_result.receiving_app = msh_result.receiving_app;
        structure_result.receiving_facility = msh_result.receiving_facility;
        structure_result.message_type = msh_result.message_type;
        structure_result.message_control_id = msh_result.message_control_id;
    }

    // Check for injection attacks
    if (pimpl_->config_.detect_sql_injection && detect_sql_injection(message)) {
        return validation_result::failure(
            validation_error::injection_detected,
            "Potential SQL injection detected");
    }

    if (pimpl_->config_.detect_command_injection && detect_command_injection(message)) {
        return validation_result::failure(
            validation_error::injection_detected,
            "Potential command injection detected");
    }

    structure_result.valid = true;
    structure_result.message_size = message.size();
    return structure_result;
}

std::pair<validation_result, std::string>
input_validator::validate_and_sanitize(std::string_view message) const {
    auto result = validate(message);
    std::string sanitized = sanitize(message);
    return {std::move(result), std::move(sanitized)};
}

std::optional<validation_error>
input_validator::check_size(std::string_view message) const {
    if (message.size() > pimpl_->config_.max_message_size) {
        return validation_error::message_too_large;
    }
    return std::nullopt;
}

validation_result
input_validator::validate_structure(std::string_view message) const {
    validation_result result;
    result.message_size = message.size();

    // Check for MSH segment at the beginning
    if (message.size() < 8 || message.substr(0, 3) != "MSH") {
        return validation_result::failure(
            validation_error::missing_msh_segment,
            "Message must start with MSH segment");
    }

    // Check for valid field delimiter
    if (message[3] != HL7_FIELD_DELIMITER) {
        return validation_result::failure(
            validation_error::invalid_hl7_structure,
            "Invalid field delimiter after MSH");
    }

    // Count segments
    size_t segment_count = 1;
    for (char c : message) {
        if (c == HL7_SEGMENT_DELIMITER) {
            ++segment_count;
        }
    }
    result.segment_count = segment_count;

    if (segment_count > pimpl_->config_.max_segment_count) {
        return validation_result::failure(
            validation_error::invalid_hl7_structure,
            "Too many segments: " + std::to_string(segment_count));
    }

    // Check for prohibited control characters (except allowed ones)
    for (size_t i = 0; i < message.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(message[i]);
        // Allow: CR (0x0D), LF (0x0A), Tab (0x09), and printable ASCII
        if (c < 0x09 || (c > 0x0D && c < 0x20 && c != 0x1B)) {
            // 0x1B (ESC) is sometimes used in HL7 for escape sequences
            if (c == 0x00 && pimpl_->config_.strip_null_bytes) {
                result.warnings.push_back("Null byte detected at position " +
                                          std::to_string(i));
                continue;
            }
            return validation_result::failure(
                validation_error::prohibited_characters,
                "Prohibited control character at position " + std::to_string(i),
                "byte=" + std::to_string(static_cast<int>(c)));
        }
    }

    result.valid = true;
    return result;
}

validation_result
input_validator::validate_msh(std::string_view msh_segment) const {
    validation_result result;

    // Split MSH into fields
    std::vector<std::string_view> fields;
    size_t start = 0;
    size_t pos = 0;

    while (pos < msh_segment.size()) {
        if (msh_segment[pos] == HL7_FIELD_DELIMITER) {
            fields.push_back(msh_segment.substr(start, pos - start));
            start = pos + 1;
        }
        ++pos;
    }
    fields.push_back(msh_segment.substr(start));

    // MSH needs at least 12 fields for basic validation
    if (fields.size() < 12) {
        return validation_result::failure(
            validation_error::invalid_msh_fields,
            "MSH segment has insufficient fields: " + std::to_string(fields.size()));
    }

    // Extract key fields (field numbering: MSH-1 is the delimiter itself)
    // fields[0] = "MSH", fields[1] = encoding characters, fields[2] = MSH-3, etc.
    if (fields.size() > 2) {
        result.sending_app = std::string(fields[2]);
    }
    if (fields.size() > 3) {
        result.sending_facility = std::string(fields[3]);
    }
    if (fields.size() > 4) {
        result.receiving_app = std::string(fields[4]);
    }
    if (fields.size() > 5) {
        result.receiving_facility = std::string(fields[5]);
    }
    if (fields.size() > 8) {
        result.message_type = std::string(fields[8]);
    }
    if (fields.size() > 9) {
        result.message_control_id = std::string(fields[9]);
    }

    // Validate sending application whitelist
    if (!pimpl_->config_.allowed_sending_apps.empty() && result.sending_app) {
        if (pimpl_->config_.allowed_sending_apps.find(*result.sending_app) ==
            pimpl_->config_.allowed_sending_apps.end()) {
            return validation_result::failure(
                validation_error::invalid_application_id,
                "Sending application not in whitelist: " + *result.sending_app,
                "MSH-3");
        }
    }

    // Validate sending facility whitelist
    if (!pimpl_->config_.allowed_sending_facilities.empty() && result.sending_facility) {
        if (pimpl_->config_.allowed_sending_facilities.find(*result.sending_facility) ==
            pimpl_->config_.allowed_sending_facilities.end()) {
            return validation_result::failure(
                validation_error::invalid_application_id,
                "Sending facility not in whitelist: " + *result.sending_facility,
                "MSH-4");
        }
    }

    // Validate receiving application whitelist
    if (!pimpl_->config_.allowed_receiving_apps.empty() && result.receiving_app) {
        if (pimpl_->config_.allowed_receiving_apps.find(*result.receiving_app) ==
            pimpl_->config_.allowed_receiving_apps.end()) {
            return validation_result::failure(
                validation_error::invalid_application_id,
                "Receiving application not in whitelist: " + *result.receiving_app,
                "MSH-5");
        }
    }

    // Check field lengths
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].size() > pimpl_->config_.max_field_length) {
            return validation_result::failure(
                validation_error::invalid_msh_fields,
                "Field exceeds maximum length at MSH-" + std::to_string(i + 1),
                "MSH-" + std::to_string(i + 1));
        }
    }

    result.valid = true;
    return result;
}

bool input_validator::detect_sql_injection(std::string_view content) const {
    for (const auto& pattern : sql_injection_patterns) {
        if (std::regex_search(content.begin(), content.end(), pattern)) {
            return true;
        }
    }
    return false;
}

bool input_validator::detect_command_injection(std::string_view content) const {
    for (const auto& pattern : command_injection_patterns) {
        if (std::regex_search(content.begin(), content.end(), pattern)) {
            return true;
        }
    }
    return false;
}

// =============================================================================
// Sanitization Methods
// =============================================================================

std::string input_validator::sanitize(std::string_view message) const {
    std::string result(message);

    // Strip null bytes if configured
    if (pimpl_->config_.strip_null_bytes) {
        result = strip_nulls(result);
    }

    // Normalize line endings if configured
    if (pimpl_->config_.normalize_line_endings) {
        result = normalize_endings(result);
    }

    return result;
}

std::string input_validator::strip_nulls(std::string_view message) {
    std::string result;
    result.reserve(message.size());

    for (char c : message) {
        if (c != '\0') {
            result.push_back(c);
        }
    }

    return result;
}

std::string input_validator::normalize_endings(std::string_view message) {
    std::string result;
    result.reserve(message.size());

    bool prev_cr = false;
    for (char c : message) {
        if (c == '\n') {
            if (!prev_cr) {
                result.push_back('\r');
            }
            prev_cr = false;
        } else if (c == '\r') {
            result.push_back('\r');
            prev_cr = true;
        } else {
            prev_cr = false;
            result.push_back(c);
        }
    }

    return result;
}

// =============================================================================
// Configuration Methods
// =============================================================================

void input_validator::set_config(const validation_config& config) {
    pimpl_->config_ = config;
}

const validation_config& input_validator::config() const noexcept {
    return pimpl_->config_;
}

void input_validator::add_allowed_sending_app(std::string_view app) {
    pimpl_->config_.allowed_sending_apps.insert(std::string(app));
}

void input_validator::add_allowed_sending_facility(std::string_view facility) {
    pimpl_->config_.allowed_sending_facilities.insert(std::string(facility));
}

void input_validator::add_allowed_receiving_app(std::string_view app) {
    pimpl_->config_.allowed_receiving_apps.insert(std::string(app));
}

void input_validator::add_allowed_receiving_facility(std::string_view facility) {
    pimpl_->config_.allowed_receiving_facilities.insert(std::string(facility));
}

}  // namespace pacs::bridge::security
