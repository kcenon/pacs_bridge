#ifndef PACS_BRIDGE_SECURITY_SMART_CONFIGURATION_H
#define PACS_BRIDGE_SECURITY_SMART_CONFIGURATION_H

/**
 * @file smart_configuration.h
 * @brief Smart-on-FHIR configuration types for PACS Bridge
 *
 * Provides structures for Smart-on-FHIR discovery configuration.
 * Smart-on-FHIR extends OAuth2 for healthcare applications, providing
 * standardized authorization for FHIR-based EMR systems.
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/103
 * @see https://github.com/kcenon/pacs_bridge/issues/113
 * @see https://hl7.org/fhir/smart-app-launch/
 */

#include <optional>
#include <string>
#include <vector>

namespace pacs::bridge::security {

// =============================================================================
// Smart-on-FHIR Capabilities
// =============================================================================

/**
 * @brief Smart-on-FHIR capability flags
 *
 * Indicates which features the FHIR server supports.
 */
enum class smart_capability {
    /** Supports launch from EHR */
    launch_ehr,

    /** Supports standalone launch */
    launch_standalone,

    /** Supports authorization code flow */
    authorize_post,

    /** Supports client_credentials grant */
    client_public,

    /** Supports confidential clients */
    client_confidential_symmetric,

    /** Supports asymmetric key authentication */
    client_confidential_asymmetric,

    /** Supports single sign-on */
    sso_openid_connect,

    /** Supports permission scopes v1 */
    permission_v1,

    /** Supports permission scopes v2 */
    permission_v2,

    /** Supports PKCE */
    code_challenge,

    /** Supports PKCE S256 method */
    code_challenge_s256,

    /** Supports context parameters */
    context_ehr_patient,

    /** Supports encounter context */
    context_ehr_encounter,

    /** Supports passthrough parameters */
    context_passthrough_banner,

    /** Supports style parameters */
    context_passthrough_style
};

/**
 * @brief Convert smart_capability to string
 */
[[nodiscard]] constexpr const char* to_string(smart_capability cap) noexcept {
    switch (cap) {
        case smart_capability::launch_ehr:
            return "launch-ehr";
        case smart_capability::launch_standalone:
            return "launch-standalone";
        case smart_capability::authorize_post:
            return "authorize-post";
        case smart_capability::client_public:
            return "client-public";
        case smart_capability::client_confidential_symmetric:
            return "client-confidential-symmetric";
        case smart_capability::client_confidential_asymmetric:
            return "client-confidential-asymmetric";
        case smart_capability::sso_openid_connect:
            return "sso-openid-connect";
        case smart_capability::permission_v1:
            return "permission-v1";
        case smart_capability::permission_v2:
            return "permission-v2";
        case smart_capability::code_challenge:
            return "code-challenge";
        case smart_capability::code_challenge_s256:
            return "code-challenge-s256";
        case smart_capability::context_ehr_patient:
            return "context-ehr-patient";
        case smart_capability::context_ehr_encounter:
            return "context-ehr-encounter";
        case smart_capability::context_passthrough_banner:
            return "context-passthrough-banner";
        case smart_capability::context_passthrough_style:
            return "context-passthrough-style";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse smart_capability from string
 * @param str Capability string from discovery response
 * @return Parsed capability or nullopt if unrecognized
 */
[[nodiscard]] inline std::optional<smart_capability>
parse_smart_capability(std::string_view str) noexcept {
    if (str == "launch-ehr") return smart_capability::launch_ehr;
    if (str == "launch-standalone") return smart_capability::launch_standalone;
    if (str == "authorize-post") return smart_capability::authorize_post;
    if (str == "client-public") return smart_capability::client_public;
    if (str == "client-confidential-symmetric")
        return smart_capability::client_confidential_symmetric;
    if (str == "client-confidential-asymmetric")
        return smart_capability::client_confidential_asymmetric;
    if (str == "sso-openid-connect") return smart_capability::sso_openid_connect;
    if (str == "permission-v1") return smart_capability::permission_v1;
    if (str == "permission-v2") return smart_capability::permission_v2;
    if (str == "code-challenge") return smart_capability::code_challenge;
    if (str == "code-challenge-s256") return smart_capability::code_challenge_s256;
    if (str == "context-ehr-patient") return smart_capability::context_ehr_patient;
    if (str == "context-ehr-encounter") return smart_capability::context_ehr_encounter;
    if (str == "context-passthrough-banner")
        return smart_capability::context_passthrough_banner;
    if (str == "context-passthrough-style")
        return smart_capability::context_passthrough_style;
    return std::nullopt;
}

// =============================================================================
// Smart Configuration Structure
// =============================================================================

/**
 * @brief Smart-on-FHIR configuration from discovery endpoint
 *
 * Contains the OAuth2 endpoints and capabilities discovered from
 * the FHIR server's .well-known/smart-configuration endpoint.
 *
 * @example Discovery Usage
 * ```cpp
 * // Typically populated via smart_discovery::discover()
 * smart_configuration config;
 * config.issuer = "https://emr.hospital.local/fhir";
 * config.authorization_endpoint = "https://emr.hospital.local/oauth/authorize";
 * config.token_endpoint = "https://emr.hospital.local/oauth/token";
 * config.capabilities = {"launch-ehr", "client-confidential-symmetric"};
 * config.scopes_supported = {"openid", "patient/*.read"};
 *
 * if (config.supports_capability(smart_capability::client_confidential_symmetric)) {
 *     // Can use client credentials flow
 * }
 * ```
 *
 * @see https://hl7.org/fhir/smart-app-launch/conformance.html
 */
struct smart_configuration {
    /** FHIR server issuer URL */
    std::string issuer;

    /** JWKS URI for token validation */
    std::optional<std::string> jwks_uri;

    /** OAuth2 authorization endpoint */
    std::string authorization_endpoint;

    /** OAuth2 token endpoint (required) */
    std::string token_endpoint;

    /** Token revocation endpoint */
    std::optional<std::string> revocation_endpoint;

    /** Token introspection endpoint */
    std::optional<std::string> introspection_endpoint;

    /** User info endpoint (OIDC) */
    std::optional<std::string> userinfo_endpoint;

    /** Dynamic client registration endpoint */
    std::optional<std::string> registration_endpoint;

    /** Management endpoint for registered clients */
    std::optional<std::string> management_endpoint;

    /** Supported capabilities (as strings from discovery) */
    std::vector<std::string> capabilities;

    /** Supported OAuth2 scopes */
    std::vector<std::string> scopes_supported;

    /** Supported response types */
    std::vector<std::string> response_types_supported;

    /** Supported grant types */
    std::vector<std::string> grant_types_supported;

    /** Supported code challenge methods (PKCE) */
    std::vector<std::string> code_challenge_methods_supported;

    /** Supported token endpoint auth methods */
    std::vector<std::string> token_endpoint_auth_methods_supported;

    /**
     * @brief Check if a specific capability is supported
     * @param cap Capability to check
     * @return true if the capability is listed
     */
    [[nodiscard]] bool supports_capability(smart_capability cap) const noexcept {
        std::string_view cap_str = to_string(cap);
        for (const auto& c : capabilities) {
            if (c == cap_str) return true;
        }
        return false;
    }

    /**
     * @brief Check if a specific capability string is supported
     * @param cap_name Capability name string
     * @return true if the capability is listed
     */
    [[nodiscard]] bool supports_capability(std::string_view cap_name) const noexcept {
        for (const auto& c : capabilities) {
            if (c == cap_name) return true;
        }
        return false;
    }

    /**
     * @brief Check if a specific scope is supported
     * @param scope Scope string to check
     * @return true if the scope is supported
     */
    [[nodiscard]] bool supports_scope(std::string_view scope) const noexcept {
        for (const auto& s : scopes_supported) {
            if (s == scope) return true;
        }
        return false;
    }

    /**
     * @brief Check if client_credentials grant is supported
     */
    [[nodiscard]] bool supports_client_credentials() const noexcept {
        for (const auto& g : grant_types_supported) {
            if (g == "client_credentials") return true;
        }
        // Also check capabilities
        return supports_capability(smart_capability::client_confidential_symmetric) ||
               supports_capability(smart_capability::client_confidential_asymmetric);
    }

    /**
     * @brief Check if the configuration has required fields
     * @return true if token_endpoint is present
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !token_endpoint.empty();
    }

    /**
     * @brief Check if PKCE is supported
     */
    [[nodiscard]] bool supports_pkce() const noexcept {
        return supports_capability(smart_capability::code_challenge) ||
               supports_capability(smart_capability::code_challenge_s256) ||
               !code_challenge_methods_supported.empty();
    }

    /**
     * @brief Check if PKCE S256 method is supported
     */
    [[nodiscard]] bool supports_pkce_s256() const noexcept {
        if (supports_capability(smart_capability::code_challenge_s256)) {
            return true;
        }
        for (const auto& m : code_challenge_methods_supported) {
            if (m == "S256") return true;
        }
        return false;
    }
};

// =============================================================================
// Discovery Configuration
// =============================================================================

/**
 * @brief Configuration for Smart-on-FHIR discovery
 *
 * Settings for discovering OAuth2 endpoints from a FHIR server.
 */
struct smart_discovery_config {
    /** FHIR server base URL */
    std::string fhir_base_url;

    /** Request timeout for discovery */
    std::chrono::seconds request_timeout{30};

    /** Whether to cache discovery results */
    bool cache_enabled{true};

    /** Cache TTL for discovery results */
    std::chrono::seconds cache_ttl{3600};

    /** Verify SSL certificate of FHIR server */
    bool verify_ssl{true};

    /**
     * @brief Get the discovery endpoint URL
     * @return Full URL to .well-known/smart-configuration
     */
    [[nodiscard]] std::string discovery_url() const {
        std::string url = fhir_base_url;
        // Remove trailing slash if present
        if (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        return url + "/.well-known/smart-configuration";
    }

    /**
     * @brief Validate configuration
     */
    [[nodiscard]] bool is_valid() const noexcept {
        return !fhir_base_url.empty() && request_timeout.count() > 0;
    }
};

}  // namespace pacs::bridge::security

#endif  // PACS_BRIDGE_SECURITY_SMART_CONFIGURATION_H
