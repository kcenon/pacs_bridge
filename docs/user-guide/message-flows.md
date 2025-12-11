# Message Flows

> **Version:** 0.1.0.0
> **Last Updated:** 2025-12-10

---

## Table of Contents

- [Overview](#overview)
- [HL7 Message Flows](#hl7-message-flows)
- [DICOM Message Flows](#dicom-message-flows)
- [FHIR API Flows](#fhir-api-flows)
- [Integration Scenarios](#integration-scenarios)

---

## Overview

PACS Bridge supports multiple protocols and message types for healthcare system integration. This document describes the message flows between systems.

### Protocol Stack

```
┌─────────────────────────────────────────────────────────────────┐
│                        PACS Bridge                              │
├─────────────────┬─────────────────┬─────────────────────────────┤
│   HL7 Gateway   │   FHIR Gateway  │     PACS Adapter            │
│   (MLLP)        │   (REST)        │     (DICOM)                 │
├─────────────────┼─────────────────┼─────────────────────────────┤
│   TCP/TLS       │   HTTP/HTTPS    │     TCP                     │
└─────────────────┴─────────────────┴─────────────────────────────┘
```

---

## HL7 Message Flows

### Patient Admission (ADT^A01)

When a patient is admitted to the hospital:

```
┌─────────┐          ┌─────────────┐          ┌──────────────┐
│   HIS   │          │ PACS Bridge │          │ Patient Cache│
└────┬────┘          └──────┬──────┘          └──────┬───────┘
     │                      │                        │
     │  ADT^A01 (Admit)     │                        │
     │─────────────────────►│                        │
     │                      │                        │
     │                      │  Store Patient         │
     │                      │───────────────────────►│
     │                      │                        │
     │                      │◄───────────────────────│
     │                      │                        │
     │  ACK^A01             │                        │
     │◄─────────────────────│                        │
```

**ADT^A01 Message Structure:**

```
MSH|^~\&|HIS|HOSPITAL|PACS_BRIDGE|PACS|20251210120000||ADT^A01|MSG001|P|2.5
EVN|A01|20251210120000
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^M||19800101|M|||123 MAIN ST^^CITY^ST^12345
PV1|1|I|WARD1^ROOM101^BED1|E|||1234^SMITH^JAMES^^^DR
```

### Patient Registration (ADT^A04)

When a patient is registered for outpatient services:

```
┌─────────┐          ┌─────────────┐          ┌──────────────┐
│   HIS   │          │ PACS Bridge │          │ Patient Cache│
└────┬────┘          └──────┬──────┘          └──────┬───────┘
     │                      │                        │
     │  ADT^A04 (Register)  │                        │
     │─────────────────────►│                        │
     │                      │                        │
     │                      │  Store/Update Patient  │
     │                      │───────────────────────►│
     │                      │                        │
     │  ACK^A04             │                        │
     │◄─────────────────────│                        │
```

### Patient Update (ADT^A08)

When patient demographics are updated:

```
┌─────────┐          ┌─────────────┐          ┌──────────────┐
│   HIS   │          │ PACS Bridge │          │ Patient Cache│
└────┬────┘          └──────┬──────┘          └──────┬───────┘
     │                      │                        │
     │  ADT^A08 (Update)    │                        │
     │─────────────────────►│                        │
     │                      │                        │
     │                      │  Update Patient        │
     │                      │───────────────────────►│
     │                      │                        │
     │  ACK^A08             │                        │
     │◄─────────────────────│                        │
```

### Patient Merge (ADT^A40)

When duplicate patient records are merged:

```
┌─────────┐          ┌─────────────┐          ┌──────────────┐
│   HIS   │          │ PACS Bridge │          │ Patient Cache│
└────┬────┘          └──────┬──────┘          └──────┬───────┘
     │                      │                        │
     │  ADT^A40 (Merge)     │                        │
     │─────────────────────►│                        │
     │                      │                        │
     │                      │  Merge Patient IDs     │
     │                      │───────────────────────►│
     │                      │                        │
     │  ACK^A40             │                        │
     │◄─────────────────────│                        │
```

### New Order (ORM^O01)

When a radiology order is placed:

```
┌─────────┐          ┌─────────────┐          ┌──────────────┐
│   RIS   │          │ PACS Bridge │          │     PACS     │
└────┬────┘          └──────┬──────┘          └──────┬───────┘
     │                      │                        │
     │  ORM^O01 (New Order) │                        │
     │─────────────────────►│                        │
     │                      │                        │
     │                      │  Convert to MWL Format │
     │                      │                        │
     │                      │  MWL Update            │
     │                      │───────────────────────►│
     │                      │                        │
     │                      │◄───────────────────────│
     │                      │                        │
     │  ACK^O01             │                        │
     │◄─────────────────────│                        │
```

**ORM^O01 Message Structure:**

```
MSH|^~\&|RIS|HOSPITAL|PACS_BRIDGE|PACS|20251210120000||ORM^O01|MSG002|P|2.5
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^M||19800101|M
PV1|1|O|RAD^WAITING
ORC|NW|ORD001|ACC001||SC||||20251210120000
OBR|1|ORD001|ACC001|CT001^CT HEAD W/O CONTRAST^L|||20251210130000
```

### Observation Result (ORU^R01)

When an imaging result is available:

```
┌─────────┐          ┌─────────────┐          ┌──────────────┐
│   RIS   │          │ PACS Bridge │          │     PACS     │
└────┬────┘          └──────┬──────┘          └──────┬───────┘
     │                      │                        │
     │                      │  MPPS Complete         │
     │                      │◄───────────────────────│
     │                      │                        │
     │                      │  Convert to ORU        │
     │                      │                        │
     │  ORU^R01 (Result)    │                        │
     │◄─────────────────────│                        │
     │                      │                        │
     │  ACK^R01             │                        │
     │─────────────────────►│                        │
```

### Scheduling Update (SIU^S12)

When an appointment is scheduled:

```
┌─────────┐          ┌─────────────┐          ┌──────────────┐
│   RIS   │          │ PACS Bridge │          │     PACS     │
└────┬────┘          └──────┬──────┘          └──────┬───────┘
     │                      │                        │
     │  SIU^S12 (Schedule)  │                        │
     │─────────────────────►│                        │
     │                      │                        │
     │                      │  Update Worklist       │
     │                      │───────────────────────►│
     │                      │                        │
     │  ACK^S12             │                        │
     │◄─────────────────────│                        │
```

---

## DICOM Message Flows

### Modality Worklist Query (C-FIND)

When a modality queries for scheduled procedures:

```
┌──────────┐          ┌─────────────┐          ┌──────────────┐
│ Modality │          │ PACS Bridge │          │ Patient Cache│
└────┬─────┘          └──────┬──────┘          └──────┬───────┘
     │                       │                        │
     │  MWL C-FIND Request   │                        │
     │──────────────────────►│                        │
     │                       │                        │
     │                       │  Lookup Patient        │
     │                       │───────────────────────►│
     │                       │                        │
     │                       │◄───────────────────────│
     │                       │                        │
     │  MWL C-FIND Response  │                        │
     │◄──────────────────────│                        │
```

### MPPS In Progress (N-CREATE)

When a procedure is started on a modality:

```
┌──────────┐          ┌─────────────┐          ┌──────────────┐
│ Modality │          │ PACS Bridge │          │     RIS      │
└────┬─────┘          └──────┬──────┘          └──────┬───────┘
     │                       │                        │
     │  MPPS N-CREATE        │                        │
     │  (In Progress)        │                        │
     │──────────────────────►│                        │
     │                       │                        │
     │                       │  Convert to ORU^R01    │
     │                       │  (Status: In Progress) │
     │                       │───────────────────────►│
     │                       │                        │
     │                       │  ACK                   │
     │                       │◄───────────────────────│
     │                       │                        │
     │  N-CREATE Response    │                        │
     │◄──────────────────────│                        │
```

### MPPS Complete (N-SET)

When a procedure is completed on a modality:

```
┌──────────┐          ┌─────────────┐          ┌──────────────┐
│ Modality │          │ PACS Bridge │          │     RIS      │
└────┬─────┘          └──────┬──────┘          └──────┬───────┘
     │                       │                        │
     │  MPPS N-SET           │                        │
     │  (Completed)          │                        │
     │──────────────────────►│                        │
     │                       │                        │
     │                       │  Convert to ORU^R01    │
     │                       │  (Status: Complete)    │
     │                       │───────────────────────►│
     │                       │                        │
     │                       │  ACK                   │
     │                       │◄───────────────────────│
     │                       │                        │
     │  N-SET Response       │                        │
     │◄──────────────────────│                        │
```

### MPPS Discontinued (N-SET)

When a procedure is discontinued:

```
┌──────────┐          ┌─────────────┐          ┌──────────────┐
│ Modality │          │ PACS Bridge │          │     RIS      │
└────┬─────┘          └──────┬──────┘          └──────┬───────┘
     │                       │                        │
     │  MPPS N-SET           │                        │
     │  (Discontinued)       │                        │
     │──────────────────────►│                        │
     │                       │                        │
     │                       │  Convert to ORU^R01    │
     │                       │  (Status: Discontinued)│
     │                       │───────────────────────►│
     │                       │                        │
     │  N-SET Response       │                        │
     │◄──────────────────────│                        │
```

---

## FHIR API Flows

### Search Patient

```
┌──────────┐          ┌─────────────┐          ┌──────────────┐
│   EHR    │          │ PACS Bridge │          │ Patient Cache│
└────┬─────┘          └──────┬──────┘          └──────┬───────┘
     │                       │                        │
     │  GET /Patient?mrn=123 │                        │
     │──────────────────────►│                        │
     │                       │                        │
     │                       │  Query Cache           │
     │                       │───────────────────────►│
     │                       │                        │
     │                       │◄───────────────────────│
     │                       │                        │
     │  200 OK (Bundle)      │                        │
     │◄──────────────────────│                        │
```

### Create ServiceRequest

```
┌──────────┐          ┌─────────────┐          ┌──────────────┐
│   EHR    │          │ PACS Bridge │          │     PACS     │
└────┬─────┘          └──────┬──────┘          └──────┬───────┘
     │                       │                        │
     │  POST /ServiceRequest │                        │
     │  (JSON Body)          │                        │
     │──────────────────────►│                        │
     │                       │                        │
     │                       │  Convert to MWL        │
     │                       │                        │
     │                       │  Update Worklist       │
     │                       │───────────────────────►│
     │                       │                        │
     │  201 Created          │                        │
     │◄──────────────────────│                        │
```

### Get ImagingStudy

```
┌──────────┐          ┌─────────────┐          ┌──────────────┐
│   EHR    │          │ PACS Bridge │          │     PACS     │
└────┬─────┘          └──────┬──────┘          └──────┬───────┘
     │                       │                        │
     │  GET /ImagingStudy/123│                        │
     │──────────────────────►│                        │
     │                       │                        │
     │                       │  Query Study           │
     │                       │───────────────────────►│
     │                       │                        │
     │                       │◄───────────────────────│
     │                       │                        │
     │  200 OK (JSON)        │                        │
     │◄──────────────────────│                        │
```

---

## Integration Scenarios

### Complete Radiology Workflow

End-to-end workflow from order to result:

```
┌─────┐     ┌─────┐     ┌─────────────┐     ┌──────────┐     ┌──────┐
│ HIS │     │ RIS │     │ PACS Bridge │     │ Modality │     │ PACS │
└──┬──┘     └──┬──┘     └──────┬──────┘     └────┬─────┘     └──┬───┘
   │           │               │                 │              │
   │ 1. ADT^A04│               │                 │              │
   │──────────────────────────►│                 │              │
   │           │               │ Store Patient   │              │
   │           │               │                 │              │
   │           │ 2. ORM^O01    │                 │              │
   │           │──────────────►│                 │              │
   │           │               │ Update MWL─────────────────────►
   │           │               │                 │              │
   │           │               │ 3. MWL Query    │              │
   │           │               │◄────────────────│              │
   │           │               │ Return Worklist │              │
   │           │               │────────────────►│              │
   │           │               │                 │              │
   │           │               │ 4. MPPS N-CREATE│              │
   │           │               │◄────────────────│              │
   │           │ ORU^R01       │                 │              │
   │           │◄──────────────│                 │              │
   │           │               │                 │ Store Images │
   │           │               │                 │─────────────►│
   │           │               │                 │              │
   │           │               │ 5. MPPS N-SET   │              │
   │           │               │◄────────────────│              │
   │           │ ORU^R01       │                 │              │
   │           │◄──────────────│                 │              │
```

**Workflow Steps:**

1. **Patient Registration**: HIS registers patient, PACS Bridge caches demographics
2. **Order Entry**: RIS places order, PACS Bridge updates PACS worklist
3. **Worklist Query**: Modality queries available procedures
4. **Procedure Start**: Modality signals procedure started via MPPS
5. **Procedure Complete**: Modality signals completion via MPPS

### Failover Scenario

When primary RIS is unavailable:

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ PACS Bridge │     │ RIS Primary │     │ RIS Backup  │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │
       │  ORU^R01          │                   │
       │──────────────────►│                   │
       │                   X (Connection Failed)
       │                   │                   │
       │  Retry 1          │                   │
       │──────────────────►│                   │
       │                   X (Timeout)         │
       │                   │                   │
       │  Failover to Backup                   │
       │  ORU^R01          │                   │
       │──────────────────────────────────────►│
       │                   │                   │
       │  ACK^R01          │                   │
       │◄──────────────────────────────────────│
       │                   │                   │
       │  Queue message for Primary            │
       │  (for later delivery)                 │
```

### Patient Merge Scenario

When duplicate patient records need to be merged:

```
┌─────┐     ┌─────────────┐     ┌──────────────┐     ┌──────┐
│ HIS │     │ PACS Bridge │     │ Patient Cache│     │ PACS │
└──┬──┘     └──────┬──────┘     └──────┬───────┘     └──┬───┘
   │               │                   │                │
   │  ADT^A40      │                   │                │
   │  (Merge 12345 │                   │                │
   │   into 67890) │                   │                │
   │──────────────►│                   │                │
   │               │                   │                │
   │               │ Update Aliases    │                │
   │               │──────────────────►│                │
   │               │                   │                │
   │               │ Notify PACS       │                │
   │               │──────────────────────────────────► │
   │               │                   │                │
   │  ACK^A40      │                   │                │
   │◄──────────────│                   │                │
```

---

## Message Transformation Examples

### ORM^O01 to MWL Item

**Input (HL7 ORM^O01):**

```
MSH|^~\&|RIS|HOSPITAL|PACS_BRIDGE|PACS|20251210120000||ORM^O01|MSG001|P|2.5
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^M||19800101|M
ORC|NW|ORD001|ACC001||SC
OBR|1|ORD001|ACC001|CT001^CT HEAD W/O CONTRAST|||20251210130000
```

**Output (MWL Attributes):**

| DICOM Tag | Attribute Name | Value |
|-----------|---------------|-------|
| (0010,0020) | Patient ID | 12345 |
| (0010,0010) | Patient's Name | DOE^JOHN^M |
| (0010,0030) | Patient's Birth Date | 19800101 |
| (0010,0040) | Patient's Sex | M |
| (0008,0050) | Accession Number | ACC001 |
| (0040,1001) | Requested Procedure ID | ORD001 |
| (0032,1060) | Requested Procedure Description | CT HEAD W/O CONTRAST |
| (0040,0100) | Scheduled Procedure Step Sequence | ... |

### MPPS to ORU^R01

**Input (MPPS N-SET - Completed):**

| DICOM Tag | Attribute Name | Value |
|-----------|---------------|-------|
| (0040,0252) | Performed Procedure Step Status | COMPLETED |
| (0008,0050) | Accession Number | ACC001 |
| (0040,0253) | Performed Procedure Step ID | MPPS001 |
| (0040,0254) | Performed Procedure Step Description | CT HEAD |

**Output (HL7 ORU^R01):**

```
MSH|^~\&|PACS_BRIDGE|PACS|RIS|HOSPITAL|20251210140000||ORU^R01|MSG002|P|2.5
PID|1||12345^^^HOSPITAL^MR
ORC|SC|ORD001|ACC001||CM
OBR|1|ORD001|ACC001|CT001^CT HEAD||20251210130000|20251210140000||||||||||||||F
```

---

## Related Documentation

- [HL7 Message Reference](../api/hl7-messages.md) - Detailed HL7 message specifications
- [Error Codes](../api/error-codes.md) - Error handling and codes
- [Troubleshooting](troubleshooting.md) - Common issues and solutions
