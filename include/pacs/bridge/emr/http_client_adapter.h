#ifndef PACS_BRIDGE_EMR_HTTP_CLIENT_ADAPTER_H
#define PACS_BRIDGE_EMR_HTTP_CLIENT_ADAPTER_H

/**
 * @file http_client_adapter.h
 * @brief HTTP client adapter interface for FHIR client
 *
 * Provides an abstract interface for HTTP operations, allowing
 * different HTTP client implementations to be used with the FHIR client.
 * This enables testing with mock implementations and integration with
 * various network libraries.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 */

#include "emr_types.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::emr {

/**
 * @brief Abstract HTTP client interface
 *
 * Provides a common interface for making HTTP requests.
 * Implementations may use cURL, Boost.Beast, or other HTTP libraries.
 *
 * @example Mock Implementation for Testing
 * ```cpp
 * class mock_http_client : public http_client_adapter {
 * public:
 *     auto execute(const http_request& req)
 *         -> Result<http_response> override {
 *         http_response response;
 *         response.status = http_status::ok;
 *         response.body = R"({"resourceType": "Patient", "id": "123"})";
 *         return response;
 *     }
 * };
 * ```
 */
class http_client_adapter {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~http_client_adapter() = default;

    // Non-copyable
    http_client_adapter(const http_client_adapter&) = delete;
    http_client_adapter& operator=(const http_client_adapter&) = delete;

    // Movable
    http_client_adapter(http_client_adapter&&) noexcept = default;
    http_client_adapter& operator=(http_client_adapter&&) noexcept = default;

    /**
     * @brief Execute an HTTP request
     *
     * Sends the HTTP request and returns the response.
     * Implementations should handle connection pooling, TLS, timeouts, etc.
     *
     * @param request HTTP request to execute
     * @return HTTP response or error
     */
    [[nodiscard]] virtual auto execute(const http_request& request)
        -> Result<http_response> = 0;

    /**
     * @brief Execute a GET request
     *
     * Convenience method for GET requests.
     *
     * @param url Request URL
     * @param headers Request headers
     * @param timeout Request timeout
     * @return HTTP response or error
     */
    [[nodiscard]] virtual auto get(
        std::string_view url,
        const std::vector<std::pair<std::string, std::string>>& headers = {},
        std::chrono::seconds timeout = std::chrono::seconds{30})
        -> Result<http_response>;

    /**
     * @brief Execute a POST request
     *
     * Convenience method for POST requests.
     *
     * @param url Request URL
     * @param body Request body
     * @param content_type Content-Type header value
     * @param headers Additional headers
     * @param timeout Request timeout
     * @return HTTP response or error
     */
    [[nodiscard]] virtual auto post(
        std::string_view url,
        std::string_view body,
        std::string_view content_type,
        const std::vector<std::pair<std::string, std::string>>& headers = {},
        std::chrono::seconds timeout = std::chrono::seconds{30})
        -> Result<http_response>;

    /**
     * @brief Execute a PUT request
     *
     * @param url Request URL
     * @param body Request body
     * @param content_type Content-Type header value
     * @param headers Additional headers
     * @param timeout Request timeout
     * @return HTTP response or error
     */
    [[nodiscard]] virtual auto put(
        std::string_view url,
        std::string_view body,
        std::string_view content_type,
        const std::vector<std::pair<std::string, std::string>>& headers = {},
        std::chrono::seconds timeout = std::chrono::seconds{30})
        -> Result<http_response>;

    /**
     * @brief Execute a DELETE request
     *
     * @param url Request URL
     * @param headers Request headers
     * @param timeout Request timeout
     * @return HTTP response or error
     */
    [[nodiscard]] virtual auto del(
        std::string_view url,
        const std::vector<std::pair<std::string, std::string>>& headers = {},
        std::chrono::seconds timeout = std::chrono::seconds{30})
        -> Result<http_response>;

    /**
     * @brief Execute a PATCH request
     *
     * @param url Request URL
     * @param body Patch body
     * @param content_type Content-Type header value
     * @param headers Additional headers
     * @param timeout Request timeout
     * @return HTTP response or error
     */
    [[nodiscard]] virtual auto patch(
        std::string_view url,
        std::string_view body,
        std::string_view content_type,
        const std::vector<std::pair<std::string, std::string>>& headers = {},
        std::chrono::seconds timeout = std::chrono::seconds{30})
        -> Result<http_response>;

protected:
    /**
     * @brief Protected default constructor
     */
    http_client_adapter() = default;
};

/**
 * @brief HTTP client configuration
 */
struct http_client_config {
    /** Whether to verify SSL certificates */
    bool verify_ssl{true};

    /** Path to CA certificate bundle (optional) */
    std::optional<std::string> ca_cert_path;

    /** Path to client certificate (optional) */
    std::optional<std::string> client_cert_path;

    /** Path to client private key (optional) */
    std::optional<std::string> client_key_path;

    /** Maximum number of connections in the pool */
    size_t max_connections{10};

    /** Connection timeout */
    std::chrono::seconds connect_timeout{10};

    /** Default request timeout */
    std::chrono::seconds request_timeout{30};

    /** User-Agent header value */
    std::string user_agent{"PACS-Bridge/1.0"};

    /** Follow redirects */
    bool follow_redirects{true};

    /** Maximum number of redirects to follow */
    size_t max_redirects{5};
};

/**
 * @brief Simple HTTP client implementation using callbacks
 *
 * Allows using function callbacks for HTTP operations, useful for
 * testing and integration with existing HTTP infrastructure.
 *
 * @example Usage with Lambda
 * ```cpp
 * auto http_func = [](const http_request& req)
 *     -> Result<http_response> {
 *     // Custom HTTP implementation
 *     return make_http_call(req);
 * };
 *
 * callback_http_client client(http_func);
 * auto response = client.execute(request);
 * ```
 */
class callback_http_client final : public http_client_adapter {
public:
    /**
     * @brief HTTP execution callback type
     */
    using execute_callback = std::function<Result<http_response>(
        const http_request&)>;

    /**
     * @brief Construct with execution callback
     * @param callback Function to execute HTTP requests
     */
    explicit callback_http_client(execute_callback callback);

    ~callback_http_client() override = default;

    /**
     * @brief Execute HTTP request using the callback
     */
    [[nodiscard]] auto execute(const http_request& request)
        -> Result<http_response> override;

private:
    execute_callback callback_;
};

/**
 * @brief Factory function for creating default HTTP client
 *
 * Creates an HTTP client using the best available implementation.
 *
 * @param config Client configuration
 * @return HTTP client instance
 */
[[nodiscard]] std::unique_ptr<http_client_adapter> create_http_client(
    const http_client_config& config = {});

/**
 * @brief Factory function for creating HTTP client with callback
 *
 * @param callback Execution callback
 * @return HTTP client instance
 */
[[nodiscard]] std::unique_ptr<http_client_adapter> create_http_client(
    callback_http_client::execute_callback callback);

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_HTTP_CLIENT_ADAPTER_H
