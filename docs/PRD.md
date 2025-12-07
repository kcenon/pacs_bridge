# Product Requirements Document - PACS Bridge

> **Version:** 1.0.0
> **Last Updated:** 2025-12-07
> **Language:** **English** | [한국어](PRD_KO.md)

---

## Table of Contents

- [Executive Summary](#executive-summary)
- [Product Vision](#product-vision)
- [Target Users](#target-users)
- [Functional Requirements](#functional-requirements)
- [Non-Functional Requirements](#non-functional-requirements)
- [System Architecture Requirements](#system-architecture-requirements)
- [Protocol Conformance Requirements](#protocol-conformance-requirements)
- [Integration Requirements](#integration-requirements)
- [Security Requirements](#security-requirements)
- [Performance Requirements](#performance-requirements)
- [Development Phases](#development-phases)
- [Success Metrics](#success-metrics)
- [Risks and Mitigations](#risks-and-mitigations)
- [Appendices](#appendices)

---

## Executive Summary

### Product Name
PACS Bridge (HIS/RIS Integration Gateway)

### Product Description
A C++20 integration gateway that bridges Hospital Information Systems (HIS) and Radiology Information Systems (RIS) with PACS (Picture Archiving and Communication System). PACS Bridge translates between HL7 v2.x/FHIR messaging protocols and DICOM services, enabling seamless workflow integration in radiology departments.

### Key Differentiators
- **Dual Protocol Support**: Both HL7 v2.x (legacy) and FHIR R4 (modern) gateways
- **Full pacs_system Integration**: Native integration with pacs_system MWL/MPPS services
- **IHE SWF Compliant**: Follows IHE Scheduled Workflow integration profile
- **Production Grade**: Built on proven kcenon ecosystem infrastructure
- **Zero External Dependencies**: Pure C++20 implementation using ecosystem components

---

## Product Vision

### Vision Statement
To provide a reliable, high-performance integration bridge that seamlessly connects legacy HIS/RIS systems with modern PACS infrastructure, eliminating workflow gaps and reducing manual data entry errors in radiology departments.

### Strategic Goals

| Goal | Description | Success Criteria |
|------|-------------|------------------|
| **Interoperability** | Bridge HL7 and DICOM protocols | 100% HL7-to-DICOM message translation |
| **Legacy Support** | Support existing HIS/RIS systems | HL7 v2.3.1 to v2.9 compatibility |
| **Modernization** | Enable FHIR-based integration | FHIR R4 ImagingStudy, ServiceRequest |
| **Reliability** | Ensure message delivery | Zero message loss, guaranteed delivery |
| **Performance** | Handle high message volumes | ≥500 messages/second throughput |

---

## Target Users

### Primary Users

#### 1. Healthcare Integration Engineers
- **Profile**: IT specialists integrating hospital systems
- **Needs**:
  - Clear HL7/DICOM mapping documentation
  - Configurable message routing
  - Comprehensive logging and debugging
- **Pain Points**:
  - Complex multi-vendor environments
  - Inconsistent HL7 implementations
  - Limited debugging tools

#### 2. Radiology IT Administrators
- **Profile**: Administrators managing radiology workflow
- **Needs**:
  - Reliable worklist synchronization
  - Real-time procedure status updates
  - Minimal configuration overhead
- **Pain Points**:
  - Manual worklist entry errors
  - Delayed status notifications
  - System downtime during updates

#### 3. Medical Equipment Vendors
- **Profile**: Companies developing modality software
- **Needs**:
  - Standard MWL query interface
  - MPPS notification endpoints
  - Integration testing tools
- **Pain Points**:
  - Varying RIS implementations
  - Protocol compatibility issues

### Secondary Users

#### Hospital System Administrators
- Monitor message queues and system health
- Configure routing rules and failover
- Manage security certificates

#### Clinical Staff (Indirect)
- Technologists selecting patients from worklist
- Radiologists receiving timely reports
- Referring physicians accessing results

---

## Functional Requirements

### FR-1: HL7 v2.x Gateway

#### FR-1.1: Message Parsing
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-1.1.1 | Parse HL7 v2.x messages (v2.3.1 to v2.9) | Must Have | 1 |
| FR-1.1.2 | Support standard delimiters and escape sequences | Must Have | 1 |
| FR-1.1.3 | Handle repeating fields and components | Must Have | 1 |
| FR-1.1.4 | Validate message structure against schemas | Should Have | 2 |
| FR-1.1.5 | Support Z-segments (custom segments) | Should Have | 2 |

#### FR-1.2: Message Types
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-1.2.1 | Process ADT^A01, A04, A08, A40 (Patient Registration) | Must Have | 1 |
| FR-1.2.2 | Process ORM^O01 (Order Management) | Must Have | 1 |
| FR-1.2.3 | Generate ORU^R01 (Observation Results) | Must Have | 2 |
| FR-1.2.4 | Process SIU^S12-S15 (Scheduling) | Should Have | 2 |
| FR-1.2.5 | Generate ACK responses | Must Have | 1 |

#### FR-1.3: MLLP Transport
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-1.3.1 | Implement MLLP server (listener) | Must Have | 1 |
| FR-1.3.2 | Implement MLLP client (sender) | Must Have | 1 |
| FR-1.3.3 | Support persistent and transient connections | Must Have | 1 |
| FR-1.3.4 | Handle message framing (VT, FS, CR) | Must Have | 1 |
| FR-1.3.5 | Support MLLP over TLS | Should Have | 2 |

---

### FR-2: FHIR R4 Gateway

#### FR-2.1: RESTful API
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-2.1.1 | Implement FHIR REST server | Should Have | 3 |
| FR-2.1.2 | Support CRUD operations on resources | Should Have | 3 |
| FR-2.1.3 | Implement search parameters | Should Have | 3 |
| FR-2.1.4 | Support JSON and XML formats | Should Have | 3 |
| FR-2.1.5 | Handle pagination for large result sets | Could Have | 4 |

#### FR-2.2: Radiology Resources
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-2.2.1 | Support Patient resource | Should Have | 3 |
| FR-2.2.2 | Support ServiceRequest (imaging orders) | Should Have | 3 |
| FR-2.2.3 | Support ImagingStudy resource | Should Have | 3 |
| FR-2.2.4 | Support DiagnosticReport resource | Should Have | 3 |
| FR-2.2.5 | Support Task (worklist items) | Could Have | 4 |

#### FR-2.3: Subscriptions
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-2.3.1 | Support REST-hook subscriptions | Could Have | 4 |
| FR-2.3.2 | Notify on study availability | Could Have | 4 |
| FR-2.3.3 | Notify on report completion | Could Have | 4 |

---

### FR-3: DICOM Integration

#### FR-3.1: Modality Worklist Bridge
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-3.1.1 | Convert HL7 ORM to DICOM MWL entries | Must Have | 1 |
| FR-3.1.2 | Map patient demographics (PID → Patient Module) | Must Have | 1 |
| FR-3.1.3 | Map order information (ORC/OBR → SPS) | Must Have | 1 |
| FR-3.1.4 | Support Study Instance UID pre-assignment (ZDS) | Must Have | 1 |
| FR-3.1.5 | Handle order cancellation (ORC-1=CA) | Must Have | 1 |
| FR-3.1.6 | Handle order modification (ORC-1=XO) | Should Have | 2 |

#### FR-3.2: MPPS Notification
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-3.2.1 | Receive MPPS N-CREATE notifications | Must Have | 2 |
| FR-3.2.2 | Convert MPPS IN PROGRESS to HL7 ORM status | Must Have | 2 |
| FR-3.2.3 | Receive MPPS N-SET (COMPLETED) notifications | Must Have | 2 |
| FR-3.2.4 | Convert MPPS COMPLETED to HL7 ORM status | Must Have | 2 |
| FR-3.2.5 | Handle MPPS DISCONTINUED | Should Have | 2 |

#### FR-3.3: Report Integration
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-3.3.1 | Receive report status from PACS | Should Have | 3 |
| FR-3.3.2 | Generate ORU^R01 for preliminary reports | Should Have | 3 |
| FR-3.3.3 | Generate ORU^R01 for final reports | Should Have | 3 |
| FR-3.3.4 | Support report amendments | Could Have | 4 |

---

### FR-4: Message Routing

#### FR-4.1: Inbound Routing
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-4.1.1 | Route ADT messages to patient cache | Must Have | 1 |
| FR-4.1.2 | Route ORM messages to MWL manager | Must Have | 1 |
| FR-4.1.3 | Route based on message type and trigger | Must Have | 1 |
| FR-4.1.4 | Support conditional routing rules | Should Have | 2 |

#### FR-4.2: Outbound Routing
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-4.2.1 | Route MPPS notifications to RIS | Must Have | 2 |
| FR-4.2.2 | Route ORU messages to configured endpoints | Should Have | 3 |
| FR-4.2.3 | Support multiple destination routing | Should Have | 2 |
| FR-4.2.4 | Implement failover routing | Should Have | 3 |

#### FR-4.3: Message Queue
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-4.3.1 | Queue outbound messages for reliable delivery | Must Have | 1 |
| FR-4.3.2 | Implement retry with exponential backoff | Must Have | 1 |
| FR-4.3.3 | Support message prioritization | Should Have | 2 |
| FR-4.3.4 | Persist queue for crash recovery | Should Have | 2 |

---

### FR-5: Configuration Management

#### FR-5.1: Endpoint Configuration
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-5.1.1 | Configure HL7 listener ports | Must Have | 1 |
| FR-5.1.2 | Configure outbound HL7 destinations | Must Have | 1 |
| FR-5.1.3 | Configure pacs_system connection | Must Have | 1 |
| FR-5.1.4 | Support hot-reload of configuration | Should Have | 3 |

#### FR-5.2: Mapping Configuration
| ID | Requirement | Priority | Phase |
|----|-------------|----------|-------|
| FR-5.2.1 | Configure modality-to-AE-title mapping | Must Have | 1 |
| FR-5.2.2 | Configure procedure code mappings | Should Have | 2 |
| FR-5.2.3 | Configure patient ID domain mappings | Should Have | 2 |
| FR-5.2.4 | Support custom field mappings | Could Have | 3 |

---

## Non-Functional Requirements

### NFR-1: Performance

| ID | Requirement | Target | Measurement |
|----|-------------|--------|-------------|
| NFR-1.1 | Message throughput | ≥500 msg/s | HL7 messages processed |
| NFR-1.2 | Message latency (P95) | <50 ms | End-to-end processing |
| NFR-1.3 | MWL query response | <100 ms | DICOM C-FIND to MWL entries |
| NFR-1.4 | Concurrent connections | ≥50 | Simultaneous HL7 connections |
| NFR-1.5 | Memory baseline | <200 MB | Idle system memory |
| NFR-1.6 | CPU utilization | <20% | Idle system load |

### NFR-2: Reliability

| ID | Requirement | Target | Measurement |
|----|-------------|--------|-------------|
| NFR-2.1 | System uptime | 99.9% | Monthly availability |
| NFR-2.2 | Message delivery | 100% | Zero message loss |
| NFR-2.3 | Graceful degradation | Required | Under high load |
| NFR-2.4 | Error recovery | Automatic | Connection failures |
| NFR-2.5 | Queue persistence | Required | Survive restarts |

### NFR-3: Scalability

| ID | Requirement | Target | Measurement |
|----|-------------|--------|-------------|
| NFR-3.1 | Horizontal scaling | Supported | Multiple instances |
| NFR-3.2 | Daily message volume | ≥100K | Messages per day |
| NFR-3.3 | MWL entry capacity | ≥10K | Active worklist entries |
| NFR-3.4 | Connection pooling | Efficient | Reuse connections |

### NFR-4: Security

| ID | Requirement | Target | Measurement |
|----|-------------|--------|-------------|
| NFR-4.1 | TLS support | TLS 1.2/1.3 | MLLP/FHIR |
| NFR-4.2 | Access logging | Complete | All transactions |
| NFR-4.3 | Audit trail | HIPAA compliant | PHI access |
| NFR-4.4 | Input validation | 100% | All network input |
| NFR-4.5 | Certificate management | X.509 | Client/server auth |

### NFR-5: Maintainability

| ID | Requirement | Target | Measurement |
|----|-------------|--------|-------------|
| NFR-5.1 | Code coverage | ≥80% | Line coverage |
| NFR-5.2 | Documentation | Complete | All public APIs |
| NFR-5.3 | CI/CD pipeline | 100% green | All platforms |
| NFR-5.4 | Configuration | Externalized | No code changes for config |
| NFR-5.5 | Logging | Structured | JSON format |

---

## System Architecture Requirements

### SAR-1: Component Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                            PACS Bridge                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌─────────────────────┐    ┌────────────────┐    ┌──────────────────┐  │
│  │    HL7 Gateway      │    │ Message Router │    │   FHIR Gateway   │  │
│  │                     │    │                │    │                  │  │
│  │  ┌───────────────┐  │    │  ┌──────────┐  │    │  ┌────────────┐  │  │
│  │  │ MLLP Server   │──┼───►│  │ Inbound  │  │    │  │ REST Server│  │  │
│  │  └───────────────┘  │    │  │ Handler  │  │    │  └────────────┘  │  │
│  │  ┌───────────────┐  │    │  └──────────┘  │    │  ┌────────────┐  │  │
│  │  │ MLLP Client   │◄─┼────│  ┌──────────┐  │◄───│  │ Subscriber │  │  │
│  │  └───────────────┘  │    │  │ Outbound │  │    │  └────────────┘  │  │
│  │  ┌───────────────┐  │    │  │ Queue    │  │    └──────────────────┘  │
│  │  │ Message Parser│  │    │  └──────────┘  │                          │
│  │  └───────────────┘  │    └────────────────┘                          │
│  └─────────────────────┘             │                                   │
│                                      │                                   │
│  ┌───────────────────────────────────▼───────────────────────────────┐  │
│  │                      Translation Layer                              │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌───────────┐  │  │
│  │  │ HL7→DICOM   │  │ DICOM→HL7   │  │ HL7→FHIR    │  │ FHIR→DICOM│  │  │
│  │  │ Mapper      │  │ Mapper      │  │ Mapper      │  │ Mapper    │  │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └───────────┘  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                      │                                   │
│  ┌───────────────────────────────────▼───────────────────────────────┐  │
│  │                      pacs_system Adapter                            │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────────┐  │  │
│  │  │ MWL Client  │  │ MPPS Handler│  │ Patient Cache               │  │  │
│  │  └─────────────┘  └─────────────┘  └─────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                      │                                   │
└──────────────────────────────────────┼───────────────────────────────────┘
                                       │
                    ┌──────────────────┼──────────────────┐
                    │                  │                  │
           ┌────────▼────────┐  ┌──────▼──────┐  ┌────────▼────────┐
           │   pacs_system   │  │  HIS / RIS  │  │   EMR / EHR     │
           │  (DICOM MWL/MPPS)│  │  (HL7 v2.x) │  │   (FHIR R4)     │
           └─────────────────┘  └─────────────┘  └─────────────────┘
```

### SAR-2: Module Dependencies

| Module | Depends On | Purpose |
|--------|------------|---------|
| **hl7_gateway** | mllp_transport, message_parser | HL7 message handling |
| **fhir_gateway** | http_server, json_parser | FHIR REST API |
| **message_router** | queue_manager, config | Message routing logic |
| **translation_layer** | hl7_dicom_mapper, fhir_mapper | Protocol translation |
| **pacs_adapter** | pacs_system (external) | DICOM service integration |
| **mllp_transport** | network_system | MLLP protocol handling |
| **http_server** | network_system | HTTP/HTTPS server |

### SAR-3: Thread Model Requirements

| Requirement | Description |
|-------------|-------------|
| IO Thread Pool | MLLP/HTTP connection handling |
| Message Worker Pool | HL7/FHIR message processing |
| Translation Workers | Protocol translation tasks |
| Queue Manager | Outbound message delivery |
| Timer Thread | Retry scheduling, timeouts |

---

## Protocol Conformance Requirements

### PCR-1: HL7 v2.x Conformance

#### Supported Versions
| Version | Support Level |
|---------|--------------|
| 2.3.1 | Full |
| 2.4 | Full |
| 2.5 | Full |
| 2.5.1 | Full (Recommended) |
| 2.6 | Partial |
| 2.7 | Partial |

#### Supported Message Types
| Message | Trigger | Direction | Priority |
|---------|---------|-----------|----------|
| ADT | A01, A04, A08, A40 | Inbound | Must Have |
| ORM | O01 | Inbound/Outbound | Must Have |
| ORU | R01 | Outbound | Should Have |
| SIU | S12-S15 | Inbound | Should Have |
| ACK | * | Inbound/Outbound | Must Have |

### PCR-2: FHIR R4 Conformance

#### Supported Resources
| Resource | Operations | Priority |
|----------|------------|----------|
| Patient | Read, Search | Should Have |
| ServiceRequest | Read, Search, Create | Should Have |
| ImagingStudy | Read, Search | Should Have |
| DiagnosticReport | Read, Search | Should Have |
| Task | Read, Search, Update | Could Have |

### PCR-3: IHE Integration Profile Conformance

#### Scheduled Workflow (SWF) Transactions
| Transaction | Actor Role | Priority |
|-------------|------------|----------|
| RAD-2 (Placer Order) | Receiver | Must Have |
| RAD-3 (Filler Order) | Sender | Should Have |
| RAD-4 (Procedure Scheduled) | Sender | Must Have |
| RAD-6 (MPPS In Progress) | Receiver | Must Have |
| RAD-7 (MPPS Completed) | Receiver | Must Have |

---

## Integration Requirements

### IR-1: pacs_system Integration

| Component | Integration Type | Purpose |
|-----------|-----------------|---------|
| **worklist_scp** | DICOM Client | Update MWL entries |
| **mpps_scp** | Event Receiver | Receive procedure status |
| **storage_scp** | Event Receiver | Image arrival notification |
| **index_database** | Query Client | Patient/Study lookup |

### IR-2: kcenon Ecosystem Integration

| System | Integration Type | Purpose |
|--------|-----------------|---------|
| **common_system** | Foundation | Result<T>, Error codes |
| **container_system** | Data Layer | Message serialization |
| **thread_system** | Concurrency | Worker pools |
| **logger_system** | Logging | Audit logs, diagnostics |
| **monitoring_system** | Observability | Metrics, health checks |
| **network_system** | Network | TCP/TLS, HTTP |

### IR-3: External System Integration

| System | Protocol | Direction |
|--------|----------|-----------|
| HIS (Hospital Information System) | HL7 v2.x / MLLP | Bidirectional |
| RIS (Radiology Information System) | HL7 v2.x / MLLP | Bidirectional |
| EMR (Electronic Medical Records) | FHIR R4 / HTTPS | Bidirectional |
| Modalities (CT, MR, etc.) | DICOM (via pacs_system) | Receive |

---

## Security Requirements

### SR-1: Transport Security

| Requirement | Implementation |
|-------------|----------------|
| MLLP over TLS | TLS 1.2+ for HL7 messages |
| HTTPS | TLS 1.2+ for FHIR API |
| Certificate Validation | X.509 certificate chain validation |
| Mutual TLS | Optional client certificate auth |

### SR-2: Access Control

| Requirement | Implementation |
|-------------|----------------|
| IP Whitelisting | Restrict connections by source IP |
| Application Authentication | HL7 MSH-3/4 validation |
| API Authentication | OAuth 2.0 / API Keys for FHIR |
| Role-Based Access | Admin, Operator, Read-only roles |

### SR-3: Audit and Compliance

| Requirement | Implementation |
|-------------|----------------|
| Audit Logging | All message transactions logged |
| PHI Tracking | Log access to patient data |
| HIPAA Compliance | Encryption, access logs, audit trail |
| Log Retention | Configurable retention period |

---

## Performance Requirements

### PR-1: Benchmarks

| Metric | Target | Test Scenario |
|--------|--------|---------------|
| HL7 message throughput | ≥500 msg/s | ORM processing |
| HL7 message latency (P50) | <20 ms | End-to-end |
| HL7 message latency (P99) | <100 ms | End-to-end |
| MLLP connection setup | <10 ms | TCP handshake |
| MWL entry creation | <50 ms | ORM to DICOM |
| Queue depth handling | 10K messages | Burst traffic |

### PR-2: Capacity Targets

| Metric | Target |
|--------|--------|
| Daily message volume | 100K+ messages |
| Concurrent HL7 connections | 50+ |
| Active MWL entries | 10K+ |
| Message queue capacity | 50K messages |

---

## Development Phases

### Phase 1: Core HL7 Gateway (Weeks 1-6)

**Objective**: Establish HL7 v2.x message processing and MWL integration

| Deliverable | Description | Acceptance Criteria |
|-------------|-------------|---------------------|
| HL7 Parser | v2.x message parser | Parse ADT, ORM messages |
| MLLP Transport | Server and client | Handle connections |
| ORM→MWL Mapper | Convert orders to MWL | Create DICOM MWL entries |
| ACK Generator | Acknowledgment handling | Proper AA/AE/AR responses |
| pacs_system Adapter | MWL integration | Update worklist via DICOM |
| Unit Tests | Core module tests | 80%+ coverage |

**Dependencies**: pacs_system v0.2.0+, network_system

### Phase 2: MPPS and Bidirectional Flow (Weeks 7-10)

**Objective**: Implement MPPS notification and outbound HL7

| Deliverable | Description | Acceptance Criteria |
|-------------|-------------|---------------------|
| MPPS Receiver | Event handler | Receive N-CREATE/N-SET |
| MPPS→HL7 Mapper | Status conversion | Generate ORM status updates |
| MLLP Client | Outbound messaging | Send to RIS |
| Message Queue | Reliable delivery | Retry with backoff |
| Integration Tests | End-to-end tests | Full workflow |

**Dependencies**: Phase 1 complete

### Phase 3: FHIR Gateway and Reporting (Weeks 11-16)

**Objective**: Add FHIR R4 support and report integration

| Deliverable | Description | Acceptance Criteria |
|-------------|-------------|---------------------|
| FHIR Server | REST API | CRUD operations |
| Resource Mappers | FHIR↔DICOM | ImagingStudy, ServiceRequest |
| ORU Generator | Report messages | Preliminary/Final reports |
| Subscription | Event notifications | REST-hook support |
| API Documentation | OpenAPI spec | Complete API docs |

**Dependencies**: Phase 2 complete

### Phase 4: Production Hardening (Weeks 17-20)

**Objective**: Security, reliability, and operational readiness

| Deliverable | Description | Acceptance Criteria |
|-------------|-------------|---------------------|
| TLS Support | Secure transport | MLLP/HTTPS with TLS 1.3 |
| Configuration | Hot-reload | Dynamic configuration |
| Monitoring | Metrics/health | Prometheus/Grafana ready |
| Documentation | Complete docs | User guide, admin guide |
| Performance | Optimization | Meet all NFR targets |

**Dependencies**: Phase 3 complete

---

## Success Metrics

### Technical Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| HL7 Conformance | 100% tested messages | Conformance test suite |
| IHE SWF Compliance | RAD-2,4,6,7 | IHE Connectathon tests |
| Code Coverage | ≥80% | CI coverage reports |
| CI/CD Success | 100% green | GitHub Actions |
| API Documentation | 100% public APIs | Generated docs |

### Performance Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Message Throughput | ≥500 msg/s | Load testing |
| Latency P95 | <50 ms | Performance tests |
| Queue Recovery | <30 seconds | Restart tests |
| Memory Baseline | <200 MB | Profiling |

### Operational Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Message Delivery | 100% | Transaction logs |
| System Uptime | 99.9% | Monitoring |
| Error Rate | <0.1% | Error logs |
| Recovery Time | <5 minutes | Incident tests |

---

## Risks and Mitigations

### Technical Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| HL7 implementation variability | High | High | Configurable mappings, validation |
| pacs_system API changes | Medium | Low | Version pinning, adapter pattern |
| Performance bottlenecks | Medium | Medium | Early benchmarking, async design |
| Complex FHIR mapping | Medium | Medium | Defer to Phase 3, incremental |

### Integration Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| HIS/RIS compatibility issues | High | High | Extensive testing with real systems |
| Message ordering issues | Medium | Medium | Sequence tracking, idempotency |
| Network reliability | Medium | Medium | Retry logic, queue persistence |

### Operational Risks

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Configuration errors | High | Medium | Validation, defaults, dry-run mode |
| Certificate expiration | Medium | Medium | Monitoring, auto-renewal |
| Queue overflow | Medium | Low | Backpressure, alerts |

---

## Appendices

### Appendix A: HL7 to DICOM MWL Mapping Summary

| HL7 Field | DICOM Tag | DICOM Keyword |
|-----------|-----------|---------------|
| PID-3 | (0010,0020) | PatientID |
| PID-5 | (0010,0010) | PatientName |
| PID-7 | (0010,0030) | PatientBirthDate |
| PID-8 | (0010,0040) | PatientSex |
| ORC-2 | (0040,2016) | PlacerOrderNumberIS |
| ORC-3 | (0008,0050) | AccessionNumber |
| OBR-4 | (0032,1064) | RequestedProcedureCodeSequence |
| OBR-7 | (0040,0002/0003) | ScheduledProcedureStepStartDate/Time |
| OBR-24 | (0008,0060) | Modality |
| ZDS-1 | (0020,000D) | StudyInstanceUID |

### Appendix B: MPPS to HL7 Status Mapping

| MPPS Status | ORC-1 | ORC-5 | Description |
|-------------|-------|-------|-------------|
| IN PROGRESS | SC | IP | Exam started |
| COMPLETED | SC | CM | Exam completed |
| DISCONTINUED | DC | CA | Exam discontinued |

### Appendix C: Error Code Registry

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
```

### Appendix D: Configuration Example

```yaml
# pacs_bridge.yaml
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
      retry_interval: 5000

fhir:
  enabled: false
  port: 8080
  base_url: "/fhir"

pacs_system:
  host: "localhost"
  port: 11112
  ae_title: "PACS_BRIDGE"
  called_ae: "PACS_SCP"

mapping:
  modality_ae_titles:
    CT: ["CT_SCANNER_1", "CT_SCANNER_2"]
    MR: ["MR_SCANNER_1"]
    US: ["US_ROOM_1", "US_ROOM_2"]

logging:
  level: "INFO"
  format: "json"
  file: "/var/log/pacs_bridge/bridge.log"
```

### Appendix E: References

- HL7 International: https://www.hl7.org/
- HL7 v2.5.1 Specification: https://www.hl7.org/implement/standards/
- FHIR R4 Specification: https://hl7.org/fhir/R4/
- IHE Radiology Technical Framework: https://www.ihe.net/resources/technical_frameworks/#radiology
- DICOM Standard: https://www.dicomstandard.org/
- pacs_system: https://github.com/kcenon/pacs_system

---

*Document Version: 1.0.0*
*Created: 2025-12-07*
*Author: kcenon@naver.com*
