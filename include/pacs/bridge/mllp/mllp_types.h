#ifndef PACS_BRIDGE_MLLP_MLLP_TYPES_H
#define PACS_BRIDGE_MLLP_MLLP_TYPES_H

/**
 * @file mllp_types.h
 * @brief MLLP protocol type definitions for HL7 message transport
 *
 * Defines constants, error codes, and configuration structures for the
 * Minimal Lower Layer Protocol (MLLP) used for HL7 message transmission.
 *
 * MLLP Frame Structure:
 *   <VT>message<FS><CR>
 *   - VT (0x0B): Vertical Tab - Start of message
 *   - message: HL7 message content
 *   - FS (0x1C): File Separator - End of message content
 *   - CR (0x0D): Carriage Return - End of frame
 *
 * @see docs/reference_materials/04_mllp_protocol.md
 */

#include "pacs/bridge/security/tls_types.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// IExecutor interface for task execution (when available)
#ifndef PACS_BRIDGE_STANDALONE_BUILD
#include <kcenon/common/interfaces/executor_interface.h>
#endif

namespace pacs::bridge::mllp {

// =============================================================================
// MLLP Protocol Constants
// =============================================================================

/** Start of message marker (Vertical Tab) */
constexpr char MLLP_START_BYTE = '\x0B';

/** End of message content marker (File Separator) */
constexpr char MLLP_END_BYTE = '\x1C';

/** End of frame marker (Carriage Return) */
constexpr char MLLP_CARRIAGE_RETURN = '\x0D';

/** Maximum HL7 message size (default: 10MB) */
constexpr size_t MLLP_MAX_MESSAGE_SIZE = 10 * 1024 * 1024;

/** Default MLLP port */
constexpr uint16_t MLLP_DEFAULT_PORT = 2575;

/** Default MLLP over TLS port */
constexpr uint16_t MLLPS_DEFAULT_PORT = 2576;

// =============================================================================
// Error Codes (-970 to -979)
// =============================================================================

/**
 * @brief MLLP specific error codes
 *
 * Allocated range: -970 to -979
 */
enum class mllp_error : int {
    /** Invalid MLLP frame structure */
    invalid_frame = -970,

    /** Message exceeds maximum allowed size */
    message_too_large = -971,

    /** Connection timeout during send or receive */
    timeout = -972,

    /** Connection was closed by peer */
    connection_closed = -973,

    /** Failed to connect to remote host */
    connection_failed = -974,

    /** Invalid server configuration */
    invalid_configuration = -975,

    /** Server is already running */
    already_running = -976,

    /** Server is not running */
    not_running = -977,

    /** Socket operation failed */
    socket_error = -978,

    /** HL7 acknowledgment indicated error */
    ack_error = -979
};

/**
 * @brief Convert mllp_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(mllp_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description of MLLP error
 */
[[nodiscard]] constexpr const char* to_string(mllp_error error) noexcept {
    switch (error) {
        case mllp_error::invalid_frame:
            return "Invalid MLLP frame structure";
        case mllp_error::message_too_large:
            return "Message exceeds maximum allowed size";
        case mllp_error::timeout:
            return "Connection timeout";
        case mllp_error::connection_closed:
            return "Connection closed by peer";
        case mllp_error::connection_failed:
            return "Failed to connect to remote host";
        case mllp_error::invalid_configuration:
            return "Invalid server configuration";
        case mllp_error::already_running:
            return "Server is already running";
        case mllp_error::not_running:
            return "Server is not running";
        case mllp_error::socket_error:
            return "Socket operation failed";
        case mllp_error::ack_error:
            return "HL7 acknowledgment indicated error";
        default:
            return "Unknown MLLP error";
    }
}

// =============================================================================
// MLLP Server Configuration
// =============================================================================

/**
 * @brief MLLP server configuration
 *
 * Configures the MLLP listener including port, TLS settings, and
 * connection limits.
 */
struct mllp_server_config {
    /** Port to listen on */
    uint16_t port = MLLP_DEFAULT_PORT;

    /** Bind address (empty = all interfaces) */
    std::string bind_address;

    /** Maximum concurrent connections */
    size_t max_connections = 50;

    /** Connection idle timeout */
    std::chrono::seconds idle_timeout{300};

    /** Maximum message size in bytes */
    size_t max_message_size = MLLP_MAX_MESSAGE_SIZE;

    /** TLS configuration (disabled by default) */
    security::tls_config tls;

#ifndef PACS_BRIDGE_STANDALONE_BUILD
    /** Optional executor for task execution (nullptr = use internal std::thread) */
    std::shared_ptr<kcenon::common::interfaces::IExecutor> executor;
#endif

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (port == 0) return false;
        if (max_connections == 0) return false;
        if (max_message_size == 0) return false;
        if (tls.enabled && !tls.is_valid_for_server()) return false;
        return true;
    }
};

// =============================================================================
// MLLP Client Configuration
// =============================================================================

/**
 * @brief MLLP client configuration
 *
 * Configures outbound MLLP connections including target host,
 * TLS settings, and retry behavior.
 */
struct mllp_client_config {
    /** Target hostname or IP */
    std::string host;

    /** Target port */
    uint16_t port = MLLP_DEFAULT_PORT;

    /** Connection timeout */
    std::chrono::milliseconds connect_timeout{5000};

    /** Read/write timeout */
    std::chrono::milliseconds io_timeout{30000};

    /** Number of retry attempts on failure */
    size_t retry_count = 3;

    /** Delay between retry attempts */
    std::chrono::milliseconds retry_delay{1000};

    /** TLS configuration (disabled by default) */
    security::tls_config tls;

    /** Keep connection alive for reuse */
    bool keep_alive = true;

    /** Validate configuration */
    [[nodiscard]] bool is_valid() const noexcept {
        if (host.empty()) return false;
        if (port == 0) return false;
        if (tls.enabled && !tls.is_valid_for_client()) return false;
        return true;
    }
};

// =============================================================================
// MLLP Session Information
// =============================================================================

/**
 * @brief Information about an MLLP connection
 */
struct mllp_session_info {
    /** Unique session identifier */
    uint64_t session_id = 0;

    /** Remote peer address */
    std::string remote_address;

    /** Remote peer port */
    uint16_t remote_port = 0;

    /** Local port */
    uint16_t local_port = 0;

    /** Session start time */
    std::chrono::system_clock::time_point connected_at;

    /** Messages received on this session */
    size_t messages_received = 0;

    /** Messages sent on this session */
    size_t messages_sent = 0;

    /** Bytes received on this session */
    size_t bytes_received = 0;

    /** Bytes sent on this session */
    size_t bytes_sent = 0;

    /** TLS is enabled for this session */
    bool tls_enabled = false;

    /** TLS protocol version (if TLS enabled) */
    std::optional<std::string> tls_version;

    /** TLS cipher suite (if TLS enabled) */
    std::optional<std::string> tls_cipher;

    /** Peer certificate subject (if TLS with client auth) */
    std::optional<std::string> peer_certificate_subject;

    /**
     * @brief Get session duration
     */
    [[nodiscard]] std::chrono::seconds duration() const noexcept {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(
            now - connected_at);
    }
};

// =============================================================================
// MLLP Server Statistics
// =============================================================================

/**
 * @brief MLLP server statistics
 */
struct mllp_server_statistics {
    /** Current active connections */
    size_t active_connections = 0;

    /** Total connections since start */
    size_t total_connections = 0;

    /** Total messages received */
    size_t messages_received = 0;

    /** Total messages sent (responses) */
    size_t messages_sent = 0;

    /** Total bytes received */
    size_t bytes_received = 0;

    /** Total bytes sent */
    size_t bytes_sent = 0;

    /** Connection errors */
    size_t connection_errors = 0;

    /** Protocol/parsing errors */
    size_t protocol_errors = 0;

    /** TLS handshake failures */
    size_t tls_failures = 0;

    /** Server uptime */
    std::chrono::system_clock::time_point started_at;

    /**
     * @brief Get server uptime
     */
    [[nodiscard]] std::chrono::seconds uptime() const noexcept {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(
            now - started_at);
    }
};

// =============================================================================
// MLLP Message
// =============================================================================

/**
 * @brief MLLP-framed message
 */
struct mllp_message {
    /** Raw HL7 message content (without MLLP framing) */
    std::vector<uint8_t> content;

    /** Session info for received messages */
    std::optional<mllp_session_info> session;

    /** Reception timestamp */
    std::chrono::system_clock::time_point received_at;

    /**
     * @brief Get message content as string
     */
    [[nodiscard]] std::string to_string() const {
        return std::string(content.begin(), content.end());
    }

    /**
     * @brief Create message from string
     */
    [[nodiscard]] static mllp_message from_string(std::string_view str) {
        mllp_message msg;
        msg.content.assign(str.begin(), str.end());
        msg.received_at = std::chrono::system_clock::now();
        return msg;
    }

    /**
     * @brief Frame message for MLLP transmission
     * @return MLLP-framed message bytes
     */
    [[nodiscard]] std::vector<uint8_t> frame() const {
        std::vector<uint8_t> framed;
        framed.reserve(content.size() + 3);
        framed.push_back(static_cast<uint8_t>(MLLP_START_BYTE));
        framed.insert(framed.end(), content.begin(), content.end());
        framed.push_back(static_cast<uint8_t>(MLLP_END_BYTE));
        framed.push_back(static_cast<uint8_t>(MLLP_CARRIAGE_RETURN));
        return framed;
    }
};

}  // namespace pacs::bridge::mllp

#endif  // PACS_BRIDGE_MLLP_MLLP_TYPES_H
