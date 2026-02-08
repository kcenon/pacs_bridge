# Software Requirements Specification - PACS Bridge

> **Version:** 0.2.1.0
> **Last Updated:** 2026-02-08
> **Language:** **English** | [한국어](SRS_KO.md)
> **Standard:** IEEE 830-1998 based

---

## Document Information

| Item | Description |
|------|-------------|
| **Document ID** | BRIDGE-SRS-001 |
| **Project** | PACS Bridge |
| **Author** | kcenon@naver.com |
| **Status** | Active |
| **Related Documents** | [PRD](PRD.md), [Reference Materials](reference_materials/README.md) |

---

## Table of Contents

- [1. Introduction](#1-introduction)
- [2. Overall Description](#2-overall-description)
- [3. Specific Requirements](#3-specific-requirements)
- [4. External Interface Requirements](#4-external-interface-requirements)
- [5. System Features](#5-system-features)
- [6. Non-Functional Requirements](#6-non-functional-requirements)
- [7. Requirements Traceability Matrix](#7-requirements-traceability-matrix)
- [8. Appendices](#8-appendices)

---

## 1. Introduction

### 1.1 Purpose

This Software Requirements Specification (SRS) document provides a complete description of all software requirements for the PACS Bridge system. It establishes the basis for agreement between stakeholders and the development team on what the software product will do.

### 1.2 Scope

The PACS Bridge is a healthcare integration gateway that:
- Bridges HL7 v2.x and FHIR R4 messaging with DICOM services
- Translates orders from HIS/RIS to DICOM Modality Worklist entries
- Converts MPPS notifications to HL7 status updates
- Implements IHE Scheduled Workflow (SWF) integration profile
- Integrates natively with pacs_system MWL/MPPS services

### 1.3 Definitions, Acronyms, and Abbreviations

| Term | Definition |
|------|------------|
| **HIS** | Hospital Information System |
| **RIS** | Radiology Information System |
| **EMR/EHR** | Electronic Medical/Health Records |
| **HL7** | Health Level Seven International messaging standard |
| **FHIR** | Fast Healthcare Interoperability Resources |
| **MLLP** | Minimal Lower Layer Protocol (HL7 transport) |
| **ADT** | Admission, Discharge, Transfer (HL7 message type) |
| **ORM** | Order Message (HL7 message type) |
| **ORU** | Observation Result (HL7 message type) |
| **SIU** | Scheduling Information Unsolicited (HL7 message type) |
| **ACK** | Acknowledgment (HL7 message type) |
| **MWL** | Modality Worklist |
| **MPPS** | Modality Performed Procedure Step |
| **IHE** | Integrating the Healthcare Enterprise |
| **SWF** | Scheduled Workflow (IHE profile) |
| **SCP** | Service Class Provider |
| **SCU** | Service Class User |

### 1.4 References

| Reference | Description |
|-----------|-------------|
| HL7 v2.5.1 | HL7 Version 2.5.1 Specification |
| FHIR R4 | HL7 FHIR Release 4 Specification |
| DICOM PS3.4 | DICOM Service Class Specifications |
| IHE RAD TF-1/2 | IHE Radiology Technical Framework |
| PRD-BRIDGE-001 | Product Requirements Document |
| pacs_system SRS | pacs_system Software Requirements Specification |

### 1.5 Overview

This document is organized as follows:
- **Section 2**: Overall system description and constraints
- **Section 3**: Detailed functional requirements with traceability
- **Section 4**: External interface requirements
- **Section 5**: System feature specifications
- **Section 6**: Non-functional requirements
- **Section 7**: Requirements traceability matrix
- **Section 8**: Appendices

---

## 2. Overall Description

### 2.1 Product Perspective

```
┌─────────────────────────────────────────────────────────────────────────┐
│                       Healthcare Integration Ecosystem                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐             │
│    │     HIS      │    │     RIS      │    │   EMR/EHR    │             │
│    │   (HL7 v2.x) │    │  (HL7 v2.x)  │    │  (FHIR R4)   │             │
│    └───────┬──────┘    └───────┬──────┘    └───────┬──────┘             │
│            │                   │                   │                     │
│            └───────────────────┼───────────────────┘                     │
│                                │ ADT/ORM/ORU/SIU                         │
│                                ▼                                         │
│    ┌─────────────────────────────────────────────────────────────────┐  │
│    │                        PACS Bridge                               │  │
│    │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌───────────┐  │  │
│    │  │ HL7 Gateway │ │FHIR Gateway │ │Msg Router   │ │Translation│  │  │
│    │  │ (MLLP)      │ │ (REST)      │ │& Queue      │ │Layer      │  │  │
│    │  └──────┬──────┘ └──────┬──────┘ └──────┬──────┘ └─────┬─────┘  │  │
│    │         └───────────────┴───────────────┴───────────────┘        │  │
│    │                              │                                   │  │
│    │  ┌───────────────────────────▼──────────────────────────────┐   │  │
│    │  │                  pacs_system Adapter                      │   │  │
│    │  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────┐  │   │  │
│    │  │  │ MWL Client  │ │MPPS Handler │ │ Patient Cache       │  │   │  │
│    │  │  └─────────────┘ └─────────────┘ └─────────────────────┘  │   │  │
│    │  └──────────────────────────────────────────────────────────┘   │  │
│    └─────────────────────────────────────────────────────────────────┘  │
│                                │                                         │
│                                │ DICOM (MWL/MPPS)                        │
│                                ▼                                         │
│    ┌─────────────────────────────────────────────────────────────────┐  │
│    │                        pacs_system                               │  │
│    │  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────────────┐  │  │
│    │  │ Worklist SCP│ │  MPPS SCP   │ │    Index Database           │  │  │
│    │  └─────────────┘ └─────────────┘ └─────────────────────────────┘  │  │
│    └─────────────────────────────────────────────────────────────────┘  │
│                                │                                         │
│            ┌───────────────────┼───────────────────┐                     │
│            ▼                   ▼                   ▼                     │
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐             │
│    │   Modality   │    │   Modality   │    │   Modality   │             │
│    │  (CT/MR/US)  │    │  (CT/MR/US)  │    │  (CT/MR/US)  │             │
│    └──────────────┘    └──────────────┘    └──────────────┘             │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Product Functions

| Function | Description |
|----------|-------------|
| **HL7 Message Processing** | Parse and process ADT, ORM, ORU, SIU messages |
| **FHIR Resource Handling** | Handle Patient, ServiceRequest, ImagingStudy resources |
| **Protocol Translation** | Convert between HL7, FHIR, and DICOM formats |
| **MWL Management** | Create and update Modality Worklist entries |
| **MPPS Notification** | Receive MPPS events and notify HIS/RIS |
| **Message Routing** | Route messages between systems with failover |
| **Queue Management** | Queue outbound messages with guaranteed delivery |

### 2.3 User Classes and Characteristics

| User Class | Characteristics | Technical Level |
|------------|-----------------|-----------------|
| Integration Engineer | Configures HL7/DICOM mappings | Expert |
| Radiology IT Admin | Manages worklist and routing | Advanced |
| Medical Equipment Vendor | Integrates modality software | Advanced |
| System Administrator | Monitors system health | Intermediate |

### 2.4 Operating Environment

| Component | Requirement |
|-----------|-------------|
| **Operating System** | Linux (Ubuntu 22.04+), macOS 14+, Windows 10/11 |
| **Compiler** | C++23 (GCC 13+, Clang 16+, MSVC 2022 v17.0+) |
| **Memory** | Minimum 2 GB, Recommended 8 GB |
| **Network** | 100 Mbps Ethernet minimum |
| **Dependencies** | pacs_system v0.2.0+, kcenon ecosystem |

### 2.5 Design and Implementation Constraints

| Constraint | Description |
|------------|-------------|
| **C1** | Must use pacs_system for DICOM services |
| **C2** | Must use kcenon ecosystem components |
| **C3** | Cross-platform compatibility required |
| **C4** | HL7 v2.3 to v2.5.1 compatibility required |
| **C5** | IHE SWF profile conformance required |
| **C6** | BSD 3-Clause license compatibility |

### 2.6 Assumptions and Dependencies

| ID | Assumption/Dependency |
|----|----------------------|
| **A1** | pacs_system MWL/MPPS services are operational |
| **A2** | network_system provides stable MLLP transport (ecosystem mode); BSD sockets used in standalone mode |
| **A3** | HIS/RIS systems implement standard HL7 v2.x |
| **A4** | FHIR endpoints follow R4 specification |
| **D1** | pacs_system v0.2.0+ available |
| **D2** | common_system v1.0+ available |
| **D3** | container_system v1.0+ available |
| **D4** | network_system v1.0+ available |
| **D5** | thread_system v1.0+ available |
| **D6** | logger_system v1.0+ available |

---

## 3. Specific Requirements

### 3.1 HL7 Gateway Module Requirements

#### SRS-HL7-001: HL7 v2.x Message Parsing
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-HL7-001 |
| **Title** | HL7 v2.x Message Parser |
| **Description** | The system shall parse HL7 v2.x messages (v2.3 to v2.5.1) with support for standard delimiters, escape sequences, repeating fields, and custom Z-segments. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-1.1.1 - FR-1.1.5 |

**Acceptance Criteria:**
1. Parse standard segment delimiters (|^~\&)
2. Handle escape sequences (\F\, \S\, \R\, \E\, \T\)
3. Support repeating fields with ~ separator
4. Support component/subcomponent separation
5. Extract all standard segment data

**Message Structure:**
```
MSH|^~\&|SendingApp|SendingFac|ReceivingApp|ReceivingFac|DateTime||MessageType|ControlID|P|Version
PID|Set ID|External ID|Patient ID|Alt Patient ID|Patient Name|...
ORC|Order Control|Placer Order|Filler Order|...
OBR|Set ID|Placer Order|Filler Order|Universal Service ID|...
```

---

#### SRS-HL7-002: ADT Message Processing
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-HL7-002 |
| **Title** | ADT Message Handler |
| **Description** | The system shall process ADT messages (A01, A04, A08, A40) to maintain patient demographic cache and update MWL entries. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-1.2.1 |

**Acceptance Criteria:**
1. Process ADT^A01 (Admit) - Create patient record
2. Process ADT^A04 (Register) - Create outpatient record
3. Process ADT^A08 (Update) - Update patient demographics
4. Process ADT^A40 (Merge) - Merge patient records
5. Update all related MWL entries on patient update

**ADT to Patient Cache Mapping:**

| HL7 Field | Cache Field | Description |
|-----------|-------------|-------------|
| PID-3 | patient_id | Patient identifier |
| PID-3.4 | issuer | Assigning authority |
| PID-5 | patient_name | Patient name (XPN) |
| PID-7 | birth_date | Date of birth |
| PID-8 | sex | Administrative sex |
| PID-11 | address | Patient address |
| PID-13 | phone | Contact number |

---

#### SRS-HL7-003: ORM Message Processing
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-HL7-003 |
| **Title** | ORM Order Message Handler |
| **Description** | The system shall process ORM^O01 messages to create, update, cancel, and manage MWL entries based on order control codes. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-1.2.2, FR-3.1.1 - FR-3.1.6 |

**Acceptance Criteria:**
1. Create MWL entry on ORC-1 = NW (New Order)
2. Update MWL entry on ORC-1 = XO (Change Order)
3. Cancel MWL entry on ORC-1 = CA (Cancel Request)
4. Handle status changes on ORC-1 = SC (Status Change)
5. Support discontinuation on ORC-1 = DC (Discontinue)

**Order Control Processing:**

| ORC-1 | ORC-5 | Action | MWL Status |
|-------|-------|--------|------------|
| NW | SC | Create new entry | SCHEDULED |
| NW | IP | Create, mark started | STARTED |
| XO | SC | Update existing | SCHEDULED |
| XO | IP | Update, mark started | STARTED |
| CA | CA | Remove entry | (deleted) |
| DC | CA | Discontinue entry | DISCONTINUED |
| SC | IP | Status → In Progress | STARTED |
| SC | CM | Status → Completed | COMPLETED |

---

#### SRS-HL7-004: ORU Message Generation
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-HL7-004 |
| **Title** | ORU Result Message Generator |
| **Description** | The system shall generate ORU^R01 messages for report status notifications, including preliminary and final report statuses. |
| **Priority** | Should Have |
| **Phase** | 2 |
| **Traces To** | FR-1.2.3, FR-3.3.1 - FR-3.3.4 |

**Acceptance Criteria:**
1. Generate ORU^R01 for preliminary reports (OBR-25 = P)
2. Generate ORU^R01 for final reports (OBR-25 = F)
3. Include proper OBX segments with LOINC codes
4. Support report amendments (OBR-25 = C)
5. Include referring physician information

---

#### SRS-HL7-005: SIU Message Processing
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-HL7-005 |
| **Title** | SIU Scheduling Message Handler |
| **Description** | The system shall process SIU^S12-S15 messages for scheduling updates, supporting appointment creation, modification, and cancellation. |
| **Priority** | Should Have |
| **Phase** | 2 |
| **Traces To** | FR-1.2.4 |

**Acceptance Criteria:**
1. Process SIU^S12 (New Appointment)
2. Process SIU^S13 (Rescheduled)
3. Process SIU^S14 (Modification)
4. Process SIU^S15 (Cancellation)
5. Update MWL scheduling information

---

#### SRS-HL7-006: ACK Response Generation
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-HL7-006 |
| **Title** | Acknowledgment Response Generator |
| **Description** | The system shall generate proper HL7 ACK responses (AA, AE, AR) with appropriate error codes and descriptions. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-1.2.5 |

**Acceptance Criteria:**
1. Generate AA (Application Accept) on success
2. Generate AE (Application Error) on business logic errors
3. Generate AR (Application Reject) on system errors
4. Include ERR segment with error details
5. Echo original MSH-10 in MSA-2

**ACK Response Structure:**
```
MSH|^~\&|PACS|RADIOLOGY|HIS|HOSPITAL|DateTime||ACK^O01|ControlID|P|2.5.1
MSA|AA|OriginalMsgID|Message accepted
```

```
MSH|^~\&|PACS|RADIOLOGY|HIS|HOSPITAL|DateTime||ACK^O01|ControlID|P|2.5.1
MSA|AE|OriginalMsgID|Missing required field
ERR|||207^Application internal error^HL70357|E|||OBR-4 required
```

---

### 3.2 MLLP Transport Requirements

#### SRS-MLLP-001: MLLP Server Implementation
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-MLLP-001 |
| **Title** | MLLP Server (Listener) |
| **Description** | The system shall implement an MLLP server that listens for incoming HL7 connections, handles message framing, and supports concurrent connections. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-1.3.1, FR-1.3.3, FR-1.3.4 |

**Acceptance Criteria:**
1. Listen on configurable TCP port (default 2575)
2. Handle MLLP framing (VT=0x0B, FS=0x1C, CR=0x0D)
3. Support concurrent connections (≥50)
4. Configurable connection timeout
5. Connection keep-alive support

**MLLP Frame Structure:**
```
<VT>HL7 Message Content<FS><CR>

VT = 0x0B (Vertical Tab)
FS = 0x1C (File Separator)
CR = 0x0D (Carriage Return)
```

---

#### SRS-MLLP-002: MLLP Client Implementation
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-MLLP-002 |
| **Title** | MLLP Client (Sender) |
| **Description** | The system shall implement an MLLP client for sending HL7 messages to remote systems with connection pooling and retry support. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-1.3.2, FR-1.3.3 |

**Acceptance Criteria:**
1. Connect to configurable host and port
2. Apply MLLP framing to outbound messages
3. Wait for and validate ACK response
4. Connection pooling for persistent connections
5. Configurable retry with exponential backoff

---

#### SRS-MLLP-003: MLLP over TLS
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-MLLP-003 |
| **Title** | Secure MLLP Transport |
| **Description** | The system shall support MLLP over TLS 1.2/1.3 for secure HL7 message transport with certificate validation. |
| **Priority** | Should Have |
| **Phase** | 2 |
| **Traces To** | FR-1.3.5, NFR-4.1 |

**Acceptance Criteria:**
1. Support TLS 1.2 and TLS 1.3
2. Server certificate validation
3. Optional client certificate (mutual TLS)
4. Configurable cipher suites
5. Certificate chain validation

---

### 3.3 FHIR Gateway Requirements

#### SRS-FHIR-001: FHIR REST Server
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-FHIR-001 |
| **Title** | FHIR R4 REST API Server |
| **Description** | The system shall implement a FHIR R4 compliant REST server supporting JSON and XML formats with standard CRUD operations. |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Traces To** | FR-2.1.1 - FR-2.1.5 |

**Acceptance Criteria:**
1. RESTful API endpoints for resources
2. JSON and XML content negotiation
3. Standard HTTP methods (GET, POST, PUT, DELETE)
4. Search parameter support
5. Pagination for large result sets

**FHIR Base URL Structure:**
```
https://server/fhir/r4/{ResourceType}/{id}
```

---

#### SRS-FHIR-002: Patient Resource
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-FHIR-002 |
| **Title** | FHIR Patient Resource Support |
| **Description** | The system shall support FHIR Patient resource with read and search operations, mapping to internal patient cache. |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Traces To** | FR-2.2.1 |

**Acceptance Criteria:**
1. GET /Patient/{id} - Read patient
2. GET /Patient?identifier=xxx - Search by ID
3. GET /Patient?name=xxx - Search by name
4. Map to internal patient demographics
5. Support patient identifier systems

---

#### SRS-FHIR-003: ServiceRequest Resource
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-FHIR-003 |
| **Title** | FHIR ServiceRequest (Imaging Order) |
| **Description** | The system shall support FHIR ServiceRequest for imaging orders, creating MWL entries from incoming requests. |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Traces To** | FR-2.2.2 |

**Acceptance Criteria:**
1. POST /ServiceRequest - Create order
2. GET /ServiceRequest/{id} - Read order
3. GET /ServiceRequest?patient=xxx - Search by patient
4. Map to DICOM MWL entry
5. Support order status updates

---

#### SRS-FHIR-004: ImagingStudy Resource
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-FHIR-004 |
| **Title** | FHIR ImagingStudy Resource |
| **Description** | The system shall support FHIR ImagingStudy resource for study availability notifications and queries. |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Traces To** | FR-2.2.3, FR-2.3.2 |

**Acceptance Criteria:**
1. GET /ImagingStudy/{id} - Read study
2. GET /ImagingStudy?patient=xxx - Search by patient
3. GET /ImagingStudy?status=xxx - Search by status
4. Include series and instance counts
5. Reference to ServiceRequest

---

### 3.4 Protocol Translation Requirements

#### SRS-TRANS-001: HL7 to DICOM MWL Mapper
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-001 |
| **Title** | HL7 ORM to DICOM MWL Translation |
| **Description** | The system shall translate HL7 ORM message fields to DICOM Modality Worklist attributes according to IHE SWF profile mapping. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-3.1.1 - FR-3.1.4 |

**Acceptance Criteria:**
1. Map PID segment to Patient Module
2. Map ORC/OBR to Imaging Service Request
3. Map OBR to Scheduled Procedure Step Sequence
4. Map ZDS-1 to Study Instance UID
5. Handle procedure code mapping

**Core Mapping Table:**

| HL7 Field | DICOM Tag | DICOM Keyword |
|-----------|-----------|---------------|
| PID-3 | (0010,0020) | PatientID |
| PID-3.4 | (0010,0021) | IssuerOfPatientID |
| PID-5 | (0010,0010) | PatientName |
| PID-7 | (0010,0030) | PatientBirthDate |
| PID-8 | (0010,0040) | PatientSex |
| ORC-2 | (0040,2016) | PlacerOrderNumberImagingServiceRequest |
| ORC-3 | (0008,0050) | AccessionNumber |
| ORC-12 | (0008,0090) | ReferringPhysicianName |
| OBR-4.1 | (0008,0100) | CodeValue (in RequestedProcedureCodeSequence) |
| OBR-4.2 | (0008,0104) | CodeMeaning |
| OBR-4.3 | (0008,0102) | CodingSchemeDesignator |
| OBR-7 | (0040,0002) | ScheduledProcedureStepStartDate |
| OBR-7 | (0040,0003) | ScheduledProcedureStepStartTime |
| OBR-24 | (0008,0060) | Modality |
| ZDS-1 | (0020,000D) | StudyInstanceUID |

---

#### SRS-TRANS-002: DICOM MPPS to HL7 ORM Mapper
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-002 |
| **Title** | DICOM MPPS to HL7 ORM Translation |
| **Description** | The system shall translate DICOM MPPS notifications to HL7 ORM status update messages. |
| **Priority** | Must Have |
| **Phase** | 2 |
| **Traces To** | FR-3.2.1 - FR-3.2.5 |

**Acceptance Criteria:**
1. Map MPPS IN PROGRESS to ORC-5 = IP
2. Map MPPS COMPLETED to ORC-5 = CM
3. Map MPPS DISCONTINUED to ORC-5 = CA
4. Include procedure timing information
5. Reference original order

**MPPS Status Mapping:**

| MPPS Status | ORC-1 | ORC-5 | Description |
|-------------|-------|-------|-------------|
| IN PROGRESS | SC | IP | Exam started |
| COMPLETED | SC | CM | Exam completed |
| DISCONTINUED | DC | CA | Exam discontinued |

---

#### SRS-TRANS-003: Patient Name Format Conversion
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-003 |
| **Title** | Patient Name Format Translation |
| **Description** | The system shall convert between HL7 XPN and DICOM PN name formats, handling component order differences. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-3.1.2 |

**Acceptance Criteria:**
1. Parse HL7 XPN format (Family^Given^Middle^Suffix^Prefix)
2. Convert to DICOM PN format (Family^Given^Middle^Prefix^Suffix)
3. Handle missing components
4. Support multi-valued names
5. Preserve special characters

**Format Conversion:**
```
HL7 XPN: DOE^JOHN^ANDREW^JR^MR^MD
         Family^Given^Middle^Suffix^Prefix^Degree

DICOM PN: DOE^JOHN^ANDREW^MR^JR
          Family^Given^Middle^Prefix^Suffix
```

---

#### SRS-TRANS-004: Date/Time Format Conversion
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-004 |
| **Title** | Date/Time Format Translation |
| **Description** | The system shall convert between HL7 DTM and DICOM DA/TM date/time formats. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-3.1.3 |

**Acceptance Criteria:**
1. Parse HL7 DTM (YYYYMMDDHHMMSS.SSSS±ZZZZ)
2. Extract DICOM DA (YYYYMMDD)
3. Extract DICOM TM (HHMMSS.FFFFFF)
4. Handle timezone conversion
5. Handle partial precision

---

#### SRS-TRANS-005: FHIR to DICOM Mapper
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-005 |
| **Title** | FHIR ServiceRequest to DICOM MWL |
| **Description** | The system shall translate FHIR ServiceRequest resources to DICOM MWL entries. |
| **Priority** | Should Have |
| **Phase** | 3 |
| **Traces To** | FR-2.2.2 |

**Acceptance Criteria:**
1. Map Patient reference to Patient Module
2. Map ServiceRequest.code to Requested Procedure
3. Map ServiceRequest.occurrence to SPS Start Date/Time
4. Map ServiceRequest.performer to Scheduled AE Title
5. Generate Study Instance UID if not provided

---

### 3.5 Message Routing Requirements

#### SRS-ROUTE-001: Inbound Message Router
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-ROUTE-001 |
| **Title** | Inbound Message Routing |
| **Description** | The system shall route inbound messages to appropriate handlers based on message type and trigger event. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-4.1.1 - FR-4.1.4 |

**Acceptance Criteria:**
1. Route ADT messages to patient cache handler
2. Route ORM messages to MWL manager
3. Route based on MSH-9 (Message Type)
4. Support conditional routing rules
5. Log routing decisions

**Routing Table:**

| Message Type | Trigger | Handler |
|--------------|---------|---------|
| ADT | A01, A04, A08, A40 | PatientCacheHandler |
| ORM | O01 | MWLManager |
| SIU | S12-S15 | SchedulingHandler |

---

#### SRS-ROUTE-002: Outbound Message Router
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-ROUTE-002 |
| **Title** | Outbound Message Routing |
| **Description** | The system shall route outbound messages to configured destinations with failover support. |
| **Priority** | Must Have |
| **Phase** | 2 |
| **Traces To** | FR-4.2.1 - FR-4.2.4 |

**Acceptance Criteria:**
1. Route MPPS notifications to RIS
2. Route ORU reports to configured endpoints
3. Support multiple destinations
4. Implement failover routing
5. Track delivery status

---

#### SRS-ROUTE-003: Message Queue Manager
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-ROUTE-003 |
| **Title** | Outbound Message Queue |
| **Description** | The system shall queue outbound messages for reliable delivery with retry logic and persistence. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-4.3.1 - FR-4.3.4 |

**Acceptance Criteria:**
1. Queue messages for asynchronous delivery
2. Retry with exponential backoff
3. Support message prioritization
4. Persist queue for crash recovery
5. Dead letter queue for failed messages

**Retry Strategy:**
```
Attempt 1: Immediate
Attempt 2: 5 seconds
Attempt 3: 30 seconds
Attempt 4: 2 minutes
Attempt 5: 10 minutes
Max Attempts: 5 (configurable)
```

---

### 3.6 pacs_system Integration Requirements

#### SRS-PACS-001: MWL Entry Management
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PACS-001 |
| **Title** | Modality Worklist Integration |
| **Description** | The system shall create, update, and delete MWL entries in pacs_system via DICOM or direct database access. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | IR-1, FR-3.1.1 |

**Acceptance Criteria:**
1. Create MWL entry from ORM^O01
2. Update MWL entry on order changes
3. Delete MWL entry on cancellation
4. Handle duplicate detection
5. Support batch operations

---

#### SRS-PACS-002: MPPS Event Handling
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PACS-002 |
| **Title** | MPPS Notification Receiver |
| **Description** | The system shall receive MPPS N-CREATE and N-SET notifications from pacs_system and convert them to HL7 messages. |
| **Priority** | Must Have |
| **Phase** | 2 |
| **Traces To** | IR-1, FR-3.2.1 - FR-3.2.5 |

**Acceptance Criteria:**
1. Register as MPPS event listener
2. Receive N-CREATE (IN PROGRESS)
3. Receive N-SET (COMPLETED/DISCONTINUED)
4. Generate HL7 ORM status message
5. Send to configured RIS endpoint

---

#### SRS-PACS-003: Patient Cache Synchronization
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PACS-003 |
| **Title** | Patient Demographics Cache |
| **Description** | The system shall maintain a patient demographics cache synchronized with HIS ADT events for MWL queries. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-4.1.1 |

**Acceptance Criteria:**
1. Cache patient demographics from ADT
2. Update on ADT^A08 (Update)
3. Handle ADT^A40 (Merge)
4. Provide lookup by Patient ID
5. Time-based cache expiration

---

### 3.7 Configuration Requirements

#### SRS-CFG-001: Endpoint Configuration
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CFG-001 |
| **Title** | System Endpoint Configuration |
| **Description** | The system shall support configuration of HL7 listener ports, outbound destinations, and pacs_system connection. |
| **Priority** | Must Have |
| **Phase** | 1 |
| **Traces To** | FR-5.1.1 - FR-5.1.4 |

**Acceptance Criteria:**
1. Configure HL7 listener port
2. Configure outbound HL7 destinations
3. Configure pacs_system connection
4. Support configuration file (YAML)
5. Support hot-reload (Phase 3)

**Configuration Example:**
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

---

#### SRS-CFG-002: Mapping Configuration
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CFG-002 |
| **Title** | Protocol Mapping Configuration |
| **Description** | The system shall support configurable mappings for modality-to-AE-title, procedure codes, and patient ID domains. |
| **Priority** | Should Have |
| **Phase** | 2 |
| **Traces To** | FR-5.2.1 - FR-5.2.4 |

**Acceptance Criteria:**
1. Configure modality to AE title mapping
2. Configure procedure code mappings
3. Configure patient ID domain mappings
4. Support custom field mappings
5. Validate configuration on load

---

### 3.8 EMR Integration Requirements (Phase 5)

#### SRS-EMR-001: FHIR R4 Client
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-EMR-001 |
| **Title** | FHIR R4 HTTP Client |
| **Description** | The system shall implement a FHIR R4 client for outbound HTTP communication with EMR systems, supporting Bundle operations and connection pooling. |
| **Priority** | Should Have |
| **Phase** | 5 |
| **Traces To** | FR-6.1.1 - FR-6.1.4 |

**Acceptance Criteria:**
1. Perform FHIR RESTful HTTP requests (GET, POST, PUT)
2. Support Bundle transaction and batch operations
3. Implement connection pooling for persistent connections
4. Support OAuth2 token-based authentication
5. Handle FHIR OperationOutcome error responses

---

#### SRS-EMR-002: Patient Lookup Service
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-EMR-002 |
| **Title** | EMR Patient Lookup |
| **Description** | The system shall query external EMR systems for patient demographics via FHIR Patient resource search, with result caching and patient matching logic. |
| **Priority** | Should Have |
| **Phase** | 5 |
| **Traces To** | FR-6.2.1 - FR-6.2.4 |

**Acceptance Criteria:**
1. Query EMR by patient identifier (MRN, national ID)
2. Parse FHIR Patient resource responses
3. Implement configurable patient matching algorithm
4. Cache lookup results with configurable TTL
5. Handle partial matches and ambiguous results

---

#### SRS-EMR-003: Result Posting Service
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-EMR-003 |
| **Title** | EMR Result Posting |
| **Description** | The system shall post radiology reports and study results to external EMR systems as FHIR DiagnosticReport resources. |
| **Priority** | Should Have |
| **Phase** | 5 |
| **Traces To** | FR-6.3.1 - FR-6.3.4 |

**Acceptance Criteria:**
1. Build FHIR DiagnosticReport from internal report data
2. Post DiagnosticReport to EMR endpoint
3. Track delivery status with retry on failure
4. Include encounter context and patient reference
5. Support preliminary and final report statuses

---

#### SRS-EMR-004: EMR Adapter Pattern
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-EMR-004 |
| **Title** | Abstract EMR Adapter Interface |
| **Description** | The system shall define an abstract EMR adapter interface to support multiple EMR system types through a pluggable architecture. |
| **Priority** | Should Have |
| **Phase** | 5 |
| **Traces To** | FR-6.4.1 - FR-6.4.3 |

**Acceptance Criteria:**
1. Define abstract interface for EMR operations (lookup, post, subscribe)
2. Implement generic FHIR-based adapter as default
3. Support EMR-specific configuration per deployment
4. Allow runtime selection of EMR adapter
5. Provide health-check and connection status monitoring
6. Support vendor-specific configurations (Epic, Cerner, Meditech, Allscripts)

---

#### SRS-EMR-005: Encounter Context Service
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-EMR-005 |
| **Title** | Clinical Encounter Context Provider |
| **Description** | The system shall query external EMR systems for encounter/visit context to correlate radiology results with clinical encounters, supporting FHIR Encounter resource lookups with caching. |
| **Priority** | Should Have |
| **Phase** | 5 |
| **Traces To** | FR-6.3.4 |

**Acceptance Criteria:**
1. Query encounters by encounter ID
2. Find encounters by visit number with optional identifier system
3. Find active encounters for a given patient
4. Parse FHIR Encounter resources including status, class, participants, and locations
5. Cache encounter lookups with configurable TTL
6. Handle encounter statuses (planned, arrived, in-progress, finished, cancelled)

---

#### SRS-EMR-006: Result Delivery Tracking
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-EMR-006 |
| **Title** | EMR Result Delivery Tracker |
| **Description** | The system shall track the delivery status of results posted to external EMR systems, supporting lookup by study UID, accession number, or report ID, with configurable capacity and TTL-based expiration. |
| **Priority** | Should Have |
| **Phase** | 5 |
| **Traces To** | FR-6.3.3 |

**Acceptance Criteria:**
1. Track posted results with study UID, accession number, and report ID
2. Support lookup by study instance UID, accession number, or report ID
3. Detect duplicate postings before submission
4. Enforce configurable capacity limits (default: 10,000 entries)
5. Implement TTL-based expiration with automatic cleanup (default: 7 days)
6. Provide in-memory implementation with thread-safe operations

---

#### SRS-EMR-007: FHIR DiagnosticReport Builder
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-EMR-007 |
| **Title** | FHIR DiagnosticReport Resource Builder |
| **Description** | The system shall construct valid FHIR R4 DiagnosticReport resources from internal study results using a fluent builder pattern, with validation and support for standard coding systems (LOINC, SNOMED CT). |
| **Priority** | Should Have |
| **Phase** | 5 |
| **Traces To** | FR-6.3.1, FR-6.3.2 |

**Acceptance Criteria:**
1. Build DiagnosticReport from internal study result data
2. Support required fields: status, code, subject
3. Include category (radiology), effective date/time, issued date
4. Attach performer and results interpreter references
5. Support conclusion text and coded conclusion (SNOMED CT)
6. Include identifiers (accession number, study instance UID)
7. Link to encounter, imaging study, and service request references
8. Validate resource completeness before serialization

---

## 4. External Interface Requirements

### 4.1 User Interfaces

The PACS Bridge provides no direct user interface. All interaction is through:
- HL7 v2.x messaging (MLLP)
- FHIR R4 REST API (HTTPS)
- Configuration files (YAML)
- Administrative CLI (future)

### 4.2 Hardware Interfaces

| Interface | Description |
|-----------|-------------|
| Network | 100 Mbps Ethernet minimum |
| Memory | 2 GB minimum, 8 GB recommended |
| Storage | 10 GB for queue persistence |

### 4.3 Software Interfaces

| Interface | Protocol | Description |
|-----------|----------|-------------|
| HIS | HL7 v2.x / MLLP | Hospital Information System |
| RIS | HL7 v2.x / MLLP | Radiology Information System |
| EMR | FHIR R4 / HTTPS | Electronic Medical Records |
| pacs_system | DICOM | PACS services |

### 4.4 Communications Interfaces

| Protocol | Port | Description |
|----------|------|-------------|
| HL7/MLLP | 2575 | Inbound HL7 messages |
| HL7/MLLP | Variable | Outbound to RIS |
| HTTPS | 8080 | FHIR REST API |
| DICOM | 11112 | pacs_system connection |

---

## 5. System Features

### 5.1 Feature: HL7 Order Processing

**Description:** Receive and process imaging orders from HIS/RIS.

| Attribute | Value |
|-----------|-------|
| **Priority** | Must Have |
| **Phase** | 1 |
| **SRS Requirements** | SRS-HL7-001, SRS-HL7-003, SRS-MLLP-001, SRS-TRANS-001 |
| **PRD Requirements** | FR-1.1.x, FR-1.2.2, FR-3.1.x |

**Use Case:**
1. RIS sends ORM^O01 (New Order) via MLLP
2. PACS Bridge parses and validates message
3. Translates HL7 fields to DICOM attributes
4. Creates MWL entry in pacs_system
5. Sends ACK response to RIS

---

### 5.2 Feature: Modality Worklist Bridge

**Description:** Provide worklist data to modalities via DICOM C-FIND.

| Attribute | Value |
|-----------|-------|
| **Priority** | Must Have |
| **Phase** | 1 |
| **SRS Requirements** | SRS-PACS-001, SRS-TRANS-001 |
| **PRD Requirements** | FR-3.1.1 - FR-3.1.6 |

**Use Case:**
1. Modality queries pacs_system MWL SCP
2. pacs_system queries internal MWL database
3. MWL entries created by PACS Bridge are returned
4. Modality receives patient/procedure information

---

### 5.3 Feature: MPPS Notification

**Description:** Notify RIS of procedure status changes.

| Attribute | Value |
|-----------|-------|
| **Priority** | Must Have |
| **Phase** | 2 |
| **SRS Requirements** | SRS-PACS-002, SRS-TRANS-002, SRS-MLLP-002 |
| **PRD Requirements** | FR-3.2.1 - FR-3.2.5 |

**Use Case:**
1. Modality sends MPPS N-CREATE (IN PROGRESS) to pacs_system
2. pacs_system notifies PACS Bridge
3. PACS Bridge translates to ORM^O01 (ORC-5=IP)
4. Sends status update to RIS via MLLP
5. RIS acknowledges with ACK

---

### 5.4 Feature: FHIR Integration

**Description:** Provide REST API for modern EMR integration.

| Attribute | Value |
|-----------|-------|
| **Priority** | Should Have |
| **Phase** | 3 |
| **SRS Requirements** | SRS-FHIR-001 - SRS-FHIR-004, SRS-TRANS-005 |
| **PRD Requirements** | FR-2.1.x, FR-2.2.x |

**Use Case:**
1. EMR posts ServiceRequest (imaging order)
2. PACS Bridge creates MWL entry
3. EMR subscribes to ImagingStudy updates
4. PACS Bridge notifies on study completion

---

### 5.5 Feature: Message Queue and Retry

**Description:** Ensure reliable message delivery with persistence.

| Attribute | Value |
|-----------|-------|
| **Priority** | Must Have |
| **Phase** | 2 |
| **SRS Requirements** | SRS-ROUTE-003 |
| **PRD Requirements** | FR-4.3.1 - FR-4.3.4 |

**Use Case:**
1. Outbound message queued for delivery
2. First attempt fails (RIS unavailable)
3. Message persisted to disk
4. Retry with exponential backoff
5. Delivery succeeds on retry

---

### 5.6 Feature: EMR Integration

**Description:** Post radiology results to external EMR systems and query patient/encounter context via FHIR R4.

| Attribute | Value |
|-----------|-------|
| **Priority** | Should Have |
| **Phase** | 5 |
| **SRS Requirements** | SRS-EMR-001 - SRS-EMR-007, SRS-SEC-006, SRS-SEC-007 |
| **PRD Requirements** | FR-6.1.x - FR-6.4.x |

**Use Case:**
1. PACS Bridge authenticates with EMR via OAuth2 (SMART on FHIR discovery)
2. Queries EMR for patient demographics by MRN
3. Matches patient across systems using configurable criteria
4. Retrieves active encounter context for clinical correlation
5. Builds FHIR DiagnosticReport from completed study
6. Posts result to EMR with delivery tracking and retry
7. Tracks delivery status for audit and duplicate prevention

---

## 6. Non-Functional Requirements

### 6.1 Performance Requirements

| ID | Requirement | Target | Traces To |
|----|-------------|--------|-----------|
| SRS-PERF-001 | HL7 message throughput | ≥500 msg/s | NFR-1.1 |
| SRS-PERF-002 | Message latency (P95) | <50 ms | NFR-1.2 |
| SRS-PERF-003a | MWL creation from ORM | <50 ms | PR-1, NFR-1.3 |
| SRS-PERF-003b | MWL query response | <100 ms | NFR-1.3 |
| SRS-PERF-004 | Concurrent connections | ≥50 | NFR-1.4 |
| SRS-PERF-005 | Memory baseline | <200 MB | NFR-1.5 |
| SRS-PERF-006 | CPU utilization (idle) | <20% | NFR-1.6 |

### 6.2 Reliability Requirements

| ID | Requirement | Target | Traces To |
|----|-------------|--------|-----------|
| SRS-REL-001 | System uptime | 99.9% | NFR-2.1 |
| SRS-REL-002 | Message delivery | 100% | NFR-2.2 |
| SRS-REL-003 | Graceful degradation | Under high load | NFR-2.3 |
| SRS-REL-004 | Error recovery | Automatic | NFR-2.4 |
| SRS-REL-005 | Queue persistence | Survive restart | NFR-2.5 |

### 6.3 Security Requirements

| ID | Requirement | Target | Traces To |
|----|-------------|--------|-----------|
| SRS-SEC-001 | TLS support | TLS 1.2/1.3 | NFR-4.1, SR-1 |
| SRS-SEC-002 | Access logging | Complete | NFR-4.2, SR-2 |
| SRS-SEC-003 | Audit trail | HIPAA compliant | NFR-4.3, SR-3 |
| SRS-SEC-004 | Input validation | 100% | NFR-4.4 |
| SRS-SEC-005 | Certificate management | X.509 | NFR-4.5 |
| SRS-SEC-006 | OAuth2 client credentials for EMR endpoints | Token-based | FR-6.1.4 |
| SRS-SEC-007 | SMART on FHIR endpoint discovery | Auto-discovery | FR-6.1.4 |

### 6.4 Scalability Requirements

| ID | Requirement | Target | Traces To |
|----|-------------|--------|-----------|
| SRS-SCALE-001 | Horizontal scaling | Supported | NFR-3.1 |
| SRS-SCALE-002 | Daily message volume | ≥100K | NFR-3.2 |
| SRS-SCALE-003 | MWL entry capacity | ≥10K | NFR-3.3 |
| SRS-SCALE-004 | Connection pooling | Efficient | NFR-3.4 |

### 6.5 Maintainability Requirements

| ID | Requirement | Target | Traces To |
|----|-------------|--------|-----------|
| SRS-MAINT-001 | Code coverage | ≥80% | NFR-5.1 |
| SRS-MAINT-002 | Documentation | Complete | NFR-5.2 |
| SRS-MAINT-003 | CI/CD pipeline | 100% green | NFR-5.3 |
| SRS-MAINT-004 | Configuration | Externalized | NFR-5.4 |
| SRS-MAINT-005 | Logging | Structured JSON | NFR-5.5 |

---

## 7. Requirements Traceability Matrix

### 7.1 PRD to SRS Traceability

| PRD Requirement | SRS Requirement(s) | Status |
|-----------------|-------------------|--------|
| FR-1.1.1-5 | SRS-HL7-001 | Specified |
| FR-1.2.1 | SRS-HL7-002 | Specified |
| FR-1.2.2 | SRS-HL7-003 | Specified |
| FR-1.2.3 | SRS-HL7-004 | Specified |
| FR-1.2.4 | SRS-HL7-005 | Specified |
| FR-1.2.5 | SRS-HL7-006 | Specified |
| FR-1.3.1-4 | SRS-MLLP-001, SRS-MLLP-002 | Specified |
| FR-1.3.5 | SRS-MLLP-003 | Specified |
| FR-2.1.1-5 | SRS-FHIR-001 | Specified |
| FR-2.2.1 | SRS-FHIR-002 | Specified |
| FR-2.2.2 | SRS-FHIR-003 | Specified |
| FR-2.2.3 | SRS-FHIR-004 | Specified |
| FR-3.1.1-6 | SRS-TRANS-001, SRS-PACS-001 | Specified |
| FR-3.2.1-5 | SRS-TRANS-002, SRS-PACS-002 | Specified |
| FR-4.1.1-4 | SRS-ROUTE-001 | Specified |
| FR-4.2.1-4 | SRS-ROUTE-002 | Specified |
| FR-4.3.1-4 | SRS-ROUTE-003 | Specified |
| FR-5.1.1-4 | SRS-CFG-001 | Specified |
| FR-5.2.1-4 | SRS-CFG-002 | Specified |
| FR-6.1.1-4 | SRS-EMR-001, SRS-SEC-006, SRS-SEC-007 | Specified |
| FR-6.2.1-4 | SRS-EMR-002 | Specified |
| FR-6.3.1-2 | SRS-EMR-003, SRS-EMR-007 | Specified |
| FR-6.3.3 | SRS-EMR-006 | Specified |
| FR-6.3.4 | SRS-EMR-005 | Specified |
| FR-6.4.1-3 | SRS-EMR-004 | Specified |
| NFR-1.1-6 | SRS-PERF-001-006 | Specified |
| NFR-2.1-5 | SRS-REL-001-005 | Specified |
| NFR-3.1-4 | SRS-SCALE-001-004 | Specified |
| NFR-4.1-5 | SRS-SEC-001-005 | Specified |
| FR-6.1.4 (OAuth2) | SRS-SEC-006, SRS-SEC-007 | Specified |
| NFR-5.1-5 | SRS-MAINT-001-005 | Specified |

### 7.2 SRS to Test Case Traceability (Template)

| SRS Requirement | Test Case ID | Test Type | Status |
|-----------------|-------------|-----------|--------|
| SRS-HL7-001 | TC-HL7-001 | Unit | Planned |
| SRS-HL7-002 | TC-HL7-002 | Integration | Planned |
| SRS-HL7-003 | TC-HL7-003 | Integration | Planned |
| SRS-HL7-004 | TC-HL7-004 | Integration | Planned |
| SRS-HL7-005 | TC-HL7-005 | Integration | Planned |
| SRS-HL7-006 | TC-HL7-006 | Unit | Planned |
| SRS-MLLP-001 | TC-MLLP-001 | Integration | Planned |
| SRS-MLLP-002 | TC-MLLP-002 | Integration | Planned |
| SRS-MLLP-003 | TC-MLLP-003 | Integration | Planned |
| SRS-FHIR-001 | TC-FHIR-001 | Integration | Planned |
| SRS-TRANS-001 | TC-TRANS-001 | Unit | Planned |
| SRS-TRANS-002 | TC-TRANS-002 | Unit | Planned |
| SRS-ROUTE-001 | TC-ROUTE-001 | Unit | Planned |
| SRS-ROUTE-002 | TC-ROUTE-002 | Integration | Planned |
| SRS-ROUTE-003 | TC-ROUTE-003 | Integration | Planned |
| SRS-PACS-001 | TC-PACS-001 | Integration | Planned |
| SRS-PACS-002 | TC-PACS-002 | Integration | Planned |
| SRS-EMR-001 | TC-EMR-001 | Integration | Planned |
| SRS-EMR-002 | TC-EMR-002 | Integration | Planned |
| SRS-EMR-003 | TC-EMR-003 | Integration | Planned |
| SRS-EMR-004 | TC-EMR-004 | Unit | Planned |
| SRS-EMR-005 | TC-EMR-005 | Integration | Planned |
| SRS-EMR-006 | TC-EMR-006 | Unit | Planned |
| SRS-EMR-007 | TC-EMR-007 | Unit | Planned |
| SRS-SEC-006 | TC-SEC-006 | Integration | Planned |
| SRS-SEC-007 | TC-SEC-007 | Integration | Planned |

### 7.3 Cross-Reference Summary

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Requirements Traceability Overview                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   PRD                    SRS                      Test Cases            │
│   ───                    ───                      ──────────            │
│                                                                          │
│   FR-1.x  ────────────►  SRS-HL7-xxx   ────────►  TC-HL7-xxx           │
│   (HL7 Gateway)          (HL7 Module)             (HL7 Tests)           │
│                                                                          │
│   FR-1.3.x ───────────►  SRS-MLLP-xxx  ────────►  TC-MLLP-xxx          │
│   (MLLP Transport)       (MLLP Module)            (Transport Tests)     │
│                                                                          │
│   FR-2.x  ────────────►  SRS-FHIR-xxx  ────────►  TC-FHIR-xxx          │
│   (FHIR Gateway)         (FHIR Module)            (FHIR Tests)          │
│                                                                          │
│   FR-3.x  ────────────►  SRS-TRANS-xxx ────────►  TC-TRANS-xxx         │
│   (Translation)          SRS-PACS-xxx             (Mapping Tests)       │
│                                                                          │
│   FR-4.x  ────────────►  SRS-ROUTE-xxx ────────►  TC-ROUTE-xxx         │
│   (Routing)              (Routing Module)         (Routing Tests)       │
│                                                                          │
│   FR-5.x  ────────────►  SRS-CFG-xxx   ────────►  TC-CFG-xxx           │
│   (Configuration)        (Config Module)          (Config Tests)        │
│                                                                          │
│   FR-6.x  ────────────►  SRS-EMR-xxx   ────────►  TC-EMR-xxx           │
│   (EMR Integration)      (EMR Module)             (EMR Tests)           │
│                                                                          │
│   NFR-x   ────────────►  SRS-PERF-xxx  ────────►  TC-PERF-xxx          │
│   (Non-Functional)       SRS-REL-xxx              (Performance)         │
│                          SRS-SEC-xxx              (Load Tests)          │
│                          SRS-SCALE-xxx                                  │
│                          SRS-MAINT-xxx                                  │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 8. Appendices

### Appendix A: Requirement ID Scheme

| Prefix | Category | Example |
|--------|----------|---------|
| SRS-HL7-xxx | HL7 Gateway module | SRS-HL7-001 |
| SRS-MLLP-xxx | MLLP transport | SRS-MLLP-001 |
| SRS-FHIR-xxx | FHIR Gateway module | SRS-FHIR-001 |
| SRS-TRANS-xxx | Translation/Mapping | SRS-TRANS-001 |
| SRS-ROUTE-xxx | Message routing | SRS-ROUTE-001 |
| SRS-PACS-xxx | pacs_system integration | SRS-PACS-001 |
| SRS-CFG-xxx | Configuration | SRS-CFG-001 |
| SRS-PERF-xxx | Performance | SRS-PERF-001 |
| SRS-REL-xxx | Reliability | SRS-REL-001 |
| SRS-SEC-xxx | Security | SRS-SEC-001 |
| SRS-SCALE-xxx | Scalability | SRS-SCALE-001 |
| SRS-MAINT-xxx | Maintainability | SRS-MAINT-001 |
| SRS-EMR-xxx | EMR Integration | SRS-EMR-001 |

### Appendix B: Error Code Registry

```
pacs_bridge error codes: -900 to -999

HL7 Parsing Errors (-900 to -919):
  -900: INVALID_HL7_MESSAGE
  -901: MISSING_MSH_SEGMENT
  -902: INVALID_SEGMENT_STRUCTURE
  -903: MISSING_REQUIRED_FIELD
  -904: INVALID_FIELD_VALUE
  -905: UNKNOWN_MESSAGE_TYPE

MLLP Transport Errors (-920 to -939):
  -920: MLLP_CONNECTION_FAILED
  -921: MLLP_SEND_FAILED
  -922: MLLP_RECEIVE_TIMEOUT
  -923: MLLP_INVALID_FRAME
  -924: MLLP_TLS_HANDSHAKE_FAILED

Translation Errors (-940 to -959):
  -940: MAPPING_FAILED
  -941: MISSING_MAPPING_CONFIG
  -942: INVALID_CODE_SYSTEM
  -943: PATIENT_NOT_FOUND
  -944: ORDER_NOT_FOUND

Queue Errors (-960 to -979):
  -960: QUEUE_FULL
  -961: MESSAGE_EXPIRED
  -962: DELIVERY_FAILED
  -963: RETRY_LIMIT_EXCEEDED

pacs_system Integration Errors (-980 to -999):
  -980: PACS_CONNECTION_FAILED
  -981: MWL_UPDATE_FAILED
  -982: MPPS_HANDLER_ERROR
  -983: DICOM_TRANSLATION_ERROR

EMR Integration error codes: -1000 to -1124

EMR Client Errors (-1000 to -1019):
  -1000: EMR_CONNECTION_FAILED
  -1001: EMR_REQUEST_FAILED
  -1002: EMR_RESPONSE_PARSE_ERROR
  -1003: EMR_TIMEOUT
  -1004: EMR_UNAUTHORIZED
  -1005: EMR_FORBIDDEN
  -1006: EMR_RESOURCE_NOT_FOUND
  -1007: EMR_CONFLICT
  -1008: EMR_GONE
  -1009: EMR_PRECONDITION_FAILED
  -1010: EMR_UNPROCESSABLE
  -1011: EMR_TOO_MANY_REQUESTS
  -1012: EMR_SERVER_ERROR

OAuth2 Errors (-1020 to -1039):
  -1020: OAUTH2_TOKEN_REQUEST_FAILED
  -1021: OAUTH2_TOKEN_PARSE_ERROR
  -1022: OAUTH2_TOKEN_EXPIRED
  -1023: OAUTH2_REFRESH_FAILED
  -1024: OAUTH2_INVALID_SCOPE
  -1025: OAUTH2_DISCOVERY_FAILED
  -1026: OAUTH2_INVALID_CONFIGURATION

Patient Errors (-1040 to -1059):
  -1040: PATIENT_NOT_FOUND
  -1041: PATIENT_QUERY_FAILED
  -1042: PATIENT_AMBIGUOUS_MATCH
  -1043: PATIENT_NO_MATCH
  -1044: PATIENT_PARSE_ERROR
  -1045: PATIENT_CACHE_ERROR
  -1046: PATIENT_INVALID_IDENTIFIER

Result Posting Errors (-1060 to -1079):
  -1060: RESULT_POST_FAILED
  -1061: RESULT_UPDATE_FAILED
  -1062: RESULT_DUPLICATE
  -1063: RESULT_INVALID_DATA
  -1064: RESULT_REJECTED
  -1065: RESULT_NOT_FOUND
  -1066: RESULT_INVALID_STATUS_TRANSITION
  -1067: RESULT_MISSING_REFERENCE
  -1068: RESULT_BUILD_FAILED
  -1069: RESULT_TRACKER_ERROR

Encounter Errors (-1080 to -1099):
  -1080: ENCOUNTER_NOT_FOUND
  -1081: ENCOUNTER_QUERY_FAILED
  -1082: ENCOUNTER_MULTIPLE_ACTIVE
  -1083: ENCOUNTER_ENDED
  -1084: ENCOUNTER_INVALID_DATA
  -1085: ENCOUNTER_VISIT_NOT_FOUND
  -1086: ENCOUNTER_INVALID_STATUS
  -1087: ENCOUNTER_LOCATION_NOT_FOUND
  -1088: ENCOUNTER_PRACTITIONER_NOT_FOUND
  -1089: ENCOUNTER_PARSE_FAILED

Adapter Errors (-1100 to -1119):
  -1100: ADAPTER_NOT_INITIALIZED
  -1101: ADAPTER_CONNECTION_FAILED
  -1102: ADAPTER_AUTHENTICATION_FAILED
  -1103: ADAPTER_NOT_SUPPORTED
  -1104: ADAPTER_INVALID_CONFIGURATION
  -1105: ADAPTER_TIMEOUT
  -1106: ADAPTER_RATE_LIMITED
  -1107: ADAPTER_INVALID_VENDOR
  -1108: ADAPTER_HEALTH_CHECK_FAILED
  -1109: ADAPTER_FEATURE_UNAVAILABLE

Tracker Errors (-1120 to -1124):
  -1120: TRACKER_NOT_FOUND
  -1121: TRACKER_CAPACITY_EXCEEDED
  -1122: TRACKER_INVALID_ENTRY
  -1123: TRACKER_ALREADY_EXISTS
  -1124: TRACKER_OPERATION_FAILED
```

### Appendix C: IHE SWF Transaction Mapping

| IHE Transaction | PACS Bridge Role | Implementation |
|-----------------|------------------|----------------|
| RAD-2 (Placer Order) | Receiver | SRS-HL7-003 (ORM processing) |
| RAD-3 (Filler Order) | Sender | SRS-TRANS-002 (MPPS to ORM) |
| RAD-4 (Procedure Scheduled) | Sender | SRS-PACS-001 (MWL creation) |
| RAD-6 (MPPS In Progress) | Receiver | SRS-PACS-002 (MPPS handler) |
| RAD-7 (MPPS Completed) | Receiver | SRS-PACS-002 (MPPS handler) |

### Appendix D: Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0.0 | 2025-12-07 | kcenon | Initial version |
| 2.0.0 | 2026-02-07 | kcenon | C++20→C++23, added SRS-EMR-001-004 (Phase 5 EMR Integration), split SRS-PERF-003 into creation/query, added requirement count reconciliation |
| 2.1.0 | 2026-02-08 | kcenon | Expanded EMR Integration: added SRS-EMR-005 (Encounter Context), SRS-EMR-006 (Result Tracking), SRS-EMR-007 (DiagnosticReport Builder), SRS-SEC-006/007 (OAuth2/SMART), added EMR error code registry (-1000 to -1124), added Section 5.6 (EMR Integration feature) |

### Appendix E: Glossary

| Term | Definition |
|------|------------|
| Accession Number | Unique identifier for an imaging order/study |
| MLLP Frame | Message envelope with VT prefix and FS/CR suffix |
| Placer Order | Order created by requesting system (HIS) |
| Filler Order | Order accepted by performing system (RIS) |
| Presentation Context | Agreement on message syntax in DICOM |
| Worklist Entry | Scheduled procedure item for modality query |

---

### Appendix F: Requirement Count Reconciliation

> **Note**: The PRD defines **77 functional requirement items** at the FR-x.y.z granularity level (62 original + 15 new EMR requirements). The SRS aggregates these into **33 functional SRS requirements** (26 original + 7 EMR). This is intentional — each SRS requirement covers a group of related PRD items (e.g., FR-1.1.1 through FR-1.1.5 → SRS-HL7-001). Additionally, there are **27 non-functional SRS requirements** (25 original + 2 EMR security), totaling **60 SRS requirements**.

---

*Document Version: 0.2.1.0*
*Created: 2025-12-07*
*Updated: 2026-02-08*
*Author: kcenon@naver.com*
