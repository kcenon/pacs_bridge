// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file pacs_bridge-emr.cppm
 * @brief EMR integration module partition for pacs_bridge.
 *
 * This module partition exports EMR (Electronic Medical Record) integration
 * functionality including FHIR client, patient lookup, result posting,
 * and encounter context management.
 *
 * Features:
 * - FHIR R4 client for external EMR systems
 * - Patient lookup and matching
 * - Diagnostic report building and posting
 * - Encounter context management
 *
 * Usage:
 * @code
 * import kcenon.pacs_bridge;
 *
 * using namespace pacs::bridge::emr;
 *
 * // Create FHIR client for EMR
 * fhir_client client("https://emr.hospital.org/fhir");
 * auto result = client.search_patient("identifier=12345");
 * if (result.is_ok()) {
 *     auto& patients = result.value();
 *     // Process patients...
 * }
 * @endcode
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/102
 */

export module kcenon.pacs_bridge:emr;

import kcenon.common;

// =============================================================================
// EMR Types (from emr_types.h)
// =============================================================================

export namespace pacs::bridge::emr {

// Result type aliases
using kcenon::common::Result;
using kcenon::common::VoidResult;
using kcenon::common::error_info;

} // namespace pacs::bridge::emr

// =============================================================================
// HTTP Client Adapter (from http_client_adapter.h)
// =============================================================================

export namespace pacs::bridge::emr {

class http_client_adapter;
class callback_http_client;

} // namespace pacs::bridge::emr

// =============================================================================
// FHIR Client (from fhir_client.h)
// =============================================================================

export namespace pacs::bridge::emr {

class fhir_client;

} // namespace pacs::bridge::emr

// =============================================================================
// FHIR Bundle (from fhir_bundle.h)
// =============================================================================

export namespace pacs::bridge::emr {

class bundle_builder;

} // namespace pacs::bridge::emr

// =============================================================================
// Patient Lookup and Matching (from patient_lookup.h, patient_matcher.h)
// =============================================================================

export namespace pacs::bridge::emr {

class emr_patient_lookup;
class patient_matcher;

} // namespace pacs::bridge::emr

// =============================================================================
// Search Parameters (from search_params.h)
// =============================================================================

export namespace pacs::bridge::emr {

class search_params;

} // namespace pacs::bridge::emr

// =============================================================================
// Diagnostic Report and Results (from diagnostic_report_builder.h, result_poster.h)
// =============================================================================

export namespace pacs::bridge::emr {

class diagnostic_report_builder;
class emr_result_poster;

} // namespace pacs::bridge::emr

// =============================================================================
// Result Tracking (from result_tracker.h)
// =============================================================================

export namespace pacs::bridge::emr {

class result_tracker;
class in_memory_result_tracker;

} // namespace pacs::bridge::emr

// =============================================================================
// Encounter Context (from encounter_context.h)
// =============================================================================

export namespace pacs::bridge::emr {

class encounter_context_provider;

} // namespace pacs::bridge::emr

// =============================================================================
// EMR Adapter (from emr_adapter.h)
// =============================================================================

export namespace pacs::bridge::emr {

class emr_adapter;

} // namespace pacs::bridge::emr

// =============================================================================
// Adapter Implementations (from adapters/)
// =============================================================================

export namespace pacs::bridge::emr {

class generic_fhir_adapter;

} // namespace pacs::bridge::emr
