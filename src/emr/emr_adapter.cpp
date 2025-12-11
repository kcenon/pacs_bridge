/**
 * @file emr_adapter.cpp
 * @brief EMR adapter factory implementation
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/107
 */

#include "pacs/bridge/emr/emr_adapter.h"
#include "pacs/bridge/emr/adapters/generic_fhir_adapter.h"

#include <algorithm>
#include <cctype>

namespace pacs::bridge::emr {

// =============================================================================
// Vendor Parsing
// =============================================================================

emr_vendor parse_emr_vendor(std::string_view vendor_str) noexcept {
    // Convert to lowercase for comparison
    std::string lower;
    lower.reserve(vendor_str.size());
    for (char c : vendor_str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (lower == "generic" || lower == "generic_fhir" || lower == "fhir") {
        return emr_vendor::generic_fhir;
    }
    if (lower == "epic") {
        return emr_vendor::epic;
    }
    if (lower == "cerner" || lower == "oracle" || lower == "oracle_health") {
        return emr_vendor::cerner;
    }
    if (lower == "meditech") {
        return emr_vendor::meditech;
    }
    if (lower == "allscripts") {
        return emr_vendor::allscripts;
    }

    return emr_vendor::unknown;
}

// =============================================================================
// Factory Function Implementation
// =============================================================================

std::expected<std::unique_ptr<emr_adapter>, adapter_error>
create_emr_adapter(const emr_adapter_config& config) {
    // Validate configuration
    if (!config.is_valid()) {
        return std::unexpected(adapter_error::invalid_configuration);
    }

    // Create adapter based on vendor
    switch (config.vendor) {
        case emr_vendor::generic_fhir:
            return std::make_unique<generic_fhir_adapter>(config);

        case emr_vendor::epic:
            // TODO: Epic-specific adapter (Phase 5.2+)
            // For now, fall through to generic
            return std::make_unique<generic_fhir_adapter>(config);

        case emr_vendor::cerner:
            // TODO: Cerner-specific adapter (Phase 5.2+)
            // For now, fall through to generic
            return std::make_unique<generic_fhir_adapter>(config);

        case emr_vendor::meditech:
        case emr_vendor::allscripts:
            // Not yet implemented
            return std::unexpected(adapter_error::not_supported);

        case emr_vendor::unknown:
        default:
            return std::unexpected(adapter_error::invalid_vendor);
    }
}

std::expected<std::unique_ptr<emr_adapter>, adapter_error>
create_emr_adapter(emr_vendor vendor, std::string_view base_url) {
    emr_adapter_config config;
    config.vendor = vendor;
    config.base_url = std::string(base_url);

    // Minimal config - may need additional setup
    return create_emr_adapter(config);
}

}  // namespace pacs::bridge::emr
