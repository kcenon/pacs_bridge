#ifndef PACS_BRIDGE_MESSAGING_HL7_REQUEST_HANDLER_H
#define PACS_BRIDGE_MESSAGING_HL7_REQUEST_HANDLER_H

/**
 * @file hl7_request_handler.h
 * @brief HL7 Request/Reply pattern integration
 *
 * Provides synchronous request/response handling for HL7 messages,
 * particularly for ACK/NAK response management. Supports:
 *   - Correlation ID tracking
 *   - Timeout handling
 *   - Automatic ACK generation
 *   - NAK error responses
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/146
 * @see https://github.com/kcenon/pacs_bridge/issues/154
 */

#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace pacs::bridge::messaging {

// =============================================================================
// Request/Reply Error Codes (-810 to -819)
// =============================================================================

/**
 * @brief Request handler specific error codes
 *
 * Allocated range: -810 to -819
 */
enum class request_error : int {
    /** Request timeout */
    timeout = -810,

    /** No handler registered */
    no_handler = -811,

    /** Handler returned error */
    handler_error = -812,

    /** Invalid request message */
    invalid_request = -813,

    /** Service not available */
    service_unavailable = -814,

    /** Correlation ID not found */
    correlation_not_found = -815,

    /** Response generation failed */
    response_failed = -816,

    /** Connection lost during request */
    connection_lost = -817,

    /** Request cancelled */
    cancelled = -818
};

/**
 * @brief Convert request_error to error code
 */
[[nodiscard]] constexpr int to_error_code(request_error error) noexcept {
    return static_cast<int>(error);
}

/**
 * @brief Get human-readable description
 */
[[nodiscard]] constexpr const char* to_string(request_error error) noexcept {
    switch (error) {
        case request_error::timeout:
            return "Request timed out waiting for response";
        case request_error::no_handler:
            return "No handler registered for request";
        case request_error::handler_error:
            return "Handler returned error";
        case request_error::invalid_request:
            return "Invalid request message";
        case request_error::service_unavailable:
            return "Service is not available";
        case request_error::correlation_not_found:
            return "Correlation ID not found";
        case request_error::response_failed:
            return "Failed to generate response";
        case request_error::connection_lost:
            return "Connection lost during request";
        case request_error::cancelled:
            return "Request was cancelled";
        default:
            return "Unknown request error";
    }
}

// =============================================================================
// HL7 ACK Types
// =============================================================================

/**
 * @brief HL7 acknowledgment code types
 */
enum class ack_code {
    /** Application Accept - Message accepted */
    AA,
    /** Application Error - Message accepted but contains errors */
    AE,
    /** Application Reject - Message rejected */
    AR,
    /** Commit Accept - Message stored successfully */
    CA,
    /** Commit Error - Message stored but with errors */
    CE,
    /** Commit Reject - Message could not be stored */
    CR
};

/**
 * @brief Convert ack_code to string
 */
[[nodiscard]] constexpr const char* to_string(ack_code code) noexcept {
    switch (code) {
        case ack_code::AA: return "AA";
        case ack_code::AE: return "AE";
        case ack_code::AR: return "AR";
        case ack_code::CA: return "CA";
        case ack_code::CE: return "CE";
        case ack_code::CR: return "CR";
        default: return "AA";
    }
}

// =============================================================================
// Request/Response Configuration
// =============================================================================

/**
 * @brief Configuration for request/reply handler
 */
struct request_handler_config {
    /** Default timeout for requests */
    std::chrono::milliseconds default_timeout{30000};

    /** Maximum concurrent pending requests */
    size_t max_pending_requests = 1000;

    /** Enable automatic ACK generation */
    bool auto_ack = true;

    /** Service topic for receiving requests */
    std::string service_topic;

    /** Reply topic (auto-generated if empty) */
    std::string reply_topic;

    /** Sending application name for ACK messages */
    std::string sending_application = "PACS_BRIDGE";

    /** Sending facility name for ACK messages */
    std::string sending_facility;

    /**
     * @brief Create default configuration
     */
    [[nodiscard]] static request_handler_config defaults() {
        return {};
    }
};

// =============================================================================
// Request Result
// =============================================================================

/**
 * @brief Result of a request operation
 */
struct request_result {
    /** Response message */
    hl7::hl7_message response;

    /** Round-trip time */
    std::chrono::milliseconds round_trip_time{0};

    /** Number of retry attempts made */
    size_t retry_count = 0;

    /** Was response from cache */
    bool from_cache = false;
};

// =============================================================================
// HL7 Request Handler
// =============================================================================

/**
 * @brief Handler function for processing HL7 requests
 *
 * @param request Incoming HL7 request message
 * @return Response message or error
 */
using request_processor = std::function<
    std::expected<hl7::hl7_message, request_error>(const hl7::hl7_message& request)>;

/**
 * @brief HL7 Request/Reply handler
 *
 * Provides synchronous request/response pattern for HL7 messages.
 * Manages correlation IDs to match requests with responses.
 *
 * @example Client Usage
 * ```cpp
 * hl7_request_client client(message_bus, "hl7.service.mwl");
 *
 * // Send request and wait for response
 * auto result = client.request(orm_message, std::chrono::seconds(30));
 * if (result) {
 *     auto& response = result->response;
 *     // Process ACK response
 * }
 * ```
 *
 * @example Server Usage
 * ```cpp
 * hl7_request_server server(message_bus, "hl7.service.mwl");
 *
 * server.register_handler([](const hl7::hl7_message& request)
 *     -> std::expected<hl7::hl7_message, request_error> {
 *
 *     // Process request and generate response
 *     auto ack = generate_ack(request, ack_code::AA);
 *     return ack;
 * });
 *
 * server.start();
 * ```
 */
class hl7_request_client {
public:
    /**
     * @brief Constructor
     *
     * @param bus Message bus to use
     * @param service_topic Service topic to send requests to
     */
    hl7_request_client(std::shared_ptr<class hl7_message_bus> bus,
                       std::string_view service_topic);

    /**
     * @brief Constructor with configuration
     */
    hl7_request_client(std::shared_ptr<class hl7_message_bus> bus,
                       const request_handler_config& config);

    /**
     * @brief Destructor
     */
    ~hl7_request_client();

    // Non-copyable, movable
    hl7_request_client(const hl7_request_client&) = delete;
    hl7_request_client& operator=(const hl7_request_client&) = delete;
    hl7_request_client(hl7_request_client&&) noexcept;
    hl7_request_client& operator=(hl7_request_client&&) noexcept;

    // =========================================================================
    // Request Operations
    // =========================================================================

    /**
     * @brief Send request and wait for response
     *
     * @param request HL7 request message
     * @param timeout Maximum time to wait for response
     * @return Request result or error
     */
    [[nodiscard]] std::expected<request_result, request_error> request(
        const hl7::hl7_message& request,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    /**
     * @brief Send request with default timeout
     *
     * Uses the default timeout from configuration.
     *
     * @param request HL7 request message
     * @return Request result or error
     */
    [[nodiscard]] std::expected<request_result, request_error> send(
        const hl7::hl7_message& request);

    /**
     * @brief Cancel a pending request
     *
     * @param correlation_id Correlation ID of the request to cancel
     * @return true if request was cancelled
     */
    bool cancel(std::string_view correlation_id);

    /**
     * @brief Cancel all pending requests
     */
    void cancel_all();

    // =========================================================================
    // Status
    // =========================================================================

    /**
     * @brief Get number of pending requests
     */
    [[nodiscard]] size_t pending_count() const noexcept;

    /**
     * @brief Check if client is ready
     */
    [[nodiscard]] bool is_ready() const noexcept;

    /**
     * @brief Get service topic
     */
    [[nodiscard]] const std::string& service_topic() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief HL7 Request/Reply server
 *
 * Handles incoming HL7 requests and generates responses.
 */
class hl7_request_server {
public:
    /**
     * @brief Constructor
     *
     * @param bus Message bus to use
     * @param service_topic Topic to listen for requests
     */
    hl7_request_server(std::shared_ptr<class hl7_message_bus> bus,
                       std::string_view service_topic);

    /**
     * @brief Constructor with configuration
     */
    hl7_request_server(std::shared_ptr<class hl7_message_bus> bus,
                       const request_handler_config& config);

    /**
     * @brief Destructor - stops server if running
     */
    ~hl7_request_server();

    // Non-copyable, movable
    hl7_request_server(const hl7_request_server&) = delete;
    hl7_request_server& operator=(const hl7_request_server&) = delete;
    hl7_request_server(hl7_request_server&&) noexcept;
    hl7_request_server& operator=(hl7_request_server&&) noexcept;

    // =========================================================================
    // Handler Registration
    // =========================================================================

    /**
     * @brief Register request handler
     *
     * @param handler Function to process requests
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, request_error> register_handler(
        request_processor handler);

    /**
     * @brief Unregister the handler
     */
    void unregister_handler();

    /**
     * @brief Check if handler is registered
     */
    [[nodiscard]] bool has_handler() const noexcept;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Start listening for requests
     *
     * @return Success or error
     */
    [[nodiscard]] std::expected<void, request_error> start();

    /**
     * @brief Stop listening for requests
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    [[nodiscard]] bool is_running() const noexcept;

    // =========================================================================
    // Status
    // =========================================================================

    /**
     * @brief Get service topic
     */
    [[nodiscard]] const std::string& service_topic() const noexcept;

    /**
     * @brief Server statistics
     */
    struct statistics {
        /** Total requests received */
        uint64_t requests_received = 0;

        /** Requests successfully processed */
        uint64_t requests_succeeded = 0;

        /** Requests that failed */
        uint64_t requests_failed = 0;

        /** Average processing time in microseconds */
        double avg_processing_time_us = 0.0;
    };

    /**
     * @brief Get server statistics
     */
    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// ACK/NAK Utilities
// =============================================================================

/**
 * @brief ACK message builder utilities
 */
namespace ack_builder {

/**
 * @brief Generate ACK message for a request
 *
 * @param request Original request message
 * @param code Acknowledgment code
 * @param text_message Optional text message
 * @param sending_app Sending application name
 * @param sending_facility Sending facility name
 * @return Generated ACK message
 */
[[nodiscard]] hl7::hl7_message generate_ack(
    const hl7::hl7_message& request,
    ack_code code,
    std::string_view text_message = "",
    std::string_view sending_app = "PACS_BRIDGE",
    std::string_view sending_facility = "");

/**
 * @brief Generate NAK message for a request
 *
 * @param request Original request message
 * @param error_message Error description
 * @param error_code Error code
 * @param sending_app Sending application name
 * @param sending_facility Sending facility name
 * @return Generated NAK message
 */
[[nodiscard]] hl7::hl7_message generate_nak(
    const hl7::hl7_message& request,
    std::string_view error_message,
    std::string_view error_code = "AE",
    std::string_view sending_app = "PACS_BRIDGE",
    std::string_view sending_facility = "");

/**
 * @brief Check if message is an ACK/NAK
 *
 * @param message Message to check
 * @return true if message is ACK type
 */
[[nodiscard]] bool is_ack(const hl7::hl7_message& message);

/**
 * @brief Extract ACK code from response
 *
 * @param ack ACK message
 * @return ACK code or nullopt if not found
 */
[[nodiscard]] std::optional<ack_code> get_ack_code(
    const hl7::hl7_message& ack);

/**
 * @brief Check if ACK indicates success
 *
 * @param ack ACK message
 * @return true if ACK code is AA or CA
 */
[[nodiscard]] bool is_ack_success(const hl7::hl7_message& ack);

}  // namespace ack_builder

}  // namespace pacs::bridge::messaging

#endif  // PACS_BRIDGE_MESSAGING_HL7_REQUEST_HANDLER_H
