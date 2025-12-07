/**
 * @file load_reporter.cpp
 * @brief Implementation of test result report generator
 */

#include "pacs/bridge/testing/load_reporter.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace pacs::bridge::testing {

// =============================================================================
// test_result::summary() implementation
// =============================================================================

std::string test_result::summary() const {
    std::ostringstream oss;

    oss << "=== Load Test Result Summary ===\n"
        << "Type: " << to_string(type) << "\n"
        << "State: " << to_string(state) << "\n"
        << "Duration: " << duration.count() << "s\n"
        << "Target: " << target_host << ":" << target_port << "\n\n";

    oss << "--- Messages ---\n"
        << "Sent: " << messages_sent << "\n"
        << "Acknowledged: " << messages_acked << "\n"
        << "Failed: " << messages_failed << "\n"
        << "Success Rate: " << std::fixed << std::setprecision(2)
        << success_rate_percent << "%\n\n";

    oss << "--- Throughput ---\n"
        << "Average: " << std::fixed << std::setprecision(1)
        << throughput << " msg/s\n"
        << "Peak: " << peak_throughput << " msg/s\n\n";

    oss << "--- Latency ---\n"
        << "P50: " << std::fixed << std::setprecision(2)
        << latency_p50_ms << " ms\n"
        << "P95: " << latency_p95_ms << " ms\n"
        << "P99: " << latency_p99_ms << " ms\n"
        << "Min: " << latency_min_ms << " ms\n"
        << "Max: " << latency_max_ms << " ms\n"
        << "Mean: " << latency_mean_ms << " ms\n\n";

    oss << "--- Errors ---\n"
        << "Connection: " << connection_errors << "\n"
        << "Timeout: " << timeout_errors << "\n"
        << "Protocol: " << protocol_errors << "\n";

    if (error_message) {
        oss << "\nError: " << *error_message << "\n";
    }

    return oss.str();
}

// =============================================================================
// Implementation Class
// =============================================================================

class load_reporter::impl {
public:
    impl() = default;

    explicit impl(const report_config& cfg) : config_(cfg) {}

    std::expected<std::string, load_error>
    generate(const test_result& result, report_format format) const {
        switch (format) {
            case report_format::text:
                return generate_text(result);
            case report_format::json:
                return to_json(result);
            case report_format::markdown:
                return generate_markdown(result);
            case report_format::csv:
                return to_csv(result);
            case report_format::html:
                return generate_html(result);
            default:
                return std::unexpected(load_error::report_failed);
        }
    }

    std::expected<std::string, load_error>
    generate(const test_result& result) const {
        return generate(result, config_.format);
    }

    std::expected<void, load_error>
    save(const test_result& result, const std::filesystem::path& path,
         std::optional<report_format> format) const {
        // Determine format from extension if not specified
        report_format fmt = format.value_or(infer_format(path));

        auto content = generate(result, fmt);
        if (!content) {
            return std::unexpected(content.error());
        }

        std::ofstream file(path);
        if (!file) {
            return std::unexpected(load_error::report_failed);
        }

        file << *content;
        return {};
    }

    std::expected<std::string, load_error>
    generate_suite_summary(std::span<const test_result> results,
                           report_format format) const {
        if (results.empty()) {
            return std::unexpected(load_error::invalid_configuration);
        }

        std::ostringstream oss;

        if (format == report_format::markdown) {
            oss << "# " << config_.title << " - Suite Summary\n\n";
            oss << "Generated: " << format_timestamp(
                std::chrono::system_clock::now()) << "\n\n";

            // Summary table
            oss << "## Test Results Overview\n\n";
            oss << "| Test Type | State | Duration | Success Rate | P95 Latency | Throughput |\n";
            oss << "|-----------|-------|----------|--------------|-------------|------------|\n";

            for (const auto& result : results) {
                oss << "| " << to_string(result.type)
                    << " | " << to_string(result.state)
                    << " | " << result.duration.count() << "s"
                    << " | " << std::fixed << std::setprecision(1)
                    << result.success_rate_percent << "%"
                    << " | " << std::setprecision(2) << result.latency_p95_ms << "ms"
                    << " | " << std::setprecision(0) << result.throughput << " msg/s |\n";
            }

            // Overall assessment
            oss << "\n## Overall Assessment\n\n";

            size_t passed = 0;
            size_t failed = 0;
            for (const auto& r : results) {
                if (r.state == test_state::completed && r.success_rate_percent >= 99.0) {
                    ++passed;
                } else {
                    ++failed;
                }
            }

            oss << "- **Tests Passed**: " << passed << "/" << results.size() << "\n";
            oss << "- **Tests Failed**: " << failed << "/" << results.size() << "\n";

            if (failed == 0) {
                oss << "\n✅ **All tests passed successfully**\n";
            } else {
                oss << "\n⚠️ **Some tests failed - review required**\n";
            }

        } else if (format == report_format::json) {
            oss << "{\n";
            oss << "  \"title\": \"" << config_.title << "\",\n";
            oss << "  \"generated\": \"" << format_timestamp(
                std::chrono::system_clock::now()) << "\",\n";
            oss << "  \"results\": [\n";

            for (size_t i = 0; i < results.size(); ++i) {
                auto json = to_json(results[i]);
                if (json) {
                    oss << "    " << *json;
                    if (i < results.size() - 1) oss << ",";
                    oss << "\n";
                }
            }

            oss << "  ]\n";
            oss << "}\n";
        } else {
            // Default to text
            oss << config_.title << " - Suite Summary\n";
            oss << std::string(50, '=') << "\n\n";

            for (const auto& result : results) {
                oss << result.summary() << "\n";
                oss << std::string(50, '-') << "\n\n";
            }
        }

        return oss.str();
    }

    std::expected<void, load_error>
    save_suite_summary(std::span<const test_result> results,
                       const std::filesystem::path& path) const {
        auto format = infer_format(path);
        auto content = generate_suite_summary(results, format);
        if (!content) {
            return std::unexpected(content.error());
        }

        std::ofstream file(path);
        if (!file) {
            return std::unexpected(load_error::report_failed);
        }

        file << *content;
        return {};
    }

    std::expected<std::string, load_error>
    generate_comparison(const test_result& current, const test_result& baseline,
                        report_format format) const {
        std::ostringstream oss;

        auto pct_change = [](double current, double baseline) -> std::string {
            if (baseline == 0) return "N/A";
            double change = ((current - baseline) / baseline) * 100.0;
            std::ostringstream s;
            s << std::fixed << std::setprecision(1);
            if (change > 0) s << "+";
            s << change << "%";
            return s.str();
        };

        if (format == report_format::markdown) {
            oss << "# Performance Comparison Report\n\n";
            oss << "## Throughput\n\n";
            oss << "| Metric | Current | Baseline | Change |\n";
            oss << "|--------|---------|----------|--------|\n";
            oss << "| Average | " << std::fixed << std::setprecision(1)
                << current.throughput << " msg/s | "
                << baseline.throughput << " msg/s | "
                << pct_change(current.throughput, baseline.throughput) << " |\n";
            oss << "| Peak | " << current.peak_throughput << " msg/s | "
                << baseline.peak_throughput << " msg/s | "
                << pct_change(current.peak_throughput, baseline.peak_throughput)
                << " |\n\n";

            oss << "## Latency\n\n";
            oss << "| Percentile | Current | Baseline | Change |\n";
            oss << "|------------|---------|----------|--------|\n";
            oss << "| P50 | " << std::setprecision(2)
                << current.latency_p50_ms << " ms | "
                << baseline.latency_p50_ms << " ms | "
                << pct_change(current.latency_p50_ms, baseline.latency_p50_ms)
                << " |\n";
            oss << "| P95 | " << current.latency_p95_ms << " ms | "
                << baseline.latency_p95_ms << " ms | "
                << pct_change(current.latency_p95_ms, baseline.latency_p95_ms)
                << " |\n";
            oss << "| P99 | " << current.latency_p99_ms << " ms | "
                << baseline.latency_p99_ms << " ms | "
                << pct_change(current.latency_p99_ms, baseline.latency_p99_ms)
                << " |\n\n";

            oss << "## Reliability\n\n";
            oss << "| Metric | Current | Baseline | Change |\n";
            oss << "|--------|---------|----------|--------|\n";
            oss << "| Success Rate | " << std::setprecision(2)
                << current.success_rate_percent << "% | "
                << baseline.success_rate_percent << "% | "
                << pct_change(current.success_rate_percent,
                              baseline.success_rate_percent) << " |\n";
        } else {
            oss << current.summary() << "\n";
            oss << "Compared to baseline:\n";
            oss << "  Throughput change: "
                << pct_change(current.throughput, baseline.throughput) << "\n";
            oss << "  P95 latency change: "
                << pct_change(current.latency_p95_ms, baseline.latency_p95_ms)
                << "\n";
        }

        return oss.str();
    }

    void set_config(const report_config& config) {
        config_ = config;
    }

    const report_config& config() const noexcept {
        return config_;
    }

    std::expected<std::string, load_error>
    to_json(const test_result& result) const {
        std::ostringstream oss;

        oss << "{\n";
        oss << "  \"type\": \"" << to_string(result.type) << "\",\n";
        oss << "  \"state\": \"" << to_string(result.state) << "\",\n";
        oss << "  \"started_at\": \"" << format_timestamp(result.started_at)
            << "\",\n";
        oss << "  \"ended_at\": \"" << format_timestamp(result.ended_at)
            << "\",\n";
        oss << "  \"duration_seconds\": " << result.duration.count() << ",\n";
        oss << "  \"target\": {\n";
        oss << "    \"host\": \"" << result.target_host << "\",\n";
        oss << "    \"port\": " << result.target_port << "\n";
        oss << "  },\n";
        oss << "  \"messages\": {\n";
        oss << "    \"sent\": " << result.messages_sent << ",\n";
        oss << "    \"acked\": " << result.messages_acked << ",\n";
        oss << "    \"failed\": " << result.messages_failed << ",\n";
        oss << "    \"success_rate_percent\": " << std::fixed
            << std::setprecision(2) << result.success_rate_percent << "\n";
        oss << "  },\n";
        oss << "  \"throughput\": {\n";
        oss << "    \"average_msg_per_sec\": " << std::setprecision(1)
            << result.throughput << ",\n";
        oss << "    \"peak_msg_per_sec\": " << result.peak_throughput << "\n";
        oss << "  },\n";
        oss << "  \"latency_ms\": {\n";
        oss << "    \"p50\": " << std::setprecision(2) << result.latency_p50_ms
            << ",\n";
        oss << "    \"p95\": " << result.latency_p95_ms << ",\n";
        oss << "    \"p99\": " << result.latency_p99_ms << ",\n";
        oss << "    \"min\": " << result.latency_min_ms << ",\n";
        oss << "    \"max\": " << result.latency_max_ms << ",\n";
        oss << "    \"mean\": " << result.latency_mean_ms << "\n";
        oss << "  },\n";
        oss << "  \"bytes\": {\n";
        oss << "    \"sent\": " << result.bytes_sent << ",\n";
        oss << "    \"received\": " << result.bytes_received << "\n";
        oss << "  },\n";
        oss << "  \"errors\": {\n";
        oss << "    \"connection\": " << result.connection_errors << ",\n";
        oss << "    \"timeout\": " << result.timeout_errors << ",\n";
        oss << "    \"protocol\": " << result.protocol_errors << "\n";
        oss << "  }";

        if (result.error_message) {
            oss << ",\n  \"error_message\": \"" << escape_json(*result.error_message)
                << "\"";
        }

        oss << "\n}";

        return oss.str();
    }

    static std::expected<test_result, load_error>
    from_json(std::string_view json) {
        // Simplified JSON parsing - in production would use a proper JSON library
        (void)json;
        return std::unexpected(load_error::report_failed);
    }

    std::expected<std::string, load_error>
    to_csv(const test_result& result) const {
        std::ostringstream oss;

        // Header
        oss << "type,state,duration_s,target_host,target_port,"
            << "messages_sent,messages_acked,messages_failed,success_rate,"
            << "throughput,peak_throughput,"
            << "latency_p50_ms,latency_p95_ms,latency_p99_ms,"
            << "latency_min_ms,latency_max_ms,latency_mean_ms,"
            << "bytes_sent,bytes_received,"
            << "connection_errors,timeout_errors,protocol_errors\n";

        // Data row
        oss << to_string(result.type) << ","
            << to_string(result.state) << ","
            << result.duration.count() << ","
            << result.target_host << ","
            << result.target_port << ","
            << result.messages_sent << ","
            << result.messages_acked << ","
            << result.messages_failed << ","
            << std::fixed << std::setprecision(2) << result.success_rate_percent
            << ","
            << std::setprecision(1) << result.throughput << ","
            << result.peak_throughput << ","
            << std::setprecision(2) << result.latency_p50_ms << ","
            << result.latency_p95_ms << ","
            << result.latency_p99_ms << ","
            << result.latency_min_ms << ","
            << result.latency_max_ms << ","
            << result.latency_mean_ms << ","
            << result.bytes_sent << ","
            << result.bytes_received << ","
            << result.connection_errors << ","
            << result.timeout_errors << ","
            << result.protocol_errors << "\n";

        return oss.str();
    }

    static void print_summary(const test_result& result) {
        std::cout << result.summary();
    }

    static void print_progress(const progress_info& info) {
        std::cout << "\r[" << std::fixed << std::setprecision(1)
                  << info.progress_percent << "%] "
                  << "Sent: " << info.messages_sent << " | "
                  << "Acked: " << info.messages_acked << " | "
                  << "Failed: " << info.messages_failed << " | "
                  << "Rate: " << std::setprecision(0) << info.current_throughput
                  << " msg/s | "
                  << "P95: " << std::setprecision(1) << info.current_latency_p95_ms
                  << " ms" << std::flush;
    }

private:
    std::expected<std::string, load_error>
    generate_text(const test_result& result) const {
        return result.summary();
    }

    std::expected<std::string, load_error>
    generate_markdown(const test_result& result) const {
        std::ostringstream oss;

        oss << "# " << config_.title << "\n\n";
        oss << "**Generated**: " << format_timestamp(std::chrono::system_clock::now())
            << "\n\n";

        oss << "## Test Configuration\n\n";
        oss << "| Parameter | Value |\n";
        oss << "|-----------|-------|\n";
        oss << "| Test Type | " << to_string(result.type) << " |\n";
        oss << "| Target | " << result.target_host << ":" << result.target_port
            << " |\n";
        oss << "| Duration | " << result.duration.count() << " seconds |\n";
        oss << "| State | " << to_string(result.state) << " |\n\n";

        oss << "## Results Summary\n\n";
        oss << "### Message Statistics\n\n";
        oss << "| Metric | Value |\n";
        oss << "|--------|-------|\n";
        oss << "| Messages Sent | " << result.messages_sent << " |\n";
        oss << "| Messages Acknowledged | " << result.messages_acked << " |\n";
        oss << "| Messages Failed | " << result.messages_failed << " |\n";
        oss << "| Success Rate | " << std::fixed << std::setprecision(2)
            << result.success_rate_percent << "% |\n\n";

        oss << "### Throughput\n\n";
        oss << "| Metric | Value |\n";
        oss << "|--------|-------|\n";
        oss << "| Average Throughput | " << std::setprecision(1)
            << result.throughput << " msg/s |\n";
        oss << "| Peak Throughput | " << result.peak_throughput << " msg/s |\n\n";

        oss << "### Latency Distribution\n\n";
        oss << "| Percentile | Latency (ms) |\n";
        oss << "|------------|-------------|\n";
        oss << "| P50 (Median) | " << std::setprecision(2)
            << result.latency_p50_ms << " |\n";
        oss << "| P95 | " << result.latency_p95_ms << " |\n";
        oss << "| P99 | " << result.latency_p99_ms << " |\n";
        oss << "| Minimum | " << result.latency_min_ms << " |\n";
        oss << "| Maximum | " << result.latency_max_ms << " |\n";
        oss << "| Mean | " << result.latency_mean_ms << " |\n\n";

        oss << "### Data Transfer\n\n";
        oss << "| Metric | Value |\n";
        oss << "|--------|-------|\n";
        oss << "| Bytes Sent | " << format_bytes(result.bytes_sent) << " |\n";
        oss << "| Bytes Received | " << format_bytes(result.bytes_received)
            << " |\n\n";

        if (result.connection_errors > 0 || result.timeout_errors > 0 ||
            result.protocol_errors > 0) {
            oss << "### Errors\n\n";
            oss << "| Error Type | Count |\n";
            oss << "|------------|-------|\n";
            oss << "| Connection Errors | " << result.connection_errors << " |\n";
            oss << "| Timeout Errors | " << result.timeout_errors << " |\n";
            oss << "| Protocol Errors | " << result.protocol_errors << " |\n\n";
        }

        if (result.error_message) {
            oss << "## Error Details\n\n";
            oss << "```\n" << *result.error_message << "\n```\n\n";
        }

        if (!result.notes.empty()) {
            oss << "## Notes\n\n";
            for (const auto& note : result.notes) {
                oss << "- " << note << "\n";
            }
            oss << "\n";
        }

        if (!config_.notes.empty()) {
            oss << "## Additional Notes\n\n";
            oss << config_.notes << "\n\n";
        }

        // Pass/Fail assessment
        oss << "## Assessment\n\n";
        if (result.passed(99.0, 50.0)) {
            oss << "✅ **PASSED** - All success criteria met\n";
        } else {
            oss << "❌ **FAILED** - ";
            if (result.success_rate_percent < 99.0) {
                oss << "Success rate below 99% threshold. ";
            }
            if (result.latency_p95_ms > 50.0) {
                oss << "P95 latency exceeds 50ms threshold.";
            }
            oss << "\n";
        }

        return oss.str();
    }

    std::expected<std::string, load_error>
    generate_html(const test_result& result) const {
        std::ostringstream oss;

        oss << R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" << config_.title << R"(</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 40px; }
        h1 { color: #333; }
        table { border-collapse: collapse; margin: 20px 0; }
        th, td { border: 1px solid #ddd; padding: 12px; text-align: left; }
        th { background-color: #f5f5f5; }
        .passed { color: #28a745; }
        .failed { color: #dc3545; }
        .metric-value { font-weight: bold; }
    </style>
</head>
<body>
    <h1>)" << config_.title << R"(</h1>
    <p>Generated: )" << format_timestamp(std::chrono::system_clock::now())
            << R"(</p>

    <h2>Test Configuration</h2>
    <table>
        <tr><th>Parameter</th><th>Value</th></tr>
        <tr><td>Test Type</td><td>)" << to_string(result.type) << R"(</td></tr>
        <tr><td>Target</td><td>)" << result.target_host << ":" << result.target_port
            << R"(</td></tr>
        <tr><td>Duration</td><td>)" << result.duration.count() << R"( seconds</td></tr>
        <tr><td>State</td><td>)" << to_string(result.state) << R"(</td></tr>
    </table>

    <h2>Results Summary</h2>
    <h3>Message Statistics</h3>
    <table>
        <tr><th>Metric</th><th>Value</th></tr>
        <tr><td>Messages Sent</td><td class="metric-value">)"
            << result.messages_sent << R"(</td></tr>
        <tr><td>Messages Acknowledged</td><td class="metric-value">)"
            << result.messages_acked << R"(</td></tr>
        <tr><td>Messages Failed</td><td class="metric-value">)"
            << result.messages_failed << R"(</td></tr>
        <tr><td>Success Rate</td><td class="metric-value">)"
            << std::fixed << std::setprecision(2) << result.success_rate_percent
            << R"(%</td></tr>
    </table>

    <h3>Latency Distribution</h3>
    <table>
        <tr><th>Percentile</th><th>Latency (ms)</th></tr>
        <tr><td>P50 (Median)</td><td>)" << result.latency_p50_ms << R"(</td></tr>
        <tr><td>P95</td><td>)" << result.latency_p95_ms << R"(</td></tr>
        <tr><td>P99</td><td>)" << result.latency_p99_ms << R"(</td></tr>
    </table>

    <h2>Assessment</h2>
    <p class=")" << (result.passed() ? "passed" : "failed") << R"(">)"
            << (result.passed() ? "✅ PASSED" : "❌ FAILED") << R"(</p>
</body>
</html>
)";

        return oss.str();
    }

    static report_format infer_format(const std::filesystem::path& path) {
        auto ext = path.extension().string();
        if (ext == ".json") return report_format::json;
        if (ext == ".md") return report_format::markdown;
        if (ext == ".csv") return report_format::csv;
        if (ext == ".html" || ext == ".htm") return report_format::html;
        return report_format::text;
    }

    static std::string format_timestamp(
        std::chrono::system_clock::time_point tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_time;
#ifdef _WIN32
        localtime_s(&tm_time, &time);
#else
        localtime_r(&time, &tm_time);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_time, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    static std::string format_bytes(uint64_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unit = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit < 4) {
            size /= 1024.0;
            ++unit;
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
        return oss.str();
    }

    static std::string escape_json(const std::string& str) {
        std::string result;
        result.reserve(str.size());
        for (char c : str) {
            switch (c) {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    result += c;
            }
        }
        return result;
    }

    report_config config_;
};

// =============================================================================
// Public Interface
// =============================================================================

load_reporter::load_reporter()
    : pimpl_(std::make_unique<impl>()) {}

load_reporter::load_reporter(const report_config& config)
    : pimpl_(std::make_unique<impl>(config)) {}

load_reporter::~load_reporter() = default;

load_reporter::load_reporter(load_reporter&&) noexcept = default;
load_reporter& load_reporter::operator=(load_reporter&&) noexcept = default;

std::expected<std::string, load_error>
load_reporter::generate(const test_result& result, report_format format) const {
    return pimpl_->generate(result, format);
}

std::expected<std::string, load_error>
load_reporter::generate(const test_result& result) const {
    return pimpl_->generate(result);
}

std::expected<void, load_error>
load_reporter::save(const test_result& result, const std::filesystem::path& path,
                    std::optional<report_format> format) const {
    return pimpl_->save(result, path, format);
}

std::expected<std::string, load_error>
load_reporter::generate_suite_summary(std::span<const test_result> results,
                                      report_format format) const {
    return pimpl_->generate_suite_summary(results, format);
}

std::expected<void, load_error>
load_reporter::save_suite_summary(std::span<const test_result> results,
                                  const std::filesystem::path& path) const {
    return pimpl_->save_suite_summary(results, path);
}

std::expected<std::string, load_error>
load_reporter::generate_comparison(const test_result& current,
                                   const test_result& baseline,
                                   report_format format) const {
    return pimpl_->generate_comparison(current, baseline, format);
}

void load_reporter::set_config(const report_config& config) {
    pimpl_->set_config(config);
}

const report_config& load_reporter::config() const noexcept {
    return pimpl_->config();
}

std::expected<std::string, load_error>
load_reporter::to_json(const test_result& result) const {
    return pimpl_->to_json(result);
}

std::expected<test_result, load_error>
load_reporter::from_json(std::string_view json) {
    return impl::from_json(json);
}

std::expected<std::string, load_error>
load_reporter::to_csv(const test_result& result) const {
    return pimpl_->to_csv(result);
}

void load_reporter::print_summary(const test_result& result) {
    impl::print_summary(result);
}

void load_reporter::print_progress(const progress_info& info) {
    impl::print_progress(info);
}

}  // namespace pacs::bridge::testing
