// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file pacs_bridge-fhir.cppm
 * @brief FHIR R4 module partition for pacs_bridge.
 *
 * This module partition exports FHIR R4 resource handling functionality
 * including resources, server, handlers, and storage implementations.
 *
 * Supported FHIR Resources:
 * - Patient: Patient demographics and identifiers
 * - ServiceRequest: Imaging orders and scheduling
 * - ImagingStudy: DICOM study references
 * - Subscription: Real-time event notifications
 *
 * Usage:
 * @code
 * import kcenon.pacs_bridge;
 *
 * using namespace pacs::bridge::fhir;
 *
 * // Create FHIR server
 * fhir_server server("0.0.0.0", 8080);
 * server.register_handler(std::make_unique<patient_resource_handler>());
 * server.start();
 * @endcode
 *
 * @note This module is conditionally compiled with BRIDGE_BUILD_FHIR option.
 * @see https://www.hl7.org/fhir/R4/
 */

export module kcenon.pacs_bridge:fhir;

import kcenon.common;

// =============================================================================
// FHIR Base Types (from fhir_types.h)
// =============================================================================

export namespace pacs::bridge::fhir {

// Result type aliases
using kcenon::common::Result;
using kcenon::common::VoidResult;
using kcenon::common::error_info;

} // namespace pacs::bridge::fhir

// =============================================================================
// FHIR Resource Base (from fhir_resource.h)
// =============================================================================

export namespace pacs::bridge::fhir {

class fhir_resource;

} // namespace pacs::bridge::fhir

// =============================================================================
// Operation Outcome (from operation_outcome.h)
// =============================================================================

export namespace pacs::bridge::fhir {

class operation_outcome;

} // namespace pacs::bridge::fhir

// =============================================================================
// FHIR Server (from fhir_server.h)
// =============================================================================

export namespace pacs::bridge::fhir {

class fhir_server;

} // namespace pacs::bridge::fhir

// =============================================================================
// Resource Handler (from resource_handler.h)
// =============================================================================

export namespace pacs::bridge::fhir {

class resource_handler;
class handler_registry;

} // namespace pacs::bridge::fhir

// =============================================================================
// Patient Resource (from patient_resource.h)
// =============================================================================

export namespace pacs::bridge::fhir {

class patient_resource;
class patient_resource_handler;

} // namespace pacs::bridge::fhir

// =============================================================================
// ServiceRequest Resource (from service_request_resource.h)
// =============================================================================

export namespace pacs::bridge::fhir {

class service_request_resource;
class mwl_storage;
class in_memory_mwl_storage;
class service_request_handler;

} // namespace pacs::bridge::fhir

// =============================================================================
// ImagingStudy Resource (from imaging_study_resource.h)
// =============================================================================

export namespace pacs::bridge::fhir {

class imaging_study_resource;
class study_storage;
class in_memory_study_storage;
class imaging_study_handler;

} // namespace pacs::bridge::fhir

// =============================================================================
// Subscription Resource (from subscription_resource.h, subscription_manager.h)
// =============================================================================

export namespace pacs::bridge::fhir {

class subscription_resource;
class subscription_storage;
class in_memory_subscription_storage;
class subscription_manager;
class subscription_handler;

} // namespace pacs::bridge::fhir
