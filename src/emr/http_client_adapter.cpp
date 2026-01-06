/**
 * @file http_client_adapter.cpp
 * @brief Implementation of HTTP client adapter
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 */

#include "pacs/bridge/emr/http_client_adapter.h"

namespace pacs::bridge::emr {

// =============================================================================
// http_client_adapter default implementations
// =============================================================================

auto http_client_adapter::get(
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::chrono::seconds timeout) -> Result<http_response> {
    http_request request;
    request.method = http_method::get;
    request.url = std::string(url);
    request.headers = headers;
    request.timeout = timeout;
    return execute(request);
}

auto http_client_adapter::post(
    std::string_view url, std::string_view body, std::string_view content_type,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::chrono::seconds timeout) -> Result<http_response> {
    http_request request;
    request.method = http_method::post;
    request.url = std::string(url);
    request.body = std::string(body);
    request.headers = headers;
    request.add_header("Content-Type", std::string(content_type));
    request.timeout = timeout;
    return execute(request);
}

auto http_client_adapter::put(
    std::string_view url, std::string_view body, std::string_view content_type,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::chrono::seconds timeout) -> Result<http_response> {
    http_request request;
    request.method = http_method::put;
    request.url = std::string(url);
    request.body = std::string(body);
    request.headers = headers;
    request.add_header("Content-Type", std::string(content_type));
    request.timeout = timeout;
    return execute(request);
}

auto http_client_adapter::del(
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::chrono::seconds timeout) -> Result<http_response> {
    http_request request;
    request.method = http_method::delete_method;
    request.url = std::string(url);
    request.headers = headers;
    request.timeout = timeout;
    return execute(request);
}

auto http_client_adapter::patch(
    std::string_view url, std::string_view body, std::string_view content_type,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::chrono::seconds timeout) -> Result<http_response> {
    http_request request;
    request.method = http_method::patch;
    request.url = std::string(url);
    request.body = std::string(body);
    request.headers = headers;
    request.add_header("Content-Type", std::string(content_type));
    request.timeout = timeout;
    return execute(request);
}

// =============================================================================
// callback_http_client implementation
// =============================================================================

callback_http_client::callback_http_client(execute_callback callback)
    : callback_(std::move(callback)) {}

auto callback_http_client::execute(const http_request& request)
    -> Result<http_response> {
    if (!callback_) {
        return Result<http_response>::err(to_error_info(emr_error::invalid_configuration));
    }
    return callback_(request);
}

// =============================================================================
// Factory functions
// =============================================================================

std::unique_ptr<http_client_adapter> create_http_client(
    [[maybe_unused]] const http_client_config& config) {
    // Default implementation returns a no-op client
    // Real implementation would use cURL or other HTTP library
    return create_http_client(
        [](const http_request&) -> Result<http_response> {
            return Result<http_response>::err(to_error_info(emr_error::not_supported));
        });
}

std::unique_ptr<http_client_adapter> create_http_client(
    callback_http_client::execute_callback callback) {
    return std::make_unique<callback_http_client>(std::move(callback));
}

}  // namespace pacs::bridge::emr
