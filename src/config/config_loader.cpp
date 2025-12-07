/**
 * @file config_loader.cpp
 * @brief Implementation of configuration file loading and parsing
 *
 * Provides YAML and JSON configuration file loading with environment
 * variable expansion and comprehensive validation.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/20
 */

#include "pacs/bridge/config/config_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>

// Note: This implementation uses a simple custom parser for YAML subset
// and nlohmann/json for JSON. In production, consider using yaml-cpp.

namespace pacs::bridge::config {

namespace {

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Read entire file contents
 */
[[nodiscard]] std::expected<std::string, config_load_error> read_file(
    const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::unexpected(config_load_error{
            .code = config_error::file_not_found,
            .message = std::format("Configuration file not found: {}",
                                   path.string()),
            .file_path = path,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    std::ifstream file(path);
    if (!file) {
        return std::unexpected(config_load_error{
            .code = config_error::io_error,
            .message =
                std::format("Failed to open file: {}", path.string()),
            .file_path = path,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    if (file.bad()) {
        return std::unexpected(config_load_error{
            .code = config_error::io_error,
            .message =
                std::format("Error reading file: {}", path.string()),
            .file_path = path,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    return buffer.str();
}

/**
 * @brief Write string to file
 */
[[nodiscard]] std::expected<void, config_load_error> write_file(
    const std::filesystem::path& path, std::string_view content) {
    // Create parent directories if needed
    if (auto parent = path.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return std::unexpected(config_load_error{
                .code = config_error::io_error,
                .message = std::format("Failed to create directory: {}",
                                       parent.string()),
                .file_path = path,
                .line_number = std::nullopt,
                .column_number = std::nullopt,
                .validation_errors = {}});
        }
    }

    std::ofstream file(path);
    if (!file) {
        return std::unexpected(config_load_error{
            .code = config_error::io_error,
            .message =
                std::format("Failed to create file: {}", path.string()),
            .file_path = path,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    file << content;

    if (!file) {
        return std::unexpected(config_load_error{
            .code = config_error::io_error,
            .message =
                std::format("Failed to write file: {}", path.string()),
            .file_path = path,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    return {};
}

/**
 * @brief Determine file format from extension
 */
[[nodiscard]] std::expected<std::string, config_load_error>
detect_format(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (ext == ".yaml" || ext == ".yml") {
        return "yaml";
    } else if (ext == ".json") {
        return "json";
    }

    return std::unexpected(config_load_error{
        .code = config_error::invalid_format,
        .message = std::format(
            "Unknown configuration file format: {}. Use .yaml, .yml, or .json",
            ext),
        .file_path = path,
        .line_number = std::nullopt,
        .column_number = std::nullopt,
        .validation_errors = {}});
}

/**
 * @brief Trim whitespace from string
 */
[[nodiscard]] std::string trim(std::string_view str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(start, end - start + 1));
}

/**
 * @brief Remove quotes from string value
 */
[[nodiscard]] std::string unquote(std::string_view str) {
    if (str.length() >= 2) {
        if ((str.front() == '"' && str.back() == '"') ||
            (str.front() == '\'' && str.back() == '\'')) {
            return std::string(str.substr(1, str.length() - 2));
        }
    }
    return std::string(str);
}

/**
 * @brief Parse boolean value
 */
[[nodiscard]] std::optional<bool> parse_bool(std::string_view str) {
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (lower == "true" || lower == "yes" || lower == "on" || lower == "1") {
        return true;
    }
    if (lower == "false" || lower == "no" || lower == "off" || lower == "0") {
        return false;
    }
    return std::nullopt;
}

/**
 * @brief Parse integer value
 */
[[nodiscard]] std::optional<int64_t> parse_int(std::string_view str) {
    try {
        size_t pos;
        int64_t value = std::stoll(std::string(str), &pos);
        if (pos == str.length()) {
            return value;
        }
    } catch (...) {
    }
    return std::nullopt;
}

/**
 * @brief Parse floating point value
 */
[[nodiscard]] std::optional<double> parse_double(std::string_view str) {
    try {
        size_t pos;
        double value = std::stod(std::string(str), &pos);
        if (pos == str.length()) {
            return value;
        }
    } catch (...) {
    }
    return std::nullopt;
}

/**
 * @brief Parse duration value (e.g., "30s", "5m", "1h")
 */
[[nodiscard]] std::optional<std::chrono::seconds> parse_duration(
    std::string_view str) {
    if (str.empty()) return std::nullopt;

    // Try to parse as plain number (seconds)
    if (auto val = parse_int(str)) {
        return std::chrono::seconds(*val);
    }

    // Parse with unit suffix
    std::string num_str;
    char unit = 's';

    for (size_t i = 0; i < str.length(); ++i) {
        if (std::isdigit(str[i])) {
            num_str += str[i];
        } else if (std::isalpha(str[i]) && i == str.length() - 1) {
            unit = static_cast<char>(
                std::tolower(static_cast<unsigned char>(str[i])));
        } else {
            return std::nullopt;
        }
    }

    auto value = parse_int(num_str);
    if (!value) return std::nullopt;

    switch (unit) {
        case 's':
            return std::chrono::seconds(*value);
        case 'm':
            return std::chrono::seconds(*value * 60);
        case 'h':
            return std::chrono::seconds(*value * 3600);
        case 'd':
            return std::chrono::seconds(*value * 86400);
        default:
            return std::nullopt;
    }
}

/**
 * @brief Parse log level string
 */
[[nodiscard]] std::optional<log_level> parse_log_level(std::string_view str) {
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (lower == "trace") return log_level::trace;
    if (lower == "debug") return log_level::debug;
    if (lower == "info") return log_level::info;
    if (lower == "warn" || lower == "warning") return log_level::warn;
    if (lower == "error") return log_level::error;
    if (lower == "fatal" || lower == "critical") return log_level::fatal;

    return std::nullopt;
}

// =============================================================================
// Simple YAML Parser (subset)
// =============================================================================

/**
 * @brief Simple key-value map for YAML parsing
 */
using yaml_map = std::map<std::string, std::string>;
using yaml_section = std::map<std::string, yaml_map>;

/**
 * @brief Parse simple YAML into nested maps
 *
 * This is a simplified YAML parser that handles the configuration file
 * structure. For complex YAML, consider using yaml-cpp library.
 */
class simple_yaml_parser {
public:
    struct parse_result {
        std::map<std::string, std::string> flat_values;
        std::vector<std::map<std::string, std::string>> arrays;
        size_t error_line = 0;
        std::string error_message;
        bool success = true;
    };

    [[nodiscard]] static parse_result parse(std::string_view content) {
        parse_result result;
        std::vector<std::string> path_stack;
        int current_indent = 0;
        size_t line_number = 0;
        bool in_array = false;
        std::string array_prefix;

        std::istringstream stream{std::string{content}};
        std::string line;

        while (std::getline(stream, line)) {
            ++line_number;

            // Skip empty lines and comments
            auto trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') {
                continue;
            }

            // Calculate indentation
            int indent = 0;
            for (char c : line) {
                if (c == ' ')
                    indent++;
                else if (c == '\t')
                    indent += 2;
                else
                    break;
            }

            // Adjust path stack based on indentation
            while (!path_stack.empty() && indent <= current_indent) {
                path_stack.pop_back();
                current_indent -= 2;
            }

            // Handle array item
            if (trimmed.starts_with("- ")) {
                in_array = true;
                auto item_content = trim(trimmed.substr(2));
                if (!item_content.empty()) {
                    // Simple array item or key-value pair in array
                    if (auto colon_pos = item_content.find(':');
                        colon_pos != std::string::npos) {
                        auto key = trim(item_content.substr(0, colon_pos));
                        auto value = trim(item_content.substr(colon_pos + 1));
                        value = unquote(value);

                        std::string full_key = build_path(path_stack) + "." +
                                               std::to_string(result.arrays.size()) +
                                               "." + key;
                        result.flat_values[full_key] = value;
                    }
                }
                result.arrays.push_back({});
                continue;
            }

            // Handle key-value pair
            auto colon_pos = trimmed.find(':');
            if (colon_pos == std::string::npos) {
                result.success = false;
                result.error_line = line_number;
                result.error_message = "Invalid YAML syntax: missing colon";
                return result;
            }

            auto key = trim(trimmed.substr(0, colon_pos));
            auto value = trim(trimmed.substr(colon_pos + 1));

            if (value.empty()) {
                // This is a section header
                path_stack.push_back(key);
                current_indent = indent;
                in_array = false;
            } else {
                // This is a key-value pair
                value = unquote(value);
                auto full_path = build_path(path_stack);
                if (!full_path.empty()) full_path += ".";
                full_path += key;
                result.flat_values[full_path] = value;
            }
        }

        return result;
    }

private:
    [[nodiscard]] static std::string build_path(
        const std::vector<std::string>& stack) {
        std::string path;
        for (const auto& part : stack) {
            if (!path.empty()) path += ".";
            path += part;
        }
        return path;
    }
};

/**
 * @brief Apply parsed values to configuration
 */
[[nodiscard]] config_result apply_yaml_values(
    const std::map<std::string, std::string>& values,
    std::string_view source) {
    bridge_config config;

    // Apply each value based on key path
    for (const auto& [key, value] : values) {
        // Expand environment variables
        auto expanded = config_loader::expand_env_vars(value);
        if (!expanded) {
            return std::unexpected(expanded.error());
        }
        const std::string& val = *expanded;

        // Server settings
        if (key == "server.name" || key == "name") {
            config.name = val;
        }
        // HL7 listener settings
        else if (key == "hl7.listener.port") {
            if (auto v = parse_int(val)) {
                config.hl7.listener.port = static_cast<uint16_t>(*v);
            }
        } else if (key == "hl7.listener.bind_address") {
            config.hl7.listener.bind_address = val;
        } else if (key == "hl7.listener.max_connections") {
            if (auto v = parse_int(val)) {
                config.hl7.listener.max_connections = static_cast<size_t>(*v);
            }
        } else if (key == "hl7.listener.idle_timeout") {
            if (auto v = parse_duration(val)) {
                config.hl7.listener.idle_timeout = *v;
            }
        } else if (key == "hl7.listener.max_message_size") {
            if (auto v = parse_int(val)) {
                config.hl7.listener.max_message_size = static_cast<size_t>(*v);
            }
        } else if (key == "hl7.listener.tls.enabled") {
            if (auto v = parse_bool(val)) {
                config.hl7.listener.tls.enabled = *v;
            }
        } else if (key == "hl7.listener.tls.cert_path") {
            config.hl7.listener.tls.cert_path = val;
        } else if (key == "hl7.listener.tls.key_path") {
            config.hl7.listener.tls.key_path = val;
        } else if (key == "hl7.listener.tls.ca_path") {
            config.hl7.listener.tls.ca_path = val;
        }
        // FHIR settings
        else if (key == "fhir.enabled") {
            if (auto v = parse_bool(val)) {
                config.fhir.enabled = *v;
            }
        } else if (key == "fhir.server.port" || key == "fhir.port") {
            if (auto v = parse_int(val)) {
                config.fhir.server.port = static_cast<uint16_t>(*v);
            }
        } else if (key == "fhir.server.base_path" || key == "fhir.base_path" ||
                   key == "fhir.base_url") {
            config.fhir.server.base_path = val;
        } else if (key == "fhir.server.max_connections") {
            if (auto v = parse_int(val)) {
                config.fhir.server.max_connections = static_cast<size_t>(*v);
            }
        } else if (key == "fhir.server.page_size") {
            if (auto v = parse_int(val)) {
                config.fhir.server.page_size = static_cast<size_t>(*v);
            }
        }
        // pacs_system settings
        else if (key == "pacs.host" || key == "pacs_system.host") {
            config.pacs.host = val;
        } else if (key == "pacs.port" || key == "pacs_system.port") {
            if (auto v = parse_int(val)) {
                config.pacs.port = static_cast<uint16_t>(*v);
            }
        } else if (key == "pacs.ae_title" || key == "pacs_system.ae_title") {
            config.pacs.ae_title = val;
        } else if (key == "pacs.called_ae" || key == "pacs_system.called_ae") {
            config.pacs.called_ae = val;
        } else if (key == "pacs.timeout" || key == "pacs_system.timeout") {
            if (auto v = parse_duration(val)) {
                config.pacs.timeout = *v;
            }
        }
        // Queue settings
        else if (key == "queue.database_path") {
            config.queue.database_path = val;
        } else if (key == "queue.max_queue_size") {
            if (auto v = parse_int(val)) {
                config.queue.max_queue_size = static_cast<size_t>(*v);
            }
        } else if (key == "queue.max_retry_count") {
            if (auto v = parse_int(val)) {
                config.queue.max_retry_count = static_cast<size_t>(*v);
            }
        } else if (key == "queue.initial_retry_delay") {
            if (auto v = parse_duration(val)) {
                config.queue.initial_retry_delay = *v;
            }
        } else if (key == "queue.retry_backoff_multiplier") {
            if (auto v = parse_double(val)) {
                config.queue.retry_backoff_multiplier = *v;
            }
        } else if (key == "queue.worker_count") {
            if (auto v = parse_int(val)) {
                config.queue.worker_count = static_cast<size_t>(*v);
            }
        }
        // Patient cache settings
        else if (key == "patient_cache.max_entries") {
            if (auto v = parse_int(val)) {
                config.patient_cache.max_entries = static_cast<size_t>(*v);
            }
        } else if (key == "patient_cache.ttl") {
            if (auto v = parse_duration(val)) {
                config.patient_cache.ttl = *v;
            }
        } else if (key == "patient_cache.evict_on_full") {
            if (auto v = parse_bool(val)) {
                config.patient_cache.evict_on_full = *v;
            }
        }
        // Logging settings
        else if (key == "logging.level") {
            if (auto v = parse_log_level(val)) {
                config.logging.level = *v;
            }
        } else if (key == "logging.format") {
            config.logging.format = val;
        } else if (key == "logging.file") {
            config.logging.file = val;
        } else if (key == "logging.max_file_size_mb") {
            if (auto v = parse_int(val)) {
                config.logging.max_file_size_mb = static_cast<size_t>(*v);
            }
        } else if (key == "logging.max_files") {
            if (auto v = parse_int(val)) {
                config.logging.max_files = static_cast<size_t>(*v);
            }
        } else if (key == "logging.include_source_location") {
            if (auto v = parse_bool(val)) {
                config.logging.include_source_location = *v;
            }
        }
    }

    // Validate configuration
    auto errors = config.validate();
    if (!errors.empty()) {
        return std::unexpected(config_load_error{
            .code = config_error::validation_error,
            .message = "Configuration validation failed",
            .file_path = std::nullopt,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = std::move(errors)});
    }

    return config;
}

// =============================================================================
// Simple JSON Parser (subset)
// =============================================================================

/**
 * @brief Simple JSON parser for configuration
 *
 * This is a simplified JSON parser. For production use, consider nlohmann/json.
 */
class simple_json_parser {
public:
    using json_map = std::map<std::string, std::string>;

    struct parse_result {
        json_map flat_values;
        size_t error_pos = 0;
        std::string error_message;
        bool success = true;
    };

    [[nodiscard]] static parse_result parse(std::string_view content) {
        parse_result result;
        size_t pos = 0;

        skip_whitespace(content, pos);
        if (pos >= content.length() || content[pos] != '{') {
            result.success = false;
            result.error_pos = pos;
            result.error_message = "Expected '{'";
            return result;
        }

        try {
            parse_object(content, pos, "", result.flat_values);
        } catch (const std::exception& e) {
            result.success = false;
            result.error_pos = pos;
            result.error_message = e.what();
        }

        return result;
    }

private:
    static void skip_whitespace(std::string_view content, size_t& pos) {
        while (pos < content.length() &&
               std::isspace(static_cast<unsigned char>(content[pos]))) {
            ++pos;
        }
    }

    static std::string parse_string(std::string_view content, size_t& pos) {
        if (content[pos] != '"') {
            throw std::runtime_error("Expected '\"'");
        }
        ++pos;

        std::string result;
        while (pos < content.length() && content[pos] != '"') {
            if (content[pos] == '\\' && pos + 1 < content.length()) {
                ++pos;
                switch (content[pos]) {
                    case 'n':
                        result += '\n';
                        break;
                    case 't':
                        result += '\t';
                        break;
                    case 'r':
                        result += '\r';
                        break;
                    case '"':
                        result += '"';
                        break;
                    case '\\':
                        result += '\\';
                        break;
                    default:
                        result += content[pos];
                }
            } else {
                result += content[pos];
            }
            ++pos;
        }

        if (pos >= content.length()) {
            throw std::runtime_error("Unterminated string");
        }
        ++pos;  // Skip closing quote

        return result;
    }

    static std::string parse_value(std::string_view content, size_t& pos) {
        skip_whitespace(content, pos);

        if (content[pos] == '"') {
            return parse_string(content, pos);
        }

        // Parse number, boolean, or null
        std::string value;
        while (pos < content.length() && content[pos] != ',' &&
               content[pos] != '}' && content[pos] != ']' &&
               !std::isspace(static_cast<unsigned char>(content[pos]))) {
            value += content[pos++];
        }
        return value;
    }

    static void parse_object(std::string_view content, size_t& pos,
                             const std::string& prefix, json_map& result) {
        if (content[pos] != '{') {
            throw std::runtime_error("Expected '{'");
        }
        ++pos;

        skip_whitespace(content, pos);

        if (content[pos] == '}') {
            ++pos;
            return;
        }

        while (true) {
            skip_whitespace(content, pos);

            // Parse key
            auto key = parse_string(content, pos);
            auto full_key = prefix.empty() ? key : prefix + "." + key;

            skip_whitespace(content, pos);
            if (content[pos] != ':') {
                throw std::runtime_error("Expected ':'");
            }
            ++pos;
            skip_whitespace(content, pos);

            // Parse value
            if (content[pos] == '{') {
                parse_object(content, pos, full_key, result);
            } else if (content[pos] == '[') {
                parse_array(content, pos, full_key, result);
            } else {
                auto value = parse_value(content, pos);
                result[full_key] = value;
            }

            skip_whitespace(content, pos);

            if (content[pos] == '}') {
                ++pos;
                break;
            }
            if (content[pos] != ',') {
                throw std::runtime_error("Expected ',' or '}'");
            }
            ++pos;
        }
    }

    static void parse_array(std::string_view content, size_t& pos,
                            const std::string& prefix, json_map& result) {
        if (content[pos] != '[') {
            throw std::runtime_error("Expected '['");
        }
        ++pos;

        skip_whitespace(content, pos);

        if (content[pos] == ']') {
            ++pos;
            return;
        }

        size_t index = 0;
        while (true) {
            skip_whitespace(content, pos);

            auto item_prefix = std::format("{}.{}", prefix, index);

            if (content[pos] == '{') {
                parse_object(content, pos, item_prefix, result);
            } else if (content[pos] == '[') {
                parse_array(content, pos, item_prefix, result);
            } else {
                auto value = parse_value(content, pos);
                result[item_prefix] = value;
            }

            skip_whitespace(content, pos);
            ++index;

            if (content[pos] == ']') {
                ++pos;
                break;
            }
            if (content[pos] != ',') {
                throw std::runtime_error("Expected ',' or ']'");
            }
            ++pos;
        }
    }
};

}  // namespace

// =============================================================================
// config_load_error Implementation
// =============================================================================

std::string config_load_error::to_string() const {
    std::string result = message;

    if (file_path) {
        result += " (file: " + file_path->string() + ")";
    }
    if (line_number) {
        result += " at line " + std::to_string(*line_number);
        if (column_number) {
            result += ", column " + std::to_string(*column_number);
        }
    }

    if (!validation_errors.empty()) {
        result += "\nValidation errors:";
        for (const auto& err : validation_errors) {
            result += "\n  - " + err.field_path + ": " + err.message;
            if (err.actual_value) {
                result += " (got: " + *err.actual_value + ")";
            }
            if (err.expected) {
                result += " (expected: " + *err.expected + ")";
            }
        }
    }

    return result;
}

// =============================================================================
// config_loader Implementation
// =============================================================================

config_result config_loader::load(const std::filesystem::path& path) {
    auto format = detect_format(path);
    if (!format) {
        return std::unexpected(format.error());
    }

    if (*format == "yaml") {
        return load_yaml(path);
    } else {
        return load_json(path);
    }
}

config_result config_loader::load_yaml(const std::filesystem::path& path) {
    auto content = read_file(path);
    if (!content) {
        return std::unexpected(content.error());
    }

    auto result = load_yaml_string(*content, path.string());
    if (!result) {
        auto error = result.error();
        error.file_path = path;
        return std::unexpected(error);
    }

    return result;
}

config_result config_loader::load_json(const std::filesystem::path& path) {
    auto content = read_file(path);
    if (!content) {
        return std::unexpected(content.error());
    }

    auto result = load_json_string(*content, path.string());
    if (!result) {
        auto error = result.error();
        error.file_path = path;
        return std::unexpected(error);
    }

    return result;
}

config_result config_loader::load_yaml_string(std::string_view yaml_content,
                                              std::string_view source_name) {
    if (yaml_content.empty() || trim(yaml_content).empty()) {
        return std::unexpected(config_load_error{
            .code = config_error::empty_config,
            .message = "Configuration content is empty",
            .file_path = std::nullopt,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    auto parse_result = simple_yaml_parser::parse(yaml_content);
    if (!parse_result.success) {
        return std::unexpected(config_load_error{
            .code = config_error::parse_error,
            .message = parse_result.error_message,
            .file_path = std::nullopt,
            .line_number = parse_result.error_line,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    return apply_yaml_values(parse_result.flat_values, source_name);
}

config_result config_loader::load_json_string(std::string_view json_content,
                                              std::string_view source_name) {
    if (json_content.empty() || trim(json_content).empty()) {
        return std::unexpected(config_load_error{
            .code = config_error::empty_config,
            .message = "Configuration content is empty",
            .file_path = std::nullopt,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    auto parse_result = simple_json_parser::parse(json_content);
    if (!parse_result.success) {
        return std::unexpected(config_load_error{
            .code = config_error::parse_error,
            .message = parse_result.error_message,
            .file_path = std::nullopt,
            .line_number = std::nullopt,
            .column_number = std::nullopt,
            .validation_errors = {}});
    }

    return apply_yaml_values(parse_result.flat_values, source_name);
}

std::vector<validation_error_info> config_loader::validate(
    const bridge_config& config) {
    return config.validate();
}

std::expected<std::vector<validation_error_info>, config_load_error>
config_loader::validate_file(const std::filesystem::path& path) {
    auto config = load(path);
    if (!config) {
        return std::unexpected(config.error());
    }
    return config->validate();
}

std::expected<void, config_load_error> config_loader::save_yaml(
    const bridge_config& config, const std::filesystem::path& path) {
    auto yaml = to_yaml(config);
    return write_file(path, yaml);
}

std::expected<void, config_load_error> config_loader::save_json(
    const bridge_config& config, const std::filesystem::path& path) {
    auto json = to_json(config, true);
    return write_file(path, json);
}

std::string config_loader::to_yaml(const bridge_config& config) {
    std::ostringstream ss;

    // Server
    ss << "# PACS Bridge Configuration\n";
    ss << "# Generated by config_loader\n\n";

    ss << "server:\n";
    ss << "  name: \"" << config.name << "\"\n\n";

    // HL7
    ss << "hl7:\n";
    ss << "  listener:\n";
    ss << "    port: " << config.hl7.listener.port << "\n";
    if (!config.hl7.listener.bind_address.empty()) {
        ss << "    bind_address: \"" << config.hl7.listener.bind_address
           << "\"\n";
    }
    ss << "    max_connections: " << config.hl7.listener.max_connections
       << "\n";
    ss << "    idle_timeout: " << config.hl7.listener.idle_timeout.count()
       << "s\n";
    ss << "    max_message_size: " << config.hl7.listener.max_message_size
       << "\n";

    if (config.hl7.listener.tls.enabled) {
        ss << "    tls:\n";
        ss << "      enabled: true\n";
        ss << "      cert_path: \"" << config.hl7.listener.tls.cert_path
           << "\"\n";
        ss << "      key_path: \"" << config.hl7.listener.tls.key_path
           << "\"\n";
        if (!config.hl7.listener.tls.ca_path.empty()) {
            ss << "      ca_path: \"" << config.hl7.listener.tls.ca_path
               << "\"\n";
        }
    }

    if (!config.hl7.outbound_destinations.empty()) {
        ss << "  outbound:\n";
        for (const auto& dest : config.hl7.outbound_destinations) {
            ss << "    - name: \"" << dest.name << "\"\n";
            ss << "      host: \"" << dest.host << "\"\n";
            ss << "      port: " << dest.port << "\n";
            ss << "      priority: " << dest.priority << "\n";
            ss << "      enabled: " << (dest.enabled ? "true" : "false")
               << "\n";
            if (!dest.message_types.empty()) {
                ss << "      message_types:\n";
                for (const auto& mt : dest.message_types) {
                    ss << "        - \"" << mt << "\"\n";
                }
            }
        }
    }
    ss << "\n";

    // FHIR
    ss << "fhir:\n";
    ss << "  enabled: " << (config.fhir.enabled ? "true" : "false") << "\n";
    if (config.fhir.enabled) {
        ss << "  server:\n";
        ss << "    port: " << config.fhir.server.port << "\n";
        ss << "    base_path: \"" << config.fhir.server.base_path << "\"\n";
        ss << "    max_connections: " << config.fhir.server.max_connections
           << "\n";
        ss << "    page_size: " << config.fhir.server.page_size << "\n";
    }
    ss << "\n";

    // pacs_system
    ss << "pacs:\n";
    ss << "  host: \"" << config.pacs.host << "\"\n";
    ss << "  port: " << config.pacs.port << "\n";
    ss << "  ae_title: \"" << config.pacs.ae_title << "\"\n";
    ss << "  called_ae: \"" << config.pacs.called_ae << "\"\n";
    ss << "  timeout: " << config.pacs.timeout.count() << "s\n\n";

    // Queue
    ss << "queue:\n";
    ss << "  database_path: \"" << config.queue.database_path.string()
       << "\"\n";
    ss << "  max_queue_size: " << config.queue.max_queue_size << "\n";
    ss << "  max_retry_count: " << config.queue.max_retry_count << "\n";
    ss << "  initial_retry_delay: " << config.queue.initial_retry_delay.count()
       << "s\n";
    ss << "  retry_backoff_multiplier: " << config.queue.retry_backoff_multiplier
       << "\n";
    ss << "  worker_count: " << config.queue.worker_count << "\n\n";

    // Patient cache
    ss << "patient_cache:\n";
    ss << "  max_entries: " << config.patient_cache.max_entries << "\n";
    ss << "  ttl: " << config.patient_cache.ttl.count() << "s\n";
    ss << "  evict_on_full: "
       << (config.patient_cache.evict_on_full ? "true" : "false") << "\n\n";

    // Logging
    ss << "logging:\n";
    ss << "  level: \"" << pacs::bridge::config::to_string(config.logging.level)
       << "\"\n";
    ss << "  format: \"" << config.logging.format << "\"\n";
    if (!config.logging.file.empty()) {
        ss << "  file: \"" << config.logging.file.string() << "\"\n";
    }
    ss << "  max_file_size_mb: " << config.logging.max_file_size_mb << "\n";
    ss << "  max_files: " << config.logging.max_files << "\n";
    ss << "  include_source_location: "
       << (config.logging.include_source_location ? "true" : "false") << "\n";

    // Routing rules
    if (!config.routing_rules.empty()) {
        ss << "\nrouting_rules:\n";
        for (const auto& rule : config.routing_rules) {
            ss << "  - name: \"" << rule.name << "\"\n";
            ss << "    message_type_pattern: \"" << rule.message_type_pattern
               << "\"\n";
            if (!rule.source_pattern.empty()) {
                ss << "    source_pattern: \"" << rule.source_pattern << "\"\n";
            }
            ss << "    destination: \"" << rule.destination << "\"\n";
            ss << "    priority: " << rule.priority << "\n";
            ss << "    enabled: " << (rule.enabled ? "true" : "false") << "\n";
        }
    }

    return ss.str();
}

std::string config_loader::to_json(const bridge_config& config, bool pretty) {
    std::ostringstream ss;
    std::string indent = pretty ? "  " : "";
    std::string newline = pretty ? "\n" : "";

    ss << "{" << newline;
    ss << indent << "\"server\": {" << newline;
    ss << indent << indent << "\"name\": \"" << config.name << "\"" << newline;
    ss << indent << "}," << newline;

    ss << indent << "\"hl7\": {" << newline;
    ss << indent << indent << "\"listener\": {" << newline;
    ss << indent << indent << indent << "\"port\": " << config.hl7.listener.port
       << "," << newline;
    ss << indent << indent << indent
       << "\"max_connections\": " << config.hl7.listener.max_connections
       << newline;
    ss << indent << indent << "}" << newline;
    ss << indent << "}," << newline;

    ss << indent << "\"pacs\": {" << newline;
    ss << indent << indent << "\"host\": \"" << config.pacs.host << "\","
       << newline;
    ss << indent << indent << "\"port\": " << config.pacs.port << ","
       << newline;
    ss << indent << indent << "\"ae_title\": \"" << config.pacs.ae_title
       << "\"," << newline;
    ss << indent << indent << "\"called_ae\": \"" << config.pacs.called_ae
       << "\"" << newline;
    ss << indent << "}," << newline;

    ss << indent << "\"logging\": {" << newline;
    ss << indent << indent << "\"level\": \""
       << pacs::bridge::config::to_string(config.logging.level) << "\","
       << newline;
    ss << indent << indent << "\"format\": \"" << config.logging.format << "\""
       << newline;
    ss << indent << "}" << newline;

    ss << "}";

    return ss.str();
}

std::expected<std::string, config_load_error> config_loader::expand_env_vars(
    std::string_view value) {
    std::string result;
    result.reserve(value.size());

    size_t pos = 0;
    while (pos < value.size()) {
        if (value[pos] == '$' && pos + 1 < value.size() &&
            value[pos + 1] == '{') {
            // Find closing brace
            size_t end = value.find('}', pos + 2);
            if (end == std::string_view::npos) {
                return std::unexpected(config_load_error{
                    .code = config_error::parse_error,
                    .message = "Unclosed environment variable reference",
                    .file_path = std::nullopt,
                    .line_number = std::nullopt,
                    .column_number = std::nullopt,
                    .validation_errors = {}});
            }

            // Extract variable name and default
            std::string_view ref = value.substr(pos + 2, end - pos - 2);
            std::string var_name;
            std::string default_value;
            bool has_default = false;

            if (auto colon_pos = ref.find(":-"); colon_pos != std::string_view::npos) {
                var_name = std::string(ref.substr(0, colon_pos));
                default_value = std::string(ref.substr(colon_pos + 2));
                has_default = true;
            } else {
                var_name = std::string(ref);
            }

            // Get environment variable
            const char* env_val = std::getenv(var_name.c_str());
            if (env_val != nullptr) {
                result += env_val;
            } else if (has_default) {
                result += default_value;
            } else {
                return std::unexpected(config_load_error{
                    .code = config_error::env_var_not_found,
                    .message =
                        std::format("Environment variable '{}' not found",
                                    var_name),
                    .file_path = std::nullopt,
                    .line_number = std::nullopt,
                    .column_number = std::nullopt,
                    .validation_errors = {}});
            }

            pos = end + 1;
        } else {
            result += value[pos++];
        }
    }

    return result;
}

bool config_loader::needs_env_expansion(std::string_view value) {
    return value.find("${") != std::string_view::npos;
}

bridge_config config_loader::get_default_config() {
    return bridge_config{};
}

bridge_config config_loader::merge(const bridge_config& base,
                                   const bridge_config& overlay) {
    bridge_config result = base;

    // Merge server name
    if (!overlay.name.empty() && overlay.name != "PACS_BRIDGE") {
        result.name = overlay.name;
    }

    // Merge HL7 config
    if (overlay.hl7.listener.port != mllp::MLLP_DEFAULT_PORT) {
        result.hl7.listener.port = overlay.hl7.listener.port;
    }
    if (!overlay.hl7.listener.bind_address.empty()) {
        result.hl7.listener.bind_address = overlay.hl7.listener.bind_address;
    }
    if (overlay.hl7.listener.max_connections != 50) {
        result.hl7.listener.max_connections =
            overlay.hl7.listener.max_connections;
    }

    // Merge outbound destinations
    if (!overlay.hl7.outbound_destinations.empty()) {
        result.hl7.outbound_destinations = overlay.hl7.outbound_destinations;
    }

    // Merge FHIR config
    if (overlay.fhir.enabled) {
        result.fhir = overlay.fhir;
    }

    // Merge pacs_system config
    if (overlay.pacs.host != "localhost") {
        result.pacs.host = overlay.pacs.host;
    }
    if (overlay.pacs.port != 11112) {
        result.pacs.port = overlay.pacs.port;
    }
    if (overlay.pacs.ae_title != "PACS_BRIDGE") {
        result.pacs.ae_title = overlay.pacs.ae_title;
    }
    if (overlay.pacs.called_ae != "PACS_SCP") {
        result.pacs.called_ae = overlay.pacs.called_ae;
    }

    // Merge routing rules
    if (!overlay.routing_rules.empty()) {
        result.routing_rules = overlay.routing_rules;
    }

    // Merge queue config
    if (overlay.queue.database_path != "queue.db") {
        result.queue.database_path = overlay.queue.database_path;
    }

    // Merge logging config
    if (overlay.logging.level != log_level::info) {
        result.logging.level = overlay.logging.level;
    }
    if (overlay.logging.format != "json") {
        result.logging.format = overlay.logging.format;
    }
    if (!overlay.logging.file.empty()) {
        result.logging.file = overlay.logging.file;
    }

    return result;
}

}  // namespace pacs::bridge::config
