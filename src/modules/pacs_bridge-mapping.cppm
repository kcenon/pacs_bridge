// BSD 3-Clause License
// Copyright (c) 2025, kcenon
// See the LICENSE file in the project root for full license information.

/**
 * @file pacs_bridge-mapping.cppm
 * @brief Data mapping module partition for pacs_bridge.
 *
 * This module partition exports data transformation mappers for converting
 * between different healthcare data formats (HL7, DICOM, FHIR).
 *
 * Mappers:
 * - hl7_dicom_mapper: HL7 -> DICOM (ORM to MWL, patient data)
 * - dicom_hl7_mapper: DICOM -> HL7 (MPPS to ORM status updates)
 * - fhir_dicom_mapper: FHIR <-> DICOM (ServiceRequest to MWL, Study to ImagingStudy)
 *
 * Usage:
 * @code
 * import kcenon.pacs_bridge;
 *
 * using namespace pacs::bridge::mapping;
 *
 * // Convert HL7 ORM to DICOM MWL
 * hl7_dicom_mapper mapper;
 * auto result = mapper.to_mwl(orm_message);
 * if (result.is_ok()) {
 *     auto& mwl = result.value();
 *     // Send to PACS...
 * }
 * @endcode
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/220
 */

export module kcenon.pacs_bridge:mapping;

import kcenon.common;

// =============================================================================
// Mapping Types
// =============================================================================

export namespace pacs::bridge::mapping {

// Result type aliases
using kcenon::common::Result;
using kcenon::common::VoidResult;
using kcenon::common::error_info;

} // namespace pacs::bridge::mapping

// =============================================================================
// HL7 to DICOM Mapper (from hl7_dicom_mapper.h)
// =============================================================================

export namespace pacs::bridge::mapping {

class hl7_dicom_mapper;
class patient_id_mapper;

} // namespace pacs::bridge::mapping

// =============================================================================
// DICOM to HL7 Mapper (from dicom_hl7_mapper.h)
// =============================================================================

export namespace pacs::bridge::mapping {

class dicom_hl7_mapper;

} // namespace pacs::bridge::mapping

// =============================================================================
// FHIR to DICOM Mapper (from fhir_dicom_mapper.h)
// Conditionally exported when FHIR module is enabled
// =============================================================================

export namespace pacs::bridge::mapping {

#ifdef PACS_BRIDGE_BUILD_FHIR
class fhir_dicom_mapper;
#endif

} // namespace pacs::bridge::mapping
