# Software Design Specification - PACS Bridge

> **Document ID:** PACS-BRIDGE-SDS-001
> **Version:** 0.1.0.0
> **Last Updated:** 2025-12-07
> **Language:** **English** | [한국어](SDS_KO.md)
> **Status:** Draft

---

## Document Suite

This SDS is organized into focused modules for maintainability:

| Document | Purpose |
|----------|---------|
| **[SDS.md](SDS.md)** (this file) | Overview, principles, module summary |
| **[SDS_COMPONENTS.md](SDS_COMPONENTS.md)** | Detailed component designs |
| **[SDS_INTERFACES.md](SDS_INTERFACES.md)** | Interface specifications |
| **[SDS_SEQUENCES.md](SDS_SEQUENCES.md)** | Sequence diagrams |
| **[SDS_TRACEABILITY.md](SDS_TRACEABILITY.md)** | Requirements traceability matrix |

---

## Table of Contents

- [1. Introduction](#1-introduction)
- [2. Design Overview](#2-design-overview)
- [3. Design Principles](#3-design-principles)
- [4. Module Summary](#4-module-summary)
- [5. Design Constraints](#5-design-constraints)
- [6. Design Decisions](#6-design-decisions)
- [7. Quality Attributes](#7-quality-attributes)
- [8. References](#8-references)

---

## 1. Introduction

### 1.1 Purpose

This Software Design Specification (SDS) describes the architectural design and component structure of PACS Bridge, an integration gateway that connects Hospital Information Systems (HIS) and Radiology Information Systems (RIS) with PACS using HL7 v2.x, FHIR R4, and DICOM protocols.

This document serves as:
- A blueprint for implementation
- A reference for maintenance and enhancement
- A contract between requirements and code

### 1.2 Scope

PACS Bridge provides:

| Capability | Description |
|------------|-------------|
| **HL7 v2.x Gateway** | Parse and process HL7 messages (ADT, ORM, ORU, SIU) |
| **MLLP Transport** | Minimal Lower Layer Protocol server and client |
| **FHIR R4 Gateway** | RESTful API for modern EHR integration |
| **Protocol Translation** | HL7 ↔ DICOM, FHIR ↔ DICOM mapping |
| **Message Routing** | Configurable message routing and queue management |
| **pacs_system Integration** | MWL update, MPPS notification, patient cache |

### 1.3 Definitions and Acronyms

| Term | Definition |
|------|------------|
| ADT | Admission, Discharge, Transfer (HL7 message type) |
| DICOM | Digital Imaging and Communications in Medicine |
| FHIR | Fast Healthcare Interoperability Resources |
| HIS | Hospital Information System |
| HL7 | Health Level Seven (healthcare messaging standard) |
| IHE | Integrating the Healthcare Enterprise |
| MLLP | Minimal Lower Layer Protocol |
| MPPS | Modality Performed Procedure Step |
| MWL | Modality Worklist |
| ORM | Order Message (HL7 message type) |
| ORU | Observation Result (HL7 message type) |
| RIS | Radiology Information System |
| SCU | Service Class User |
| SCP | Service Class Provider |
| SIU | Scheduling Information Update (HL7 message type) |
| SWF | Scheduled Workflow (IHE integration profile) |

### 1.4 Document Conventions

**Design Identifier Format:** `DES-<MODULE>-<NUMBER>`

| Module ID | Description |
|-----------|-------------|
| HL7 | HL7 Gateway Module |
| MLLP | MLLP Transport Module |
| FHIR | FHIR Gateway Module |
| TRANS | Translation Layer Module |
| ROUTE | Message Routing Module |
| PACS | pacs_system Adapter Module |
| CFG | Configuration Module |
| INT | Integration Adapter Module |

---

## 2. Design Overview

### 2.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              PACS Bridge                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   ┌───────────────────────────────────────────────────────────────────────┐ │
│   │                        Gateway Layer                                   │ │
│   │                                                                        │ │
│   │   ┌─────────────────────┐          ┌─────────────────────┐           │ │
│   │   │    HL7 Gateway      │          │    FHIR Gateway     │           │ │
│   │   │                     │          │                     │           │ │
│   │   │  ┌───────────────┐  │          │  ┌───────────────┐  │           │ │
│   │   │  │ MLLP Server   │  │          │  │ REST Server   │  │           │ │
│   │   │  │ (Port 2575)   │  │          │  │ (Port 8080)   │  │           │ │
│   │   │  └───────────────┘  │          │  └───────────────┘  │           │ │
│   │   │  ┌───────────────┐  │          │  ┌───────────────┐  │           │ │
│   │   │  │ MLLP Client   │  │          │  │ REST Client   │  │           │ │
│   │   │  │ (Outbound)    │  │          │  │ (Webhook)     │  │           │ │
│   │   │  └───────────────┘  │          │  └───────────────┘  │           │ │
│   │   │  ┌───────────────┐  │          │  ┌───────────────┐  │           │ │
│   │   │  │ Message Parser│  │          │  │ JSON Parser   │  │           │ │
│   │   │  └───────────────┘  │          │  └───────────────┘  │           │ │
│   │   └─────────────────────┘          └─────────────────────┘           │ │
│   └───────────────────────────────────────────────────────────────────────┘ │
│                                      │                                       │
│                                      ▼                                       │
│   ┌───────────────────────────────────────────────────────────────────────┐ │
│   │                       Message Router                                   │ │
│   │                                                                        │ │
│   │   ┌───────────────┐  ┌───────────────┐  ┌───────────────┐            │ │
│   │   │ Inbound       │  │ Outbound      │  │ Queue         │            │ │
│   │   │ Handler       │  │ Handler       │  │ Manager       │            │ │
│   │   └───────────────┘  └───────────────┘  └───────────────┘            │ │
│   └───────────────────────────────────────────────────────────────────────┘ │
│                                      │                                       │
│                                      ▼                                       │
│   ┌───────────────────────────────────────────────────────────────────────┐ │
│   │                     Translation Layer                                  │ │
│   │                                                                        │ │
│   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │ │
│   │   │ HL7→DICOM   │  │ DICOM→HL7   │  │ FHIR→DICOM  │  │ HL7→FHIR    │ │ │
│   │   │ Mapper      │  │ Mapper      │  │ Mapper      │  │ Mapper      │ │ │
│   │   └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘ │ │
│   └───────────────────────────────────────────────────────────────────────┘ │
│                                      │                                       │
│                                      ▼                                       │
│   ┌───────────────────────────────────────────────────────────────────────┐ │
│   │                    pacs_system Adapter                                 │ │
│   │                                                                        │ │
│   │   ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────┐   │ │
│   │   │ MWL Client      │  │ MPPS Handler    │  │ Patient Cache       │   │ │
│   │   │ (C-FIND/Update) │  │ (N-CREATE/SET)  │  │ (In-Memory)         │   │ │
│   │   └─────────────────┘  └─────────────────┘  └─────────────────────┘   │ │
│   └───────────────────────────────────────────────────────────────────────┘ │
│                                      │                                       │
│   ┌───────────────────────────────────────────────────────────────────────┐ │
│   │                     Integration Layer                                  │ │
│   │                                                                        │ │
│   │   ┌───────────────┐  ┌───────────────┐  ┌───────────────┐            │ │
│   │   │ network_system│  │ thread_system │  │ logger_system │            │ │
│   │   │ Adapter       │  │ Adapter       │  │ Adapter       │            │ │
│   │   └───────────────┘  └───────────────┘  └───────────────┘            │ │
│   └───────────────────────────────────────────────────────────────────────┘ │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
                                       │
          ┌────────────────────────────┼────────────────────────────┐
          │                            │                            │
          ▼                            ▼                            ▼
┌─────────────────────┐   ┌─────────────────────┐   ┌─────────────────────┐
│     pacs_system     │   │      HIS / RIS      │   │      EMR / EHR      │
│   (DICOM MWL/MPPS)  │   │    (HL7 v2.x)       │   │     (FHIR R4)       │
└─────────────────────┘   └─────────────────────┘   └─────────────────────┘
```

### 2.2 Layered Architecture

PACS Bridge follows a strict layered architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 4: Gateway Layer                                          │
│   - HL7 Gateway (MLLP Server/Client, Message Parser)           │
│   - FHIR Gateway (REST Server, Resource Handlers)              │
├─────────────────────────────────────────────────────────────────┤
│ Layer 3: Routing Layer                                          │
│   - Message Router (Inbound/Outbound Handlers)                 │
│   - Queue Manager (Persistent, Retry Logic)                    │
├─────────────────────────────────────────────────────────────────┤
│ Layer 2: Translation Layer                                      │
│   - HL7 ↔ DICOM Mapper                                         │
│   - FHIR ↔ DICOM Mapper                                        │
│   - HL7 ↔ FHIR Mapper                                          │
├─────────────────────────────────────────────────────────────────┤
│ Layer 1: pacs_system Adapter                                    │
│   - MWL Client (DICOM C-FIND/Update)                           │
│   - MPPS Handler (N-CREATE/N-SET)                              │
│   - Patient Cache (In-Memory, TTL-based)                       │
├─────────────────────────────────────────────────────────────────┤
│ Layer 0: Integration Layer (kcenon ecosystem)                   │
│   - network_system (TCP/TLS, HTTP)                             │
│   - thread_system (Worker Pools)                               │
│   - logger_system (Audit Logging)                              │
│   - container_system (Serialization)                           │
│   - common_system (Result<T>, Error Codes)                     │
└─────────────────────────────────────────────────────────────────┘
```

**Layer Dependencies:**
- Higher layers depend only on lower layers
- No circular dependencies
- Integration layer provides foundation for all modules

---

## 3. Design Principles

### 3.1 SOLID Principles

| Principle | Application in PACS Bridge |
|-----------|---------------------------|
| **Single Responsibility** | Each module handles one protocol domain |
| **Open/Closed** | Mapper interfaces extensible for new message types |
| **Liskov Substitution** | All mappers implement common interface |
| **Interface Segregation** | Separate interfaces for parse, map, serialize |
| **Dependency Inversion** | High-level routing depends on mapper abstractions |

### 3.2 Design Patterns

| Pattern | Usage |
|---------|-------|
| **Factory** | Message parser creation by type |
| **Strategy** | Routing strategies (direct, queued, failover) |
| **Observer** | MPPS event notification to subscribers |
| **Adapter** | pacs_system ↔ Bridge integration |
| **Chain of Responsibility** | Message validation pipeline |
| **Builder** | HL7 message construction |

### 3.3 Error Handling Strategy

All public APIs return `common::Result<T>`:

```cpp
namespace pacs::bridge {

// Error-handling pattern
template<typename T>
using Result = common::Result<T>;

// Example usage
Result<hl7_message> parse_message(std::string_view raw) {
    if (raw.empty()) {
        return Result<hl7_message>::err(-900, "Empty message");
    }
    // ...
}

} // namespace pacs::bridge
```

### 3.4 Thread Safety

| Component | Thread-Safety Guarantee |
|-----------|------------------------|
| Message Parser | Thread-safe (stateless) |
| MLLP Server | Thread-safe (per-connection isolation) |
| Message Queue | Thread-safe (lock-free where possible) |
| Patient Cache | Thread-safe (read-write lock) |
| Mappers | Thread-safe (stateless) |

---

## 4. Module Summary

### 4.1 Module Overview

| Module | Namespace | Responsibility |
|--------|-----------|----------------|
| **HL7 Gateway** | `pacs::bridge::hl7` | HL7 v2.x message handling |
| **MLLP Transport** | `pacs::bridge::mllp` | MLLP network protocol |
| **FHIR Gateway** | `pacs::bridge::fhir` | FHIR R4 REST API |
| **Translation** | `pacs::bridge::mapping` | Protocol translation |
| **Routing** | `pacs::bridge::router` | Message routing and queue |
| **PACS Adapter** | `pacs::bridge::pacs_adapter` | pacs_system integration |
| **Configuration** | `pacs::bridge::config` | Configuration management |
| **Integration** | `pacs::bridge::integration` | kcenon ecosystem adapters |

### 4.2 Module Dependency Graph

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          Module Dependencies                              │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│                      ┌───────────────┐                                   │
│                      │  hl7_gateway  │                                   │
│                      └───────┬───────┘                                   │
│                              │                                           │
│         ┌────────────────────┼────────────────────┐                     │
│         │                    │                    │                     │
│         ▼                    ▼                    ▼                     │
│  ┌─────────────┐     ┌─────────────┐      ┌─────────────┐              │
│  │ mllp_server │     │ hl7_parser  │      │ hl7_builder │              │
│  └──────┬──────┘     └──────┬──────┘      └──────┬──────┘              │
│         │                   │                    │                      │
│         └───────────────────┼────────────────────┘                      │
│                             │                                            │
│                             ▼                                            │
│                      ┌─────────────┐                                    │
│                      │   router    │◄─────────┐                         │
│                      └──────┬──────┘          │                         │
│                             │                 │                         │
│              ┌──────────────┼──────────────┐  │                         │
│              │              │              │  │                         │
│              ▼              ▼              ▼  │                         │
│       ┌───────────┐  ┌───────────┐  ┌───────────┐                      │
│       │ hl7_dicom │  │ dicom_hl7 │  │ fhir_dicom│                      │
│       │  mapper   │  │  mapper   │  │  mapper   │                      │
│       └─────┬─────┘  └─────┬─────┘  └─────┬─────┘                      │
│             │              │              │                             │
│             └──────────────┼──────────────┘                             │
│                            │                                            │
│                            ▼                                            │
│                     ┌─────────────┐          ┌─────────────────────┐   │
│                     │pacs_adapter │◄────────►│    pacs_system      │   │
│                     └──────┬──────┘          │    (external)       │   │
│                            │                 └─────────────────────┘   │
│                            ▼                                            │
│                    ┌───────────────┐                                   │
│                    │  integration  │                                   │
│                    │   (adapters)  │                                   │
│                    └───────┬───────┘                                   │
│                            │                                            │
│     ┌──────────────────────┼──────────────────────┐                    │
│     │                      │                      │                    │
│     ▼                      ▼                      ▼                    │
│ ┌─────────┐          ┌─────────┐           ┌─────────┐                │
│ │network  │          │ thread  │           │ logger  │                │
│ │ system  │          │ system  │           │ system  │                │
│ └─────────┘          └─────────┘           └─────────┘                │
│                                                                         │
└──────────────────────────────────────────────────────────────────────────┘
```

### 4.3 Component Summary Table

| Design ID | Component | Layer | Lines (Est.) | Dependencies |
|-----------|-----------|-------|--------------|--------------|
| DES-HL7-001 | hl7_message | HL7 | 500 | - |
| DES-HL7-002 | hl7_parser | HL7 | 800 | hl7_message |
| DES-HL7-003 | hl7_builder | HL7 | 600 | hl7_message |
| DES-HL7-004 | hl7_validator | HL7 | 400 | hl7_message |
| DES-MLLP-001 | mllp_server | MLLP | 700 | network_adapter |
| DES-MLLP-002 | mllp_client | MLLP | 500 | network_adapter |
| DES-MLLP-003 | mllp_connection | MLLP | 400 | network_adapter |
| DES-FHIR-001 | fhir_server | FHIR | 600 | network_adapter |
| DES-FHIR-002 | fhir_resource | FHIR | 500 | - |
| DES-TRANS-001 | hl7_dicom_mapper | Translation | 1000 | pacs::core |
| DES-TRANS-002 | dicom_hl7_mapper | Translation | 800 | pacs::core |
| DES-TRANS-003 | fhir_dicom_mapper | Translation | 600 | pacs::core |
| DES-ROUTE-001 | message_router | Routing | 600 | - |
| DES-ROUTE-002 | queue_manager | Routing | 800 | thread_adapter |
| DES-PACS-001 | mwl_client | PACS Adapter | 500 | pacs_system |
| DES-PACS-002 | mpps_handler | PACS Adapter | 400 | pacs_system |
| DES-PACS-003 | patient_cache | PACS Adapter | 300 | - |
| DES-CFG-001 | bridge_config | Configuration | 400 | container_system |

---

## 5. Design Constraints

### 5.1 Technical Constraints

| ID | Constraint | Rationale |
|----|------------|-----------|
| **C1** | Must use pacs_system v0.2.0+ for DICOM services | Core PACS dependency |
| **C2** | Must use kcenon ecosystem only (no external libs) | Ecosystem consistency |
| **C3** | C++20 standard required | Modern language features |
| **C4** | Cross-platform (Linux, macOS, Windows) | Deployment flexibility |
| **C5** | HL7 v2.3.1 to v2.9 compatibility | Legacy system support |

### 5.2 Protocol Constraints

| ID | Constraint | Rationale |
|----|------------|-----------|
| **PC1** | IHE SWF profile compliance | Industry interoperability |
| **PC2** | MLLP framing (VT/FS/CR) required | HL7 transport standard |
| **PC3** | FHIR R4 resource format | Modern EHR compatibility |
| **PC4** | DICOM MWL SOP Class support | Worklist integration |

### 5.3 Performance Constraints

| ID | Constraint | Target |
|----|------------|--------|
| **PF1** | Message throughput | ≥500 msg/s |
| **PF2** | Message latency (P95) | <50 ms |
| **PF3** | Concurrent connections | ≥50 |
| **PF4** | Memory baseline | <200 MB |

---

## 6. Design Decisions

### 6.1 Key Design Decisions

#### DD-001: HL7 Parser Strategy

**Decision:** Streaming parser with segment-based validation

**Rationale:**
- Handles variable-length messages efficiently
- Validates each segment as parsed
- Supports Z-segments (custom extensions)

**Alternatives Considered:**
- DOM-style parser: Rejected due to memory overhead
- Regex-based: Rejected due to complexity

#### DD-002: Message Queue Persistence

**Decision:** SQLite-based persistent queue

**Rationale:**
- Survives process restarts
- ACID guarantees for message delivery
- Consistent with pacs_system approach

**Alternatives Considered:**
- In-memory only: Rejected for reliability
- External MQ: Rejected for dependency constraint

#### DD-003: Patient Cache Strategy

**Decision:** In-memory cache with TTL and LRU eviction

**Rationale:**
- Fast lookup for repeated patient references
- Bounded memory usage
- Configurable TTL for data freshness

**Configuration:**
```yaml
patient_cache:
  max_entries: 10000
  ttl_seconds: 3600
  eviction_policy: "lru"
```

#### DD-004: Translation Mapping Approach

**Decision:** Table-driven mapping with code lookups

**Rationale:**
- Configurable without code changes
- Supports site-specific code mappings
- Easy to audit and maintain

**Example Mapping Table:**
```yaml
modality_mapping:
  CT: "1.2.840.10008.5.1.4.1.1.2"    # CT Image Storage
  MR: "1.2.840.10008.5.1.4.1.1.4"    # MR Image Storage
  US: "1.2.840.10008.5.1.4.1.1.6.1"  # US Image Storage
```

#### DD-005: Error Code Allocation

**Decision:** Allocate error codes -900 to -999 for pacs_bridge

**Rationale:**
- Non-overlapping with pacs_system (-800 to -899)
- Consistent with ecosystem error handling
- Easy to identify source of errors

---

## 7. Quality Attributes

### 7.1 Performance

| Attribute | Target | Implementation |
|-----------|--------|----------------|
| Throughput | ≥500 msg/s | Async I/O, thread pools |
| Latency (P50) | <20 ms | Lock-free queues |
| Latency (P99) | <100 ms | Connection pooling |
| Memory | <200 MB | Bounded caches, streaming |

### 7.2 Reliability

| Attribute | Target | Implementation |
|-----------|--------|----------------|
| Message Delivery | 100% | Persistent queue |
| Uptime | 99.9% | Graceful degradation |
| Error Recovery | Automatic | Retry with backoff |
| Data Integrity | Guaranteed | Transaction isolation |

### 7.3 Maintainability

| Attribute | Target | Implementation |
|-----------|--------|----------------|
| Code Coverage | ≥80% | Unit and integration tests |
| Modularity | High | Layered architecture |
| Documentation | Complete | Doxygen, API docs |
| Configuration | Externalized | YAML/JSON config files |

### 7.4 Security

| Attribute | Target | Implementation |
|-----------|--------|----------------|
| Transport | TLS 1.2+ | MLLP/HTTPS encryption |
| Authentication | X.509 | Certificate-based |
| Audit | Complete | All transactions logged |
| Input Validation | 100% | Schema validation |

---

## 8. References

### 8.1 Standards

| Standard | Reference |
|----------|-----------|
| HL7 v2.5.1 | https://www.hl7.org/implement/standards/ |
| FHIR R4 | https://hl7.org/fhir/R4/ |
| DICOM | https://www.dicomstandard.org/ |
| IHE SWF | https://www.ihe.net/Technical_Framework/upload/IHE_RAD_TF_Rev13.0_Vol1_FT_2014-07-30.pdf |
| MLLP | https://www.hl7.org/implement/standards/product_brief.cfm?product_id=55 |

### 8.2 Related Documents

| Document | Description |
|----------|-------------|
| [PRD.md](PRD.md) | Product Requirements Document |
| [SRS.md](SRS.md) | Software Requirements Specification |
| pacs_system SDS | Core PACS design reference |

### 8.3 Reference Materials

| Document | Description |
|----------|-------------|
| [01_hl7_v2x_overview.md](reference_materials/01_hl7_v2x_overview.md) | HL7 v2.x fundamentals |
| [02_hl7_message_types.md](reference_materials/02_hl7_message_types.md) | ADT, ORM, ORU, SIU specs |
| [03_hl7_segments.md](reference_materials/03_hl7_segments.md) | Segment structures |
| [04_mllp_protocol.md](reference_materials/04_mllp_protocol.md) | MLLP transport |
| [05_fhir_radiology.md](reference_materials/05_fhir_radiology.md) | FHIR R4 radiology |
| [06_ihe_swf_profile.md](reference_materials/06_ihe_swf_profile.md) | IHE SWF profile |
| [07_dicom_hl7_mapping.md](reference_materials/07_dicom_hl7_mapping.md) | DICOM-HL7 mapping |
| [08_mwl_hl7_integration.md](reference_materials/08_mwl_hl7_integration.md) | MWL integration |

---

*Document Version: 0.1.0.0*
*Created: 2025-12-07*
*Author: kcenon@naver.com*
