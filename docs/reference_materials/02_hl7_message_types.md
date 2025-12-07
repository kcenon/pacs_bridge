# HL7 Message Types for Radiology Integration

## Overview

This document details the HL7 v2.x message types most relevant to PACS-RIS/HIS integration.

## 1. ADT Messages (Admit, Discharge, Transfer)

ADT messages manage patient demographic and visit information.

### ADT^A04 - Patient Registration

**Purpose**: Register a new outpatient or update existing patient registration.

**Trigger**: Patient arrives for imaging exam.

```
MSH|^~\&|HIS|HOSPITAL|PACS|RADIOLOGY|20231115103000||ADT^A04^ADT_A01|MSG00001|P|2.5.1
EVN|A04|20231115103000
PID|1||12345^^^HOSP^MR~9876543210^^^SSN||DOE^JOHN^ANDREW^^MR||19800115|M|||123 MAIN ST^^CITY^ST^12345^USA||555-123-4567|||S|||987-65-4321
PV1|1|O|RAD^WAITING^01||||1234^SMITH^ROBERT^J^MD|||RAD||||||||V12345|||||||||||||||||||||||||20231115103000
```

**Key Segments**:
- MSH: Message header
- EVN: Event type and timestamp
- PID: Patient identification
- PV1: Patient visit information

### ADT^A08 - Update Patient Information

**Purpose**: Update patient demographics without changing visit status.

**Trigger**: Patient information corrected in HIS.

```
MSH|^~\&|HIS|HOSPITAL|PACS|RADIOLOGY|20231115110000||ADT^A08^ADT_A01|MSG00002|P|2.5.1
EVN|A08|20231115110000
PID|1||12345^^^HOSP^MR||DOE^JOHN^ANDREW^^MR||19800115|M|||456 NEW ST^^CITY^ST^12345^USA||555-987-6543
PV1|1|O|RAD^WAITING^01||||||||RAD
```

### ADT^A40 - Merge Patient

**Purpose**: Merge duplicate patient records.

**Trigger**: Duplicate patient identified and merged in HIS.

```
MSH|^~\&|HIS|HOSPITAL|PACS|RADIOLOGY|20231115120000||ADT^A40^ADT_A39|MSG00003|P|2.5.1
EVN|A40|20231115120000
PID|1||12345^^^HOSP^MR||DOE^JOHN^ANDREW^^MR||19800115|M
MRG|98765^^^HOSP^MR|12345
```

**Key**: MRG segment contains the "old" (merged from) patient ID.

---

## 2. ORM Messages (Order Message)

ORM messages handle radiology order lifecycle.

### ORM^O01 - General Order Message

**Purpose**: Create, update, or cancel radiology orders.

**Trigger**: Order placed, modified, or cancelled in RIS/HIS.

#### New Order Example

```
MSH|^~\&|RIS|RADIOLOGY|PACS|RADIOLOGY|20231115130000||ORM^O01^ORM_O01|MSG00004|P|2.5.1
PID|1||12345^^^HOSP^MR||DOE^JOHN^ANDREW^^MR||19800115|M|||123 MAIN ST^^CITY^ST^12345
PV1|1|O|RAD^CT^01||||1234^SMITH^ROBERT^J^MD
ORC|NW|ORD001^RIS|ACC001^PACS||SC|||1^ONCE^^^^S||20231115130000|1234^SMITH^ROBERT^J^MD
OBR|1|ORD001^RIS|ACC001^PACS|CT001^CT CHEST W/O CONTRAST^LOCAL|||20231115140000||||||||1234^SMITH^ROBERT^J^MD||||||||||1^^^20231115140000^^S
ZDS|1.2.840.113619.2.55.3.604688119.929.1234567890.1^STUDY_UID
```

**Order Control (ORC-1) Values**:

| Code | Description | Action |
|------|-------------|--------|
| NW | New Order | Create new order |
| XO | Change Order | Modify existing order |
| CA | Cancel Order | Cancel the order |
| DC | Discontinue | Discontinue order |
| SC | Status Changed | Status update only |
| HD | Hold Order | Put order on hold |
| RL | Release Hold | Release held order |

**Order Status (ORC-5) Values**:

| Code | Description |
|------|-------------|
| SC | Scheduled |
| IP | In Progress |
| CM | Completed |
| CA | Cancelled |
| HD | On Hold |

#### Cancel Order Example

```
MSH|^~\&|RIS|RADIOLOGY|PACS|RADIOLOGY|20231115140000||ORM^O01^ORM_O01|MSG00005|P|2.5.1
PID|1||12345^^^HOSP^MR||DOE^JOHN
ORC|CA|ORD001^RIS|ACC001^PACS||CA
OBR|1|ORD001^RIS|ACC001^PACS|CT001^CT CHEST^LOCAL
```

---

## 3. ORU Messages (Observation Result)

ORU messages transmit radiology reports and results.

### ORU^R01 - Unsolicited Observation Result

**Purpose**: Transmit radiology report from PACS/RIS to HIS/EMR.

**Trigger**: Radiologist finalizes report.

```
MSH|^~\&|PACS|RADIOLOGY|HIS|HOSPITAL|20231115160000||ORU^R01^ORU_R01|MSG00006|P|2.5.1
PID|1||12345^^^HOSP^MR||DOE^JOHN^ANDREW
PV1|1|O|RAD^CT^01
ORC|RE|ORD001^RIS|ACC001^PACS||CM
OBR|1|ORD001^RIS|ACC001^PACS|CT001^CT CHEST W/O CONTRAST^LOCAL|||20231115140000|||||||20231115150000|CHEST|5678^JONES^MARY^L^MD||||||20231115155000|||F
OBX|1|TX|59776-5^FINDINGS^LN||LUNGS: Clear. No infiltrates or effusions.||||||F
OBX|2|TX|19005-8^IMPRESSION^LN||No acute cardiopulmonary disease.||||||F
OBX|3|TX|18782-3^RECOMMENDATION^LN||No follow-up required.||||||F
```

**Report Status (OBR-25) Values**:

| Code | Description |
|------|-------------|
| P | Preliminary |
| F | Final |
| C | Correction |
| X | Cancelled |
| A | Amended |

**Observation Status (OBX-11) Values**:

| Code | Description |
|------|-------------|
| F | Final |
| P | Preliminary |
| C | Corrected |
| D | Deleted |
| W | Wrong |

---

## 4. SIU Messages (Scheduling)

SIU messages manage appointment scheduling.

### SIU^S12 - New Appointment Notification

**Purpose**: Notify systems of a newly scheduled appointment.

**Trigger**: Appointment scheduled in RIS.

```
MSH|^~\&|RIS|RADIOLOGY|PACS|RADIOLOGY|20231115080000||SIU^S12^SIU_S12|MSG00007|P|2.5.1
SCH|APT001^RIS|ORD001^RIS|||CT001^CT CHEST|20231115140000|30|MIN|SCHEDULED|||||1234^SMITH^ROBERT
PID|1||12345^^^HOSP^MR||DOE^JOHN^ANDREW
PV1|1|O|RAD^CT^01
RGS|1|A
AIS|1|A|CT001^CT CHEST|20231115140000|30|MIN
AIL|1|A|CT_SCANNER_1^CT ROOM 1^RAD
AIP|1|A|5678^TECH^JOHN|TECH
```

**Key Segments**:
- SCH: Scheduling activity information
- RGS: Resource group segment
- AIS: Appointment information - service
- AIL: Appointment information - location
- AIP: Appointment information - personnel

### SIU^S15 - Cancellation Notification

**Purpose**: Notify systems of appointment cancellation.

```
MSH|^~\&|RIS|RADIOLOGY|PACS|RADIOLOGY|20231115090000||SIU^S15^SIU_S12|MSG00008|P|2.5.1
SCH|APT001^RIS|ORD001^RIS|||CT001^CT CHEST|20231115140000|30|MIN|CANCELLED
PID|1||12345^^^HOSP^MR||DOE^JOHN
RGS|1|D
AIS|1|D|CT001^CT CHEST
```

---

## 5. Message Type Summary for PACS Bridge

### Inbound Messages (RIS/HIS -> PACS)

| Message | Trigger | PACS Action |
|---------|---------|-------------|
| ADT^A04 | Patient registered | Cache patient info |
| ADT^A08 | Patient updated | Update cached info |
| ADT^A40 | Patient merged | Merge patient records |
| ORM^O01 (NW) | New order | Create MWL entry |
| ORM^O01 (XO) | Order modified | Update MWL entry |
| ORM^O01 (CA) | Order cancelled | Remove MWL entry |
| SIU^S12 | Appointment scheduled | Create/update MWL |
| SIU^S15 | Appointment cancelled | Remove MWL entry |

### Outbound Messages (PACS -> RIS/HIS)

| Message | Trigger | Source Event |
|---------|---------|--------------|
| ORM^O01 (SC/IP) | Exam started | MPPS N-CREATE |
| ORM^O01 (CM) | Exam completed | MPPS N-SET COMPLETED |
| ORU^R01 | Report finalized | Report dictated/signed |

---

## References

- [InterfaceWare - HL7 ORM Message](https://www.interfaceware.com/hl7-orm)
- [InterfaceWare - HL7 ORU Message](https://www.interfaceware.com/hl7-oru)
- [HL7 v2.5.1 Chapter 4 - Order Entry](https://www.hl7.eu/HL7v2x/v251/std251/ch04.html)
- [HL7 v2.5.1 Chapter 7 - Observation Reporting](https://www.vico.org/HL7_V2_5/v251/std251/ch07.html)
