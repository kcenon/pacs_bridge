/**
 * @file result_stub.h
 * @brief Stub header for Result<T> pattern in standalone builds
 *
 * This header provides stub implementations for the Result pattern types
 * when building without the full kcenon ecosystem. This enables the EMR
 * module to build in standalone mode.
 *
 * For full Result pattern functionality from common_system, build with:
 *   cmake -B build -DBRIDGE_STANDALONE_BUILD=OFF
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/217
 */

#ifndef PACS_BRIDGE_INTERNAL_RESULT_STUB_H
#define PACS_BRIDGE_INTERNAL_RESULT_STUB_H

#ifdef PACS_BRIDGE_STANDALONE_BUILD

#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace pacs::bridge::internal {

/**
 * @struct error_info
 * @brief Standard error information used by Result<T>.
 */
struct error_info {
    int code;
    std::string message;
    std::string module;
    std::optional<std::string> details;

    error_info() : code(0) {}

    error_info(const std::string& msg)
        : code(-1), message(msg), module("") {}

    error_info(int c, const std::string& msg, const std::string& mod = "")
        : code(c), message(msg), module(mod) {}

    error_info(int c, const std::string& msg, const std::string& mod,
               const std::string& det)
        : code(c), message(msg), module(mod), details(det) {}

    template<typename Enum,
             typename std::enable_if_t<std::is_enum_v<Enum>, int> = 0>
    error_info(Enum c, std::string msg, std::string mod = "",
               std::optional<std::string> det = std::nullopt)
        : code(static_cast<int>(c)),
          message(std::move(msg)),
          module(std::move(mod)),
          details(std::move(det)) {}

    bool operator==(const error_info& other) const {
        return code == other.code && message == other.message &&
               module == other.module && details == other.details;
    }

    bool operator!=(const error_info& other) const {
        return !(*this == other);
    }
};

using error_code = error_info;

/**
 * @class Result
 * @brief Result type for error handling without exceptions
 *
 * A Result<T> can be in one of two states:
 * 1. Ok - Contains a valid value of type T
 * 2. Error - Contains an error_info describing the failure
 */
template<typename T>
class Result {
public:
    using value_type = T;
    using error_type = error_info;

private:
    std::optional<T> value_;
    std::optional<error_info> error_;

public:
    Result(const T& value) : value_(value), error_(std::nullopt) {}
    Result(T&& value) : value_(std::move(value)), error_(std::nullopt) {}
    Result(const error_info& error) : value_(std::nullopt), error_(error) {}
    Result(error_info&& error) : value_(std::nullopt), error_(std::move(error)) {}

    Result() = delete;

    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) = default;

    template<typename U = T>
    static Result<T> ok(U&& value) {
        return Result<T>(std::forward<U>(value));
    }

    static Result<T> err(const error_info& error) {
        return Result<T>(error);
    }

    static Result<T> err(error_info&& error) {
        return Result<T>(std::move(error));
    }

    static Result<T> err(int code, const std::string& message,
                         const std::string& module = "") {
        return Result<T>(error_info{code, message, module});
    }

    static Result<T> uninitialized() {
        return Result<T>(
            error_info{-6, "Result not initialized", "common::Result"});
    }

    bool is_ok() const { return value_.has_value(); }
    bool is_err() const { return error_.has_value(); }

    const T& unwrap() const {
        if (is_err()) {
            const auto& err = error_.value();
            throw std::runtime_error("Called unwrap on error: " + err.message);
        }
        return value_.value();
    }

    T& unwrap() {
        if (is_err()) {
            const auto& err = error_.value();
            throw std::runtime_error("Called unwrap on error: " + err.message);
        }
        return value_.value();
    }

    T unwrap_or(T default_value) const {
        if (is_ok()) {
            return value_.value();
        }
        return default_value;
    }

    T value_or(T default_value) const { return unwrap_or(std::move(default_value)); }

    const T& value() const { return value_.value(); }
    T& value() { return value_.value(); }

    const error_info& error() const { return error_.value(); }

    template<typename F>
    auto map(F&& func) const -> Result<decltype(func(std::declval<T>()))> {
        using ReturnType = decltype(func(std::declval<T>()));

        if (is_ok()) {
            return Result<ReturnType>(func(value_.value()));
        } else if (is_err()) {
            return Result<ReturnType>(error_.value());
        } else {
            return Result<ReturnType>::uninitialized();
        }
    }

    template<typename F>
    auto and_then(F&& func) const -> decltype(func(std::declval<T>())) {
        using ReturnType = decltype(func(std::declval<T>()));

        if (is_ok()) {
            return func(value_.value());
        } else if (is_err()) {
            return ReturnType(error_.value());
        } else {
            return ReturnType::uninitialized();
        }
    }

    template<typename F>
    Result<T> or_else(F&& func) const {
        if (is_ok()) {
            return *this;
        } else if (is_err()) {
            return func(error_.value());
        } else {
            return *this;
        }
    }
};

/**
 * @brief Specialized Result for void operations
 */
using VoidResult = Result<std::monostate>;

}  // namespace pacs::bridge::internal

// Provide compatibility aliases in the expected namespace for standalone builds
namespace kcenon::common {
    using error_info = pacs::bridge::internal::error_info;
    using error_code = pacs::bridge::internal::error_code;

    template<typename T>
    using Result = pacs::bridge::internal::Result<T>;

    using VoidResult = pacs::bridge::internal::VoidResult;

    // Factory functions for creating Results

    /**
     * @brief Create a successful result
     */
    template<typename T>
    inline Result<T> ok(T value) {
        return Result<T>(std::move(value));
    }

    /**
     * @brief Create a successful void result
     */
    inline VoidResult ok() {
        return VoidResult(std::monostate{});
    }

    /**
     * @brief Create an error result with code and message
     */
    template<typename T>
    inline Result<T> make_error(int code, const std::string& message,
                                const std::string& module = "") {
        return Result<T>(error_info{code, message, module});
    }

    /**
     * @brief Create an error result with details
     */
    template<typename T>
    inline Result<T> make_error(int code, const std::string& message,
                                const std::string& module,
                                const std::string& details) {
        return Result<T>(error_info{code, message, module, details});
    }

    /**
     * @brief Create an error result from existing error_info
     */
    template<typename T>
    inline Result<T> make_error(const error_info& err) {
        return Result<T>(err);
    }

}  // namespace kcenon::common

#endif  // PACS_BRIDGE_STANDALONE_BUILD

#endif  // PACS_BRIDGE_INTERNAL_RESULT_STUB_H
