#ifndef PACS_BRIDGE_EMR_FHIR_CLIENT_H
#define PACS_BRIDGE_EMR_FHIR_CLIENT_H

/**
 * @file fhir_client.h
 * @brief FHIR R4 HTTP client for EMR integration
 *
 * Provides a FHIR R4 compliant HTTP client for connecting to external
 * EMR systems. Supports all standard FHIR REST operations including
 * read, search, create, update, and delete.
 *
 * Features:
 *   - Connection pooling for efficient resource usage
 *   - Automatic retry with exponential backoff
 *   - OAuth2/Basic authentication support
 *   - TLS 1.2/1.3 support
 *   - FHIR resource parsing
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 * @see https://www.hl7.org/fhir/http.html
 */

#include "emr_types.h"
#include "fhir_bundle.h"
#include "http_client_adapter.h"
#include "search_params.h"
#include "../security/auth_provider.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace pacs::bridge::emr {

/**
 * @brief FHIR operation result with metadata
 *
 * Contains the result of a FHIR operation along with HTTP metadata
 * such as status code, ETag, and location header.
 */
template <typename T>
struct fhir_result {
    /** The result value */
    T value;

    /** HTTP status code */
    http_status status{http_status::ok};

    /** ETag for version awareness */
    std::optional<std::string> etag;

    /** Location header (for created resources) */
    std::optional<std::string> location;

    /** Last-Modified header */
    std::optional<std::string> last_modified;
};

/**
 * @brief FHIR resource wrapper
 *
 * Wraps a FHIR resource JSON with its metadata.
 */
struct fhir_resource_wrapper {
    /** Resource type name */
    std::string resource_type;

    /** Resource ID */
    std::optional<std::string> id;

    /** Resource version ID */
    std::optional<std::string> version_id;

    /** Resource JSON */
    std::string json;

    /**
     * @brief Check if resource is valid (has content)
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !json.empty() && !resource_type.empty();
    }
};

/**
 * @brief FHIR R4 HTTP client
 *
 * Provides FHIR R4 REST API operations for EMR integration.
 * Supports read, search, create, update, and delete operations
 * with automatic retry and authentication handling.
 *
 * Thread-safe: All operations are thread-safe for concurrent use.
 *
 * @example Basic Usage
 * ```cpp
 * // Configure client
 * fhir_client_config config;
 * config.base_url = "https://emr.hospital.local/fhir";
 * config.timeout = std::chrono::seconds{30};
 *
 * // Create client
 * fhir_client client(config);
 *
 * // Read a patient
 * auto result = client.read("Patient", "123");
 * if (result) {
 *     auto& resource = result->value;
 *     std::cout << "Patient: " << resource.json << "\n";
 * } else {
 *     std::cerr << "Error: " << to_string(result.error()) << "\n";
 * }
 * ```
 *
 * @example Search with Parameters
 * ```cpp
 * auto params = search_params::for_patient()
 *     .name("Smith")
 *     .birthdate_before("2000-01-01")
 *     .count(20);
 *
 * auto result = client.search("Patient", params);
 * if (result) {
 *     std::cout << "Found " << result->value.total.value_or(0) << " patients\n";
 *     for (const auto& entry : result->value.entries) {
 *         std::cout << "  - " << entry.resource_id.value_or("?") << "\n";
 *     }
 * }
 * ```
 *
 * @example With OAuth2 Authentication
 * ```cpp
 * oauth2_config auth_config;
 * auth_config.token_url = "https://emr.hospital.local/oauth/token";
 * auth_config.client_id = "pacs_bridge";
 * auth_config.client_secret = get_secret();
 *
 * auto auth = std::make_shared<oauth2_auth_provider>(auth_config);
 *
 * fhir_client_config config;
 * config.base_url = "https://emr.hospital.local/fhir";
 *
 * fhir_client client(config);
 * client.set_auth_provider(auth);
 *
 * // Client will automatically use OAuth2 tokens
 * auto result = client.read("Patient", "123");
 * ```
 */
class fhir_client {
public:
    /**
     * @brief Construct FHIR client with configuration
     * @param config Client configuration
     */
    explicit fhir_client(const fhir_client_config& config);

    /**
     * @brief Construct FHIR client with configuration and HTTP adapter
     * @param config Client configuration
     * @param http_client HTTP client adapter to use
     */
    fhir_client(const fhir_client_config& config,
                std::unique_ptr<http_client_adapter> http_client);

    /**
     * @brief Destructor
     */
    ~fhir_client();

    // Non-copyable
    fhir_client(const fhir_client&) = delete;
    fhir_client& operator=(const fhir_client&) = delete;

    // Movable
    fhir_client(fhir_client&&) noexcept;
    fhir_client& operator=(fhir_client&&) noexcept;

    // =========================================================================
    // Authentication
    // =========================================================================

    /**
     * @brief Set authentication provider
     *
     * Sets the authentication provider for API requests.
     * The provider will be used to obtain Authorization headers.
     *
     * @param provider Authentication provider (OAuth2, Basic, etc.)
     */
    void set_auth_provider(
        std::shared_ptr<security::auth_provider> provider) noexcept;

    /**
     * @brief Get current authentication provider
     */
    [[nodiscard]] std::shared_ptr<security::auth_provider>
    auth_provider() const noexcept;

    // =========================================================================
    // Read Operations
    // =========================================================================

    /**
     * @brief Read a FHIR resource by ID
     *
     * Performs a FHIR read interaction to retrieve a single resource.
     *
     * @param resource_type FHIR resource type (e.g., "Patient")
     * @param id Resource ID
     * @return Resource with metadata or error
     *
     * @example
     * ```cpp
     * auto result = client.read("Patient", "123");
     * if (result) {
     *     process_patient(result->value.json);
     * }
     * ```
     */
    [[nodiscard]] auto read(std::string_view resource_type, std::string_view id)
        -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error>;

    /**
     * @brief Read a specific version of a resource (vread)
     *
     * @param resource_type FHIR resource type
     * @param id Resource ID
     * @param version_id Version ID
     * @return Resource with metadata or error
     */
    [[nodiscard]] auto vread(std::string_view resource_type,
                             std::string_view id,
                             std::string_view version_id)
        -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error>;

    // =========================================================================
    // Search Operations
    // =========================================================================

    /**
     * @brief Search for FHIR resources
     *
     * Performs a FHIR search interaction to find matching resources.
     *
     * @param resource_type FHIR resource type
     * @param params Search parameters
     * @return Bundle with search results or error
     *
     * @example
     * ```cpp
     * auto params = search_params::for_patient()
     *     .identifier("http://hospital.org/mrn", "123456")
     *     .count(10);
     *
     * auto result = client.search("Patient", params);
     * ```
     */
    [[nodiscard]] auto search(std::string_view resource_type,
                              const search_params& params)
        -> std::expected<fhir_result<fhir_bundle>, emr_error>;

    /**
     * @brief Search all resources (without type restriction)
     *
     * Performs a system-level search across all resource types.
     *
     * @param params Search parameters
     * @return Bundle with search results or error
     */
    [[nodiscard]] auto search_all(const search_params& params)
        -> std::expected<fhir_result<fhir_bundle>, emr_error>;

    /**
     * @brief Fetch next page of search results
     *
     * @param bundle Current bundle with next link
     * @return Next page bundle or error
     */
    [[nodiscard]] auto next_page(const fhir_bundle& bundle)
        -> std::expected<fhir_result<fhir_bundle>, emr_error>;

    /**
     * @brief Fetch all pages of search results
     *
     * Iteratively fetches all pages of search results.
     * Use with caution for large result sets.
     *
     * @param resource_type FHIR resource type
     * @param params Search parameters
     * @param max_pages Maximum number of pages to fetch (0 = unlimited)
     * @return Vector of all resources or error
     */
    [[nodiscard]] auto search_all_pages(std::string_view resource_type,
                                        const search_params& params,
                                        size_t max_pages = 0)
        -> std::expected<std::vector<fhir_resource_wrapper>, emr_error>;

    // =========================================================================
    // Create Operations
    // =========================================================================

    /**
     * @brief Create a new FHIR resource
     *
     * Performs a FHIR create interaction.
     *
     * @param resource_type FHIR resource type
     * @param resource Resource JSON
     * @return Created resource with metadata or error
     *
     * @example
     * ```cpp
     * std::string patient_json = R"({
     *     "resourceType": "Patient",
     *     "name": [{"family": "Smith", "given": ["John"]}]
     * })";
     *
     * auto result = client.create("Patient", patient_json);
     * if (result) {
     *     std::cout << "Created: " << result->location.value_or("") << "\n";
     * }
     * ```
     */
    [[nodiscard]] auto create(std::string_view resource_type,
                              std::string_view resource)
        -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error>;

    /**
     * @brief Create a resource conditionally
     *
     * Creates a resource only if no matching resource exists.
     *
     * @param resource_type FHIR resource type
     * @param resource Resource JSON
     * @param search Search criteria for existence check
     * @return Created resource with metadata or error
     */
    [[nodiscard]] auto create_if_none_exist(std::string_view resource_type,
                                            std::string_view resource,
                                            const search_params& search)
        -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error>;

    // =========================================================================
    // Update Operations
    // =========================================================================

    /**
     * @brief Update a FHIR resource
     *
     * Performs a FHIR update interaction to replace a resource.
     *
     * @param resource_type FHIR resource type
     * @param id Resource ID
     * @param resource Updated resource JSON
     * @return Updated resource with metadata or error
     *
     * @example
     * ```cpp
     * auto result = client.update("Patient", "123", updated_json);
     * if (result) {
     *     std::cout << "Updated, new version: "
     *               << result->etag.value_or("") << "\n";
     * }
     * ```
     */
    [[nodiscard]] auto update(std::string_view resource_type,
                              std::string_view id,
                              std::string_view resource)
        -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error>;

    /**
     * @brief Update a resource with version check
     *
     * Updates only if the resource version matches (optimistic locking).
     *
     * @param resource_type FHIR resource type
     * @param id Resource ID
     * @param resource Updated resource JSON
     * @param etag Expected ETag (version)
     * @return Updated resource or error
     */
    [[nodiscard]] auto update_if_match(std::string_view resource_type,
                                       std::string_view id,
                                       std::string_view resource,
                                       std::string_view etag)
        -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error>;

    /**
     * @brief Create or update a resource (upsert)
     *
     * Creates the resource if it doesn't exist, updates if it does.
     *
     * @param resource_type FHIR resource type
     * @param id Resource ID
     * @param resource Resource JSON
     * @return Resource with metadata or error
     */
    [[nodiscard]] auto upsert(std::string_view resource_type,
                              std::string_view id,
                              std::string_view resource)
        -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error>;

    // =========================================================================
    // Delete Operations
    // =========================================================================

    /**
     * @brief Delete a FHIR resource
     *
     * @param resource_type FHIR resource type
     * @param id Resource ID
     * @return Success or error
     */
    [[nodiscard]] auto remove(std::string_view resource_type,
                              std::string_view id)
        -> std::expected<void, emr_error>;

    /**
     * @brief Delete a resource conditionally
     *
     * Deletes resources matching the search criteria.
     *
     * @param resource_type FHIR resource type
     * @param search Search criteria
     * @return Success or error
     */
    [[nodiscard]] auto conditional_delete(std::string_view resource_type,
                                          const search_params& search)
        -> std::expected<void, emr_error>;

    // =========================================================================
    // Transaction/Batch Operations
    // =========================================================================

    /**
     * @brief Execute a transaction bundle
     *
     * Executes a bundle of operations as an atomic transaction.
     *
     * @param bundle Transaction bundle
     * @return Response bundle or error
     */
    [[nodiscard]] auto transaction(const fhir_bundle& bundle)
        -> std::expected<fhir_result<fhir_bundle>, emr_error>;

    /**
     * @brief Execute a batch bundle
     *
     * Executes a bundle of operations as independent requests.
     *
     * @param bundle Batch bundle
     * @return Response bundle or error
     */
    [[nodiscard]] auto batch(const fhir_bundle& bundle)
        -> std::expected<fhir_result<fhir_bundle>, emr_error>;

    // =========================================================================
    // Server Capabilities
    // =========================================================================

    /**
     * @brief Get server CapabilityStatement
     *
     * Retrieves the server's FHIR capability statement.
     *
     * @return CapabilityStatement JSON or error
     */
    [[nodiscard]] auto capabilities()
        -> std::expected<fhir_result<fhir_resource_wrapper>, emr_error>;

    /**
     * @brief Check if server supports a resource type
     *
     * @param resource_type Resource type to check
     * @return true if supported, false otherwise
     */
    [[nodiscard]] auto supports_resource(std::string_view resource_type)
        -> std::expected<bool, emr_error>;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] const fhir_client_config& config() const noexcept;

    /**
     * @brief Get the base URL
     */
    [[nodiscard]] std::string_view base_url() const noexcept;

    /**
     * @brief Set request timeout
     * @param timeout New timeout duration
     */
    void set_timeout(std::chrono::seconds timeout) noexcept;

    /**
     * @brief Set retry policy
     * @param policy New retry policy
     */
    void set_retry_policy(const retry_policy& policy) noexcept;

    // =========================================================================
    // Statistics
    // =========================================================================

    /**
     * @brief Client statistics
     */
    struct statistics {
        size_t total_requests{0};
        size_t successful_requests{0};
        size_t failed_requests{0};
        size_t retried_requests{0};
        std::chrono::milliseconds total_request_time{0};
    };

    /**
     * @brief Get client statistics
     */
    [[nodiscard]] statistics get_statistics() const noexcept;

    /**
     * @brief Reset statistics
     */
    void reset_statistics() noexcept;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace pacs::bridge::emr

#endif  // PACS_BRIDGE_EMR_FHIR_CLIENT_H
