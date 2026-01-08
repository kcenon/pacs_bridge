// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file pacs_bridge-hl7.cppm
 * @brief HL7 v2.x module partition for pacs_bridge.
 *
 * This module partition exports all HL7 v2.x message handling functionality
 * including message parsing, building, validation, and handlers for various
 * message types (ADT, ORM, ORU, SIU).
 *
 * Usage:
 * @code
 * import kcenon.pacs_bridge;
 *
 * using namespace pacs::bridge::hl7;
 *
 * // Parse an HL7 message
 * hl7_parser parser;
 * auto result = parser.parse(raw_message);
 * if (result.is_ok()) {
 *     auto& msg = result.value();
 *     // Process message...
 * }
 * @endcode
 *
 * Exported Classes:
 * - Core types: hl7_subcomponent, hl7_component, hl7_field, hl7_segment, hl7_message
 * - Parser: hl7_parser, hl7_streaming_parser
 * - Builder: hl7_builder, adt_builder, orm_builder, oru_builder
 * - Validator: hl7_validator
 * - Handlers: adt_handler, orm_handler, oru_generator, siu_handler
 *
 * @see https://www.hl7.org/implement/standards/product_brief.cfm?product_id=185
 */

export module kcenon.pacs_bridge:hl7;

import kcenon.common;

// =============================================================================
// Re-export HL7 Types and Constants
// =============================================================================

export namespace pacs::bridge::hl7 {

// Result type aliases from common_system
using kcenon::common::Result;
using kcenon::common::VoidResult;
using kcenon::common::error_info;

// Protocol constants
constexpr char HL7_FIELD_SEPARATOR = '|';
constexpr char HL7_COMPONENT_SEPARATOR = '^';
constexpr char HL7_REPETITION_SEPARATOR = '~';
constexpr char HL7_ESCAPE_CHARACTER = '\\';
constexpr char HL7_SUBCOMPONENT_SEPARATOR = '&';
constexpr char HL7_SEGMENT_TERMINATOR = '\x0D';

} // namespace pacs::bridge::hl7

// =============================================================================
// Core Message Types (from hl7_message.h)
// =============================================================================

export namespace pacs::bridge::hl7 {

// Forward declarations for implementation files
class hl7_subcomponent;
class hl7_component;
class hl7_field;
class hl7_segment;
class hl7_message;

} // namespace pacs::bridge::hl7

// =============================================================================
// Parser (from hl7_parser.h)
// =============================================================================

export namespace pacs::bridge::hl7 {

class hl7_parser;
class hl7_streaming_parser;

} // namespace pacs::bridge::hl7

// =============================================================================
// Builder (from hl7_builder.h)
// =============================================================================

export namespace pacs::bridge::hl7 {

class hl7_builder;
class adt_builder;
class orm_builder;
class oru_builder;
class message_id_generator;

} // namespace pacs::bridge::hl7

// =============================================================================
// Validator (from hl7_validator.h)
// =============================================================================

export namespace pacs::bridge::hl7 {

class hl7_validator;

} // namespace pacs::bridge::hl7

// =============================================================================
// Handlers (from adt_handler.h, orm_handler.h, oru_generator.h, siu_handler.h)
// =============================================================================

export namespace pacs::bridge::hl7 {

class adt_handler;
class orm_handler;
class oru_generator;
class siu_handler;

} // namespace pacs::bridge::hl7
