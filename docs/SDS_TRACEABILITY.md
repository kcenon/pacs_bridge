# SDS - Requirements Traceability Matrix

> **Version:** 0.2.0.0
> **Parent Document:** [SDS.md](SDS.md)
> **Last Updated:** 2026-02-07

---

## Table of Contents

- [1. Overview](#1-overview)
- [2. Functional Requirements Traceability](#2-functional-requirements-traceability)
- [3. Non-Functional Requirements Traceability](#3-non-functional-requirements-traceability)
- [4. Design Element Index](#4-design-element-index)
- [5. Test Case Mapping](#5-test-case-mapping)
- [6. Gap Analysis](#6-gap-analysis)

---

## 1. Overview

### 1.1 Purpose

This document establishes bidirectional traceability between:
- **SRS Requirements** → **Design Elements** → **Test Cases**

### 1.2 Traceability Legend

| Symbol | Meaning |
|--------|---------|
| ✓ | Fully traced and implemented |
| ◐ | Partially traced |
| ○ | Planned but not yet designed |
| - | Not applicable |

### 1.3 Document References

| Document | Description |
|----------|-------------|
| SRS.md | Software Requirements Specification |
| SDS.md | Software Design Specification (main) |
| SDS_COMPONENTS.md | Component Designs |
| SDS_INTERFACES.md | Interface Specifications |
| SDS_SEQUENCES.md | Sequence Diagrams |

---

## 2. Functional Requirements Traceability

### 2.1 FR-1: HL7 v2.x Gateway

| Requirement ID | Requirement | Design Element | Sequence | Status |
|---------------|-------------|----------------|----------|--------|
| FR-1.1.1 | Parse HL7 v2.x messages (v2.3.1-v2.9) | DES-HL7-002 (hl7_parser) | SEQ-001 | ✓ |
| FR-1.1.2 | Support standard delimiters and escape sequences | DES-HL7-001 (hl7_delimiters) | SEQ-001 | ✓ |
| FR-1.1.3 | Handle repeating fields and components | DES-HL7-001 (hl7_field) | SEQ-001 | ✓ |
| FR-1.1.4 | Validate message structure against schemas | DES-HL7-004 (hl7_validator) | - | ✓ |
| FR-1.1.5 | Support Z-segments (custom segments) | DES-HL7-002 (hl7_parser) | SEQ-003 | ✓ |
| FR-1.2.1 | Process ADT^A01, A04, A08, A40 | DES-HL7-004, INT-MOD-001 | SEQ-002 | ✓ |
| FR-1.2.2 | Process ORM^O01 | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-1.2.3 | Generate ORU^R01 | DES-HL7-003 (hl7_builder) | - | ✓ |
| FR-1.2.4 | Process SIU^S12-S15 | DES-HL7-004 | - | ✓ |
| FR-1.2.5 | Generate ACK responses | DES-HL7-003, INT-API-002 | SEQ-001 | ✓ |
| FR-1.3.1 | Implement MLLP server (listener) | DES-MLLP-001 | SEQ-001 | ✓ |
| FR-1.3.2 | Implement MLLP client (sender) | DES-MLLP-002 | SEQ-005 | ✓ |
| FR-1.3.3 | Support persistent and transient connections | DES-MLLP-001, DES-MLLP-002 | - | ✓ |
| FR-1.3.4 | Handle message framing (VT, FS, CR) | DES-MLLP-003 | SEQ-001 | ✓ |
| FR-1.3.5 | Support MLLP over TLS | DES-MLLP-001, DES-MLLP-002 | - | ✓ |

### 2.2 FR-2: FHIR R4 Gateway

| Requirement ID | Requirement | Design Element | Sequence | Status |
|---------------|-------------|----------------|----------|--------|
| FR-2.1.1 | Implement FHIR REST server | DES-FHIR-001 | SEQ-007 | ✓ |
| FR-2.1.2 | Support CRUD operations on resources | DES-FHIR-001 | SEQ-007, SEQ-008 | ✓ |
| FR-2.1.3 | Implement search parameters | DES-FHIR-001, INT-EXT-002 | SEQ-008 | ✓ |
| FR-2.1.4 | Support JSON and XML formats | DES-FHIR-002 | - | ◐ |
| FR-2.1.5 | Handle pagination for large result sets | DES-FHIR-001 | - | ○ |
| FR-2.2.1 | Support Patient resource | DES-FHIR-002 (patient_resource) | - | ✓ |
| FR-2.2.2 | Support ServiceRequest (imaging orders) | DES-FHIR-002 (service_request_resource) | SEQ-007 | ✓ |
| FR-2.2.3 | Support ImagingStudy resource | DES-FHIR-002 (imaging_study_resource) | SEQ-008 | ✓ |
| FR-2.2.4 | Support DiagnosticReport resource | DES-FHIR-002 (diagnostic_report_resource) | - | ✓ |
| FR-2.2.5 | Support Task (worklist items) | DES-FHIR-002 | - | ○ |
| FR-2.3.1 | Support REST-hook subscriptions | DES-FHIR-003 (subscription_resource) | - | ✓ |
| FR-2.3.2 | Notify on study availability | DES-FHIR-003 | - | ◐ |
| FR-2.3.3 | Notify on report completion | DES-FHIR-003 | - | ◐ |

### 2.3 FR-3: DICOM Integration

| Requirement ID | Requirement | Design Element | Sequence | Status |
|---------------|-------------|----------------|----------|--------|
| FR-3.1.1 | Convert HL7 ORM to DICOM MWL entries | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-3.1.2 | Map patient demographics (PID → Patient Module) | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-3.1.3 | Map order information (ORC/OBR → SPS) | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-3.1.4 | Support Study Instance UID pre-assignment (ZDS) | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-3.1.5 | Handle order cancellation (ORC-1=CA) | DES-TRANS-001, DES-PACS-001 | SEQ-004 | ✓ |
| FR-3.1.6 | Handle order modification (ORC-1=XO) | DES-TRANS-001, DES-PACS-001 | - | ✓ |
| FR-3.2.1 | Receive MPPS N-CREATE notifications | DES-PACS-002 | SEQ-005 | ✓ |
| FR-3.2.2 | Convert MPPS IN PROGRESS to HL7 ORM status | DES-TRANS-002 | SEQ-005 | ✓ |
| FR-3.2.3 | Receive MPPS N-SET (COMPLETED) notifications | DES-PACS-002 | SEQ-006 | ✓ |
| FR-3.2.4 | Convert MPPS COMPLETED to HL7 ORM status | DES-TRANS-002 | SEQ-006 | ✓ |
| FR-3.2.5 | Handle MPPS DISCONTINUED | DES-TRANS-002 | - | ✓ |
| FR-3.3.1 | Receive report status from PACS | DES-PACS-002 | - | ✓ |
| FR-3.3.2 | Generate ORU^R01 for preliminary reports | DES-HL7-003, DES-TRANS-002 | - | ✓ |
| FR-3.3.3 | Generate ORU^R01 for final reports | DES-HL7-003, DES-TRANS-002 | - | ✓ |
| FR-3.3.4 | Support report amendments | DES-HL7-003 | - | ○ |

### 2.4 FR-4: Message Routing

| Requirement ID | Requirement | Design Element | Sequence | Status |
|---------------|-------------|----------------|----------|--------|
| FR-4.1.1 | Route ADT messages to patient cache | DES-ROUTE-001, DES-PACS-003 | SEQ-002 | ✓ |
| FR-4.1.2 | Route ORM messages to MWL manager | DES-ROUTE-001 | SEQ-003 | ✓ |
| FR-4.1.3 | Route based on message type and trigger | DES-ROUTE-001 | SEQ-001 | ✓ |
| FR-4.1.4 | Support conditional routing rules | DES-ROUTE-001 | - | ✓ |
| FR-4.2.1 | Route MPPS notifications to RIS | DES-ROUTE-001 | SEQ-005, SEQ-006 | ✓ |
| FR-4.2.2 | Route ORU messages to configured endpoints | DES-ROUTE-001 | - | ✓ |
| FR-4.2.3 | Support multiple destination routing | DES-ROUTE-001 | - | ✓ |
| FR-4.2.4 | Implement failover routing | DES-ROUTE-001 | - | ✓ |
| FR-4.3.1 | Queue outbound messages for reliable delivery | DES-ROUTE-002 | SEQ-009 | ✓ |
| FR-4.3.2 | Implement retry with exponential backoff | DES-ROUTE-002 | SEQ-009 | ✓ |
| FR-4.3.3 | Support message prioritization | DES-ROUTE-002 | SEQ-009 | ✓ |
| FR-4.3.4 | Persist queue for crash recovery | DES-ROUTE-002 | SEQ-009 | ✓ |

### 2.5 FR-5: Configuration Management

| Requirement ID | Requirement | Design Element | Sequence | Status |
|---------------|-------------|----------------|----------|--------|
| FR-5.1.1 | Configure HL7 listener ports | DES-CFG-001, INT-CFG-001 | - | ✓ |
| FR-5.1.2 | Configure outbound HL7 destinations | DES-CFG-001, INT-CFG-001 | - | ✓ |
| FR-5.1.3 | Configure pacs_system connection | DES-CFG-001, INT-CFG-001 | - | ✓ |
| FR-5.1.4 | Support hot-reload of configuration | DES-CFG-001, INT-API-001 | - | ○ |
| FR-5.2.1 | Configure modality-to-AE-title mapping | DES-CFG-001, INT-CFG-001 | - | ✓ |
| FR-5.2.2 | Configure procedure code mappings | DES-CFG-001, DES-TRANS-001 | - | ✓ |
| FR-5.2.3 | Configure patient ID domain mappings | DES-CFG-001 | - | ✓ |
| FR-5.2.4 | Support custom field mappings | DES-CFG-001 | - | ◐ |

---

## 3. Non-Functional Requirements Traceability

### 3.1 NFR-1: Performance

| Requirement ID | Requirement | Design Element | Implementation Strategy | Status |
|---------------|-------------|----------------|------------------------|--------|
| NFR-1.1 | Message throughput ≥500 msg/s | DES-MLLP-001, DES-ROUTE-002 | Thread pools, async I/O | ✓ |
| NFR-1.2 | Message latency (P95) <50 ms | DES-ROUTE-001 | Lock-free routing | ✓ |
| NFR-1.3 | MWL query response <100 ms | DES-PACS-001 | Connection pooling | ✓ |
| NFR-1.4 | Concurrent connections ≥50 | DES-MLLP-001 | Per-connection threads | ✓ |
| NFR-1.5 | Memory baseline <200 MB | DES-PACS-003 | Bounded caches | ✓ |
| NFR-1.6 | CPU utilization <20% (idle) | All | Efficient event loop | ✓ |

### 3.2 NFR-2: Reliability

| Requirement ID | Requirement | Design Element | Implementation Strategy | Status |
|---------------|-------------|----------------|------------------------|--------|
| NFR-2.1 | System uptime 99.9% | INT-API-001 | Health checks, auto-restart | ○ |
| NFR-2.2 | Message delivery 100% | DES-ROUTE-002 | Persistent queue, retry | ✓ |
| NFR-2.3 | Graceful degradation | INT-API-001 | Queue buffering | ✓ |
| NFR-2.4 | Error recovery (automatic) | DES-ROUTE-002 | Exponential backoff | ✓ |
| NFR-2.5 | Queue persistence | DES-ROUTE-002 | SQLite storage | ✓ |

### 3.3 NFR-3: Scalability

| Requirement ID | Requirement | Design Element | Implementation Strategy | Status |
|---------------|-------------|----------------|------------------------|--------|
| NFR-3.1 | Horizontal scaling | INT-API-001 | Stateless design | ○ |
| NFR-3.2 | Daily message volume ≥100K | DES-ROUTE-002 | Queue capacity | ✓ |
| NFR-3.3 | MWL entry capacity ≥10K | DES-PACS-001 | Database indexing | ✓ |
| NFR-3.4 | Connection pooling | DES-PACS-001, DES-MLLP-002 | Pool management | ✓ |

### 3.4 NFR-4: Security

| Requirement ID | Requirement | Design Element | Implementation Strategy | Status |
|---------------|-------------|----------------|------------------------|--------|
| NFR-4.1 | TLS support (TLS 1.2/1.3) | DES-MLLP-001, DES-FHIR-001 | OpenSSL TLS | ✓ |
| NFR-4.2 | Access logging (complete) | INT-ECO-002 | Structured logging | ✓ |
| NFR-4.3 | Audit trail (HIPAA compliant) | INT-ECO-002 | Audit file | ✓ |
| NFR-4.4 | Input validation (100%) | DES-HL7-004 | Schema validation | ✓ |
| NFR-4.5 | Certificate management (X.509) | DES-MLLP-001 | TLS config | ✓ |

### 3.5 NFR-5: Maintainability

| Requirement ID | Requirement | Design Element | Implementation Strategy | Status |
|---------------|-------------|----------------|------------------------|--------|
| NFR-5.1 | Code coverage ≥80% | All | Unit tests (94 test files) | ◐ |
| NFR-5.2 | Documentation (complete) | SDS suite | API docs, examples | ✓ |
| NFR-5.3 | CI/CD pipeline (100% green) | - | GitHub Actions | ✓ |
| NFR-5.4 | Configuration (externalized) | DES-CFG-001 | YAML/JSON files | ✓ |
| NFR-5.5 | Logging (structured) | INT-ECO-002 | JSON format | ✓ |

---

## 4. Design Element Index

### 4.1 Component Designs

| Design ID | Component | Module | SRS Reference |
|-----------|-----------|--------|---------------|
| DES-HL7-001 | hl7_message | HL7 Gateway | FR-1.1.1, FR-1.1.2, FR-1.1.3 |
| DES-HL7-002 | hl7_parser | HL7 Gateway | FR-1.1.1, FR-1.1.5 |
| DES-HL7-003 | hl7_builder | HL7 Gateway | FR-1.2.3, FR-1.2.5 |
| DES-HL7-004 | hl7_validator | HL7 Gateway | FR-1.1.4, FR-1.2 |
| DES-MLLP-001 | mllp_server | MLLP Transport | FR-1.3.1, FR-1.3.3, FR-1.3.4 |
| DES-MLLP-002 | mllp_client | MLLP Transport | FR-1.3.2, FR-1.3.3 |
| DES-MLLP-003 | mllp_connection | MLLP Transport | FR-1.3.4 |
| DES-FHIR-001 | fhir_server | FHIR Gateway | FR-2.1.1, FR-2.1.2, FR-2.1.3 |
| DES-FHIR-002 | fhir_resource | FHIR Gateway | FR-2.2.1 - FR-2.2.5 |
| DES-TRANS-001 | hl7_dicom_mapper | Translation | FR-3.1.1 - FR-3.1.6 |
| DES-TRANS-002 | dicom_hl7_mapper | Translation | FR-3.2.1 - FR-3.2.5 |
| DES-TRANS-003 | fhir_dicom_mapper | Translation | FR-2.2.2, FR-2.2.3 |
| DES-ROUTE-001 | message_router | Routing | FR-4.1, FR-4.2 |
| DES-ROUTE-002 | queue_manager | Routing | FR-4.3 |
| DES-PACS-001 | mwl_client | PACS Adapter | IR-1, FR-3.1 |
| DES-PACS-002 | mpps_handler | PACS Adapter | IR-1, FR-3.2 |
| DES-PACS-003 | patient_cache | PACS Adapter | FR-4.1.1 |
| DES-CFG-001 | bridge_config | Configuration | FR-5.1, FR-5.2 |

### 4.2 Interface Specifications

| Interface ID | Name | Type | SRS Reference |
|--------------|------|------|---------------|
| INT-EXT-001 | HL7/MLLP Interface | External | FR-1.3, PCR-1 |
| INT-EXT-002 | FHIR REST Interface | External | FR-2.1, PCR-2 |
| INT-EXT-003 | pacs_system DICOM Interface | External | IR-1 |
| INT-API-001 | bridge_server | Public API | - |
| INT-API-002 | hl7_gateway | Public API | FR-1 |
| INT-MOD-001 | hl7_message Access | Internal | FR-1.1 |
| INT-MOD-002 | Mapper Interface | Internal | FR-3 |
| INT-ECO-001 | Result<T> Pattern | Integration | - |
| INT-ECO-002 | Logger Integration | Integration | NFR-4.2, NFR-5.5 |
| INT-ERR-001 | Error Codes | Error Handling | Appendix C |
| INT-CFG-001 | Configuration File | Configuration | FR-5, Appendix D |

### 4.3 Sequence Diagrams

| Sequence ID | Name | SRS Reference |
|-------------|------|---------------|
| SEQ-001 | Inbound HL7 Message Flow | FR-1.1, FR-1.2, FR-1.3 |
| SEQ-002 | ADT Message Processing | FR-4.1.1 |
| SEQ-003 | ORM → MWL Entry Creation | FR-3.1.1 - FR-3.1.4 |
| SEQ-004 | Order Cancellation | FR-3.1.5 |
| SEQ-005 | MPPS In Progress → HL7 | FR-3.2.1, FR-3.2.2 |
| SEQ-006 | MPPS Completed → HL7 | FR-3.2.3, FR-3.2.4 |
| SEQ-007 | FHIR ServiceRequest Creation | FR-2.1.3, FR-2.2.2 |
| SEQ-008 | FHIR ImagingStudy Query | FR-2.1.2, FR-2.2.3 |
| SEQ-009 | Message Queue and Retry | FR-4.3.1, FR-4.3.2 |
| SEQ-010 | Dead Letter Handling | FR-4.3.2, NFR-2.2 |
| SEQ-011 | Invalid HL7 Message Handling | Error Handling |
| SEQ-012 | pacs_system Connection Failure | Error Handling |
| SEQ-013 | Complete IHE SWF Sequence | PCR-3 |

---

## 5. Test Case Mapping

### 5.1 Unit Test Coverage

| Design ID | Component | Test File | Test Count (Est.) |
|-----------|-----------|-----------|-------------------|
| DES-HL7-001 | hl7_message | tests/hl7/hl7_message_test.cpp | 15 |
| DES-HL7-002 | hl7_parser | tests/hl7/hl7_parser_test.cpp | 20 |
| DES-HL7-003 | hl7_builder | tests/hl7/hl7_builder_test.cpp | 15 |
| DES-HL7-004 | hl7_validator | tests/hl7/hl7_validator_test.cpp | 12 |
| DES-MLLP-001 | mllp_server | tests/mllp/mllp_server_test.cpp | 10 |
| DES-MLLP-002 | mllp_client | tests/mllp/mllp_client_test.cpp | 10 |
| DES-MLLP-003 | mllp_connection | tests/mllp/mllp_frame_test.cpp | 8 |
| DES-TRANS-001 | hl7_dicom_mapper | tests/mapping/hl7_dicom_mapper_test.cpp | 20 |
| DES-TRANS-002 | dicom_hl7_mapper | tests/dicom_hl7_mapper_test.cpp | 17 |
| DES-ROUTE-001 | message_router | tests/router/message_router_test.cpp | 10 |
| DES-ROUTE-002 | queue_manager | tests/router/queue_manager_test.cpp | 15 |
| DES-PACS-001 | mwl_client | tests/pacs/mwl_client_test.cpp | 10 |
| DES-PACS-002 | mpps_handler | tests/pacs/mpps_handler_test.cpp | 8 |
| DES-PACS-003 | patient_cache | tests/pacs/patient_cache_test.cpp | 10 |
| DES-CFG-001 | bridge_config | tests/config/bridge_config_test.cpp | 10 |

**Total Estimated Unit Tests:** ~180

### 5.2 Integration Test Scenarios

| Scenario ID | Description | Requirements Covered |
|-------------|-------------|---------------------|
| IT-001 | End-to-end ORM to MWL flow | FR-1.3, FR-3.1, FR-4.1 |
| IT-002 | MPPS notification to HL7 | FR-3.2, FR-4.2 |
| IT-003 | Queue retry and recovery | FR-4.3, NFR-2.2 |
| IT-004 | Multiple concurrent connections | NFR-1.4 |
| IT-005 | Throughput benchmark (500 msg/s) | NFR-1.1 |
| IT-006 | Patient cache synchronization | FR-4.1.1 |
| IT-007 | Order cancellation workflow | FR-3.1.5 |
| IT-008 | Error handling and NAK | Error Handling |

### 5.3 Conformance Tests

| Test ID | Standard | Description | Requirements |
|---------|----------|-------------|--------------|
| CT-001 | HL7 v2.5.1 | ADT message conformance | PCR-1 |
| CT-002 | HL7 v2.5.1 | ORM message conformance | PCR-1 |
| CT-003 | HL7 v2.5.1 | ORU message conformance | PCR-1 |
| CT-004 | MLLP | Frame handling | FR-1.3.4 |
| CT-005 | IHE SWF | RAD-2/4 transactions | PCR-3 |
| CT-006 | IHE SWF | RAD-6/7 transactions | PCR-3 |
| CT-007 | FHIR R4 | Resource validation | PCR-2 |

---

## 6. Gap Analysis

### 6.1 Requirements Not Yet Designed

| Requirement ID | Description | Priority | Target Phase |
|---------------|-------------|----------|--------------|
| FR-2.1.5 | Handle pagination for large result sets | Could Have | Future |
| FR-2.2.5 | Support Task (worklist items) | Could Have | Future |
| FR-3.3.4 | Support report amendments | Could Have | Future |
| FR-5.1.4 | Hot-reload configuration | Should Have | Future |

### 6.2 Design Elements Pending Implementation

All originally planned design elements have been implemented. Remaining gaps are low-priority features deferred to future phases.

### 6.3 Coverage Summary

| Category | Total | Designed | Implemented | Coverage |
|----------|-------|----------|-------------|----------|
| FR-1 (HL7 Gateway) | 15 | 15 | 15 | 100% |
| FR-2 (FHIR Gateway) | 13 | 13 | 10 | 77% implemented |
| FR-3 (DICOM Integration) | 14 | 14 | 13 | 93% implemented |
| FR-4 (Message Routing) | 12 | 12 | 12 | 100% |
| FR-5 (Configuration) | 8 | 8 | 7 | 88% implemented |
| **Total (Planned)** | **62** | **62** | **57** | **92% implemented** |

### 6.4 Undocumented Modules (Implemented Without PRD/SRS Coverage)

The following modules were implemented during development but have no formal requirements traceability. Requirements should be retroactively created (see PRD FR-6.x series).

| Module | Location | Source Files | Description | Proposed Requirement |
|--------|----------|-------------|-------------|---------------------|
| EMR Client | `src/emr/` | 13 | FHIR R4 client for external EMR integration (patient lookup, result posting) | FR-6.x (Phase 5) |
| Distributed Tracing | `src/tracing/` | 6 | OpenTelemetry-compatible span tracking and context propagation | NFR-6.x |
| Performance | `src/performance/` | 6 | Zero-copy parser, object pools, lock-free queues, thread pool manager | NFR-7.x |
| Messaging Patterns | `src/messaging/` | 5 | Event bus, HL7 pipeline, async request handling | Internal architecture |
| Load Testing | `src/testing/` | 3 | Load generation framework with reporting | Test infrastructure |

**Total undocumented: ~33 source files requiring retroactive requirements traceability.**

### 6.5 MLLP Adapter Architecture (Undocumented in PRD)

PRD SAR-2 states `mllp_transport` depends on `network_system`. The actual implementation uses an **adapter pattern** with three interchangeable backends:

| Backend | File | Dependency | Build Mode |
|---------|------|-----------|------------|
| BSD Sockets | `bsd_mllp_server.cpp` | None (POSIX) | `BRIDGE_STANDALONE_BUILD=ON` (default) |
| TLS | `tls_mllp_server.cpp` | OpenSSL | `BRIDGE_ENABLE_TLS=ON` |
| network_system | `network_system_mllp_server.cpp` | kcenon ecosystem | `BRIDGE_STANDALONE_BUILD=OFF` |

This flexible architecture allows standalone deployment without ecosystem dependencies, which is not reflected in the planning documents.

### 6.6 Recommended Actions

1. **Immediate:**
   - Create FR-6.x requirements for EMR Client module in PRD addendum
   - Create SRS-EMR-xxx requirements for FHIR Client functionality
   - Update C++ standard references from C++20 to C++23 across all documents

2. **Short-term:**
   - Create NFR-6.x requirements for distributed tracing
   - Create NFR-7.x requirements for performance optimizations
   - Document MLLP adapter pattern in architecture documentation

3. **Medium-term:**
   - Perform full code-to-requirement mapping audit
   - Establish documentation update policy tied to PR workflow

---

*Document Version: 0.2.0.0*
*Created: 2025-12-07*
*Updated: 2026-02-07*
*Author: kcenon@naver.com*
