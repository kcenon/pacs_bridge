/**
 * @file logger_adapter.cpp
 * @brief Implementation of logger adapter for pacs_bridge
 *
 * Bridges logger_adapter interface with common_system's ILogger.
 * Provides two implementations:
 *   - ilogger_adapter: wraps ILogger for full ecosystem integration
 *   - console_logger_adapter: standalone fallback for simpler deployments
 *
 * @see include/pacs/bridge/integration/logger_adapter.h
 * @see https://github.com/kcenon/pacs_bridge/issues/267
 */

#include "pacs/bridge/integration/logger_adapter.h"

#include <kcenon/common/interfaces/logger_interface.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace pacs::bridge::integration {

namespace {

// =============================================================================
// Log Level Conversion
// =============================================================================

/**
 * @brief Convert pacs log_level to kcenon log_level
 */
kcenon::common::interfaces::log_level to_kcenon_level(log_level level) {
    switch (level) {
        case log_level::trace:
            return kcenon::common::interfaces::log_level::trace;
        case log_level::debug:
            return kcenon::common::interfaces::log_level::debug;
        case log_level::info:
            return kcenon::common::interfaces::log_level::info;
        case log_level::warning:
            return kcenon::common::interfaces::log_level::warning;
        case log_level::error:
            return kcenon::common::interfaces::log_level::error;
        case log_level::critical:
            return kcenon::common::interfaces::log_level::critical;
        default:
            return kcenon::common::interfaces::log_level::info;
    }
}

/**
 * @brief Convert kcenon log_level to pacs log_level
 */
log_level from_kcenon_level(kcenon::common::interfaces::log_level level) {
    switch (level) {
        case kcenon::common::interfaces::log_level::trace:
            return log_level::trace;
        case kcenon::common::interfaces::log_level::debug:
            return log_level::debug;
        case kcenon::common::interfaces::log_level::info:
            return log_level::info;
        case kcenon::common::interfaces::log_level::warning:
            return log_level::warning;
        case kcenon::common::interfaces::log_level::error:
            return log_level::error;
        case kcenon::common::interfaces::log_level::critical:
            return log_level::critical;
        case kcenon::common::interfaces::log_level::off:
            return log_level::critical;
        default:
            return log_level::info;
    }
}

/**
 * @brief Get log level name string
 */
const char* level_name(log_level level) {
    switch (level) {
        case log_level::trace:
            return "TRACE";
        case log_level::debug:
            return "DEBUG";
        case log_level::info:
            return "INFO";
        case log_level::warning:
            return "WARN";
        case log_level::error:
            return "ERROR";
        case log_level::critical:
            return "CRIT";
        default:
            return "UNKNOWN";
    }
}

}  // namespace

// =============================================================================
// ilogger_adapter - Wraps ILogger
// =============================================================================

/**
 * @class ilogger_adapter
 * @brief Logger adapter that wraps common_system's ILogger
 *
 * Provides full integration with the kcenon ecosystem logging infrastructure.
 */
class ilogger_adapter : public logger_adapter {
public:
    explicit ilogger_adapter(std::shared_ptr<kcenon::common::interfaces::ILogger> logger)
        : logger_(std::move(logger)) {
        if (logger_) {
            current_level_ = from_kcenon_level(logger_->get_level());
        }
    }

    void log(log_level level, std::string_view message) override {
        if (!logger_) {
            return;
        }

        if (static_cast<int>(level) < static_cast<int>(current_level_)) {
            return;
        }

        logger_->log(to_kcenon_level(level), message);
    }

    void set_level(log_level level) override {
        current_level_ = level;
        if (logger_) {
            logger_->set_level(to_kcenon_level(level));
        }
    }

    [[nodiscard]] log_level get_level() const noexcept override {
        return current_level_;
    }

    void flush() override {
        if (logger_) {
            logger_->flush();
        }
    }

private:
    std::shared_ptr<kcenon::common::interfaces::ILogger> logger_;
    log_level current_level_{log_level::info};
};

// =============================================================================
// console_logger_adapter - Standalone Fallback
// =============================================================================

/**
 * @class console_logger_adapter
 * @brief Lightweight console logger for standalone deployments
 *
 * Provides basic logging to stdout/stderr when ILogger is not available.
 * Thread-safe output with timestamped messages.
 */
class console_logger_adapter : public logger_adapter {
public:
    explicit console_logger_adapter(std::string_view name)
        : name_(name) {}

    void log(log_level level, std::string_view message) override {
        if (static_cast<int>(level) < static_cast<int>(current_level_)) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << '.'
            << std::setfill('0') << std::setw(3) << ms.count() << " ["
            << level_name(level) << "] ";

        if (!name_.empty()) {
            oss << "[" << name_ << "] ";
        }

        oss << message << '\n';

        auto& stream = (level >= log_level::error) ? std::cerr : std::cout;
        stream << oss.str();
    }

    void set_level(log_level level) override { current_level_ = level; }

    [[nodiscard]] log_level get_level() const noexcept override {
        return current_level_;
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout.flush();
        std::cerr.flush();
    }

private:
    std::string name_;
    log_level current_level_{log_level::info};
    std::mutex mutex_;
};

// =============================================================================
// Global Logger Instance
// =============================================================================

namespace {

std::unique_ptr<logger_adapter> g_default_logger;
std::mutex g_logger_mutex;

}  // namespace

logger_adapter& get_logger() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);

    if (!g_default_logger) {
        g_default_logger = std::make_unique<console_logger_adapter>("pacs_bridge");
    }

    return *g_default_logger;
}

std::unique_ptr<logger_adapter> create_logger(std::string_view name) {
    return std::make_unique<console_logger_adapter>(name);
}

// =============================================================================
// Factory Functions for ILogger Integration
// =============================================================================

std::unique_ptr<logger_adapter> create_logger(
    std::shared_ptr<kcenon::common::interfaces::ILogger> logger) {
    return std::make_unique<ilogger_adapter>(std::move(logger));
}

void set_default_logger(std::shared_ptr<kcenon::common::interfaces::ILogger> logger) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_default_logger = std::make_unique<ilogger_adapter>(std::move(logger));
}

void reset_default_logger() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    g_default_logger.reset();
}

}  // namespace pacs::bridge::integration
