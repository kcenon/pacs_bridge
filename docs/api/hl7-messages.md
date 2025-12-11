# HL7 Message Reference

> **Version:** 0.1.0.0
> **Last Updated:** 2025-12-10

---

## Table of Contents

- [Overview](#overview)
- [Message Structure](#message-structure)
- [Supported Message Types](#supported-message-types)
- [Segment Definitions](#segment-definitions)
- [Field Mappings](#field-mappings)
- [Examples](#examples)

---

## Overview

PACS Bridge supports HL7 v2.x messaging (versions 2.3.1 through 2.9) over MLLP (Minimal Lower Layer Protocol). This document provides reference information for all supported message types and their structures.

### Protocol Stack

```
┌─────────────────────────────┐
│        HL7 Message          │
├─────────────────────────────┤
│      MLLP Framing           │
│  <VT> message <FS><CR>      │
├─────────────────────────────┤
│         TCP/TLS             │
└─────────────────────────────┘
```

### MLLP Framing

| Character | Hex | Name | Position |
|-----------|-----|------|----------|
| VT | 0x0B | Start Block | Before message |
| FS | 0x1C | End Block | After message |
| CR | 0x0D | Carriage Return | After FS |

---

## Message Structure

### General Structure

An HL7 message consists of segments, each containing fields:

```
MSH|^~\&|SENDING_APP|SENDING_FAC|RECEIVING_APP|RECEIVING_FAC|20251210120000||MSG_TYPE|MSG_ID|P|2.5
SEGMENT|Field1|Field2|Field3^Component1^Component2|Field4
```

### Delimiters

| Delimiter | Character | Purpose |
|-----------|-----------|---------|
| Field | `|` | Separates fields |
| Component | `^` | Separates components within a field |
| Subcomponent | `&` | Separates subcomponents |
| Repetition | `~` | Separates repeated fields |
| Escape | `\` | Escape character |

---

## Supported Message Types

### ADT Messages (Admission/Discharge/Transfer)

| Message Type | Event | Description | Direction |
|--------------|-------|-------------|-----------|
| ADT^A01 | A01 | Patient Admit | Inbound |
| ADT^A04 | A04 | Patient Registration | Inbound |
| ADT^A08 | A08 | Patient Information Update | Inbound |
| ADT^A40 | A40 | Patient Merge | Inbound |

### ORM Messages (Orders)

| Message Type | Event | Description | Direction |
|--------------|-------|-------------|-----------|
| ORM^O01 | O01 | Order Message | Both |

### ORU Messages (Results)

| Message Type | Event | Description | Direction |
|--------------|-------|-------------|-----------|
| ORU^R01 | R01 | Observation Result | Outbound |

### SIU Messages (Scheduling)

| Message Type | Event | Description | Direction |
|--------------|-------|-------------|-----------|
| SIU^S12 | S12 | Schedule Notification | Inbound |
| SIU^S13 | S13 | Schedule Modification | Inbound |
| SIU^S14 | S14 | Schedule Cancellation | Inbound |

### ACK Messages (Acknowledgment)

| Message Type | Description | Direction |
|--------------|-------------|-----------|
| ACK | General Acknowledgment | Outbound |

---

## Segment Definitions

### MSH - Message Header

| Field | Position | Name | Type | Required | Description |
|-------|----------|------|------|----------|-------------|
| MSH-1 | 1 | Field Separator | ST | R | Always `|` |
| MSH-2 | 2 | Encoding Characters | ST | R | Always `^~\&` |
| MSH-3 | 3 | Sending Application | HD | O | Source application |
| MSH-4 | 4 | Sending Facility | HD | O | Source facility |
| MSH-5 | 5 | Receiving Application | HD | O | Destination application |
| MSH-6 | 6 | Receiving Facility | HD | O | Destination facility |
| MSH-7 | 7 | Date/Time of Message | TS | R | Message timestamp |
| MSH-8 | 8 | Security | ST | O | Security token |
| MSH-9 | 9 | Message Type | MSG | R | Message type and event |
| MSH-10 | 10 | Message Control ID | ST | R | Unique message identifier |
| MSH-11 | 11 | Processing ID | PT | R | P=Production, T=Training |
| MSH-12 | 12 | Version ID | VID | R | HL7 version (e.g., 2.5) |

### PID - Patient Identification

| Field | Position | Name | Type | Required | Description |
|-------|----------|------|------|----------|-------------|
| PID-1 | 1 | Set ID | SI | O | Sequence number |
| PID-2 | 2 | Patient ID (External) | CX | B | External patient ID |
| PID-3 | 3 | Patient ID (Internal) | CX | R | Patient identifier list |
| PID-4 | 4 | Alternate Patient ID | CX | B | Alternate ID |
| PID-5 | 5 | Patient Name | XPN | R | Patient name |
| PID-6 | 6 | Mother's Maiden Name | XPN | O | Mother's maiden name |
| PID-7 | 7 | Date/Time of Birth | TS | O | Birth date |
| PID-8 | 8 | Sex | IS | O | M, F, O, U |
| PID-9 | 9 | Patient Alias | XPN | O | Alias names |
| PID-10 | 10 | Race | CE | O | Race code |
| PID-11 | 11 | Patient Address | XAD | O | Address |
| PID-12 | 12 | County Code | IS | O | County |
| PID-13 | 13 | Phone Number (Home) | XTN | O | Home phone |
| PID-14 | 14 | Phone Number (Business) | XTN | O | Business phone |
| PID-18 | 18 | Patient Account Number | CX | O | Account number |
| PID-19 | 19 | SSN Number | ST | O | Social security number |

### PV1 - Patient Visit

| Field | Position | Name | Type | Required | Description |
|-------|----------|------|------|----------|-------------|
| PV1-1 | 1 | Set ID | SI | O | Sequence number |
| PV1-2 | 2 | Patient Class | IS | R | I=Inpatient, O=Outpatient, E=Emergency |
| PV1-3 | 3 | Assigned Patient Location | PL | O | Ward^Room^Bed |
| PV1-4 | 4 | Admission Type | IS | O | Admission type |
| PV1-7 | 7 | Attending Doctor | XCN | O | Attending physician |
| PV1-8 | 8 | Referring Doctor | XCN | O | Referring physician |
| PV1-10 | 10 | Hospital Service | IS | O | Hospital service |
| PV1-14 | 14 | Admit Source | IS | O | Source of admission |
| PV1-17 | 17 | Admitting Doctor | XCN | O | Admitting physician |
| PV1-19 | 19 | Visit Number | CX | O | Visit identifier |
| PV1-44 | 44 | Admit Date/Time | TS | O | Admission date/time |

### ORC - Common Order

| Field | Position | Name | Type | Required | Description |
|-------|----------|------|------|----------|-------------|
| ORC-1 | 1 | Order Control | ID | R | NW=New, CA=Cancel, etc. |
| ORC-2 | 2 | Placer Order Number | EI | R | Order number from placer |
| ORC-3 | 3 | Filler Order Number | EI | O | Order number from filler |
| ORC-4 | 4 | Placer Group Number | EI | O | Group number |
| ORC-5 | 5 | Order Status | ID | O | Order status code |
| ORC-7 | 7 | Quantity/Timing | TQ | O | Timing information |
| ORC-9 | 9 | Date/Time of Transaction | TS | O | Transaction time |
| ORC-10 | 10 | Entered By | XCN | O | Person entering order |
| ORC-12 | 12 | Ordering Provider | XCN | O | Ordering physician |
| ORC-13 | 13 | Enterer's Location | PL | O | Order entry location |
| ORC-15 | 15 | Order Effective Date/Time | TS | O | When order becomes effective |
| ORC-16 | 16 | Order Control Code Reason | CE | O | Reason for order control |

### OBR - Observation Request

| Field | Position | Name | Type | Required | Description |
|-------|----------|------|------|----------|-------------|
| OBR-1 | 1 | Set ID | SI | O | Sequence number |
| OBR-2 | 2 | Placer Order Number | EI | R | Order number from placer |
| OBR-3 | 3 | Filler Order Number | EI | O | Accession number |
| OBR-4 | 4 | Universal Service ID | CE | R | Procedure code |
| OBR-5 | 5 | Priority | ID | O | S=Stat, R=Routine |
| OBR-6 | 6 | Requested Date/Time | TS | O | Requested date/time |
| OBR-7 | 7 | Observation Date/Time | TS | O | Start date/time |
| OBR-8 | 8 | Observation End Date/Time | TS | O | End date/time |
| OBR-10 | 10 | Collector Identifier | XCN | O | Specimen collector |
| OBR-16 | 16 | Ordering Provider | XCN | O | Ordering physician |
| OBR-18 | 18 | Placer Field 1 | ST | O | Placer defined field |
| OBR-19 | 19 | Placer Field 2 | ST | O | Placer defined field |
| OBR-20 | 20 | Filler Field 1 | ST | O | Filler defined field |
| OBR-21 | 21 | Filler Field 2 | ST | O | Filler defined field |
| OBR-24 | 24 | Diagnostic Service Sect ID | ID | O | Department code |
| OBR-25 | 25 | Result Status | ID | O | F=Final, P=Preliminary |
| OBR-27 | 27 | Quantity/Timing | TQ | O | Timing specification |
| OBR-31 | 31 | Reason for Study | CE | O | Reason for procedure |
| OBR-44 | 44 | Procedure Code | CE | O | Procedure code |

### OBX - Observation Result

| Field | Position | Name | Type | Required | Description |
|-------|----------|------|------|----------|-------------|
| OBX-1 | 1 | Set ID | SI | O | Sequence number |
| OBX-2 | 2 | Value Type | ID | R | Data type of value |
| OBX-3 | 3 | Observation Identifier | CE | R | What was observed |
| OBX-4 | 4 | Observation Sub-ID | ST | O | Sub-identifier |
| OBX-5 | 5 | Observation Value | varies | R | The observed value |
| OBX-6 | 6 | Units | CE | O | Units of measure |
| OBX-7 | 7 | References Range | ST | O | Normal range |
| OBX-8 | 8 | Abnormal Flags | IS | O | Abnormality flags |
| OBX-11 | 11 | Observation Result Status | ID | R | F=Final, P=Preliminary |
| OBX-14 | 14 | Date/Time of Observation | TS | O | Observation date/time |
| OBX-15 | 15 | Producer's ID | CE | O | Producer identifier |
| OBX-16 | 16 | Responsible Observer | XCN | O | Observer |

### EVN - Event Type

| Field | Position | Name | Type | Required | Description |
|-------|----------|------|------|----------|-------------|
| EVN-1 | 1 | Event Type Code | ID | O | Event type |
| EVN-2 | 2 | Recorded Date/Time | TS | R | Event timestamp |
| EVN-3 | 3 | Date/Time Planned Event | TS | O | Planned event time |
| EVN-4 | 4 | Event Reason Code | IS | O | Reason for event |
| EVN-5 | 5 | Operator ID | XCN | O | Operator |
| EVN-6 | 6 | Event Occurred | TS | O | When event occurred |

### MRG - Merge Patient Information

| Field | Position | Name | Type | Required | Description |
|-------|----------|------|------|----------|-------------|
| MRG-1 | 1 | Prior Patient ID | CX | R | Previous patient ID |
| MRG-2 | 2 | Prior Alternate Patient ID | CX | O | Previous alternate ID |
| MRG-3 | 3 | Prior Patient Account Number | CX | O | Previous account number |
| MRG-4 | 4 | Prior Patient ID (External) | CX | O | Previous external ID |
| MRG-5 | 5 | Prior Visit Number | CX | O | Previous visit number |
| MRG-6 | 6 | Prior Alternate Visit ID | CX | O | Previous alternate visit ID |
| MRG-7 | 7 | Prior Patient Name | XPN | O | Previous patient name |

---

## Field Mappings

### HL7 to DICOM MWL Mapping

| HL7 Field | DICOM Tag | DICOM Attribute |
|-----------|-----------|-----------------|
| PID-3 | (0010,0020) | Patient ID |
| PID-5 | (0010,0010) | Patient's Name |
| PID-7 | (0010,0030) | Patient's Birth Date |
| PID-8 | (0010,0040) | Patient's Sex |
| PID-11 | (0010,1040) | Patient's Address |
| ORC-2 | (0040,1001) | Requested Procedure ID |
| OBR-3 | (0008,0050) | Accession Number |
| OBR-4 | (0032,1060) | Requested Procedure Description |
| OBR-4 | (0040,0007) | Scheduled Procedure Step Description |
| OBR-7 | (0040,0002) | Scheduled Procedure Step Start Date |
| OBR-7 | (0040,0003) | Scheduled Procedure Step Start Time |
| OBR-16 | (0032,1032) | Requesting Physician |

### MPPS to HL7 ORU Mapping

| DICOM Tag | DICOM Attribute | HL7 Field |
|-----------|-----------------|-----------|
| (0008,0050) | Accession Number | OBR-3 |
| (0040,0252) | Performed Procedure Step Status | OBR-25 |
| (0040,0253) | Performed Procedure Step ID | OBR-20 |
| (0040,0254) | Performed Procedure Step Description | OBR-4 |
| (0040,0244) | Performed Procedure Step Start Date | OBR-7 |
| (0040,0250) | Performed Procedure Step End Date | OBR-8 |

---

## Examples

### ADT^A04 - Patient Registration

```
MSH|^~\&|HIS|HOSPITAL|PACS_BRIDGE|PACS|20251210120000||ADT^A04^ADT_A01|MSG001|P|2.5|||AL|NE
EVN|A04|20251210120000
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^MIDDLE^^MR||19800115|M|||123 MAIN STREET^^ANYTOWN^CA^90210^USA||555-123-4567||S|CAT|12345678
PV1|1|O|RAD^WAITING^1|E|||1234^SMITH^JAMES^^^DR|||RAD||||1||V|1234^SMITH^JAMES^^^DR|OP|VN123456|||||||||||||||||||||20251210120000
```

### ORM^O01 - New Order

```
MSH|^~\&|RIS|HOSPITAL|PACS_BRIDGE|PACS|20251210120500||ORM^O01^ORM_O01|MSG002|P|2.5|||AL|NE
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^MIDDLE^^MR||19800115|M
PV1|1|O|RAD^WAITING
ORC|NW|ORD001|ACC001||SC||||20251210120500|||1234^SMITH^JAMES^^^DR
OBR|1|ORD001|ACC001|CT001^CT HEAD WITHOUT CONTRAST^L|||20251210130000||||||||1234^SMITH^JAMES^^^DR|||||||||||^Headache for 2 weeks
```

### ORU^R01 - Procedure Complete

```
MSH|^~\&|PACS_BRIDGE|PACS|RIS|HOSPITAL|20251210140000||ORU^R01^ORU_R01|MSG003|P|2.5|||AL|NE
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^MIDDLE^^MR||19800115|M
PV1|1|O|RAD^CT^1
ORC|SC|ORD001|ACC001||CM
OBR|1|ORD001|ACC001|CT001^CT HEAD WITHOUT CONTRAST^L||20251210120500|20251210130000|20251210140000|||||||1234^SMITH^JAMES^^^DR||||||||||F
OBX|1|TX|PROCEDURE_STATUS||COMPLETED||||||F
```

### ADT^A40 - Patient Merge

```
MSH|^~\&|HIS|HOSPITAL|PACS_BRIDGE|PACS|20251210150000||ADT^A40^ADT_A39|MSG004|P|2.5|||AL|NE
EVN|A40|20251210150000
PID|1||67890^^^HOSPITAL^MR||DOE^JOHN^MIDDLE^^MR||19800115|M
MRG|12345^^^HOSPITAL^MR||12345678|||DOE^JOHN^M^^MR
```

### ACK - Acknowledgment

```
MSH|^~\&|PACS_BRIDGE|PACS|HIS|HOSPITAL|20251210120001||ACK^A04^ACK|ACK001|P|2.5|||AL|NE
MSA|AA|MSG001
```

**ACK Codes:**

| Code | Meaning | Description |
|------|---------|-------------|
| AA | Application Accept | Message processed successfully |
| AE | Application Error | Error in message content |
| AR | Application Reject | Message rejected |

---

## Related Documentation

- [Configuration Guide](../user-guide/configuration.md) - HL7 configuration options
- [Message Flows](../user-guide/message-flows.md) - Message flow diagrams
- [Error Codes](error-codes.md) - Error handling and codes
