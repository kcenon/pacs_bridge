#ifndef PACS_BRIDGE_CONFIG_CONFIG_LOADER_H
#define PACS_BRIDGE_CONFIG_CONFIG_LOADER_H

/**
 * @file config_loader.h
 * @brief Configuration file loader with YAML and JSON support
 *
 * Provides loading and validation of PACS Bridge configuration files
 * with support for both YAML and JSON formats. Features include:
 *   - Automatic format detection by file extension
 *   - Environment variable substitution (${VAR} syntax)
 *   - Comprehensive validation with detailed error messages
 *   - Default value population for optional fields
 *
 * Supported environment variable syntax:
 *   - ${VAR} - Required variable (error if not set)
 *   - ${VAR:-default} - Optional with default value
 *
 * @example Loading Configuration
 * ```cpp
 * auto result = config_loader::load("/etc/pacs_bridge/config.yaml");
 * if (result) {
 *     auto config = result.value();
 *     // Use configuration...
 * } else {
 *     std::cerr << "Error: " << result.error().message << std::endl;
 * }
 * ```
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/20
 * @see docs/PRD.md - FR-5.1.1 to FR-5.1.4
 */

#include "bridge_config.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace pacs::bridge::config {

// =============================================================================
// Load Result Types
// =============================================================================

/**
 * @brief Detailed error information from configuration loading
 */
struct config_load_error {
    /** Error code */
    config_error code;

    /** Human-readable error message */
    std::string message;

    /** File path where error occurred (if applicable) */
    std::optional<std::filesystem::path> file_path;

    /** Line number where error occurred (if applicable) */
    std::optional<size_t> line_number;

    /** Column number where error occurred (if applicable) */
    std::optional<size_t> column_number;

    /** Validation errors (if validation failed) */
    std::vector<validation_error_info> validation_errors;

    /**
     * @brief Get formatted error message with location
     */
    [[nodiscard]] std::string to_string() const;
};

/**
 * @brief Result type for configuration loading operations
 */
using config_result = std::expected<bridge_config, config_load_error>;

// =============================================================================
// Configuration Loader
// =============================================================================

/**
 * @brief Configuration file loader
 *
 * Static class providing configuration loading and validation functions.
 *
 * @example Load from YAML file
 * ```cpp
 * auto result = config_loader::load_yaml("/etc/pacs/config.yaml");
 * if (!result) {
 *     std::cerr << result.error().to_string() << std::endl;
 *     return 1;
 * }
 * auto config = std::move(result.value());
 * ```
 *
 * @example Load with automatic format detection
 * ```cpp
 * auto result = config_loader::load("config.yaml");  // Detects YAML
 * auto result2 = config_loader::load("config.json"); // Detects JSON
 * ```
 *
 * @example Load from string
 * ```cpp
 * std::string yaml = R"(
 * server:
 *   name: "TEST_BRIDGE"
 * hl7:
 *   listener:
 *     port: 2575
 * )";
 * auto result = config_loader::load_yaml_string(yaml);
 * ```
 */
class config_loader {
public:
    // =========================================================================
    // File Loading
    // =========================================================================

    /**
     * @brief Load configuration from file (auto-detect format)
     *
     * Automatically detects file format based on extension:
     *   - .yaml, .yml -> YAML format
     *   - .json -> JSON format
     *
     * @param path Path to configuration file
     * @return Configuration or error
     */
    [[nodiscard]] static config_result load(const std::filesystem::path& path);

    /**
     * @brief Load configuration from YAML file
     *
     * @param path Path to YAML configuration file
     * @return Configuration or error
     */
    [[nodiscard]] static config_result load_yaml(
        const std::filesystem::path& path);

    /**
     * @brief Load configuration from JSON file
     *
     * @param path Path to JSON configuration file
     * @return Configuration or error
     */
    [[nodiscard]] static config_result load_json(
        const std::filesystem::path& path);

    // =========================================================================
    // String Loading
    // =========================================================================

    /**
     * @brief Load configuration from YAML string
     *
     * @param yaml_content YAML configuration string
     * @param source_name Optional source name for error messages
     * @return Configuration or error
     */
    [[nodiscard]] static config_result load_yaml_string(
        std::string_view yaml_content,
        std::string_view source_name = "<string>");

    /**
     * @brief Load configuration from JSON string
     *
     * @param json_content JSON configuration string
     * @param source_name Optional source name for error messages
     * @return Configuration or error
     */
    [[nodiscard]] static config_result load_json_string(
        std::string_view json_content,
        std::string_view source_name = "<string>");

    // =========================================================================
    // Validation
    // =========================================================================

    /**
     * @brief Validate a configuration
     *
     * @param config Configuration to validate
     * @return Empty vector if valid, otherwise list of errors
     */
    [[nodiscard]] static std::vector<validation_error_info> validate(
        const bridge_config& config);

    /**
     * @brief Validate configuration file without loading
     *
     * @param path Path to configuration file
     * @return Empty vector if valid, otherwise list of errors
     */
    [[nodiscard]] static std::expected<std::vector<validation_error_info>,
                                       config_load_error>
    validate_file(const std::filesystem::path& path);

    // =========================================================================
    // Saving
    // =========================================================================

    /**
     * @brief Save configuration to YAML file
     *
     * @param config Configuration to save
     * @param path Output file path
     * @return Success or error
     */
    [[nodiscard]] static std::expected<void, config_load_error> save_yaml(
        const bridge_config& config, const std::filesystem::path& path);

    /**
     * @brief Save configuration to JSON file
     *
     * @param config Configuration to save
     * @param path Output file path
     * @return Success or error
     */
    [[nodiscard]] static std::expected<void, config_load_error> save_json(
        const bridge_config& config, const std::filesystem::path& path);

    // =========================================================================
    // Serialization
    // =========================================================================

    /**
     * @brief Serialize configuration to YAML string
     *
     * @param config Configuration to serialize
     * @return YAML string representation
     */
    [[nodiscard]] static std::string to_yaml(const bridge_config& config);

    /**
     * @brief Serialize configuration to JSON string
     *
     * @param config Configuration to serialize
     * @param pretty If true, format with indentation
     * @return JSON string representation
     */
    [[nodiscard]] static std::string to_json(const bridge_config& config,
                                             bool pretty = true);

    // =========================================================================
    // Environment Variable Processing
    // =========================================================================

    /**
     * @brief Expand environment variables in a string
     *
     * Supports the following syntax:
     *   - ${VAR} - Required variable (error if not set)
     *   - ${VAR:-default} - Optional with default value
     *
     * @param value String containing environment variable references
     * @return Expanded string or error if required variable is missing
     */
    [[nodiscard]] static std::expected<std::string, config_load_error>
    expand_env_vars(std::string_view value);

    /**
     * @brief Check if environment variable expansion is needed
     *
     * @param value String to check
     * @return true if string contains ${...} patterns
     */
    [[nodiscard]] static bool needs_env_expansion(std::string_view value);

    // =========================================================================
    // Utility
    // =========================================================================

    /**
     * @brief Get default configuration
     *
     * Returns a bridge_config with all default values populated.
     *
     * @return Default configuration
     */
    [[nodiscard]] static bridge_config get_default_config();

    /**
     * @brief Merge two configurations
     *
     * Values from `overlay` override values from `base`.
     * Useful for applying environment-specific overrides.
     *
     * @param base Base configuration
     * @param overlay Configuration values to overlay
     * @return Merged configuration
     */
    [[nodiscard]] static bridge_config merge(const bridge_config& base,
                                             const bridge_config& overlay);

private:
    // Prevent instantiation
    config_loader() = delete;
    ~config_loader() = delete;
    config_loader(const config_loader&) = delete;
    config_loader& operator=(const config_loader&) = delete;
};

}  // namespace pacs::bridge::config

#endif  // PACS_BRIDGE_CONFIG_CONFIG_LOADER_H
