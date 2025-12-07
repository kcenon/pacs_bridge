#ifndef PACS_BRIDGE_TESTING_LOAD_REPORTER_H
#define PACS_BRIDGE_TESTING_LOAD_REPORTER_H

/**
 * @file load_reporter.h
 * @brief Test result report generator
 *
 * Generates comprehensive test reports in multiple formats including
 * JSON, Markdown, and text. Supports metric visualization and
 * comparison with baseline results.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/45
 */

#include "load_types.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace pacs::bridge::testing {

/**
 * @brief Report output format
 */
enum class report_format {
    /** Plain text format */
    text,

    /** JSON format for programmatic processing */
    json,

    /** Markdown format for documentation */
    markdown,

    /** CSV format for spreadsheet import */
    csv,

    /** HTML format with charts (if supported) */
    html
};

/**
 * @brief Convert report_format to string
 */
[[nodiscard]] constexpr const char* to_string(report_format format) noexcept {
    switch (format) {
        case report_format::text:
            return "text";
        case report_format::json:
            return "json";
        case report_format::markdown:
            return "markdown";
        case report_format::csv:
            return "csv";
        case report_format::html:
            return "html";
        default:
            return "unknown";
    }
}

/**
 * @brief Get file extension for report format
 */
[[nodiscard]] constexpr const char* extension_for(report_format format) noexcept {
    switch (format) {
        case report_format::text:
            return ".txt";
        case report_format::json:
            return ".json";
        case report_format::markdown:
            return ".md";
        case report_format::csv:
            return ".csv";
        case report_format::html:
            return ".html";
        default:
            return ".txt";
    }
}

/**
 * @brief Report configuration
 */
struct report_config {
    /** Output format */
    report_format format = report_format::markdown;

    /** Include detailed timing breakdown */
    bool include_timing_details = true;

    /** Include system resource usage */
    bool include_resource_usage = true;

    /** Include raw metric data */
    bool include_raw_metrics = false;

    /** Include charts/graphs (HTML only) */
    bool include_charts = true;

    /** Include comparison with previous results */
    bool include_comparison = false;

    /** Baseline result path for comparison */
    std::filesystem::path baseline_path;

    /** Report title */
    std::string title = "PACS Bridge Load Test Report";

    /** Additional notes to include */
    std::string notes;
};

/**
 * @brief Load test report generator
 *
 * Generates comprehensive reports from test results in various formats.
 * Supports single results and multi-test suite summaries.
 *
 * @code
 * load_reporter reporter;
 *
 * // Generate single test report
 * auto report = reporter.generate(result, report_format::markdown);
 *
 * // Save to file
 * reporter.save(result, "./reports/load_test.md", report_format::markdown);
 *
 * // Generate suite summary
 * std::vector<test_result> results = run_test_suite(runner, configs);
 * auto summary = reporter.generate_suite_summary(results);
 * @endcode
 */
class load_reporter {
public:
    /**
     * @brief Default constructor
     */
    load_reporter();

    /**
     * @brief Construct with configuration
     * @param config Reporter configuration
     */
    explicit load_reporter(const report_config& config);

    /**
     * @brief Destructor
     */
    ~load_reporter();

    // Non-copyable
    load_reporter(const load_reporter&) = delete;
    load_reporter& operator=(const load_reporter&) = delete;

    // Movable
    load_reporter(load_reporter&&) noexcept;
    load_reporter& operator=(load_reporter&&) noexcept;

    /**
     * @brief Generate report string
     * @param result Test result to report
     * @param format Output format
     * @return Generated report string or error
     */
    [[nodiscard]] std::expected<std::string, load_error>
    generate(const test_result& result, report_format format) const;

    /**
     * @brief Generate report with current configuration
     * @param result Test result to report
     * @return Generated report string or error
     */
    [[nodiscard]] std::expected<std::string, load_error>
    generate(const test_result& result) const;

    /**
     * @brief Save report to file
     * @param result Test result to report
     * @param path Output file path
     * @param format Output format (default: infer from extension)
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, load_error>
    save(const test_result& result,
         const std::filesystem::path& path,
         std::optional<report_format> format = std::nullopt) const;

    /**
     * @brief Generate suite summary report
     * @param results Vector of test results
     * @param format Output format
     * @return Generated report string or error
     */
    [[nodiscard]] std::expected<std::string, load_error>
    generate_suite_summary(
        std::span<const test_result> results,
        report_format format = report_format::markdown) const;

    /**
     * @brief Save suite summary to file
     * @param results Vector of test results
     * @param path Output file path
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, load_error>
    save_suite_summary(
        std::span<const test_result> results,
        const std::filesystem::path& path) const;

    /**
     * @brief Generate comparison report between two results
     * @param current Current test result
     * @param baseline Baseline test result
     * @param format Output format
     * @return Generated comparison report or error
     */
    [[nodiscard]] std::expected<std::string, load_error>
    generate_comparison(
        const test_result& current,
        const test_result& baseline,
        report_format format = report_format::markdown) const;

    /**
     * @brief Set report configuration
     * @param config New configuration
     */
    void set_config(const report_config& config);

    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    [[nodiscard]] const report_config& config() const noexcept;

    /**
     * @brief Generate JSON result for export
     * @param result Test result
     * @return JSON string representation
     */
    [[nodiscard]] std::expected<std::string, load_error>
    to_json(const test_result& result) const;

    /**
     * @brief Parse JSON to test result
     * @param json JSON string
     * @return Parsed test result or error
     */
    [[nodiscard]] static std::expected<test_result, load_error>
    from_json(std::string_view json);

    /**
     * @brief Generate metrics CSV for analysis
     * @param result Test result
     * @return CSV string with metrics
     */
    [[nodiscard]] std::expected<std::string, load_error>
    to_csv(const test_result& result) const;

    /**
     * @brief Print result summary to stdout
     * @param result Test result to print
     */
    static void print_summary(const test_result& result);

    /**
     * @brief Print real-time progress to stdout
     * @param info Progress information
     */
    static void print_progress(const progress_info& info);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Builder for report_config
 */
class report_config_builder {
public:
    report_config_builder() = default;

    report_config_builder& format(report_format f) {
        config_.format = f;
        return *this;
    }

    report_config_builder& include_timing_details(bool include = true) {
        config_.include_timing_details = include;
        return *this;
    }

    report_config_builder& include_resource_usage(bool include = true) {
        config_.include_resource_usage = include;
        return *this;
    }

    report_config_builder& include_raw_metrics(bool include = true) {
        config_.include_raw_metrics = include;
        return *this;
    }

    report_config_builder& include_charts(bool include = true) {
        config_.include_charts = include;
        return *this;
    }

    report_config_builder& compare_with(const std::filesystem::path& baseline) {
        config_.include_comparison = true;
        config_.baseline_path = baseline;
        return *this;
    }

    report_config_builder& title(std::string_view t) {
        config_.title = std::string(t);
        return *this;
    }

    report_config_builder& notes(std::string_view n) {
        config_.notes = std::string(n);
        return *this;
    }

    [[nodiscard]] report_config build() const {
        return config_;
    }

private:
    report_config config_;
};

}  // namespace pacs::bridge::testing

#endif  // PACS_BRIDGE_TESTING_LOAD_REPORTER_H
