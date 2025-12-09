#ifndef PACS_BRIDGE_INTEGRATION_LOGGER_ADAPTER_H
#define PACS_BRIDGE_INTEGRATION_LOGGER_ADAPTER_H

/**
 * @file logger_adapter.h
 * @brief Integration Module - Logger system adapter
 *
 * Provides structured logging for bridge operations.
 *
 * @see docs/SDS_COMPONENTS.md - Section 8: Integration Module (DES-INT-003)
 */

#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::integration {

/**
 * @brief Log levels
 */
enum class log_level {
    trace,
    debug,
    info,
    warning,
    error,
    critical
};

/**
 * @brief Logger adapter interface
 *
 * Provides structured logging for bridge operations.
 * Wraps logger_system for consistent logging.
 */
class logger_adapter {
public:
    virtual ~logger_adapter() = default;

    /**
     * @brief Log a message at specified level
     * @param level Log level
     * @param message Log message
     */
    virtual void log(log_level level, std::string_view message) = 0;

    /**
     * @brief Log trace message
     */
    void trace(std::string_view message) { log(log_level::trace, message); }

    /**
     * @brief Log debug message
     */
    void debug(std::string_view message) { log(log_level::debug, message); }

    /**
     * @brief Log info message
     */
    void info(std::string_view message) { log(log_level::info, message); }

    /**
     * @brief Log warning message
     */
    void warning(std::string_view message) { log(log_level::warning, message); }

    /**
     * @brief Log error message
     */
    void error(std::string_view message) { log(log_level::error, message); }

    /**
     * @brief Log critical message
     */
    void critical(std::string_view message) {
        log(log_level::critical, message);
    }

    /**
     * @brief Set minimum log level
     * @param level Minimum level to log
     */
    virtual void set_level(log_level level) = 0;

    /**
     * @brief Get current log level
     */
    [[nodiscard]] virtual log_level get_level() const noexcept = 0;

    /**
     * @brief Flush pending log entries
     */
    virtual void flush() = 0;
};

/**
 * @brief Get the global logger instance
 * @return Logger adapter instance
 */
[[nodiscard]] logger_adapter& get_logger();

/**
 * @brief Create a named logger instance
 * @param name Logger name/category
 * @return Logger adapter instance
 */
[[nodiscard]] std::unique_ptr<logger_adapter> create_logger(
    std::string_view name);

} // namespace pacs::bridge::integration

#endif // PACS_BRIDGE_INTEGRATION_LOGGER_ADAPTER_H
