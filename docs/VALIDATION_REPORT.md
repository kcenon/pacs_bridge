# PACS Bridge Validation Report

> **Report Version:** 1.0.0
> **Report Date:** 2025-12-07
> **Language:** **English** | [한국어](VALIDATION_REPORT_KO.md)
> **Status:** Pre-Implementation Phase (Planned Validation)
> **Reference Project:** [pacs_system](../../pacs_system) v0.2.0+

---

## Executive Summary

This **Validation Report** documents the planned validation activities for PACS Bridge, ensuring that the implementation will meet all requirements specified in the Software Requirements Specification (SRS).

> **Validation**: "Are we building the right product?"
> - Confirms implementation meets SRS requirements
> - System Tests → Functional Requirements (SRS-xxx)
> - Acceptance Tests → User Requirements and Use Cases

> **Note**: For **Verification** (SDS design compliance), see [VERIFICATION_REPORT.md](VERIFICATION_REPORT.md).

### Overall Status: **Phase 0 - Pre-Implementation (Validation Planned)**

| Category | Requirements | Planned Tests | Status |
|----------|--------------|---------------|--------|
| **HL7 Gateway Module** | 6 | 6 | ⏳ Phase 1 |
| **MLLP Transport** | 3 | 3 | ⏳ Phase 1 |
| **FHIR Gateway Module** | 4 | 4 | ⏳ Phase 3 |
| **Translation Layer** | 5 | 5 | ⏳ Phase 1-3 |
| **Message Routing** | 3 | 3 | ⏳ Phase 1-2 |
| **pacs_system Integration** | 3 | 3 | ⏳ Phase 1-2 |
| **Configuration** | 2 | 2 | ⏳ Phase 1-2 |
| **Performance** | 6 | 6 | ⏳ Phase 4 |
| **Reliability** | 5 | 5 | ⏳ Phase 2-4 |
| **Security** | 5 | 5 | ⏳ Phase 2-4 |
| **Scalability** | 4 | 4 | ⏳ Phase 4 |
| **Maintainability** | 5 | 5 | ⏳ Phase 1-4 |
| **Total** | **51** | **51** | **⏳ Planned** |

---

## 1. V-Model Context

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              V-Model Traceability                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Requirements                                                    Tests       │
│  ────────────                                                   ─────       │
│                                                                              │
│  ┌──────────┐                                      ┌──────────────────┐     │
│  │   PRD    │◄────────────────────────────────────►│  Acceptance Tests │     │
│  │(Product) │                                      │ (User Scenarios)  │     │
│  └────┬─────┘                                      └──────────────────┘     │
│       │                                                     ▲               │
│       ▼                                                     │               │
│  ┌──────────┐        ═══════════════════          ┌─────────┴────────┐     │
│  │   SRS    │◄═══════   THIS DOCUMENT   ═════════►│   System Tests    │     │
│  │(Software)│       (VALIDATION REPORT)           │ (SRS Compliance)  │     │
│  └────┬─────┘        ═══════════════════          └──────────────────┘     │
│       │                                                     ▲               │
│       ▼                                                     │               │
│  ┌──────────┐                                      ┌────────┴─────────┐     │
│  │   SDS    │◄────────────────────────────────────►│ Integration Tests │     │
│  │ (Design) │       VERIFICATION_REPORT.md         │   (SDS Modules)   │     │
│  └────┬─────┘                                      └──────────────────┘     │
│       │                                                     ▲               │
│       ▼                                                     │               │
│  ┌──────────┐                                      ┌────────┴─────────┐     │
│  │   Code   │◄────────────────────────────────────►│    Unit Tests     │     │
│  │(Modules) │       VERIFICATION_REPORT.md         │  (SDS Details)    │     │
│  └──────────┘                                      └──────────────────┘     │
│                                                                              │
│  Legend:                                                                     │
│    ═══► Validation: "Are we building the right product?" (SRS compliance)   │
│    ───► Verification: "Are we building the product right?" (SDS compliance) │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Functional Requirements Validation Plan

### 2.1 HL7 Gateway Module (SRS-HL7)

#### SRS-HL7-001: HL7 v2.x Message Parsing

| Attribute | Value |
|-----------|-------|
| **Requirement** | Parse HL7 v2.x messages (v2.3.1 ~ v2.9) with standard delimiters, escape sequences, repeating fields, and custom Z-segments |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | System Test |
| **Test ID** | VAL-HL7-001 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Parse standard segment delimiters (\|^~\\&) | Unit Test | ⏳ Phase 1 |
| 2 | Handle escape sequences (\\F\\, \\S\\, \\R\\, \\E\\, \\T\\) | Unit Test | ⏳ Phase 1 |
| 3 | Support repeating fields with ~ delimiter | Unit Test | ⏳ Phase 1 |
| 4 | Support component/subcomponent separation | Unit Test | ⏳ Phase 1 |
| 5 | Extract data from all standard segments | Integration Test | ⏳ Phase 1 |

**Planned Test Cases:**

| Test Case | Description | Type |
|-----------|-------------|------|
| VAL-HL7-001-01 | Parse ORM^O01 with standard delimiters | Unit |
| VAL-HL7-001-02 | Parse message with escape sequences | Unit |
| VAL-HL7-001-03 | Parse PID with repeating identifiers | Unit |
| VAL-HL7-001-04 | Parse complex OBR with subcomponents | Unit |
| VAL-HL7-001-05 | Parse message with custom ZDS segment | Unit |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-HL7-002: ADT Message Processing

| Attribute | Value |
|-----------|-------|
| **Requirement** | Process ADT messages (A01, A04, A08, A40) to maintain patient demographics cache and update MWL entries |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-HL7-002 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | ADT^A01 (Admit) processing - create patient record | Integration | ⏳ Phase 1 |
| 2 | ADT^A04 (Register) processing - create outpatient record | Integration | ⏳ Phase 1 |
| 3 | ADT^A08 (Update) processing - update demographics | Integration | ⏳ Phase 1 |
| 4 | ADT^A40 (Merge) processing - merge patient records | Integration | ⏳ Phase 1 |
| 5 | Update all related MWL entries on patient update | System | ⏳ Phase 1 |

**ADT-Patient Cache Mapping Verification:**

| HL7 Field | Cache Field | Validation |
|-----------|-------------|------------|
| PID-3 | patient_id | ⏳ Phase 1 |
| PID-3.4 | issuer | ⏳ Phase 1 |
| PID-5 | patient_name | ⏳ Phase 1 |
| PID-7 | birth_date | ⏳ Phase 1 |
| PID-8 | sex | ⏳ Phase 1 |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-HL7-003: ORM Message Processing

| Attribute | Value |
|-----------|-------|
| **Requirement** | Process ORM^O01 messages to create, update, cancel, and manage MWL entries based on order control codes |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-HL7-003 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Create MWL entry on ORC-1 = NW (New Order) | Integration | ⏳ Phase 1 |
| 2 | Update MWL entry on ORC-1 = XO (Change Order) | Integration | ⏳ Phase 1 |
| 3 | Cancel MWL entry on ORC-1 = CA (Cancel Request) | Integration | ⏳ Phase 1 |
| 4 | Handle status change on ORC-1 = SC (Status Changed) | Integration | ⏳ Phase 1 |
| 5 | Support discontinue on ORC-1 = DC | Integration | ⏳ Phase 1 |

**Order Control Processing Verification:**

| ORC-1 | ORC-5 | Expected Action | MWL Status | Validation |
|-------|-------|-----------------|------------|------------|
| NW | SC | Create new entry | SCHEDULED | ⏳ Phase 1 |
| NW | IP | Create and mark started | STARTED | ⏳ Phase 1 |
| XO | SC | Update existing entry | SCHEDULED | ⏳ Phase 1 |
| CA | CA | Delete entry | (deleted) | ⏳ Phase 1 |
| SC | IP | Status → in progress | STARTED | ⏳ Phase 1 |
| SC | CM | Status → completed | COMPLETED | ⏳ Phase 1 |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-HL7-004: ORU Message Generation

| Attribute | Value |
|-----------|-------|
| **Requirement** | Generate ORU^R01 messages for report status notifications including preliminary and final report statuses |
| **Priority** | Should Have |
| **Phase** | 2 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-HL7-004 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Generate ORU^R01 for preliminary reports (OBR-25 = P) | Integration | ⏳ Phase 2 |
| 2 | Generate ORU^R01 for final reports (OBR-25 = F) | Integration | ⏳ Phase 2 |
| 3 | Include appropriate OBX segments with LOINC codes | Integration | ⏳ Phase 2 |
| 4 | Support report corrections (OBR-25 = C) | Integration | ⏳ Phase 2 |
| 5 | Include referring physician information | Integration | ⏳ Phase 2 |

**Status:** ⏳ **Validation Planned (Phase 2)**

---

#### SRS-HL7-005: SIU Message Processing

| Attribute | Value |
|-----------|-------|
| **Requirement** | Process SIU^S12-S15 messages for scheduling updates, supporting appointment creation, modification, and cancellation |
| **Priority** | Should Have |
| **Phase** | 2 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-HL7-005 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Process SIU^S12 (New Appointment) | Integration | ⏳ Phase 2 |
| 2 | Process SIU^S13 (Rescheduling) | Integration | ⏳ Phase 2 |
| 3 | Process SIU^S14 (Modification) | Integration | ⏳ Phase 2 |
| 4 | Process SIU^S15 (Cancellation) | Integration | ⏳ Phase 2 |
| 5 | Update MWL scheduling information | System | ⏳ Phase 2 |

**Status:** ⏳ **Validation Planned (Phase 2)**

---

#### SRS-HL7-006: ACK Response Generation

| Attribute | Value |
|-----------|-------|
| **Requirement** | Generate correct HL7 ACK responses (AA, AE, AR) with appropriate error codes and descriptions |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | System Test |
| **Test ID** | VAL-HL7-006 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Generate AA (Application Accept) on success | Unit | ⏳ Phase 1 |
| 2 | Generate AE (Application Error) on business logic error | Unit | ⏳ Phase 1 |
| 3 | Generate AR (Application Reject) on system error | Unit | ⏳ Phase 1 |
| 4 | Include ERR segment with error details | Unit | ⏳ Phase 1 |
| 5 | Echo original MSH-10 in MSA-2 | Unit | ⏳ Phase 1 |

**ACK Response Validation:**

```
Input:  ORM^O01 with missing OBR-4
Output: MSH|...|ACK^O01|...
        MSA|AE|OriginalMsgID|Missing required field
        ERR|||207^Application internal error^HL70357|E|||OBR-4 required
```

**Status:** ⏳ **Validation Planned (Phase 1)**

---

### 2.2 MLLP Transport Module (SRS-MLLP)

#### SRS-MLLP-001: MLLP Server Implementation

| Attribute | Value |
|-----------|-------|
| **Requirement** | Implement MLLP server that listens for incoming HL7 connections, handles message framing, and supports concurrent connections |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | System Test |
| **Test ID** | VAL-MLLP-001 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Listen on configurable TCP port (default 2575) | System | ⏳ Phase 1 |
| 2 | Handle MLLP framing (VT=0x0B, FS=0x1C, CR=0x0D) | Unit | ⏳ Phase 1 |
| 3 | Support concurrent connections (≥50) | Load Test | ⏳ Phase 1 |
| 4 | Configurable connection timeout | System | ⏳ Phase 1 |
| 5 | Keep-alive support | System | ⏳ Phase 1 |

**MLLP Frame Validation:**

```
Expected Frame Structure:
<0x0B>HL7 Message Content<0x1C><0x0D>

Test Cases:
- Valid frame with complete message
- Fragmented frame reassembly
- Invalid frame detection and rejection
```

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-MLLP-002: MLLP Client Implementation

| Attribute | Value |
|-----------|-------|
| **Requirement** | Implement MLLP client for sending HL7 messages to remote systems with connection pooling and retry support |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-MLLP-002 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Connect to configurable host and port | Integration | ⏳ Phase 1 |
| 2 | Apply MLLP framing to outgoing messages | Unit | ⏳ Phase 1 |
| 3 | Wait for and validate ACK response | Integration | ⏳ Phase 1 |
| 4 | Connection pooling for persistent connections | System | ⏳ Phase 1 |
| 5 | Configurable retry with exponential backoff | System | ⏳ Phase 1 |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-MLLP-003: MLLP over TLS

| Attribute | Value |
|-----------|-------|
| **Requirement** | Support MLLP over TLS 1.2/1.3 for secure HL7 message transport with certificate validation |
| **Priority** | Should Have |
| **Phase** | 2 |
| **Validation Method** | Security Test |
| **Test ID** | VAL-MLLP-003 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Support TLS 1.2 and TLS 1.3 | Security | ⏳ Phase 2 |
| 2 | Server certificate validation | Security | ⏳ Phase 2 |
| 3 | Optional client certificate (mutual TLS) | Security | ⏳ Phase 2 |
| 4 | Configurable cipher suites | Security | ⏳ Phase 2 |
| 5 | Certificate chain validation | Security | ⏳ Phase 2 |

**Status:** ⏳ **Validation Planned (Phase 2)**

---

### 2.3 FHIR Gateway Module (SRS-FHIR)

#### SRS-FHIR-001: FHIR REST Server

| Attribute | Value |
|-----------|-------|
| **Requirement** | Implement FHIR R4 compliant REST server with JSON and XML formats, standard CRUD operations |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Validation Method** | System Test |
| **Test ID** | VAL-FHIR-001 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | RESTful API endpoints for resources | System | ⏳ Phase 3 |
| 2 | JSON and XML content negotiation | System | ⏳ Phase 3 |
| 3 | Standard HTTP methods (GET, POST, PUT, DELETE) | System | ⏳ Phase 3 |
| 4 | Search parameter support | System | ⏳ Phase 3 |
| 5 | Pagination for large result sets | System | ⏳ Phase 3 |

**Status:** ⏳ **Validation Planned (Phase 3)**

---

#### SRS-FHIR-002: Patient Resource

| Attribute | Value |
|-----------|-------|
| **Requirement** | Support FHIR Patient resource with read and search operations mapped to internal patient cache |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-FHIR-002 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | GET /Patient/{id} - read patient | Integration | ⏳ Phase 3 |
| 2 | GET /Patient?identifier=xxx - search by ID | Integration | ⏳ Phase 3 |
| 3 | GET /Patient?name=xxx - search by name | Integration | ⏳ Phase 3 |
| 4 | Map to internal patient demographics | Integration | ⏳ Phase 3 |
| 5 | Support patient identifier systems | Integration | ⏳ Phase 3 |

**Status:** ⏳ **Validation Planned (Phase 3)**

---

#### SRS-FHIR-003: ServiceRequest Resource

| Attribute | Value |
|-----------|-------|
| **Requirement** | Support FHIR ServiceRequest for imaging orders, creating MWL entries from incoming requests |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-FHIR-003 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | POST /ServiceRequest - create order | Integration | ⏳ Phase 3 |
| 2 | GET /ServiceRequest/{id} - read order | Integration | ⏳ Phase 3 |
| 3 | GET /ServiceRequest?patient=xxx - search by patient | Integration | ⏳ Phase 3 |
| 4 | Map to DICOM MWL entry | Integration | ⏳ Phase 3 |
| 5 | Support order status updates | Integration | ⏳ Phase 3 |

**Status:** ⏳ **Validation Planned (Phase 3)**

---

#### SRS-FHIR-004: ImagingStudy Resource

| Attribute | Value |
|-----------|-------|
| **Requirement** | Support FHIR ImagingStudy resource for study availability notification and queries |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-FHIR-004 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | GET /ImagingStudy/{id} - read study | Integration | ⏳ Phase 3 |
| 2 | GET /ImagingStudy?patient=xxx - search by patient | Integration | ⏳ Phase 3 |
| 3 | GET /ImagingStudy?status=xxx - search by status | Integration | ⏳ Phase 3 |
| 4 | Include series and instance counts | Integration | ⏳ Phase 3 |
| 5 | Reference ServiceRequest | Integration | ⏳ Phase 3 |

**Status:** ⏳ **Validation Planned (Phase 3)**

---

### 2.4 Translation Layer (SRS-TRANS)

#### SRS-TRANS-001: HL7-DICOM MWL Mapper

| Attribute | Value |
|-----------|-------|
| **Requirement** | Transform HL7 ORM message fields to DICOM Modality Worklist attributes per IHE SWF profile mappings |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | System Test |
| **Test ID** | VAL-TRANS-001 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Map PID segment to Patient Module | Unit | ⏳ Phase 1 |
| 2 | Map ORC/OBR to Imaging Service Request | Unit | ⏳ Phase 1 |
| 3 | Map OBR to Scheduled Procedure Step Sequence | Unit | ⏳ Phase 1 |
| 4 | Map ZDS-1 to Study Instance UID | Unit | ⏳ Phase 1 |
| 5 | Handle procedure code mapping | Unit | ⏳ Phase 1 |

**Core Mapping Validation:**

| HL7 Field | DICOM Tag | Validation |
|-----------|-----------|------------|
| PID-3 | (0010,0020) PatientID | ⏳ Phase 1 |
| PID-5 | (0010,0010) PatientName | ⏳ Phase 1 |
| PID-7 | (0010,0030) PatientBirthDate | ⏳ Phase 1 |
| PID-8 | (0010,0040) PatientSex | ⏳ Phase 1 |
| ORC-2 | (0040,2016) PlacerOrderNumber | ⏳ Phase 1 |
| ORC-3 | (0008,0050) AccessionNumber | ⏳ Phase 1 |
| OBR-4.1 | (0008,0100) CodeValue | ⏳ Phase 1 |
| OBR-7 | (0040,0002/3) ScheduledDate/Time | ⏳ Phase 1 |
| OBR-24 | (0008,0060) Modality | ⏳ Phase 1 |
| ZDS-1 | (0020,000D) StudyInstanceUID | ⏳ Phase 1 |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-TRANS-002: DICOM MPPS-HL7 ORM Mapper

| Attribute | Value |
|-----------|-------|
| **Requirement** | Transform DICOM MPPS notifications to HL7 ORM status update messages |
| **Priority** | Must Have |
| **Phase** | 2 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-TRANS-002 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Map MPPS IN PROGRESS to ORC-5 = IP | Integration | ⏳ Phase 2 |
| 2 | Map MPPS COMPLETED to ORC-5 = CM | Integration | ⏳ Phase 2 |
| 3 | Map MPPS DISCONTINUED to ORC-5 = CA | Integration | ⏳ Phase 2 |
| 4 | Include procedure timing information | Integration | ⏳ Phase 2 |
| 5 | Reference original order | Integration | ⏳ Phase 2 |

**MPPS Status Mapping Validation:**

| MPPS Status | ORC-1 | ORC-5 | Validation |
|-------------|-------|-------|------------|
| IN PROGRESS | SC | IP | ⏳ Phase 2 |
| COMPLETED | SC | CM | ⏳ Phase 2 |
| DISCONTINUED | DC | CA | ⏳ Phase 2 |

**Status:** ⏳ **Validation Planned (Phase 2)**

---

#### SRS-TRANS-003: Patient Name Format Conversion

| Attribute | Value |
|-----------|-------|
| **Requirement** | Convert between HL7 XPN and DICOM PN name formats, handling component order differences |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | Unit Test |
| **Test ID** | VAL-TRANS-003 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Parse HL7 XPN format (Family^Given^Middle^Suffix^Prefix) | Unit | ⏳ Phase 1 |
| 2 | Convert to DICOM PN format (Family^Given^Middle^Prefix^Suffix) | Unit | ⏳ Phase 1 |
| 3 | Handle missing components | Unit | ⏳ Phase 1 |
| 4 | Support multi-valued names | Unit | ⏳ Phase 1 |
| 5 | Preserve special characters | Unit | ⏳ Phase 1 |

**Format Conversion Test:**

```
Input:  DOE^JOHN^ANDREW^JR^MR (HL7 XPN)
Output: DOE^JOHN^ANDREW^MR^JR (DICOM PN)
```

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-TRANS-004: Date/Time Format Conversion

| Attribute | Value |
|-----------|-------|
| **Requirement** | Convert between HL7 DTM and DICOM DA/TM date/time formats |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | Unit Test |
| **Test ID** | VAL-TRANS-004 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Parse HL7 DTM (YYYYMMDDHHMMSS.SSSS±ZZZZ) | Unit | ⏳ Phase 1 |
| 2 | Extract DICOM DA (YYYYMMDD) | Unit | ⏳ Phase 1 |
| 3 | Extract DICOM TM (HHMMSS.FFFFFF) | Unit | ⏳ Phase 1 |
| 4 | Handle timezone conversion | Unit | ⏳ Phase 1 |
| 5 | Handle partial precision | Unit | ⏳ Phase 1 |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-TRANS-005: FHIR-DICOM Mapper

| Attribute | Value |
|-----------|-------|
| **Requirement** | Transform FHIR ServiceRequest resources to DICOM MWL entries |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-TRANS-005 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Map Patient reference to Patient Module | Integration | ⏳ Phase 3 |
| 2 | Map ServiceRequest.code to Requested Procedure | Integration | ⏳ Phase 3 |
| 3 | Map ServiceRequest.occurrence to SPS Start Date/Time | Integration | ⏳ Phase 3 |
| 4 | Map ServiceRequest.performer to Scheduled AE Title | Integration | ⏳ Phase 3 |
| 5 | Generate Study Instance UID if not provided | Integration | ⏳ Phase 3 |

**Status:** ⏳ **Validation Planned (Phase 3)**

---

### 2.5 Message Routing Module (SRS-ROUTE)

#### SRS-ROUTE-001: Inbound Message Router

| Attribute | Value |
|-----------|-------|
| **Requirement** | Route incoming messages to appropriate handlers based on message type and trigger event |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | System Test |
| **Test ID** | VAL-ROUTE-001 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Route ADT messages to Patient Cache Handler | System | ⏳ Phase 1 |
| 2 | Route ORM messages to MWL Manager | System | ⏳ Phase 1 |
| 3 | Route based on MSH-9 (Message Type) | System | ⏳ Phase 1 |
| 4 | Support conditional routing rules | System | ⏳ Phase 1 |
| 5 | Log routing decisions | System | ⏳ Phase 1 |

**Routing Table Validation:**

| Message Type | Trigger | Handler | Validation |
|--------------|---------|---------|------------|
| ADT | A01, A04, A08, A40 | PatientCacheHandler | ⏳ Phase 1 |
| ORM | O01 | MWLManager | ⏳ Phase 1 |
| SIU | S12-S15 | SchedulingHandler | ⏳ Phase 2 |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-ROUTE-002: Outbound Message Router

| Attribute | Value |
|-----------|-------|
| **Requirement** | Route outgoing messages to configured destinations with failover support |
| **Priority** | Must Have |
| **Phase** | 2 |
| **Validation Method** | System Test |
| **Test ID** | VAL-ROUTE-002 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Route MPPS notifications to RIS | System | ⏳ Phase 2 |
| 2 | Route ORU reports to configured endpoints | System | ⏳ Phase 2 |
| 3 | Support multiple destinations | System | ⏳ Phase 2 |
| 4 | Implement failover routing | System | ⏳ Phase 2 |
| 5 | Track delivery status | System | ⏳ Phase 2 |

**Status:** ⏳ **Validation Planned (Phase 2)**

---

#### SRS-ROUTE-003: Message Queue Manager

| Attribute | Value |
|-----------|-------|
| **Requirement** | Queue outgoing messages for reliable delivery with retry logic and persistence |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | System Test |
| **Test ID** | VAL-ROUTE-003 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Queue messages for asynchronous delivery | System | ⏳ Phase 1 |
| 2 | Retry with exponential backoff | System | ⏳ Phase 1 |
| 3 | Message priority support | System | ⏳ Phase 1 |
| 4 | Queue persistence for crash recovery | System | ⏳ Phase 2 |
| 5 | Dead letter queue for failed messages | System | ⏳ Phase 2 |

**Retry Strategy Validation:**

```
Attempt 1: Immediate
Attempt 2: 5 seconds
Attempt 3: 30 seconds
Attempt 4: 2 minutes
Attempt 5: 10 minutes
Max Attempts: 5 (configurable)
```

**Status:** ⏳ **Validation Planned (Phase 1-2)**

---

### 2.6 pacs_system Integration (SRS-PACS)

#### SRS-PACS-001: MWL Entry Management

| Attribute | Value |
|-----------|-------|
| **Requirement** | Create, update, and delete MWL entries in pacs_system via DICOM or direct database access |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-PACS-001 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Create MWL entry from ORM^O01 | Integration | ⏳ Phase 1 |
| 2 | Update MWL entry on order change | Integration | ⏳ Phase 1 |
| 3 | Delete MWL entry on cancellation | Integration | ⏳ Phase 1 |
| 4 | Handle duplicate detection | Integration | ⏳ Phase 1 |
| 5 | Batch operations support | Integration | ⏳ Phase 1 |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-PACS-002: MPPS Event Handling

| Attribute | Value |
|-----------|-------|
| **Requirement** | Receive MPPS N-CREATE and N-SET notifications from pacs_system and transform to HL7 messages |
| **Priority** | Must Have |
| **Phase** | 2 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-PACS-002 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Register as MPPS event listener | Integration | ⏳ Phase 2 |
| 2 | Receive N-CREATE (IN PROGRESS) | Integration | ⏳ Phase 2 |
| 3 | Receive N-SET (COMPLETED/DISCONTINUED) | Integration | ⏳ Phase 2 |
| 4 | Generate HL7 ORM status messages | Integration | ⏳ Phase 2 |
| 5 | Deliver to configured RIS endpoints | Integration | ⏳ Phase 2 |

**Status:** ⏳ **Validation Planned (Phase 2)**

---

#### SRS-PACS-003: Patient Cache Synchronization

| Attribute | Value |
|-----------|-------|
| **Requirement** | Maintain patient demographics cache synchronized with HIS ADT events for MWL queries |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | Integration Test |
| **Test ID** | VAL-PACS-003 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Cache patient demographics from ADT | Integration | ⏳ Phase 1 |
| 2 | Update on ADT^A08 (update) | Integration | ⏳ Phase 1 |
| 3 | Handle ADT^A40 (merge) | Integration | ⏳ Phase 1 |
| 4 | Provide lookup by Patient ID | Integration | ⏳ Phase 1 |
| 5 | Time-based cache expiration | Integration | ⏳ Phase 1 |

**Status:** ⏳ **Validation Planned (Phase 1)**

---

### 2.7 Configuration Module (SRS-CFG)

#### SRS-CFG-001: Endpoint Configuration

| Attribute | Value |
|-----------|-------|
| **Requirement** | Support configuration for HL7 listener ports, outbound destinations, and pacs_system connection |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Validation Method** | System Test |
| **Test ID** | VAL-CFG-001 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Configure HL7 listener port | System | ⏳ Phase 1 |
| 2 | Configure outbound HL7 destinations | System | ⏳ Phase 1 |
| 3 | Configure pacs_system connection | System | ⏳ Phase 1 |
| 4 | Support configuration file (YAML) | System | ⏳ Phase 1 |
| 5 | Hot reload support (Phase 3) | System | ⏳ Phase 3 |

**Configuration Example Validation:**

```yaml
server:
  name: "PACS_BRIDGE"

hl7:
  listener:
    port: 2575
    tls: false
    max_connections: 50

  outbound:
    - name: "RIS"
      host: "ris.hospital.local"
      port: 2576
      retry_count: 3

pacs_system:
  host: "localhost"
  port: 11112
  ae_title: "PACS_BRIDGE"
```

**Status:** ⏳ **Validation Planned (Phase 1)**

---

#### SRS-CFG-002: Mapping Configuration

| Attribute | Value |
|-----------|-------|
| **Requirement** | Support configurable mappings for modality-AE-title, procedure codes, and patient ID domains |
| **Priority** | Should Have |
| **Phase** | 2 |
| **Validation Method** | System Test |
| **Test ID** | VAL-CFG-002 |

**Acceptance Criteria:**

| # | Criterion | Validation Method | Status |
|---|-----------|-------------------|--------|
| 1 | Configure modality-AE title mappings | System | ⏳ Phase 2 |
| 2 | Configure procedure code mappings | System | ⏳ Phase 2 |
| 3 | Configure patient ID domain mappings | System | ⏳ Phase 2 |
| 4 | Support custom field mappings | System | ⏳ Phase 2 |
| 5 | Configuration validation on load | System | ⏳ Phase 2 |

**Status:** ⏳ **Validation Planned (Phase 2)**

---

## 3. Non-Functional Requirements Validation Plan

### 3.1 Performance (SRS-PERF)

| Requirement ID | Requirement | Target | Validation Method | Phase |
|----------------|-------------|--------|-------------------|-------|
| SRS-PERF-001 | HL7 Message Throughput | ≥500 msg/s | Load Test | 4 |
| SRS-PERF-002 | Message Latency (P50) | <20 ms | Benchmark | 4 |
| SRS-PERF-003 | Message Latency (P95) | <50 ms | Benchmark | 4 |
| SRS-PERF-004 | Message Latency (P99) | <100 ms | Benchmark | 4 |
| SRS-PERF-005 | Concurrent Connections | ≥50 | Stress Test | 4 |
| SRS-PERF-006 | Memory Baseline | <200 MB | Profiling | 4 |

**Validation Method:** Load testing with simulated HL7 traffic

**Planned Test Environment:**
- CPU: 4+ cores
- Memory: 8+ GB
- Network: 1 Gbps
- Concurrent clients: 50+

**Status:** ⏳ **Validation Planned (Phase 4)**

---

### 3.2 Reliability (SRS-REL)

| Requirement ID | Requirement | Target | Validation Method | Phase |
|----------------|-------------|--------|-------------------|-------|
| SRS-REL-001 | System Uptime | 99.9% | RAII Design Review | 1-4 |
| SRS-REL-002 | Message Delivery | 100% | Integration Test | 2 |
| SRS-REL-003 | Graceful Degradation | Under high load | Stress Test | 4 |
| SRS-REL-004 | Error Recovery | Automatic | Integration Test | 2 |
| SRS-REL-005 | Queue Persistence | Survive restart | System Test | 2 |

**Validation Method:** Stress testing, failure injection

**Status:** ⏳ **Validation Planned (Phase 1-4)**

---

### 3.3 Security (SRS-SEC)

| Requirement ID | Requirement | Target | Validation Method | Phase |
|----------------|-------------|--------|-------------------|-------|
| SRS-SEC-001 | TLS Support | TLS 1.2/1.3 | Security Audit | 2 |
| SRS-SEC-002 | Access Logging | Complete | Audit Review | 1 |
| SRS-SEC-003 | Audit Trail | HIPAA compliant | Compliance Audit | 4 |
| SRS-SEC-004 | Input Validation | 100% | Security Test | 1 |
| SRS-SEC-005 | Certificate Management | X.509 | Security Test | 2 |

**Security Tests Planned:**
- AddressSanitizer: Memory error detection
- LeakSanitizer: Memory leak detection
- ThreadSanitizer: Data race detection
- TLS cipher suite validation

**Status:** ⏳ **Validation Planned (Phase 1-4)**

---

### 3.4 Scalability (SRS-SCALE)

| Requirement ID | Requirement | Target | Validation Method | Phase |
|----------------|-------------|--------|-------------------|-------|
| SRS-SCALE-001 | Horizontal Scaling | Supported | Architecture Review | 4 |
| SRS-SCALE-002 | Daily Message Volume | ≥100K | Load Test | 4 |
| SRS-SCALE-003 | MWL Entry Capacity | ≥10K | Capacity Test | 4 |
| SRS-SCALE-004 | Connection Pooling | Efficient | Performance Test | 4 |

**Status:** ⏳ **Validation Planned (Phase 4)**

---

### 3.5 Maintainability (SRS-MAINT)

| Requirement ID | Requirement | Target | Validation Method | Phase |
|----------------|-------------|--------|-------------------|-------|
| SRS-MAINT-001 | Code Coverage | ≥80% | CI/CD | 1-4 |
| SRS-MAINT-002 | Documentation | Complete | Review | 1-4 |
| SRS-MAINT-003 | CI/CD Pipeline | 100% green | Automation | 1 |
| SRS-MAINT-004 | Configuration | Externalized | Code Review | 1 |
| SRS-MAINT-005 | Logging | Structured JSON | Code Review | 1 |

**Status:** ⏳ **Validation Planned (Phase 1-4)**

---

## 4. Acceptance Test Plan

### 4.1 User Scenario Validation (Planned)

| Scenario | Description | Phase | Status |
|----------|-------------|-------|--------|
| **US-001** | RIS sends imaging order via HL7 ORM | 1 | ⏳ Planned |
| **US-002** | Modality queries Worklist and receives MWL | 1 | ⏳ Planned |
| **US-003** | Modality sends MPPS, RIS receives status update | 2 | ⏳ Planned |
| **US-004** | Patient demographics update propagates to MWL | 1 | ⏳ Planned |
| **US-005** | Order cancellation removes MWL entry | 1 | ⏳ Planned |
| **US-006** | EMR creates order via FHIR ServiceRequest | 3 | ⏳ Planned |
| **US-007** | Secure TLS HL7 communication | 2 | ⏳ Planned |

### 4.2 IHE SWF Profile Compliance (Planned)

| IHE Transaction | IHE ID | PACS Bridge Role | Validation Phase |
|-----------------|--------|------------------|------------------|
| Placer Order Management | RAD-2 | Receiver | Phase 1 |
| Filler Order Management | RAD-3 | Sender | Phase 2 |
| Procedure Scheduled | RAD-4 | Sender | Phase 1 |
| Modality PS In Progress | RAD-6 | Receiver | Phase 2 |
| Modality PS Completed | RAD-7 | Receiver | Phase 2 |

---

## 5. Traceability Matrix

### 5.1 SRS to Validation Test Mapping

| SRS ID | Validation Test | Test Type | Phase |
|--------|-----------------|-----------|-------|
| SRS-HL7-001 | VAL-HL7-001 | System Test | 1 |
| SRS-HL7-002 | VAL-HL7-002 | Integration | 1 |
| SRS-HL7-003 | VAL-HL7-003 | Integration | 1 |
| SRS-HL7-004 | VAL-HL7-004 | Integration | 2 |
| SRS-HL7-005 | VAL-HL7-005 | Integration | 2 |
| SRS-HL7-006 | VAL-HL7-006 | System Test | 1 |
| SRS-MLLP-001 | VAL-MLLP-001 | System Test | 1 |
| SRS-MLLP-002 | VAL-MLLP-002 | Integration | 1 |
| SRS-MLLP-003 | VAL-MLLP-003 | Security Test | 2 |
| SRS-FHIR-001 | VAL-FHIR-001 | System Test | 3 |
| SRS-FHIR-002 | VAL-FHIR-002 | Integration | 3 |
| SRS-FHIR-003 | VAL-FHIR-003 | Integration | 3 |
| SRS-FHIR-004 | VAL-FHIR-004 | Integration | 3 |
| SRS-TRANS-001 | VAL-TRANS-001 | System Test | 1 |
| SRS-TRANS-002 | VAL-TRANS-002 | Integration | 2 |
| SRS-TRANS-003 | VAL-TRANS-003 | Unit Test | 1 |
| SRS-TRANS-004 | VAL-TRANS-004 | Unit Test | 1 |
| SRS-TRANS-005 | VAL-TRANS-005 | Integration | 3 |
| SRS-ROUTE-001 | VAL-ROUTE-001 | System Test | 1 |
| SRS-ROUTE-002 | VAL-ROUTE-002 | System Test | 2 |
| SRS-ROUTE-003 | VAL-ROUTE-003 | System Test | 1-2 |
| SRS-PACS-001 | VAL-PACS-001 | Integration | 1 |
| SRS-PACS-002 | VAL-PACS-002 | Integration | 2 |
| SRS-PACS-003 | VAL-PACS-003 | Integration | 1 |
| SRS-CFG-001 | VAL-CFG-001 | System Test | 1 |
| SRS-CFG-002 | VAL-CFG-002 | System Test | 2 |
| SRS-PERF-001~006 | Performance Tests | Load Test | 4 |
| SRS-REL-001~005 | Reliability Tests | Stress Test | 1-4 |
| SRS-SEC-001~005 | Security Tests | Security Audit | 1-4 |
| SRS-SCALE-001~004 | Scalability Tests | Capacity Test | 4 |
| SRS-MAINT-001~005 | Maintainability Review | Code Review | 1-4 |

### 5.2 Coverage Summary (Planned)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    SRS Requirement Validation Coverage                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   Category              Total   Planned   Phase                          │
│   ────────              ─────   ───────   ─────                          │
│                                                                          │
│   HL7 Gateway Module      6        6      Phase 1-2                      │
│   MLLP Transport          3        3      Phase 1-2                      │
│   FHIR Gateway Module     4        4      Phase 3                        │
│   Translation Layer       5        5      Phase 1-3                      │
│   Message Routing         3        3      Phase 1-2                      │
│   pacs_system Integ       3        3      Phase 1-2                      │
│   Configuration           2        2      Phase 1-2                      │
│   Performance             6        6      Phase 4                        │
│   Reliability             5        5      Phase 1-4                      │
│   Security                5        5      Phase 1-4                      │
│   Scalability             4        4      Phase 4                        │
│   Maintainability         5        5      Phase 1-4                      │
│   ────────────────────────────────────────────────────────────────────  │
│   Total                  51       51      Phase 1-4                      │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Comparison with pacs_system Validation

### 6.1 Validation Approach Alignment

| Aspect | pacs_system | pacs_bridge | Alignment |
|--------|-------------|-------------|-----------|
| **V-Model Traceability** | PRD→SRS→SDS→Code→Tests | PRD→SRS→SDS→Code→Tests | ✅ Aligned |
| **Test Categories** | System, Integration, Unit, Acceptance | System, Integration, Unit, Acceptance | ✅ Aligned |
| **Acceptance Criteria** | Detailed per requirement | Detailed per requirement | ✅ Aligned |
| **Performance Metrics** | Quantitative targets | Quantitative targets | ✅ Aligned |
| **Security Testing** | Sanitizers, TLS validation | Sanitizers, TLS validation | ✅ Aligned |
| **Dual Language** | EN + KO | EN + KO | ✅ Aligned |

### 6.2 Requirement Coverage Comparison

| Category | pacs_system | pacs_bridge |
|----------|-------------|-------------|
| Functional Requirements | 30 | 26 |
| Non-Functional Requirements | 19 | 25 |
| **Total Requirements** | **49** | **51** |
| **Validation Status** | 100% Validated | 100% Planned |

---

## 7. Phase-wise Validation Schedule

### 7.1 Phase 1 (Weeks 1-6): Core HL7 Gateway

| Validation ID | Requirement | Priority |
|---------------|-------------|----------|
| VAL-HL7-001 | HL7 v2.x Message Parsing | Must Have |
| VAL-HL7-002 | ADT Message Processing | Must Have |
| VAL-HL7-003 | ORM Message Processing | Must Have |
| VAL-HL7-006 | ACK Response Generation | Must Have |
| VAL-MLLP-001 | MLLP Server | Must Have |
| VAL-MLLP-002 | MLLP Client | Must Have |
| VAL-TRANS-001 | HL7-DICOM MWL Mapper | Must Have |
| VAL-TRANS-003 | Patient Name Conversion | Must Have |
| VAL-TRANS-004 | Date/Time Conversion | Must Have |
| VAL-ROUTE-001 | Inbound Message Router | Must Have |
| VAL-ROUTE-003 | Message Queue Manager | Must Have |
| VAL-PACS-001 | MWL Entry Management | Must Have |
| VAL-PACS-003 | Patient Cache | Must Have |
| VAL-CFG-001 | Endpoint Configuration | Must Have |

### 7.2 Phase 2 (Weeks 7-10): MPPS & Bidirectional

| Validation ID | Requirement | Priority |
|---------------|-------------|----------|
| VAL-HL7-004 | ORU Message Generation | Should Have |
| VAL-HL7-005 | SIU Message Processing | Should Have |
| VAL-MLLP-003 | MLLP over TLS | Should Have |
| VAL-TRANS-002 | DICOM MPPS-HL7 Mapper | Must Have |
| VAL-ROUTE-002 | Outbound Message Router | Must Have |
| VAL-PACS-002 | MPPS Event Handling | Must Have |
| VAL-CFG-002 | Mapping Configuration | Should Have |

### 7.3 Phase 3 (Weeks 11-16): FHIR & Reporting

| Validation ID | Requirement | Priority |
|---------------|-------------|----------|
| VAL-FHIR-001 | FHIR REST Server | Should Have |
| VAL-FHIR-002 | Patient Resource | Should Have |
| VAL-FHIR-003 | ServiceRequest Resource | Should Have |
| VAL-FHIR-004 | ImagingStudy Resource | Should Have |
| VAL-TRANS-005 | FHIR-DICOM Mapper | Should Have |

### 7.4 Phase 4 (Weeks 17-20): Production Hardening

| Validation ID | Requirement | Priority |
|---------------|-------------|----------|
| VAL-PERF-001~006 | Performance Testing | Must Have |
| VAL-SCALE-001~004 | Scalability Testing | Should Have |
| VAL-SEC-001~005 | Security Audit | Must Have |
| VAL-REL-001~005 | Reliability Testing | Must Have |

---

## 8. Conclusion

### 8.1 Validation Plan Summary

PACS Bridge has a comprehensive validation plan aligned with pacs_system:

| Aspect | Status |
|--------|--------|
| **Functional Requirements** | 26 requirements with planned tests |
| **Non-Functional Requirements** | 25 requirements with planned tests |
| **User Scenarios** | 7 acceptance tests planned |
| **IHE SWF Compliance** | 5 transactions planned |
| **Total Validation Tests** | 51 planned |

### 8.2 Readiness Assessment

| Aspect | Status | Confidence |
|--------|--------|------------|
| Requirement Completeness | ✅ Complete | High |
| Design Completeness | ✅ Complete | High |
| Validation Plan | ✅ Complete | High |
| Test Case Design | ⏳ Pending | Medium |
| Phase 1 Ready | ✅ Ready | High |

**Validation Plan Status: Complete**

---

## Appendix A: Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0.0 | 2025-12-07 | kcenon@naver.com | Initial validation plan (pre-implementation) |

---

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| **Validation** | Confirming requirements meet user needs ("right product") |
| **Verification** | Confirming implementation matches design ("product right") |
| **SRS** | Software Requirements Specification |
| **Acceptance Test** | Test confirming user scenario completion |
| **System Test** | Test confirming functional requirement compliance |

---

## Appendix C: Related Documents

| Document | Purpose | Link |
|----------|---------|------|
| SRS.md | Software Requirements Specification | [SRS.md](SRS.md) |
| SDS.md | Software Design Specification | [SDS.md](SDS.md) |
| SDS_TRACEABILITY.md | Design Traceability | [SDS_TRACEABILITY.md](SDS_TRACEABILITY.md) |
| VERIFICATION_REPORT.md | Design Verification | [VERIFICATION_REPORT.md](VERIFICATION_REPORT.md) |
| PRD.md | Product Requirements Document | [PRD.md](PRD.md) |

---

*Report Version: 1.0.0*
*Created: 2025-12-07*
*Validator: kcenon@naver.com*
