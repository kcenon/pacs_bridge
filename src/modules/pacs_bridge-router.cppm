// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file pacs_bridge-router.cppm
 * @brief Message routing module partition for pacs_bridge.
 *
 * This module partition exports message routing functionality including
 * inbound message routing, outbound delivery, queue management, and
 * reliable message delivery.
 *
 * Features:
 * - Message routing based on message type and content
 * - Outbound routing with destination management
 * - Queue management for reliable delivery
 * - Retry logic and dead letter handling
 *
 * Usage:
 * @code
 * import kcenon.pacs_bridge;
 *
 * using namespace pacs::bridge::router;
 *
 * // Create message router
 * message_router router;
 * router.add_route("ORM", orm_handler);
 * router.add_route("ADT", adt_handler);
 * router.route(message);
 * @endcode
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/175
 */

export module kcenon.pacs_bridge:router;

import kcenon.common;

// =============================================================================
// Router Types
// =============================================================================

export namespace pacs::bridge::router {

// Result type aliases
using kcenon::common::Result;
using kcenon::common::VoidResult;
using kcenon::common::error_info;

} // namespace pacs::bridge::router

// =============================================================================
// Message Router (from message_router.h)
// =============================================================================

export namespace pacs::bridge::router {

class message_router;
class route_builder;

} // namespace pacs::bridge::router

// =============================================================================
// Outbound Router (from outbound_router.h)
// =============================================================================

export namespace pacs::bridge::router {

class outbound_router;
class destination_builder;

} // namespace pacs::bridge::router

// =============================================================================
// Queue Manager (from queue_manager.h)
// Requires SQLite for persistence
// =============================================================================

export namespace pacs::bridge::router {

class queue_manager;
class queue_config_builder;

} // namespace pacs::bridge::router

// =============================================================================
// Reliable Outbound Sender (from reliable_outbound_sender.h)
// Requires SQLite for persistence
// =============================================================================

export namespace pacs::bridge::router {

class reliable_outbound_sender;
class reliable_sender_config_builder;

} // namespace pacs::bridge::router
