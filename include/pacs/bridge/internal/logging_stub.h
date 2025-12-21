/**
 * @file logging_stub.h
 * @brief Stub header for standalone builds without kcenon ecosystem
 *
 * This header provides stub implementations for the logging interfaces
 * when building without the full kcenon ecosystem. In standalone mode,
 * logging calls are no-ops.
 *
 * For full logging functionality, build with:
 *   cmake -B build -DBRIDGE_STANDALONE_BUILD=OFF
 */

#ifndef PACS_BRIDGE_INTERNAL_LOGGING_STUB_H
#define PACS_BRIDGE_INTERNAL_LOGGING_STUB_H

#ifdef PACS_BRIDGE_STANDALONE_BUILD

#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::internal {

/**
 * @brief Log levels for the logging system
 */
enum class log_level {
    debug,
    info,
    warning,
    error
};

/**
 * @brief Stub logger interface for standalone builds
 *
 * All logging operations are no-ops in standalone mode.
 */
class logger {
public:
    virtual ~logger() = default;

    virtual void log(log_level /*level*/, std::string_view /*message*/) {}
    virtual void debug(std::string_view /*message*/) {}
    virtual void info(std::string_view /*message*/) {}
    virtual void warning(std::string_view /*message*/) {}
    virtual void error(std::string_view /*message*/) {}
};

/**
 * @brief Stub logger that does nothing
 */
class null_logger : public logger {
public:
    static null_logger& instance() {
        static null_logger instance;
        return instance;
    }
};

/**
 * @brief Get a logger instance (returns stub in standalone mode)
 *
 * @param name Logger name (ignored in stub implementation)
 * @return Pointer to stub logger
 */
inline logger* get_logger(std::string_view /*name*/) {
    return &null_logger::instance();
}

/**
 * @brief Get a logger instance as shared_ptr
 *
 * @param name Logger name (ignored in stub implementation)
 * @return Shared pointer to stub logger
 */
inline std::shared_ptr<logger> get_logger_ptr(std::string_view /*name*/) {
    static auto instance = std::make_shared<null_logger>();
    return instance;
}

}  // namespace pacs::bridge::internal

// Provide compatibility aliases in the expected namespace for standalone builds
namespace kcenon::common::interfaces {
    using log_level = pacs::bridge::internal::log_level;
    using logger = pacs::bridge::internal::logger;
    using null_logger = pacs::bridge::internal::null_logger;

    inline logger* get_logger(std::string_view name) {
        return pacs::bridge::internal::get_logger(name);
    }

    inline std::shared_ptr<logger> get_logger_ptr(std::string_view name) {
        return pacs::bridge::internal::get_logger_ptr(name);
    }
}  // namespace kcenon::common::interfaces

#endif  // PACS_BRIDGE_STANDALONE_BUILD

#endif  // PACS_BRIDGE_INTERNAL_LOGGING_STUB_H
