#ifndef PACS_BRIDGE_INTEGRATION_NETWORK_ADAPTER_H
#define PACS_BRIDGE_INTEGRATION_NETWORK_ADAPTER_H

/**
 * @file network_adapter.h
 * @brief Integration Module - Network system adapter
 *
 * Wraps network_system for TCP/TLS operations.
 *
 * @see docs/SDS_COMPONENTS.md - Section 8: Integration Module (DES-INT-001)
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::integration {

// =============================================================================
// Error Codes (-700 to -749)
// =============================================================================

/**
 * @brief Integration module specific error codes
 *
 * Allocated range: -700 to -749
 */
enum class integration_error : int {
    /** Connection failed */
    connection_failed = -700,

    /** Connection timeout */
    connection_timeout = -701,

    /** Send failed */
    send_failed = -702,

    /** Receive failed */
    receive_failed = -703,

    /** TLS handshake failed */
    tls_handshake_failed = -704,

    /** Invalid configuration */
    invalid_config = -705
};

/**
 * @brief Convert integration_error to error code integer
 */
[[nodiscard]] constexpr int to_error_code(integration_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Network connection configuration
 */
struct connection_config {
    std::string host;
    uint16_t port = 0;
    bool use_tls = false;
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds read_timeout{30000};
    std::chrono::milliseconds write_timeout{30000};
};

/**
 * @brief Network adapter interface
 *
 * Wraps network_system for TCP/TLS operations.
 * Provides connection pooling and retry logic.
 */
class network_adapter {
public:
    virtual ~network_adapter() = default;

    /**
     * @brief Connect to remote host
     * @param config Connection configuration
     * @return true if connected successfully
     */
    [[nodiscard]] virtual bool connect(const connection_config& config) = 0;

    /**
     * @brief Disconnect from remote host
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if connected
     */
    [[nodiscard]] virtual bool is_connected() const noexcept = 0;

    /**
     * @brief Send data
     * @param data Data to send
     * @return Number of bytes sent, or -1 on error
     */
    [[nodiscard]] virtual int64_t send(const std::vector<uint8_t>& data) = 0;

    /**
     * @brief Receive data
     * @param max_size Maximum number of bytes to receive
     * @return Received data, or empty on error/timeout
     */
    [[nodiscard]] virtual std::vector<uint8_t> receive(size_t max_size) = 0;

    /**
     * @brief Get last error message
     */
    [[nodiscard]] virtual std::string last_error() const = 0;
};

/**
 * @brief Create a network adapter instance
 * @return Network adapter implementation (plain TCP)
 */
[[nodiscard]] std::unique_ptr<network_adapter> create_network_adapter();

/**
 * @brief Create a network adapter instance with TLS option
 * @param use_tls If true, creates a TLS-enabled adapter
 * @param verify_cert If true, verifies server certificate (only for TLS)
 * @return Network adapter implementation
 */
[[nodiscard]] std::unique_ptr<network_adapter> create_network_adapter(
    bool use_tls, bool verify_cert = true);

} // namespace pacs::bridge::integration

#endif // PACS_BRIDGE_INTEGRATION_NETWORK_ADAPTER_H
