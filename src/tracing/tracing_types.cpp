/**
 * @file tracing_types.cpp
 * @brief Implementation of tracing type utilities
 */

#include "pacs/bridge/tracing/tracing_types.h"

#include <iomanip>
#include <random>
#include <sstream>

namespace pacs::bridge::tracing {

namespace {

/**
 * @brief Generate a random hex ID
 */
std::string generate_hex_id(size_t length) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char hex_chars[] = "0123456789abcdef";

    std::string result;
    result.reserve(length);

    std::uniform_int_distribution<int> dist(0, 15);
    for (size_t i = 0; i < length; ++i) {
        result += hex_chars[dist(rng)];
    }

    return result;
}

/**
 * @brief Check if string is valid hex
 */
bool is_valid_hex(std::string_view str, size_t expected_length) {
    if (str.length() != expected_length) {
        return false;
    }

    for (char c : str) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    return true;
}

}  // namespace

std::string trace_context::to_traceparent() const {
    if (!is_valid()) {
        return "";
    }

    std::ostringstream ss;
    ss << "00-"                              // version
       << trace_id << "-"                    // trace-id
       << span_id << "-"                     // parent-id (current span)
       << std::hex << std::setfill('0')
       << std::setw(2) << static_cast<int>(trace_flags);

    return ss.str();
}

std::optional<trace_context> trace_context::from_traceparent(
    std::string_view traceparent) {

    // Format: {version}-{trace-id}-{parent-id}-{trace-flags}
    // Example: 00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01

    if (traceparent.empty()) {
        return std::nullopt;
    }

    // Split by '-'
    std::vector<std::string> parts;
    std::string current;

    for (char c : traceparent) {
        if (c == '-') {
            parts.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(std::move(current));

    // Must have exactly 4 parts
    if (parts.size() != 4) {
        return std::nullopt;
    }

    // Validate version (must be "00")
    if (parts[0] != "00") {
        return std::nullopt;
    }

    // Validate trace-id (32 hex chars)
    if (!is_valid_hex(parts[1], 32)) {
        return std::nullopt;
    }

    // Validate parent-id (16 hex chars)
    if (!is_valid_hex(parts[2], 16)) {
        return std::nullopt;
    }

    // Validate trace-flags (2 hex chars)
    if (!is_valid_hex(parts[3], 2)) {
        return std::nullopt;
    }

    trace_context ctx;
    ctx.trace_id = parts[1];
    ctx.parent_span_id = parts[2];
    ctx.span_id = generate_hex_id(16);  // Generate new span ID

    // Parse trace flags
    try {
        ctx.trace_flags = static_cast<uint8_t>(std::stoi(parts[3], nullptr, 16));
    } catch (...) {
        ctx.trace_flags = 0x01;
    }

    return ctx;
}

}  // namespace pacs::bridge::tracing
