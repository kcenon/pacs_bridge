/**
 * @file fhir_client.cpp
 * @brief Implementation of FHIR R4 HTTP client
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 */

#include "pacs/bridge/emr/fhir_client.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>

namespace pacs::bridge::emr {

namespace {

// Simple JSON field finder (inline helper)
std::string_view find_json_field(std::string_view json, std::string_view field) {
    std::string pattern = "\"";
    pattern += field;
    pattern += "\"";

    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) {
        return {};
    }

    pos += pattern.size();
    // Skip whitespace
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != ':') {
        return {};
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }

    // Find end of value
    size_t end = pos;
    if (json[pos] == '"') {
        // String value
        ++end;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\' && end + 1 < json.size()) {
                ++end;
            }
            ++end;
        }
        if (end < json.size()) {
            ++end;
        }
        // Return without quotes
        return json.substr(pos + 1, end - pos - 2);
    } else {
        // Non-string value
        while (end < json.size() && !std::isspace(static_cast<unsigned char>(json[end])) &&
               json[end] != ',' && json[end] != '}' && json[end] != ']') {
            ++end;
        }
        return json.substr(pos, end - pos);
    }
}

// Parse resource wrapper from JSON
fhir_resource_wrapper parse_resource(std::string_view json) {
    fhir_resource_wrapper wrapper;
    wrapper.json = std::string(json);

    auto type = find_json_field(json, "resourceType");
    if (!type.empty()) {
        wrapper.resource_type = std::string(type);
    }

    auto id = find_json_field(json, "id");
    if (!id.empty()) {
        wrapper.id = std::string(id);
    }

    // Look for meta.versionId
    size_t meta_pos = json.find("\"meta\"");
    if (meta_pos != std::string_view::npos) {
        // Find versionId within meta object
        size_t version_pos = json.find("\"versionId\"", meta_pos);
        if (version_pos != std::string_view::npos) {
            auto version = find_json_field(json.substr(version_pos - 1), "versionId");
            if (!version.empty()) {
                wrapper.version_id = std::string(version);
            }
        }
    }

    return wrapper;
}

}  // namespace

// =============================================================================
// fhir_client::impl
// =============================================================================

class fhir_client::impl {
public:
    explicit impl(const fhir_client_config& config)
        : config_(config), http_client_(create_http_client(http_client_config{})) {}

    impl(const fhir_client_config& config,
         std::unique_ptr<http_client_adapter> http_client)
        : config_(config), http_client_(std::move(http_client)) {}

    // Authentication
    void set_auth_provider(std::shared_ptr<security::auth_provider> provider) {
        std::lock_guard lock(auth_mutex_);
        auth_provider_ = std::move(provider);
    }

    std::shared_ptr<security::auth_provider> auth_provider() const {
        std::lock_guard lock(auth_mutex_);
        return auth_provider_;
    }

    // Build URL for resource
    std::string build_url(std::string_view resource_type,
                          std::string_view id = {}) const {
        std::string url = config_.base_url;
        if (!url.empty() && url.back() != '/') {
            url += '/';
        }
        url += resource_type;
        if (!id.empty()) {
            url += '/';
            url += id;
        }
        return url;
    }

    // Build headers for request
    std::vector<std::pair<std::string, std::string>> build_headers() const {
        std::vector<std::pair<std::string, std::string>> headers;
        headers.emplace_back("Accept", std::string(to_mime_type(config_.content_type)));
        headers.emplace_back("User-Agent", config_.user_agent);

        // Add authorization if available
        auto auth = auth_provider();
        if (auth && auth->is_authenticated()) {
            auto auth_result = auth->get_authorization_header();
            if (auth_result && !auth_result->empty()) {
                headers.emplace_back("Authorization", *auth_result);
            }
        }

        return headers;
    }

    // Execute request with retry
    auto execute_with_retry(http_request request)
        -> std::expected<http_response, emr_error> {
        auto start_time = std::chrono::steady_clock::now();
        emr_error last_error = emr_error::unknown;

        for (size_t attempt = 0; attempt <= config_.retry.max_retries; ++attempt) {
            if (attempt > 0) {
                // Wait with exponential backoff
                auto backoff = config_.retry.backoff_for(attempt - 1);
                std::this_thread::sleep_for(backoff);
                stats_.retried_requests.fetch_add(1, std::memory_order_relaxed);
            }

            stats_.total_requests.fetch_add(1, std::memory_order_relaxed);

            auto result = http_client_->execute(request);
            if (result) {
                auto& response = *result;

                // Check for server errors that might be retryable
                if (is_server_error(response.status)) {
                    last_error = status_to_error(response.status);
                    // Retry on server errors
                    continue;
                }

                // Handle rate limiting
                if (response.status == http_status::too_many_requests) {
                    last_error = emr_error::rate_limited;
                    // Could check Retry-After header here
                    continue;
                }

                // Success or client error (not retryable)
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                stats_.total_request_time.fetch_add(
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                        .count(),
                    std::memory_order_relaxed);

                if (response.is_success()) {
                    stats_.successful_requests.fetch_add(1,
                                                          std::memory_order_relaxed);
                } else {
                    stats_.failed_requests.fetch_add(1, std::memory_order_relaxed);
                }

                return result;
            }

            last_error = result.error();

            // Don't retry certain errors
            if (last_error == emr_error::invalid_configuration ||
                last_error == emr_error::bad_request ||
                last_error == emr_error::unauthorized ||
                last_error == emr_error::forbidden ||
                last_error == emr_error::cancelled) {
                break;
            }
        }

        stats_.failed_requests.fetch_add(1, std::memory_order_relaxed);

        if (last_error == emr_error::unknown) {
            return std::unexpected(emr_error::retry_exhausted);
        }
        return std::unexpected(last_error);
    }

    // Process response into result
    template <typename T>
    auto process_response(const http_response& response)
        -> std::expected<fhir_result<T>, emr_error>;

    // Configuration
    const fhir_client_config& config() const { return config_; }
    std::string_view base_url() const { return config_.base_url; }
    void set_timeout(std::chrono::seconds timeout) { config_.timeout = timeout; }
    void set_retry_policy(const retry_policy& policy) { config_.retry = policy; }

    // Statistics
    struct atomic_stats {
        std::atomic<size_t> total_requests{0};
        std::atomic<size_t> successful_requests{0};
        std::atomic<size_t> failed_requests{0};
        std::atomic<size_t> retried_requests{0};
        std::atomic<int64_t> total_request_time{0};
    };

    fhir_client::statistics get_statistics() const {
        fhir_client::statistics stats;
        stats.total_requests = stats_.total_requests.load(std::memory_order_relaxed);
        stats.successful_requests =
            stats_.successful_requests.load(std::memory_order_relaxed);
        stats.failed_requests =
            stats_.failed_requests.load(std::memory_order_relaxed);
        stats.retried_requests =
            stats_.retried_requests.load(std::memory_order_relaxed);
        stats.total_request_time = std::chrono::milliseconds{
            stats_.total_request_time.load(std::memory_order_relaxed)};
        return stats;
    }

    void reset_statistics() {
        stats_.total_requests.store(0, std::memory_order_relaxed);
        stats_.successful_requests.store(0, std::memory_order_relaxed);
        stats_.failed_requests.store(0, std::memory_order_relaxed);
        stats_.retried_requests.store(0, std::memory_order_relaxed);
        stats_.total_request_time.store(0, std::memory_order_relaxed);
    }

    http_client_adapter* http_client() { return http_client_.get(); }

private:
    fhir_client_config config_;
    std::unique_ptr<http_client_adapter> http_client_;
    std::shared_ptr<security::auth_provider> auth_provider_;
    mutable std::mutex auth_mutex_;
    atomic_stats stats_;
};

// Template specializations for process_response
template <>
auto fhir_client::impl::process_response<fhir_resource_wrapper>(
    const http_response& response)
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    if (!response.is_success()) {
        return std::unexpected(status_to_error(response.status));
    }

    fhir_result<fhir_resource_wrapper> result;
    result.status = response.status;
    result.value = parse_resource(response.body);

    if (auto etag = response.etag()) {
        result.etag = std::string(*etag);
    }
    if (auto location = response.location()) {
        result.location = std::string(*location);
    }
    if (auto last_mod = response.get_header("Last-Modified")) {
        result.last_modified = std::string(*last_mod);
    }

    return result;
}

template <>
auto fhir_client::impl::process_response<fhir_bundle>(
    const http_response& response)
    -> std::expected<fhir_result<fhir_bundle>, emr_error> {
    if (!response.is_success()) {
        return std::unexpected(status_to_error(response.status));
    }

    auto bundle = fhir_bundle::parse(response.body);
    if (!bundle) {
        return std::unexpected(emr_error::invalid_response);
    }

    fhir_result<fhir_bundle> result;
    result.status = response.status;
    result.value = std::move(*bundle);

    if (auto etag = response.etag()) {
        result.etag = std::string(*etag);
    }

    return result;
}

// =============================================================================
// fhir_client implementation
// =============================================================================

fhir_client::fhir_client(const fhir_client_config& config)
    : impl_(std::make_unique<impl>(config)) {}

fhir_client::fhir_client(const fhir_client_config& config,
                         std::unique_ptr<http_client_adapter> http_client)
    : impl_(std::make_unique<impl>(config, std::move(http_client))) {}

fhir_client::~fhir_client() = default;

fhir_client::fhir_client(fhir_client&&) noexcept = default;
fhir_client& fhir_client::operator=(fhir_client&&) noexcept = default;

// Authentication
void fhir_client::set_auth_provider(
    std::shared_ptr<security::auth_provider> provider) noexcept {
    impl_->set_auth_provider(std::move(provider));
}

std::shared_ptr<security::auth_provider> fhir_client::auth_provider() const noexcept {
    return impl_->auth_provider();
}

// Read operations
auto fhir_client::read(std::string_view resource_type, std::string_view id)
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    http_request request;
    request.method = http_method::get;
    request.url = impl_->build_url(resource_type, id);
    request.headers = impl_->build_headers();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_resource_wrapper>(*response);
}

auto fhir_client::vread(std::string_view resource_type, std::string_view id,
                        std::string_view version_id)
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    http_request request;
    request.method = http_method::get;
    request.url = impl_->build_url(resource_type, id) + "/_history/" +
                  std::string(version_id);
    request.headers = impl_->build_headers();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_resource_wrapper>(*response);
}

// Search operations
auto fhir_client::search(std::string_view resource_type,
                         const search_params& params)
    -> std::expected<fhir_result<fhir_bundle>, emr_error> {
    http_request request;
    request.method = http_method::get;
    request.url = impl_->build_url(resource_type);
    if (!params.empty()) {
        request.url += "?" + params.to_query_string();
    }
    request.headers = impl_->build_headers();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_bundle>(*response);
}

auto fhir_client::search_all(const search_params& params)
    -> std::expected<fhir_result<fhir_bundle>, emr_error> {
    http_request request;
    request.method = http_method::get;
    request.url = impl_->config().base_url;
    if (!request.url.empty() && request.url.back() != '/') {
        request.url += '/';
    }
    if (!params.empty()) {
        request.url += "?" + params.to_query_string();
    }
    request.headers = impl_->build_headers();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_bundle>(*response);
}

auto fhir_client::next_page(const fhir_bundle& bundle)
    -> std::expected<fhir_result<fhir_bundle>, emr_error> {
    auto next_url = bundle.next_url();
    if (!next_url) {
        return std::unexpected(emr_error::resource_not_found);
    }

    http_request request;
    request.method = http_method::get;
    request.url = std::string(*next_url);
    request.headers = impl_->build_headers();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_bundle>(*response);
}

auto fhir_client::search_all_pages(std::string_view resource_type,
                                   const search_params& params,
                                   size_t max_pages)
    -> std::expected<std::vector<fhir_resource_wrapper>, emr_error> {
    std::vector<fhir_resource_wrapper> all_resources;
    size_t page_count = 0;

    auto result = search(resource_type, params);
    if (!result) {
        return std::unexpected(result.error());
    }

    while (true) {
        ++page_count;

        for (const auto& entry : result->value.entries) {
            fhir_resource_wrapper wrapper;
            wrapper.resource_type = entry.resource_type;
            wrapper.id = entry.resource_id;
            wrapper.json = entry.resource;
            all_resources.push_back(std::move(wrapper));
        }

        // Check if we've reached max pages
        if (max_pages > 0 && page_count >= max_pages) {
            break;
        }

        // Check for next page
        if (!result->value.has_next()) {
            break;
        }

        result = next_page(result->value);
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    return all_resources;
}

// Create operations
auto fhir_client::create(std::string_view resource_type,
                         std::string_view resource)
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    http_request request;
    request.method = http_method::post;
    request.url = impl_->build_url(resource_type);
    request.headers = impl_->build_headers();
    request.add_header("Content-Type",
                       std::string(to_mime_type(impl_->config().content_type)));
    request.body = std::string(resource);
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_resource_wrapper>(*response);
}

auto fhir_client::create_if_none_exist(std::string_view resource_type,
                                       std::string_view resource,
                                       const search_params& search)
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    http_request request;
    request.method = http_method::post;
    request.url = impl_->build_url(resource_type);
    request.headers = impl_->build_headers();
    request.add_header("Content-Type",
                       std::string(to_mime_type(impl_->config().content_type)));
    request.add_header("If-None-Exist", search.to_query_string());
    request.body = std::string(resource);
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_resource_wrapper>(*response);
}

// Update operations
auto fhir_client::update(std::string_view resource_type, std::string_view id,
                         std::string_view resource)
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    http_request request;
    request.method = http_method::put;
    request.url = impl_->build_url(resource_type, id);
    request.headers = impl_->build_headers();
    request.add_header("Content-Type",
                       std::string(to_mime_type(impl_->config().content_type)));
    request.body = std::string(resource);
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_resource_wrapper>(*response);
}

auto fhir_client::update_if_match(std::string_view resource_type,
                                  std::string_view id,
                                  std::string_view resource,
                                  std::string_view etag)
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    http_request request;
    request.method = http_method::put;
    request.url = impl_->build_url(resource_type, id);
    request.headers = impl_->build_headers();
    request.add_header("Content-Type",
                       std::string(to_mime_type(impl_->config().content_type)));
    request.add_header("If-Match", std::string(etag));
    request.body = std::string(resource);
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    // Check for precondition failed (version mismatch)
    if (response->status == http_status::precondition_failed) {
        return std::unexpected(emr_error::conflict);
    }

    return impl_->process_response<fhir_resource_wrapper>(*response);
}

auto fhir_client::upsert(std::string_view resource_type, std::string_view id,
                         std::string_view resource)
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    // Upsert uses PUT with ID - creates if doesn't exist, updates if it does
    return update(resource_type, id, resource);
}

// Delete operations
auto fhir_client::remove(std::string_view resource_type, std::string_view id)
    -> std::expected<void, emr_error> {
    http_request request;
    request.method = http_method::delete_method;
    request.url = impl_->build_url(resource_type, id);
    request.headers = impl_->build_headers();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (!response->is_success() &&
        response->status != http_status::no_content &&
        response->status != http_status::gone) {
        return std::unexpected(status_to_error(response->status));
    }

    return {};
}

auto fhir_client::conditional_delete(std::string_view resource_type,
                                     const search_params& search)
    -> std::expected<void, emr_error> {
    http_request request;
    request.method = http_method::delete_method;
    request.url = impl_->build_url(resource_type);
    if (!search.empty()) {
        request.url += "?" + search.to_query_string();
    }
    request.headers = impl_->build_headers();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    if (!response->is_success() &&
        response->status != http_status::no_content) {
        return std::unexpected(status_to_error(response->status));
    }

    return {};
}

// Transaction/Batch operations
auto fhir_client::transaction(const fhir_bundle& bundle)
    -> std::expected<fhir_result<fhir_bundle>, emr_error> {
    http_request request;
    request.method = http_method::post;
    request.url = impl_->config().base_url;
    request.headers = impl_->build_headers();
    request.add_header("Content-Type",
                       std::string(to_mime_type(impl_->config().content_type)));
    request.body = bundle.to_json();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_bundle>(*response);
}

auto fhir_client::batch(const fhir_bundle& bundle)
    -> std::expected<fhir_result<fhir_bundle>, emr_error> {
    // Batch uses the same endpoint as transaction
    return transaction(bundle);
}

// Server capabilities
auto fhir_client::capabilities()
    -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error> {
    http_request request;
    request.method = http_method::get;
    request.url = impl_->config().base_url;
    if (!request.url.empty() && request.url.back() != '/') {
        request.url += '/';
    }
    request.url += "metadata";
    request.headers = impl_->build_headers();
    request.timeout = impl_->config().timeout;

    auto response = impl_->execute_with_retry(request);
    if (!response) {
        return std::unexpected(response.error());
    }

    return impl_->process_response<fhir_resource_wrapper>(*response);
}

auto fhir_client::supports_resource(std::string_view resource_type)
    -> std::expected<bool, emr_error> {
    auto cap_result = capabilities();
    if (!cap_result) {
        return std::unexpected(cap_result.error());
    }

    // Search for resource type in capability statement
    // This is a simplified check - real implementation would parse the JSON properly
    std::string search_pattern = "\"type\":\"";
    search_pattern += resource_type;
    search_pattern += "\"";

    return cap_result->value.json.find(search_pattern) != std::string::npos;
}

// Configuration
const fhir_client_config& fhir_client::config() const noexcept {
    return impl_->config();
}

std::string_view fhir_client::base_url() const noexcept {
    return impl_->base_url();
}

void fhir_client::set_timeout(std::chrono::seconds timeout) noexcept {
    impl_->set_timeout(timeout);
}

void fhir_client::set_retry_policy(const retry_policy& policy) noexcept {
    impl_->set_retry_policy(policy);
}

// Statistics
fhir_client::statistics fhir_client::get_statistics() const noexcept {
    return impl_->get_statistics();
}

void fhir_client::reset_statistics() noexcept {
    impl_->reset_statistics();
}

}  // namespace pacs::bridge::emr
