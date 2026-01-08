// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file pacs_bridge-server.cppm
 * @brief Bridge server core module partition for pacs_bridge.
 *
 * This module partition exports the main bridge server orchestration layer
 * including configuration, monitoring, and health checking functionality.
 *
 * Components:
 * - BridgeServer: Main orchestration server
 * - Configuration: Config loading, management, and admin interface
 * - Monitoring: Health checks, metrics collection
 *
 * Usage:
 * @code
 * import kcenon.pacs_bridge;
 *
 * using namespace pacs::bridge;
 *
 * // Load configuration and start bridge
 * auto config_result = config::config_loader::load("config.yaml");
 * if (config_result.is_ok()) {
 *     bridge_server server(config_result.value());
 *     server.start();
 * }
 * @endcode
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/175
 */

export module kcenon.pacs_bridge:server;

import kcenon.common;

// =============================================================================
// Bridge Server (from bridge_server.h)
// =============================================================================

export namespace pacs::bridge {

class bridge_server;

} // namespace pacs::bridge

// =============================================================================
// Configuration (from config/*.h)
// =============================================================================

export namespace pacs::bridge::config {

class config_loader;
class config_manager;
class admin_server;

} // namespace pacs::bridge::config

// =============================================================================
// Monitoring (from monitoring/*.h)
// =============================================================================

export namespace pacs::bridge::monitoring {

// Health checking
class component_check;
class mllp_server_check;
class pacs_connection_check;
class queue_health_check;
class fhir_server_check;
class memory_health_check;
class health_checker;
class health_server;

// Metrics
class bridge_metrics_collector;
class scoped_metrics_timer;

} // namespace pacs::bridge::monitoring
