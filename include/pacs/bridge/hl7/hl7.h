#ifndef PACS_BRIDGE_HL7_HL7_H
#define PACS_BRIDGE_HL7_HL7_H

/**
 * @file hl7.h
 * @brief HL7 Gateway Module - Main include header
 *
 * This header provides convenience includes for the HL7 Gateway module.
 * The actual implementation is in the protocol/hl7 subdirectory.
 *
 * @see protocol/hl7/hl7_message.h
 * @see protocol/hl7/hl7_parser.h
 * @see docs/SDS_COMPONENTS.md - Section 1: HL7 Gateway Module
 */

// Include the actual HL7 implementations from protocol/hl7
#include "pacs/bridge/protocol/hl7/hl7_message.h"
#include "pacs/bridge/protocol/hl7/hl7_parser.h"
#include "pacs/bridge/protocol/hl7/hl7_types.h"
#include "pacs/bridge/protocol/hl7/adt_handler.h"
#include "pacs/bridge/protocol/hl7/orm_handler.h"

/**
 * @namespace pacs::bridge::hl7
 * @brief HL7 v2.x message handling
 *
 * Provides parsing, validation, and construction of HL7 v2.x messages.
 * Components include:
 *   - hl7_message: Message representation
 *   - hl7_parser: Message parsing
 *   - hl7_builder: Message construction
 *   - hl7_validator: Message validation
 *   - adt_handler: ADT message handling
 *   - orm_handler: ORM message handling
 */

#endif // PACS_BRIDGE_HL7_HL7_H
