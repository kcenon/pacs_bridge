// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file pacs_bridge-mllp.cppm
 * @brief MLLP protocol module partition for pacs_bridge.
 *
 * This module partition exports the Minimal Lower Layer Protocol (MLLP)
 * transport implementation for HL7 message communication.
 *
 * MLLP Frame Format:
 *   <SB>message_data<EB><CR>
 *   - SB (Start Block): 0x0B (vertical tab)
 *   - EB (End Block): 0x1C (file separator)
 *   - CR (Carriage Return): 0x0D
 *
 * Usage:
 * @code
 * import kcenon.pacs_bridge;
 *
 * using namespace pacs::bridge::mllp;
 *
 * // Create MLLP server
 * mllp_server server("0.0.0.0", 2575);
 * server.set_message_handler([](const std::string& msg) {
 *     // Handle incoming HL7 message
 *     return ack_message;
 * });
 * server.start();
 * @endcode
 *
 * Exported Classes:
 * - mllp_server: MLLP server for receiving HL7 messages
 * - mllp_client: MLLP client for sending HL7 messages
 * - mllp_connection_pool: Connection pool for efficient client connections
 *
 * @see https://www.hl7.org/documentcenter/public/wg/inm/mllp_transport_specification.PDF
 */

export module kcenon.pacs_bridge:mllp;

import kcenon.common;

// =============================================================================
// MLLP Protocol Constants
// =============================================================================

export namespace pacs::bridge::mllp {

// MLLP framing characters
constexpr char MLLP_START_BLOCK = '\x0B';     // Vertical Tab (VT)
constexpr char MLLP_END_BLOCK = '\x1C';       // File Separator (FS)
constexpr char MLLP_CARRIAGE_RETURN = '\x0D'; // Carriage Return (CR)

// Default configuration
constexpr int DEFAULT_MLLP_PORT = 2575;
constexpr int DEFAULT_TIMEOUT_MS = 30000;

} // namespace pacs::bridge::mllp

// =============================================================================
// MLLP Server (from mllp_server.h)
// =============================================================================

export namespace pacs::bridge::mllp {

class mllp_server;

} // namespace pacs::bridge::mllp

// =============================================================================
// MLLP Client (from mllp_client.h)
// =============================================================================

export namespace pacs::bridge::mllp {

class mllp_client;
class mllp_connection_pool;

} // namespace pacs::bridge::mllp
