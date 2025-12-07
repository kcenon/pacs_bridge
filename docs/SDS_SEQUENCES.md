# SDS - Sequence Diagrams

> **Version:** 1.0.0
> **Parent Document:** [SDS.md](SDS.md)
> **Last Updated:** 2025-12-07

---

## Table of Contents

- [1. HL7 Message Processing](#1-hl7-message-processing)
- [2. Order Management Workflow](#2-order-management-workflow)
- [3. MPPS Notification Flow](#3-mpps-notification-flow)
- [4. FHIR API Operations](#4-fhir-api-operations)
- [5. Queue and Retry Flow](#5-queue-and-retry-flow)
- [6. Error Handling Scenarios](#6-error-handling-scenarios)
- [7. IHE Scheduled Workflow](#7-ihe-scheduled-workflow)

---

## 1. HL7 Message Processing

### SEQ-001: Inbound HL7 Message Flow

**Traces to:** FR-1.1, FR-1.2, FR-1.3

```
┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│ HIS/RIS │     │   MLLP   │     │   HL7    │     │  Message │     │  Handler │
│         │     │  Server  │     │  Parser  │     │  Router  │     │          │
└────┬────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │               │                │                │                │
     │  1. TCP Connect               │                │                │
     │──────────────►│                │                │                │
     │               │                │                │                │
     │  2. MLLP Frame (VT + Message + FS + CR)        │                │
     │  ┌─────────────────────────────────────┐       │                │
     │  │ <VT>                                │       │                │
     │  │ MSH|^~\&|HIS|FAC|BRIDGE|FAC|...    │       │                │
     │  │ PID|||123456||DOE^JOHN||...        │       │                │
     │  │ ORC|NW|ORD001|...                  │       │                │
     │  │ OBR|1|ORD001||CT^CT CHEST|...      │       │                │
     │  │ <FS><CR>                            │       │                │
     │  └─────────────────────────────────────┘       │                │
     │──────────────►│                │                │                │
     │               │                │                │                │
     │               │ 3. Unwrap MLLP │                │                │
     │               │───────────────►│                │                │
     │               │                │                │                │
     │               │                │ 4. Parse message               │
     │               │                │ - Extract delimiters           │
     │               │                │ - Parse segments               │
     │               │                │ - Validate structure           │
     │               │                │                │                │
     │               │                │ 5. Route message               │
     │               │                │───────────────►│                │
     │               │                │                │                │
     │               │                │                │ 6. Find handler
     │               │                │                │ (ORM^O01 → MWL)
     │               │                │                │───────────────►│
     │               │                │                │                │
     │               │                │                │                │ 7. Process
     │               │                │                │                │ (See SEQ-002)
     │               │                │                │                │
     │               │                │                │◄───────────────│
     │               │                │                │    Result      │
     │               │                │                │                │
     │               │                │◄───────────────│                │
     │               │                │  8. Build ACK  │                │
     │               │                │                │                │
     │               │◄───────────────│                │                │
     │               │   ACK message  │                │                │
     │               │                │                │                │
     │  9. MLLP ACK Response          │                │                │
     │  ┌─────────────────────────────────────┐       │                │
     │  │ <VT>                                │       │                │
     │  │ MSH|^~\&|BRIDGE|FAC|HIS|FAC|...    │       │                │
     │  │ MSA|AA|MSG001|                      │       │                │
     │  │ <FS><CR>                            │       │                │
     │  └─────────────────────────────────────┘       │                │
     │◄──────────────│                │                │                │
     │               │                │                │                │
```

### SEQ-002: ADT Message Processing (Patient Cache Update)

**Traces to:** FR-4.1.1

```
┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│ HIS/RIS │     │  Bridge  │     │ HL7_DICOM│     │ Patient  │
│         │     │  Server  │     │  Mapper  │     │  Cache   │
└────┬────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │               │                │                │
     │  ADT^A01      │                │                │
     │  (Patient Admit)               │                │
     │──────────────►│                │                │
     │               │                │                │
     │               │ 1. Extract patient info         │
     │               │───────────────►│                │
     │               │                │                │
     │               │                │ 2. Map HL7→Patient
     │               │                │  PID-3 → patient_id
     │               │                │  PID-5 → patient_name
     │               │                │  PID-7 → birth_date
     │               │                │  PID-8 → sex
     │               │                │                │
     │               │◄───────────────│                │
     │               │  patient_info  │                │
     │               │                │                │
     │               │ 3. Update cache │                │
     │               │────────────────────────────────►│
     │               │                │                │
     │               │                │                │ 4. Store/Update
     │               │                │                │ - Check existing
     │               │                │                │ - Update fields
     │               │                │                │ - Update timestamp
     │               │                │                │
     │               │◄────────────────────────────────│
     │               │   (success)    │                │
     │               │                │                │
     │  ACK (AA)     │                │                │
     │◄──────────────│                │                │
     │               │                │                │
```

---

## 2. Order Management Workflow

### SEQ-003: ORM → MWL Entry Creation

**Traces to:** FR-3.1.1, FR-3.1.2, FR-3.1.3, FR-3.1.4

```
┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│   HIS   │     │  Bridge  │     │ HL7_DICOM│     │   MWL    │     │pacs_system│
│         │     │  Server  │     │  Mapper  │     │  Client  │     │  MWL SCP  │
└────┬────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │               │                │                │                │
     │  ORM^O01      │                │                │                │
     │  (New Order)  │                │                │                │
     │  ┌─────────────────────────────────────┐       │                │
     │  │ MSH|...|ORM^O01|...                 │       │                │
     │  │ PID|||MRN001||DOE^JOHN||19800101|M  │       │                │
     │  │ ORC|NW|ORD001|||SC|...              │       │                │
     │  │ OBR|1|ORD001||CT^CT CHEST||         │       │                │
     │  │     |202512071000|||...             │       │                │
     │  │ ZDS|1.2.840.113619.2.55.12345|      │       │                │
     │  └─────────────────────────────────────┘       │                │
     │──────────────►│                │                │                │
     │               │                │                │                │
     │               │ 1. Convert ORM to MWL dataset   │                │
     │               │───────────────►│                │                │
     │               │                │                │                │
     │               │                │ 2. Map fields:                  │
     │               │                │  PID-3 → PatientID              │
     │               │                │  PID-5 → PatientName            │
     │               │                │  ORC-3 → AccessionNumber        │
     │               │                │  OBR-4 → RequestedProcedureCode │
     │               │                │  OBR-7 → ScheduledDateTime      │
     │               │                │  ZDS-1 → StudyInstanceUID       │
     │               │                │                │                │
     │               │◄───────────────│                │                │
     │               │  MWL Dataset   │                │                │
     │               │                │                │                │
     │               │ 3. Add to worklist              │                │
     │               │────────────────────────────────►│                │
     │               │                │                │                │
     │               │                │                │ 4. DICOM       │
     │               │                │                │ Association    │
     │               │                │                │───────────────►│
     │               │                │                │                │
     │               │                │                │ 5. N-CREATE    │
     │               │                │                │ (MWL Entry)    │
     │               │                │                │───────────────►│
     │               │                │                │                │
     │               │                │                │◄───────────────│
     │               │                │                │  N-CREATE-RSP  │
     │               │                │                │  (Success)     │
     │               │                │                │                │
     │               │◄────────────────────────────────│                │
     │               │   Result       │                │                │
     │               │                │                │                │
     │  ACK (AA)     │                │                │                │
     │◄──────────────│                │                │                │
     │               │                │                │                │
```

### SEQ-004: Order Cancellation

**Traces to:** FR-3.1.5

```
┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│   HIS   │     │  Bridge  │     │   MWL    │     │pacs_system│
│         │     │  Server  │     │  Client  │     │  MWL SCP  │
└────┬────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │               │                │                │
     │  ORM^O01      │                │                │
     │  (ORC-1=CA)   │                │                │
     │  ┌─────────────────────────────────────┐       │
     │  │ MSH|...|ORM^O01|...                 │       │
     │  │ ORC|CA|ORD001|||CA|...              │       │
     │  │ OBR|1|ORD001||CT^CT CHEST|...       │       │
     │  └─────────────────────────────────────┘       │
     │──────────────►│                │                │
     │               │                │                │
     │               │ 1. Identify order              │
     │               │ (ORC-1 = CA = Cancel)          │
     │               │                │                │
     │               │ 2. Cancel worklist entry       │
     │               │───────────────►│                │
     │               │                │                │
     │               │                │ 3. Query MWL   │
     │               │                │ by AccessionNo │
     │               │                │───────────────►│
     │               │                │                │
     │               │                │◄───────────────│
     │               │                │  (Entry found) │
     │               │                │                │
     │               │                │ 4. Delete MWL  │
     │               │                │ entry          │
     │               │                │───────────────►│
     │               │                │                │
     │               │                │◄───────────────│
     │               │                │  (Deleted)     │
     │               │                │                │
     │               │◄───────────────│                │
     │               │   (success)    │                │
     │               │                │                │
     │  ACK (AA)     │                │                │
     │◄──────────────│                │                │
     │               │                │                │
```

---

## 3. MPPS Notification Flow

### SEQ-005: MPPS In Progress → HL7 Status Update

**Traces to:** FR-3.2.1, FR-3.2.2

```
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌─────────┐
│ Modality │     │pacs_system│    │  Bridge  │     │DICOM_HL7 │     │   RIS   │
│  (CT)    │     │ MPPS SCP │     │  Server  │     │  Mapper  │     │         │
└────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬────┘
     │                │                │                │                │
     │  [Exam Start]  │                │                │                │
     │                │                │                │                │
     │ 1. N-CREATE    │                │                │                │
     │ (MPPS)         │                │                │                │
     │  ┌────────────────────────────────────────┐     │                │
     │  │ PerformedProcedureStepStatus:          │     │                │
     │  │   "IN PROGRESS"                        │     │                │
     │  │ PerformedStationAETitle: "CT_SCANNER_1"│     │                │
     │  │ PerformedProcedureStepStartDateTime:   │     │                │
     │  │   "20251207100000"                     │     │                │
     │  │ ScheduledStepAttributesSequence:       │     │                │
     │  │   AccessionNumber: "ACC001"            │     │                │
     │  │   StudyInstanceUID: "1.2.3..."         │     │                │
     │  └────────────────────────────────────────┘     │                │
     │───────────────►│                │                │                │
     │                │                │                │                │
     │                │ 2. Forward MPPS event          │                │
     │                │───────────────►│                │                │
     │                │                │                │                │
     │                │                │ 3. Convert to HL7               │
     │                │                │───────────────►│                │
     │                │                │                │                │
     │                │                │                │ 4. Build ORM   │
     │                │                │                │  ORC-1 = SC    │
     │                │                │                │  ORC-5 = IP    │
     │                │                │                │  OBR-22 = StartDT
     │                │                │                │                │
     │                │                │◄───────────────│                │
     │                │                │  ORM^O01       │                │
     │                │                │                │                │
     │                │                │ 5. Send to RIS (via queue)      │
     │                │                │────────────────────────────────►│
     │                │                │                │                │
     │                │                │◄────────────────────────────────│
     │                │                │   ACK (AA)     │                │
     │                │                │                │                │
     │◄───────────────│                │                │                │
     │  N-CREATE-RSP  │                │                │                │
     │                │                │                │                │
```

### SEQ-006: MPPS Completed → HL7 Status Update

**Traces to:** FR-3.2.3, FR-3.2.4

```
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌─────────┐
│ Modality │     │pacs_system│    │  Bridge  │     │DICOM_HL7 │     │   RIS   │
│  (CT)    │     │ MPPS SCP │     │  Server  │     │  Mapper  │     │         │
└────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬────┘
     │                │                │                │                │
     │  [Exam Done]   │                │                │                │
     │                │                │                │                │
     │ 1. N-SET       │                │                │                │
     │ (MPPS)         │                │                │                │
     │  ┌────────────────────────────────────────┐     │                │
     │  │ PerformedProcedureStepStatus:          │     │                │
     │  │   "COMPLETED"                          │     │                │
     │  │ PerformedProcedureStepEndDateTime:     │     │                │
     │  │   "20251207103000"                     │     │                │
     │  │ PerformedSeriesSequence:               │     │                │
     │  │   SeriesInstanceUID: "1.2.3.1..."      │     │                │
     │  │   NumberOfSeriesRelatedInstances: 150  │     │                │
     │  └────────────────────────────────────────┘     │                │
     │───────────────►│                │                │                │
     │                │                │                │                │
     │                │ 2. Forward MPPS event          │                │
     │                │───────────────►│                │                │
     │                │                │                │                │
     │                │                │ 3. Convert to HL7               │
     │                │                │───────────────►│                │
     │                │                │                │                │
     │                │                │                │ 4. Build ORM   │
     │                │                │                │  ORC-1 = SC    │
     │                │                │                │  ORC-5 = CM    │
     │                │                │                │  OBR-27 = EndDT│
     │                │                │                │                │
     │                │                │◄───────────────│                │
     │                │                │  ORM^O01       │                │
     │                │                │                │                │
     │                │                │ 5. Send to RIS                  │
     │                │                │────────────────────────────────►│
     │                │                │                │                │
     │                │                │◄────────────────────────────────│
     │                │                │   ACK (AA)     │                │
     │                │                │                │                │
     │◄───────────────│                │                │                │
     │  N-SET-RSP     │                │                │                │
     │                │                │                │                │
```

---

## 4. FHIR API Operations

### SEQ-007: FHIR ServiceRequest Creation

**Traces to:** FR-2.1.3, FR-2.2.2

```
┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│   EMR   │     │   FHIR   │     │FHIR_DICOM│     │   MWL    │     │pacs_system│
│         │     │  Server  │     │  Mapper  │     │  Client  │     │          │
└────┬────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │               │                │                │                │
     │  POST /fhir/ServiceRequest     │                │                │
     │  ┌─────────────────────────────────────┐       │                │
     │  │ {                                   │       │                │
     │  │   "resourceType": "ServiceRequest", │       │                │
     │  │   "status": "active",               │       │                │
     │  │   "intent": "order",                │       │                │
     │  │   "subject": {                      │       │                │
     │  │     "reference": "Patient/123"      │       │                │
     │  │   },                                │       │                │
     │  │   "code": {                         │       │                │
     │  │     "coding": [{                    │       │                │
     │  │       "code": "CT_CHEST"            │       │                │
     │  │     }]                              │       │                │
     │  │   },                                │       │                │
     │  │   "occurrenceDateTime": "..."       │       │                │
     │  │ }                                   │       │                │
     │  └─────────────────────────────────────┘       │                │
     │──────────────►│                │                │                │
     │               │                │                │                │
     │               │ 1. Validate request             │                │
     │               │                │                │                │
     │               │ 2. Resolve patient reference    │                │
     │               │ (GET Patient/123)               │                │
     │               │                │                │                │
     │               │ 3. Convert to MWL               │                │
     │               │───────────────►│                │                │
     │               │                │                │                │
     │               │                │ 4. Map FHIR→DICOM               │
     │               │                │  code → Modality                │
     │               │                │  occurrence → ScheduledDT       │
     │               │                │  subject → PatientID            │
     │               │                │                │                │
     │               │◄───────────────│                │                │
     │               │  MWL Dataset   │                │                │
     │               │                │                │                │
     │               │ 5. Add to worklist              │                │
     │               │────────────────────────────────►│                │
     │               │                │                │                │
     │               │                │                │───────────────►│
     │               │                │                │ DICOM N-CREATE │
     │               │                │                │                │
     │               │                │                │◄───────────────│
     │               │                │                │   Success      │
     │               │                │                │                │
     │               │◄────────────────────────────────│                │
     │               │                │                │                │
     │  201 Created  │                │                │                │
     │  ┌─────────────────────────────────────┐       │                │
     │  │ {                                   │       │                │
     │  │   "resourceType": "ServiceRequest", │       │                │
     │  │   "id": "sr-12345",                 │       │                │
     │  │   "status": "active",               │       │                │
     │  │   ...                               │       │                │
     │  │ }                                   │       │                │
     │  └─────────────────────────────────────┘       │                │
     │◄──────────────│                │                │                │
     │               │                │                │                │
```

### SEQ-008: FHIR ImagingStudy Query

**Traces to:** FR-2.1.2, FR-2.2.3

```
┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│   EMR   │     │   FHIR   │     │pacs_system│    │  Index   │
│         │     │  Server  │     │ Query SCP│     │ Database │
└────┬────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │               │                │                │
     │  GET /fhir/ImagingStudy?patient=123            │
     │──────────────►│                │                │
     │               │                │                │
     │               │ 1. Parse query parameters       │
     │               │                │                │
     │               │ 2. Query DICOM studies          │
     │               │───────────────►│                │
     │               │                │                │
     │               │                │ 3. C-FIND      │
     │               │                │───────────────►│
     │               │                │                │
     │               │                │◄───────────────│
     │               │                │  Study results │
     │               │                │                │
     │               │◄───────────────│                │
     │               │  DICOM datasets│                │
     │               │                │                │
     │               │ 4. Convert to FHIR Bundle       │
     │               │ (ImagingStudy resources)        │
     │               │                │                │
     │  200 OK       │                │                │
     │  ┌─────────────────────────────────────┐       │
     │  │ {                                   │       │
     │  │   "resourceType": "Bundle",         │       │
     │  │   "type": "searchset",              │       │
     │  │   "total": 3,                       │       │
     │  │   "entry": [                        │       │
     │  │     { "resource": { ... } },        │       │
     │  │     ...                             │       │
     │  │   ]                                 │       │
     │  │ }                                   │       │
     │  └─────────────────────────────────────┘       │
     │◄──────────────│                │                │
     │               │                │                │
```

---

## 5. Queue and Retry Flow

### SEQ-009: Message Queue and Retry

**Traces to:** FR-4.3.1, FR-4.3.2

```
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌─────────┐
│  Bridge  │     │  Queue   │     │   MLLP   │     │   RIS    │     │  Timer  │
│  Server  │     │ Manager  │     │  Client  │     │          │     │         │
└────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬────┘
     │                │                │                │                │
     │  1. Enqueue message            │                │                │
     │───────────────►│                │                │                │
     │                │                │                │                │
     │                │ 2. Persist to SQLite           │                │
     │                │ (priority, timestamp)          │                │
     │                │                │                │                │
     │◄───────────────│                │                │                │
     │  message_id    │                │                │                │
     │                │                │                │                │
     │                │ 3. Dequeue for delivery        │                │
     │                │───────────────►│                │                │
     │                │                │                │                │
     │                │                │ 4. Attempt send│                │
     │                │                │───────────────►│                │
     │                │                │                │                │
     │                │                │                │ [RIS Down]     │
     │                │                │◄───────────────┤                │
     │                │                │  Connection    │                │
     │                │                │  Failed        │                │
     │                │                │                │                │
     │                │◄───────────────│                │                │
     │                │  NACK (error)  │                │                │
     │                │                │                │                │
     │                │ 5. Schedule retry              │                │
     │                │ (exponential backoff)          │                │
     │                │ next_attempt = now + 5s        │                │
     │                │                │                │                │
     │                │                │                │                │
     │                │                │                │ [5 seconds]    │
     │                │                │                │◄───────────────│
     │                │                │                │  Timer fires   │
     │                │                │                │                │
     │                │ 6. Retry delivery              │                │
     │                │───────────────►│                │                │
     │                │                │                │                │
     │                │                │ 7. Attempt send│                │
     │                │                │───────────────►│                │
     │                │                │                │                │
     │                │                │◄───────────────│                │
     │                │                │   ACK (AA)     │                │
     │                │                │                │                │
     │                │◄───────────────│                │                │
     │                │  ACK (success) │                │                │
     │                │                │                │                │
     │                │ 8. Delete from queue           │                │
     │                │                │                │                │
```

### SEQ-010: Dead Letter Handling

**Traces to:** FR-4.3.2, NFR-2.2

```
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌─────────┐
│  Queue   │     │   MLLP   │     │   RIS    │     │  Admin  │
│ Manager  │     │  Client  │     │          │     │  Alert  │
└────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬────┘
     │                │                │                │
     │  [Retry 1 - 5s]│                │                │
     │───────────────►│                │                │
     │                │───────────────►│                │
     │◄───────────────│◄───────────────│                │
     │  (Failed)      │                │                │
     │                │                │                │
     │  [Retry 2 - 10s]               │                │
     │───────────────►│                │                │
     │◄───────────────│                │                │
     │  (Failed)      │                │                │
     │                │                │                │
     │  [Retry 3 - 20s]               │                │
     │───────────────►│                │                │
     │◄───────────────│                │                │
     │  (Failed)      │                │                │
     │                │                │                │
     │  [Retry 4 - 40s]               │                │
     │───────────────►│                │                │
     │◄───────────────│                │                │
     │  (Failed)      │                │                │
     │                │                │                │
     │  [Retry 5 - 80s] (max retries) │                │
     │───────────────►│                │                │
     │◄───────────────│                │                │
     │  (Failed)      │                │                │
     │                │                │                │
     │  1. Move to dead letter        │                │
     │  queue (permanent failure)     │                │
     │                │                │                │
     │  2. Send alert │                │                │
     │───────────────────────────────────────────────►│
     │                │                │                │
     │                │                │                │ Log alert:
     │                │                │                │ "Message MSG001
     │                │                │                │  to RIS_PRIMARY
     │                │                │                │  failed after
     │                │                │                │  5 retries"
     │                │                │                │
```

---

## 6. Error Handling Scenarios

### SEQ-011: Invalid HL7 Message Handling

**Traces to:** Error codes -900 to -908

```
┌─────────┐     ┌──────────┐     ┌──────────┐
│ HIS/RIS │     │   MLLP   │     │   HL7    │
│         │     │  Server  │     │  Parser  │
└────┬────┘     └────┬─────┘     └────┬─────┘
     │               │                │
     │  Invalid HL7  │                │
     │  (missing MSH)│                │
     │──────────────►│                │
     │               │                │
     │               │ 1. Parse       │
     │               │───────────────►│
     │               │                │
     │               │                │ Error: -901
     │               │                │ MISSING_MSH_SEGMENT
     │               │                │
     │               │◄───────────────│
     │               │  Error         │
     │               │                │
     │               │ 2. Build NAK   │
     │               │ (MSA-1 = AE)   │
     │               │                │
     │  ACK (AE)     │                │
     │  ┌─────────────────────────────────────┐
     │  │ MSH|^~\&|BRIDGE|...|ACK|...         │
     │  │ MSA|AE||Missing MSH segment|        │
     │  └─────────────────────────────────────┘
     │◄──────────────│                │
     │               │                │
     │               │ 3. Log error   │
     │               │ with details   │
     │               │                │
```

### SEQ-012: pacs_system Connection Failure

**Traces to:** Error codes -980 to -986

```
┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│   HIS   │     │  Bridge  │     │   MWL    │     │pacs_system│
│         │     │  Server  │     │  Client  │     │  (Down)   │
└────┬────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │               │                │                │
     │  ORM^O01      │                │                │
     │──────────────►│                │                │
     │               │                │                │
     │               │ 1. Convert to MWL               │
     │               │ (success)      │                │
     │               │                │                │
     │               │ 2. Add to MWL  │                │
     │               │───────────────►│                │
     │               │                │                │
     │               │                │ 3. DICOM       │
     │               │                │ Association    │
     │               │                │───────────────►│
     │               │                │                │
     │               │                │                │ [Timeout]
     │               │                │◄─ ─ ─ ─ ─ ─ ─ ┤
     │               │                │  Connection   │
     │               │                │  Failed       │
     │               │                │                │
     │               │◄───────────────│                │
     │               │  Error: -980   │                │
     │               │  PACS_CONNECTION_FAILED         │
     │               │                │                │
     │               │ 4. Queue for retry              │
     │               │ (store locally)│                │
     │               │                │                │
     │               │ 5. Return provisional ACK      │
     │               │                │                │
     │  ACK (AA)     │                │                │
     │  (Queued for  │                │                │
     │   delivery)   │                │                │
     │◄──────────────│                │                │
     │               │                │                │
```

---

## 7. IHE Scheduled Workflow

### SEQ-013: Complete IHE SWF Sequence

**Traces to:** PCR-3, IHE SWF Profile

```
┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐
│   ADT   │  │Order    │  │ PACS    │  │ Modality│  │ PACS    │  │   RIS   │
│Registr. │  │Placer   │  │ Bridge  │  │  (CT)   │  │ Server  │  │         │
└────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘
     │            │            │            │            │            │
     │ RAD-1: Patient Registration         │            │            │
     │ (ADT^A04)  │            │            │            │            │
     │───────────────────────►│            │            │            │
     │            │            │            │            │            │
     │            │            │ Update patient cache    │            │
     │            │            │────────────────────────►│            │
     │            │            │            │            │            │
     │            │            │            │            │            │
     │            │ RAD-2: Placer Order                 │            │
     │            │ (ORM^O01)  │            │            │            │
     │            │───────────►│            │            │            │
     │            │            │            │            │            │
     │            │            │ RAD-4: Create MWL entry │            │
     │            │            │────────────────────────►│            │
     │            │            │            │            │            │
     │            │            │            │ RAD-5: MWL Query         │
     │            │            │            │ (C-FIND)   │            │
     │            │            │            │───────────►│            │
     │            │            │            │            │            │
     │            │            │            │◄───────────│            │
     │            │            │            │  Worklist  │            │
     │            │            │            │            │            │
     │            │            │            │            │            │
     │            │            │            │ [Exam Starts]           │
     │            │            │            │            │            │
     │            │            │            │ RAD-6: MPPS In Progress │
     │            │            │            │ (N-CREATE) │            │
     │            │            │            │───────────►│            │
     │            │            │            │            │            │
     │            │            │◄────────────────────────│            │
     │            │            │  MPPS Event│            │            │
     │            │            │            │            │            │
     │            │            │ Status Update (IP)      │            │
     │            │            │───────────────────────────────────────►│
     │            │            │            │            │            │
     │            │            │            │            │            │
     │            │            │            │ [Exam Done]│            │
     │            │            │            │            │            │
     │            │            │            │ RAD-7: MPPS Completed   │
     │            │            │            │ (N-SET)    │            │
     │            │            │            │───────────►│            │
     │            │            │            │            │            │
     │            │            │◄────────────────────────│            │
     │            │            │  MPPS Event│            │            │
     │            │            │            │            │            │
     │            │            │ Status Update (CM)      │            │
     │            │            │───────────────────────────────────────►│
     │            │            │            │            │            │
     │            │            │            │ RAD-8: Storage Commitment│
     │            │            │            │ (Images)   │            │
     │            │            │            │───────────►│            │
     │            │            │            │            │            │
     │            │            │            │            │ Images stored
     │            │            │            │            │            │
```

---

*Document Version: 1.0.0*
*Created: 2025-12-07*
*Author: kcenon@naver.com*
