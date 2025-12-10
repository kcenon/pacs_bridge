#ifndef PACS_BRIDGE_SECURITY_SMART_DISCOVERY_H
#define PACS_BRIDGE_SECURITY_SMART_DISCOVERY_H

/**
 * @file smart_discovery.h
 * @brief Smart-on-FHIR configuration discovery for PACS Bridge
 *
 * Provides functionality to discover OAuth2 endpoints and capabilities
 * from FHIR servers using the Smart-on-FHIR specification.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/103
 * @see https://github.com/kcenon/pacs_bridge/issues/113
 * @see https://hl7.org/fhir/smart-app-launch/conformance.html
 */

#include "oauth2_types.h"
#include "smart_configuration.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace pacs::bridge::security {

/**
 * @brief HTTP GET callback type for discovery requests
 *
 * Returns JSON response body or error on failure.
 */
using http_get_callback = std::function<std::expected<std::string, oauth2_error>(
    std::string_view url,
    std::chrono::seconds timeout
)>;

/**
 * @brief Smart-on-FHIR configuration discovery
 *
 * Discovers OAuth2 endpoints and capabilities from a FHIR server's
 * .well-known/smart-configuration endpoint.
 *
 * @example Discovery Usage
 * ```cpp
 * smart_discovery_config config;
 * config.fhir_base_url = "https://emr.hospital.local/fhir/r4";
 *
 * smart_discovery discovery(config);
 *
 * auto result = discovery.discover();
 * if (result) {
 *     auto& smart_config = result.value();
 *     std::cout << "Token endpoint: " << smart_config.token_endpoint << "\n";
 *
 *     if (smart_config.supports_client_credentials()) {
 *         // Can use client credentials flow
 *     }
 * }
 * ```
 *
 * @example With Caching
 * ```cpp
 * smart_discovery discovery(config);
 *
 * // First call fetches from server
 * auto result1 = discovery.discover();
 *
 * // Second call returns cached result
 * auto result2 = discovery.discover();
 *
 * // Force refresh from server
 * auto result3 = discovery.refresh();
 * ```
 */
class smart_discovery {
public:
    /**
     * @brief Construct with configuration
     * @param config Discovery configuration
     */
    explicit smart_discovery(const smart_discovery_config& config);

    /**
     * @brief Construct with custom HTTP client
     * @param config Discovery configuration
     * @param http_client HTTP GET callback
     */
    smart_discovery(const smart_discovery_config& config,
                    http_get_callback http_client);

    /**
     * @brief Destructor
     */
    ~smart_discovery();

    // Non-copyable
    smart_discovery(const smart_discovery&) = delete;
    smart_discovery& operator=(const smart_discovery&) = delete;

    // Movable
    smart_discovery(smart_discovery&&) noexcept;
    smart_discovery& operator=(smart_discovery&&) noexcept;

    // =========================================================================
    // Discovery Operations
    // =========================================================================

    /**
     * @brief Discover Smart-on-FHIR configuration
     *
     * Fetches and parses the .well-known/smart-configuration document.
     * Returns cached result if available and not expired.
     *
     * @return Smart configuration or error
     */
    [[nodiscard]] auto discover()
        -> std::expected<smart_configuration, oauth2_error>;

    /**
     * @brief Force refresh of discovery
     *
     * Fetches configuration from server even if cache is valid.
     *
     * @return Smart configuration or error
     */
    [[nodiscard]] auto refresh()
        -> std::expected<smart_configuration, oauth2_error>;

    /**
     * @brief Get cached configuration if available
     *
     * Returns the cached configuration without making a request.
     *
     * @return Cached configuration or nullopt
     */
    [[nodiscard]] std::optional<smart_configuration> cached() const noexcept;

    /**
     * @brief Clear cached configuration
     */
    void clear_cache() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Get current configuration
     * @return Configuration reference
     */
    [[nodiscard]] const smart_discovery_config& config() const noexcept;

    /**
     * @brief Update configuration
     *
     * Clears cache if FHIR base URL changed.
     *
     * @param config New configuration
     */
    void update_config(const smart_discovery_config& config);

    // =========================================================================
    // Static Utilities
    // =========================================================================

    /**
     * @brief Parse Smart configuration from JSON
     *
     * Parses the .well-known/smart-configuration JSON document.
     *
     * @param json JSON string
     * @return Parsed configuration or error
     */
    [[nodiscard]] static auto parse_configuration(std::string_view json)
        -> std::expected<smart_configuration, oauth2_error>;

    /**
     * @brief Build discovery URL from FHIR base URL
     *
     * @param fhir_base_url FHIR server base URL
     * @return Full discovery endpoint URL
     */
    [[nodiscard]] static std::string
    build_discovery_url(std::string_view fhir_base_url);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

// =============================================================================
// Convenience Functions
// =============================================================================

/**
 * @brief Discover Smart configuration from FHIR server
 *
 * One-shot discovery without caching.
 *
 * @param fhir_base_url FHIR server base URL
 * @param timeout Request timeout (default 30s)
 * @return Smart configuration or error
 */
[[nodiscard]] std::expected<smart_configuration, oauth2_error>
discover_smart_configuration(std::string_view fhir_base_url,
                             std::chrono::seconds timeout = std::chrono::seconds{30});

/**
 * @brief Discover Smart configuration with custom HTTP client
 *
 * @param fhir_base_url FHIR server base URL
 * @param http_client HTTP GET callback
 * @param timeout Request timeout
 * @return Smart configuration or error
 */
[[nodiscard]] std::expected<smart_configuration, oauth2_error>
discover_smart_configuration(std::string_view fhir_base_url,
                             http_get_callback http_client,
                             std::chrono::seconds timeout = std::chrono::seconds{30});

/**
 * @brief Create OAuth2 config from Smart discovery
 *
 * Populates an oauth2_config from Smart-on-FHIR discovery results.
 *
 * @param smart Smart configuration from discovery
 * @param client_id Client identifier
 * @param client_secret Client secret
 * @param scopes Requested scopes
 * @return Populated OAuth2 configuration
 */
[[nodiscard]] oauth2_config
create_oauth2_config_from_smart(const smart_configuration& smart,
                                std::string_view client_id,
                                std::string_view client_secret,
                                const std::vector<std::string>& scopes);

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_SMART_DISCOVERY_H
