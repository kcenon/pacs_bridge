/**
 * @file emr_config.cpp
 * @brief EMR configuration implementation
 *
 * Implements validation and utility functions for EMR configuration.
 *
 * @see include/pacs/bridge/config/emr_config.h
 */

#include "pacs/bridge/config/emr_config.h"

#include <cstdlib>
#include <sstream>

namespace pacs::bridge::config {

std::vector<std::string> emr_config::validate() const {
    std::vector<std::string> errors;

    if (!enabled) {
        return errors;  // Disabled config is always valid
    }

    // Validate connection
    if (connection.base_url.empty()) {
        errors.push_back("emr.connection.base_url: Required field is empty");
    }
    if (connection.timeout.count() <= 0) {
        errors.push_back("emr.connection.timeout: Must be greater than 0");
    }
    if (connection.max_connections == 0) {
        errors.push_back("emr.connection.max_connections: Must be greater than 0");
    }

    // Validate authentication based on type
    switch (auth.type) {
        case security::auth_type::oauth2:
            if (auth.oauth2.token_url.empty()) {
                errors.push_back("emr.auth.oauth2.token_url: Required field is empty");
            }
            if (auth.oauth2.client_id.empty()) {
                errors.push_back("emr.auth.oauth2.client_id: Required field is empty");
            }
            if (auth.oauth2.client_secret.empty()) {
                errors.push_back("emr.auth.oauth2.client_secret: Required field is empty");
            }
            break;

        case security::auth_type::basic:
            if (auth.basic.username.empty()) {
                errors.push_back("emr.auth.basic.username: Required field is empty");
            }
            if (auth.basic.password.empty()) {
                errors.push_back("emr.auth.basic.password: Required field is empty");
            }
            break;

        case security::auth_type::api_key:
            if (auth.api_key.header_name.empty()) {
                errors.push_back("emr.auth.api_key.header_name: Required field is empty");
            }
            if (auth.api_key.key.empty()) {
                errors.push_back("emr.auth.api_key.key: Required field is empty");
            }
            break;

        case security::auth_type::none:
            // No validation needed
            break;
    }

    // Validate retry configuration
    if (retry.max_attempts == 0) {
        errors.push_back("emr.retry.max_attempts: Must be greater than 0");
    }
    if (retry.initial_backoff.count() <= 0) {
        errors.push_back("emr.retry.initial_backoff: Must be greater than 0");
    }
    if (retry.max_backoff.count() < retry.initial_backoff.count()) {
        errors.push_back("emr.retry.max_backoff: Must be >= initial_backoff");
    }
    if (retry.backoff_multiplier <= 0.0) {
        errors.push_back("emr.retry.backoff_multiplier: Must be greater than 0");
    }

    // Validate cache configuration
    if (cache.max_entries == 0) {
        errors.push_back("emr.cache.max_entries: Must be greater than 0");
    }

    return errors;
}

std::string substitute_env_vars(std::string_view str) {
    std::string result;
    result.reserve(str.size());

    size_t pos = 0;
    while (pos < str.size()) {
        // Look for ${
        if (pos + 1 < str.size() && str[pos] == '$' && str[pos + 1] == '{') {
            // Find closing }
            size_t end = str.find('}', pos + 2);
            if (end != std::string_view::npos) {
                // Extract variable name
                std::string var_name(str.substr(pos + 2, end - pos - 2));

                // Get environment variable
                const char* env_value = std::getenv(var_name.c_str());
                if (env_value != nullptr) {
                    result += env_value;
                } else {
                    // Keep original if env var not found
                    result += str.substr(pos, end - pos + 1);
                }
                pos = end + 1;
            } else {
                // No closing }, append as-is
                result += str[pos];
                ++pos;
            }
        } else {
            result += str[pos];
            ++pos;
        }
    }

    return result;
}

emr_config apply_env_substitution(const emr_config& config) {
    emr_config result = config;

    // Connection
    result.connection.base_url = substitute_env_vars(config.connection.base_url);

    // OAuth2 auth
    result.auth.oauth2.token_url = substitute_env_vars(config.auth.oauth2.token_url);
    result.auth.oauth2.client_id = substitute_env_vars(config.auth.oauth2.client_id);
    result.auth.oauth2.client_secret = substitute_env_vars(config.auth.oauth2.client_secret);

    // Basic auth
    result.auth.basic.username = substitute_env_vars(config.auth.basic.username);
    result.auth.basic.password = substitute_env_vars(config.auth.basic.password);

    // API key auth
    result.auth.api_key.header_name = substitute_env_vars(config.auth.api_key.header_name);
    result.auth.api_key.key = substitute_env_vars(config.auth.api_key.key);

    // Mapping
    result.mapping.patient_id_system = substitute_env_vars(config.mapping.patient_id_system);
    result.mapping.default_performer_id = substitute_env_vars(config.mapping.default_performer_id);
    result.mapping.accession_number_system = substitute_env_vars(config.mapping.accession_number_system);
    result.mapping.organization_id = substitute_env_vars(config.mapping.organization_id);

    return result;
}

}  // namespace pacs::bridge::config
