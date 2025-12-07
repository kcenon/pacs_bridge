# IHE Scheduled Workflow (SWF) Integration Profile

## Overview

The IHE (Integrating the Healthcare Enterprise) Scheduled Workflow (SWF) profile defines the integration of ordering, scheduling, imaging acquisition, storage, and viewing activities in a radiology department. It bridges HL7-based systems (HIS, RIS) with DICOM-based systems (modalities, PACS).

## Objectives

1. **Data Consistency**: Maintain consistency of patient demographics and ordering information across all systems
2. **Workflow Tracking**: Accurate tracking of study identification and status throughout the department
3. **Error Reduction**: Minimize staff time wasted on identifying and correcting errors
4. **Image Security**: Ensure acquired images are not lost through proper custody transfer

## Actors and Transactions

### Actors

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         IHE SWF Actors                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐               │
│  │   ADT       │     │  Order      │     │  DSS/OF     │               │
│  │   System    │     │  Placer     │     │  (RIS)      │               │
│  └──────┬──────┘     └──────┬──────┘     └──────┬──────┘               │
│         │                   │                   │                       │
│    RAD-1│              RAD-2│             RAD-3 │                       │
│    Patient              Placer          Filler                          │
│    Registration         Order           Order                           │
│         │                   │                   │                       │
│         ▼                   ▼                   ▼                       │
│  ┌─────────────────────────────────────────────────────┐               │
│  │              Department System Scheduler             │               │
│  │                     (DSS/OF)                         │               │
│  └─────────────────────────┬───────────────────────────┘               │
│                            │                                            │
│                     RAD-4  │  Query Modality Worklist                   │
│                     RAD-5  │  Modality Procedure Step                   │
│                            ▼                                            │
│  ┌─────────────────────────────────────────────────────┐               │
│  │           Acquisition Modality (CT, MR, etc.)        │               │
│  └─────────────────────────┬───────────────────────────┘               │
│                            │                                            │
│                     RAD-8  │  Modality Images Stored                    │
│                     RAD-10 │  Storage Commitment                        │
│                            ▼                                            │
│  ┌─────────────────────────────────────────────────────┐               │
│  │              Image Archive (PACS)                    │               │
│  └─────────────────────────┬───────────────────────────┘               │
│                            │                                            │
│                     RAD-14 │  Query Images                              │
│                     RAD-16 │  Retrieve Images                           │
│                            ▼                                            │
│  ┌─────────────────────────────────────────────────────┐               │
│  │              Image Display (Viewer)                  │               │
│  └─────────────────────────────────────────────────────┘               │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key Transactions

| ID | Transaction | From | To | Protocol |
|----|-------------|------|-----|----------|
| RAD-1 | Patient Registration | ADT | DSS/OF | HL7 ADT |
| RAD-2 | Placer Order Management | Order Placer | DSS/OF | HL7 ORM |
| RAD-3 | Filler Order Management | DSS/OF | Order Placer | HL7 ORM |
| RAD-4 | Procedure Scheduled | DSS/OF | Image Manager | HL7 ORM |
| RAD-5 | Query Modality Worklist | Modality | DSS/OF | DICOM MWL |
| RAD-6 | Modality Procedure Step In Progress | Modality | DSS/OF | DICOM MPPS |
| RAD-7 | Modality Procedure Step Completed | Modality | DSS/OF | DICOM MPPS |
| RAD-8 | Modality Images Stored | Modality | Image Archive | DICOM C-STORE |
| RAD-10 | Storage Commitment | Modality | Image Archive | DICOM Storage Commit |
| RAD-14 | Query Images | Display | Image Archive | DICOM C-FIND |
| RAD-16 | Retrieve Images | Display | Image Archive | DICOM C-MOVE/C-GET |

---

## Workflow Sequence

### Complete SWF Sequence Diagram

```
ADT      Order      DSS/OF       Modality      Image        Image
System   Placer     (RIS)                      Archive      Display
  │         │          │             │            │            │
  │  RAD-1 (ADT^A04)   │             │            │            │
  │────────────────────│             │            │            │
  │                    │             │            │            │
  │         │  RAD-2   │             │            │            │
  │         │(ORM^O01) │             │            │            │
  │         │─────────►│             │            │            │
  │         │          │             │            │            │
  │         │  RAD-3   │             │            │            │
  │         │(ORM^O01) │             │            │            │
  │         │◄─────────│             │            │            │
  │         │          │             │            │            │
  │         │          │   RAD-4     │            │            │
  │         │          │ (ORM^O01)   │            │            │
  │         │          │────────────►│            │            │
  │         │          │             │            │            │
  │         │          │   RAD-5     │            │            │
  │         │          │ (C-FIND MWL)│            │            │
  │         │          │◄────────────│            │            │
  │         │          │             │            │            │
  │         │          │   MWL Response           │            │
  │         │          │────────────►│            │            │
  │         │          │             │            │            │
  │         │          │   RAD-6     │            │            │
  │         │          │ (MPPS N-CREATE)          │            │
  │         │          │◄────────────│            │            │
  │         │          │             │            │            │
  │         │          │             │   RAD-8    │            │
  │         │          │             │ (C-STORE)  │            │
  │         │          │             │───────────►│            │
  │         │          │             │            │            │
  │         │          │   RAD-7     │            │            │
  │         │          │ (MPPS N-SET)│            │            │
  │         │          │◄────────────│            │            │
  │         │          │             │            │            │
  │         │          │             │  RAD-10    │            │
  │         │          │             │(Stg Commit)│            │
  │         │          │             │───────────►│            │
  │         │          │             │            │            │
  │         │          │             │            │  RAD-14    │
  │         │          │             │            │ (C-FIND)   │
  │         │          │             │            │◄───────────│
  │         │          │             │            │            │
  │         │          │             │            │  RAD-16    │
  │         │          │             │            │(C-MOVE/GET)│
  │         │          │             │            │───────────►│
```

---

## Key Transactions Details

### RAD-1: Patient Registration (HL7 ADT)

**Purpose**: Register or update patient demographic information.

**Messages**:
- ADT^A01 - Admit
- ADT^A04 - Register
- ADT^A08 - Update Patient Information
- ADT^A40 - Merge Patient

**Required Data**:
- Patient ID (PID-3)
- Patient Name (PID-5)
- Date of Birth (PID-7)
- Sex (PID-8)

### RAD-2/RAD-3: Order Management (HL7 ORM)

**Purpose**: Create and manage imaging orders.

**Messages**:
- ORM^O01 - Order Message

**Order Control Values**:
| Code | Direction | Description |
|------|-----------|-------------|
| NW | Placer→Filler | New Order |
| OK | Filler→Placer | Order Accepted |
| CA | Placer→Filler | Cancel Request |
| CR | Filler→Placer | Cancelled as Requested |
| SC | Filler→Placer | Status Changed |

### RAD-5: Query Modality Worklist (DICOM MWL C-FIND)

**Purpose**: Modality queries for scheduled procedures.

**Query Keys**:
- Scheduled Station AE Title (0040,0001)
- Scheduled Procedure Step Start Date (0040,0002)
- Scheduled Procedure Step Start Time (0040,0003)
- Modality (0008,0060)

**Return Keys**:
- Patient demographics (0010,xxxx)
- Scheduled Procedure Step Sequence (0040,0100)
- Study Instance UID (0020,000D)
- Accession Number (0008,0050)

### RAD-6/RAD-7: MPPS (DICOM N-CREATE/N-SET)

**Purpose**: Report procedure step progress to RIS.

**RAD-6 (N-CREATE)**:
- Status: IN PROGRESS
- Patient/Study information
- Scheduled Step Attributes

**RAD-7 (N-SET)**:
- Status: COMPLETED or DISCONTINUED
- Performed Series Sequence
- Performed Procedure Step End Date/Time

---

## Data Mapping

### HL7 to DICOM MWL Mapping

| HL7 Segment.Field | DICOM Tag | Keyword |
|-------------------|-----------|---------|
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

### DICOM MPPS to HL7 ORM Mapping

| DICOM Tag | Keyword | HL7 Segment.Field |
|-----------|---------|-------------------|
| (0040,0252) | PerformedProcedureStepStatus | ORC-5 (Order Status) |
| (0040,0250) | PerformedProcedureStepEndDate | OBR-8 |
| (0040,0251) | PerformedProcedureStepEndTime | OBR-8 |
| (0040,0253) | PerformedProcedureStepID | OBR-3 |

---

## SWF.b (Scheduled Workflow.b)

SWF.b is the updated version of the SWF profile with enhancements:

### Key Improvements

1. **Unscheduled Workflows**: Support for emergency/unscheduled exams
2. **Patient ID Reconciliation**: Better handling of unknown patients
3. **Group Scheduling**: Multiple procedure steps per order
4. **Enhanced Status Tracking**: More granular status values

### Additional Status Values

| Status | Description |
|--------|-------------|
| SCHEDULED | Procedure scheduled (default) |
| ARRIVED | Patient arrived |
| READY | Ready to perform |
| STARTED | Exam started (MPPS IN PROGRESS) |
| DEPARTED | Patient left |
| COMPLETED | Exam completed (MPPS COMPLETED) |
| CANCELLED | Procedure cancelled |
| DISCONTINUED | Procedure discontinued (MPPS DISCONTINUED) |

---

## Related IHE Profiles

### Patient Information Reconciliation (PIR)

Handles cases of unidentified (trauma) or misidentified patients:

- Links unscheduled exams to correct patients
- Reconciles patient demographics post-acquisition

### Reporting Workflow (RWF)

Extends SWF to include radiology reporting:

- Report creation and status tracking
- Distribution to referring physicians

### Post-Processing Workflow (PPWF)

Handles post-acquisition processing:

- 3D reconstructions
- CAD analysis
- Advanced visualization

### Cross-Enterprise Document Sharing for Imaging (XDS-I.b)

Shares imaging studies across healthcare enterprises:

- Document registry and repository
- Image manifest sharing

---

## Implementation Considerations

### For PACS Bridge

1. **Inbound Processing**:
   - Parse HL7 ADT/ORM messages from HIS/RIS
   - Create/update MWL entries in DICOM MWL database
   - Map HL7 fields to DICOM attributes

2. **Outbound Processing**:
   - Receive MPPS notifications from modalities
   - Convert MPPS status to HL7 ORM status updates
   - Send HL7 messages to RIS

3. **Synchronization**:
   - Handle timing differences between systems
   - Manage duplicate detection
   - Support retry/error handling

### Error Handling

| Scenario | Handling |
|----------|----------|
| Patient not in HIS | Create temporary record, flag for reconciliation |
| Order not found | Create unscheduled exam, link later |
| MPPS without MWL | Accept and store, reconcile with order |
| Duplicate messages | Detect by message control ID, ignore duplicates |

---

## References

- [IHE Radiology Technical Framework](https://www.ihe.net/resources/technical_frameworks/#radiology)
- [Scheduled Workflow - IHE Wiki](https://wiki.ihe.net/index.php/Scheduled_Workflow)
- [Scheduled Workflow.b - IHE Wiki](https://wiki.ihe.net/index.php/Scheduled_Workflow.b)
- [IHE Radiology User's Handbook](https://www.ihe.net/wp-content/uploads/2018/07/ihe_radiology_users_handbook_2005edition.pdf)
- [Reporting Workflow - IHE Wiki](https://wiki.ihe.net/index.php/Reporting_Workflow)
