// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file pacs_bridge.cppm
 * @brief Primary C++20 module for pacs_bridge.
 *
 * This is the main module interface for the pacs_bridge library.
 * It aggregates all module partitions to provide a single import point.
 *
 * Usage:
 * @code
 * import kcenon.pacs_bridge;
 *
 * using namespace kcenon::pacs_bridge;
 *
 * // Create and configure bridge server
 * auto config = BridgeConfig::load("config.yaml");
 * BridgeServer server(config);
 * server.start();
 * @endcode
 *
 * Module Structure:
 * - kcenon.pacs_bridge:hl7 - HL7 v2.x message handling (9 handlers)
 * - kcenon.pacs_bridge:mllp - MLLP protocol transport
 * - kcenon.pacs_bridge:fhir - FHIR R4 resources (conditional)
 * - kcenon.pacs_bridge:emr - EMR integration client
 * - kcenon.pacs_bridge:mapping - Data transformation mappers
 * - kcenon.pacs_bridge:router - Message routing
 * - kcenon.pacs_bridge:server - Bridge server core
 *
 * Dependencies:
 * - kcenon.common (required) - Result<T>, error handling, interfaces
 * - kcenon.network (required) - Network communication
 * - kcenon.messaging (required) - Message bus
 * - kcenon.pacs (required) - DICOM integration
 * - kcenon.database (optional) - Persistence
 * - kcenon.monitoring (optional) - Metrics and health checks
 */

export module kcenon.pacs_bridge;

// =============================================================================
// Re-export Dependent Modules
// =============================================================================

export import kcenon.common;

// =============================================================================
// Protocol Modules (Tier 1)
// =============================================================================

// HL7 v2.x handling - 9 handlers for different message types
export import :hl7;

// MLLP transport protocol
export import :mllp;

// FHIR R4 resources (conditional compilation)
#ifdef PACS_BRIDGE_BUILD_FHIR
export import :fhir;
#endif

// =============================================================================
// Integration Modules (Tier 2)
// =============================================================================

// EMR integration client
export import :emr;

// Data transformation mappers
export import :mapping;

// =============================================================================
// Server Modules (Tier 3)
// =============================================================================

// Message routing
export import :router;

// Bridge server core (includes config, monitoring)
export import :server;

// =============================================================================
// Module-Level API
// =============================================================================

export namespace kcenon::pacs_bridge {

/**
 * @brief Version information for pacs_bridge module.
 */
struct module_version {
    static constexpr int major = 0;
    static constexpr int minor = 1;
    static constexpr int patch = 0;
    static constexpr int tweak = 0;
    static constexpr const char* string = "0.1.0.0";
    static constexpr const char* module_name = "kcenon.pacs_bridge";
};

/**
 * @brief Get the pacs_bridge module version string
 * @return Version string in "major.minor.patch.tweak" format
 */
constexpr const char* version() noexcept {
    return module_version::string;
}

} // namespace kcenon::pacs_bridge
