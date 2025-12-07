/**
 * @file bridge_config.cpp
 * @brief Implementation of bridge configuration validation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/20
 */

#include "pacs/bridge/config/bridge_config.h"

#include <format>

namespace pacs::bridge::config {

std::vector<validation_error_info> bridge_config::validate() const {
    std::vector<validation_error_info> errors;

    // Validate server name
    if (name.empty()) {
        errors.push_back(
            {.field_path = "name",
             .message = "Server name cannot be empty",
             .actual_value = std::nullopt,
             .expected = "Non-empty string"});
    }

    // Validate HL7 configuration
    if (!hl7.listener.is_valid()) {
        if (hl7.listener.port == 0) {
            errors.push_back({.field_path = "hl7.listener.port",
                              .message = "MLLP listener port must be > 0",
                              .actual_value = "0",
                              .expected = "1-65535"});
        }
        if (hl7.listener.max_connections == 0) {
            errors.push_back(
                {.field_path = "hl7.listener.max_connections",
                 .message = "Maximum connections must be > 0",
                 .actual_value = "0",
                 .expected = "Positive integer"});
        }
        if (hl7.listener.max_message_size == 0) {
            errors.push_back({.field_path = "hl7.listener.max_message_size",
                              .message = "Maximum message size must be > 0",
                              .actual_value = "0",
                              .expected = "Positive integer"});
        }
        if (hl7.listener.tls.enabled &&
            !hl7.listener.tls.is_valid_for_server()) {
            errors.push_back(
                {.field_path = "hl7.listener.tls",
                 .message = "TLS configuration is invalid for server",
                 .actual_value = std::nullopt,
                 .expected = "Valid TLS server configuration"});
        }
    }

    // Validate outbound destinations
    for (size_t i = 0; i < hl7.outbound_destinations.size(); ++i) {
        const auto& dest = hl7.outbound_destinations[i];
        const std::string prefix =
            std::format("hl7.outbound_destinations[{}].", i);

        if (dest.name.empty()) {
            errors.push_back({.field_path = prefix + "name",
                              .message = "Destination name cannot be empty",
                              .actual_value = std::nullopt,
                              .expected = "Non-empty string"});
        }
        if (dest.host.empty()) {
            errors.push_back({.field_path = prefix + "host",
                              .message = "Destination host cannot be empty",
                              .actual_value = std::nullopt,
                              .expected = "Hostname or IP address"});
        }
        if (dest.port == 0) {
            errors.push_back({.field_path = prefix + "port",
                              .message = "Destination port must be > 0",
                              .actual_value = "0",
                              .expected = "1-65535"});
        }
        if (dest.tls.enabled && !dest.tls.is_valid_for_client()) {
            errors.push_back(
                {.field_path = prefix + "tls",
                 .message = "TLS configuration is invalid for client",
                 .actual_value = std::nullopt,
                 .expected = "Valid TLS client configuration"});
        }
    }

    // Validate FHIR configuration (only if enabled)
    if (fhir.enabled) {
        if (!fhir.server.is_valid()) {
            if (fhir.server.port == 0) {
                errors.push_back({.field_path = "fhir.server.port",
                                  .message = "FHIR server port must be > 0",
                                  .actual_value = "0",
                                  .expected = "1-65535"});
            }
            if (fhir.server.base_path.empty()) {
                errors.push_back(
                    {.field_path = "fhir.server.base_path",
                     .message = "FHIR server base path cannot be empty",
                     .actual_value = std::nullopt,
                     .expected = "Path like /fhir/r4"});
            }
            if (fhir.server.max_connections == 0) {
                errors.push_back(
                    {.field_path = "fhir.server.max_connections",
                     .message = "Maximum connections must be > 0",
                     .actual_value = "0",
                     .expected = "Positive integer"});
            }
            if (fhir.server.page_size == 0) {
                errors.push_back({.field_path = "fhir.server.page_size",
                                  .message = "Page size must be > 0",
                                  .actual_value = "0",
                                  .expected = "Positive integer"});
            }
            if (fhir.server.tls.enabled &&
                !fhir.server.tls.is_valid_for_server()) {
                errors.push_back(
                    {.field_path = "fhir.server.tls",
                     .message = "TLS configuration is invalid for server",
                     .actual_value = std::nullopt,
                     .expected = "Valid TLS server configuration"});
            }
        }
    }

    // Validate pacs_system configuration
    if (!pacs.is_valid()) {
        if (pacs.host.empty()) {
            errors.push_back({.field_path = "pacs.host",
                              .message = "pacs_system host cannot be empty",
                              .actual_value = std::nullopt,
                              .expected = "Hostname or IP address"});
        }
        if (pacs.port == 0) {
            errors.push_back({.field_path = "pacs.port",
                              .message = "pacs_system port must be > 0",
                              .actual_value = "0",
                              .expected = "1-65535"});
        }
        if (pacs.ae_title.empty()) {
            errors.push_back({.field_path = "pacs.ae_title",
                              .message = "AE title cannot be empty",
                              .actual_value = std::nullopt,
                              .expected = "AE title string"});
        }
        if (pacs.called_ae.empty()) {
            errors.push_back({.field_path = "pacs.called_ae",
                              .message = "Called AE title cannot be empty",
                              .actual_value = std::nullopt,
                              .expected = "AE title string"});
        }
    }

    // Validate routing rules
    for (size_t i = 0; i < routing_rules.size(); ++i) {
        const auto& rule = routing_rules[i];
        const std::string prefix = std::format("routing_rules[{}].", i);

        if (rule.name.empty()) {
            errors.push_back({.field_path = prefix + "name",
                              .message = "Rule name cannot be empty",
                              .actual_value = std::nullopt,
                              .expected = "Non-empty string"});
        }
        if (rule.message_type_pattern.empty() && rule.source_pattern.empty()) {
            errors.push_back(
                {.field_path = prefix + "message_type_pattern",
                 .message = "At least one of message_type_pattern or "
                            "source_pattern must be specified",
                 .actual_value = std::nullopt,
                 .expected = "Pattern string"});
        }
        if (rule.destination.empty()) {
            errors.push_back({.field_path = prefix + "destination",
                              .message = "Rule destination cannot be empty",
                              .actual_value = std::nullopt,
                              .expected = "Handler name"});
        }
    }

    // Validate queue configuration
    if (!queue.is_valid()) {
        if (queue.max_queue_size == 0) {
            errors.push_back({.field_path = "queue.max_queue_size",
                              .message = "Maximum queue size must be > 0",
                              .actual_value = "0",
                              .expected = "Positive integer"});
        }
        if (queue.max_retry_count == 0) {
            errors.push_back({.field_path = "queue.max_retry_count",
                              .message = "Maximum retry count must be > 0",
                              .actual_value = "0",
                              .expected = "Positive integer"});
        }
        if (queue.worker_count == 0) {
            errors.push_back({.field_path = "queue.worker_count",
                              .message = "Worker count must be > 0",
                              .actual_value = "0",
                              .expected = "Positive integer"});
        }
        if (queue.retry_backoff_multiplier <= 0.0) {
            errors.push_back(
                {.field_path = "queue.retry_backoff_multiplier",
                 .message = "Retry backoff multiplier must be > 0",
                 .actual_value = std::to_string(queue.retry_backoff_multiplier),
                 .expected = "Positive number"});
        }
    }

    // Validate patient cache configuration
    if (!patient_cache.is_valid()) {
        if (patient_cache.max_entries == 0) {
            errors.push_back({.field_path = "patient_cache.max_entries",
                              .message = "Maximum cache entries must be > 0",
                              .actual_value = "0",
                              .expected = "Positive integer"});
        }
    }

    // Validate logging configuration
    if (!logging.is_valid()) {
        if (logging.format != "json" && logging.format != "text") {
            errors.push_back(
                {.field_path = "logging.format",
                 .message = "Log format must be 'json' or 'text'",
                 .actual_value = logging.format,
                 .expected = "'json' or 'text'"});
        }
    }

    return errors;
}

}  // namespace pacs::bridge::config
