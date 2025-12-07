# HL7 Segment Structures for Radiology

## Overview

This document details the HL7 v2.5.1 segment structures most relevant to radiology integration.

## 1. MSH - Message Header Segment

The MSH segment defines message metadata and must always be the first segment.

### Structure

| Field | Name | Length | Required | Description |
|-------|------|--------|----------|-------------|
| MSH-1 | Field Separator | 1 | R | Always `\|` |
| MSH-2 | Encoding Characters | 4 | R | Usually `^~\&` |
| MSH-3 | Sending Application | 227 | O | Source system name |
| MSH-4 | Sending Facility | 227 | O | Source facility |
| MSH-5 | Receiving Application | 227 | O | Destination system |
| MSH-6 | Receiving Facility | 227 | O | Destination facility |
| MSH-7 | Date/Time of Message | 26 | R | YYYYMMDDHHMMSS format |
| MSH-8 | Security | 40 | O | Security code |
| MSH-9 | Message Type | 15 | R | Type^Trigger^Structure |
| MSH-10 | Message Control ID | 199 | R | Unique message identifier |
| MSH-11 | Processing ID | 3 | R | P=Production, D=Debug, T=Training |
| MSH-12 | Version ID | 60 | R | HL7 version (e.g., 2.5.1) |

### Example

```
MSH|^~\&|RIS|HOSPITAL|PACS|RADIOLOGY|20231115143052||ORM^O01^ORM_O01|MSG123456|P|2.5.1|||AL|NE|USA|ASCII|EN
```

---

## 2. PID - Patient Identification Segment

The PID segment contains patient demographic information.

### Structure

| Field | Name | Length | Required | Description |
|-------|------|--------|----------|-------------|
| PID-1 | Set ID | 4 | O | Sequence number |
| PID-2 | Patient ID (External) | 20 | O | External ID (deprecated) |
| PID-3 | Patient Identifier List | 250 | R | **Primary patient ID** |
| PID-4 | Alternate Patient ID | 20 | O | (deprecated) |
| PID-5 | Patient Name | 250 | R | **XPN format** |
| PID-6 | Mother's Maiden Name | 250 | O | |
| PID-7 | Date/Time of Birth | 26 | O | YYYYMMDD |
| PID-8 | Administrative Sex | 1 | O | M, F, O, U, A, N |
| PID-9 | Patient Alias | 250 | O | Other names |
| PID-10 | Race | 250 | O | |
| PID-11 | Patient Address | 250 | O | XAD format |
| PID-12 | County Code | 4 | O | |
| PID-13 | Phone Number - Home | 250 | O | XTN format |
| PID-14 | Phone Number - Business | 250 | O | |
| PID-15 | Primary Language | 250 | O | |
| PID-16 | Marital Status | 250 | O | |
| PID-17 | Religion | 250 | O | |
| PID-18 | Patient Account Number | 250 | O | |
| PID-19 | SSN Number | 16 | O | (deprecated) |

### PID-3 Patient Identifier (CX Data Type)

```
ID Number^Check Digit^Code^Assigning Authority^ID Type^Assigning Facility
12345^^^HOSPITAL^MR
```

**Common ID Type Codes**:
| Code | Description |
|------|-------------|
| MR | Medical Record Number |
| PI | Patient Internal ID |
| SS | Social Security Number |
| AN | Account Number |

### PID-5 Patient Name (XPN Data Type)

```
FamilyName^GivenName^MiddleName^Suffix^Prefix^Degree
DOE^JOHN^ANDREW^^MR^MD
```

### Example

```
PID|1||12345^^^HOSPITAL^MR~9876543210^^^SSA^SS||DOE^JOHN^ANDREW^^MR||19800115|M|||123 MAIN ST^^CITY^ST^12345^USA^^H||555-123-4567^PRN^PH|||M|||987-65-4321
```

---

## 3. PV1 - Patient Visit Segment

The PV1 segment contains visit/encounter information.

### Key Fields

| Field | Name | Description |
|-------|------|-------------|
| PV1-1 | Set ID | Sequence number |
| PV1-2 | Patient Class | I=Inpatient, O=Outpatient, E=Emergency |
| PV1-3 | Assigned Patient Location | Point of care^Room^Bed |
| PV1-7 | Attending Doctor | XCN format |
| PV1-8 | Referring Doctor | XCN format |
| PV1-10 | Hospital Service | Department code |
| PV1-19 | Visit Number | Encounter/visit ID |
| PV1-44 | Admit Date/Time | YYYYMMDDHHMMSS |

### Example

```
PV1|1|O|RAD^CT^01^HOSPITAL||||1234^SMITH^ROBERT^J^MD^DR|||RAD||||||||V12345^^^HOSPITAL^VN|||||||||||||||||||||||||20231115103000
```

---

## 4. ORC - Common Order Segment

The ORC segment contains order control information.

### Structure

| Field | Name | Length | Required | Description |
|-------|------|--------|----------|-------------|
| ORC-1 | Order Control | 2 | R | **NW, CA, XO, SC, etc.** |
| ORC-2 | Placer Order Number | 427 | C | Order ID from ordering system |
| ORC-3 | Filler Order Number | 427 | C | Order ID from filling system |
| ORC-4 | Placer Group Number | 427 | O | Group ID |
| ORC-5 | Order Status | 2 | O | **SC, IP, CM, CA, HD** |
| ORC-7 | Quantity/Timing | 705 | O | (deprecated in 2.5+) |
| ORC-9 | Date/Time of Transaction | 26 | O | When order was placed |
| ORC-10 | Entered By | 3220 | O | Person who entered order |
| ORC-12 | Ordering Provider | 3220 | O | **Ordering physician** |
| ORC-13 | Enterer's Location | 80 | O | Location of order entry |
| ORC-14 | Call Back Phone Number | 250 | O | |
| ORC-15 | Order Effective Date/Time | 26 | O | When order becomes effective |
| ORC-16 | Order Control Code Reason | 250 | O | Reason for control code |
| ORC-17 | Entering Organization | 250 | O | |
| ORC-21 | Ordering Facility Name | 250 | O | |

### Order Control Codes (ORC-1)

| Code | Description | Typical Use |
|------|-------------|-------------|
| NW | New Order | Create new order |
| OK | Order Accepted | Acknowledge receipt |
| UA | Unable to Accept | Reject order |
| CA | Cancel Order Request | Request cancellation |
| OC | Order Cancelled | Confirm cancellation |
| CR | Cancelled as Requested | Response to CA |
| DC | Discontinue Order | Stop order |
| XO | Change Order | Modify order |
| XR | Changed as Requested | Response to XO |
| SC | Status Changed | Status update |
| SN | Send Order Number | |
| RE | Observations/Results | For ORU messages |

### Example

```
ORC|NW|ORD001^RIS|ACC001^PACS||SC|||1^ONCE^^^^S||20231115130000|CLERK^JANE|1234^SMITH^ROBERT^J^MD|||20231115130000||||||HOSPITAL
```

---

## 5. OBR - Observation Request Segment

The OBR segment contains details about the ordered procedure.

### Structure

| Field | Name | Length | Required | Description |
|-------|------|--------|----------|-------------|
| OBR-1 | Set ID | 4 | O | Sequence number |
| OBR-2 | Placer Order Number | 427 | C | **Same as ORC-2** |
| OBR-3 | Filler Order Number | 427 | C | **Same as ORC-3** |
| OBR-4 | Universal Service Identifier | 250 | R | **Procedure code** |
| OBR-5 | Priority | 2 | O | S=Stat, R=Routine |
| OBR-6 | Requested Date/Time | 26 | O | When order was placed |
| OBR-7 | Observation Date/Time | 26 | C | **Scheduled/performed time** |
| OBR-8 | Observation End Date/Time | 26 | O | End time |
| OBR-10 | Collector Identifier | 3220 | O | Technologist |
| OBR-13 | Relevant Clinical Info | 300 | O | Clinical history |
| OBR-14 | Specimen Received Date/Time | 26 | O | |
| OBR-15 | Specimen Source | 300 | O | |
| OBR-16 | Ordering Provider | 3220 | O | |
| OBR-18 | Placer Field 1 | 60 | O | Custom field |
| OBR-19 | Placer Field 2 | 60 | O | Custom field |
| OBR-20 | Filler Field 1 | 60 | O | Custom field |
| OBR-21 | Filler Field 2 | 60 | O | Custom field |
| OBR-22 | Results Rpt/Status Change | 26 | C | Time of report |
| OBR-24 | Diagnostic Service Sect ID | 10 | O | **Modality: CT, MR, US** |
| OBR-25 | Result Status | 1 | C | **P, F, C, X, A** |
| OBR-27 | Quantity/Timing | 705 | O | |
| OBR-31 | Reason for Study | 250 | O | **ICD codes** |
| OBR-32 | Principal Result Interpreter | 200 | O | **Radiologist** |
| OBR-44 | Procedure Code | 250 | O | CPT code |

### OBR-4 Universal Service Identifier (CE Data Type)

```
Identifier^Text^Coding System^Alt ID^Alt Text^Alt Coding System
CT001^CT CHEST W/O CONTRAST^LOCAL
71260^CT Thorax wo contrast^CPT
```

### Result Status (OBR-25)

| Code | Description |
|------|-------------|
| O | Order received |
| I | Specimen in lab |
| S | Scheduled |
| A | Some results available |
| P | Preliminary |
| C | Corrected |
| R | Results stored |
| F | Final |
| X | Cancelled |

### Example

```
OBR|1|ORD001^RIS|ACC001^PACS|71260^CT CHEST W/O CONTRAST^CPT|||20231115140000|20231115143000||TECH001^TECHNOLOGIST^JOHN|||CHEST PAIN||20231115140000||1234^SMITH^ROBERT^J^MD||ACC001||||CT|F|||||||5678^JONES^MARY^L^MD~9999^RESIDENT^JAMES||71260^CT Thorax^CPT
```

---

## 6. OBX - Observation/Result Segment

The OBX segment contains individual observations or report text.

### Structure

| Field | Name | Length | Required | Description |
|-------|------|--------|----------|-------------|
| OBX-1 | Set ID | 4 | O | Sequence number |
| OBX-2 | Value Type | 3 | C | **TX, FT, ED, CE** |
| OBX-3 | Observation Identifier | 250 | R | **LOINC code** |
| OBX-4 | Observation Sub-ID | 20 | C | Sub-identifier |
| OBX-5 | Observation Value | 65536 | C | **Report text** |
| OBX-6 | Units | 250 | O | Units of measure |
| OBX-7 | Reference Range | 60 | O | Normal range |
| OBX-8 | Abnormal Flags | 5 | O | H, L, A, etc. |
| OBX-11 | Observation Result Status | 1 | R | **F, P, C** |
| OBX-14 | Date/Time of Observation | 26 | O | When observed |

### Value Types (OBX-2)

| Code | Description | Use Case |
|------|-------------|----------|
| TX | Text | Narrative report sections |
| FT | Formatted Text | Report with formatting |
| ED | Encapsulated Data | PDF, images |
| CE | Coded Entry | Structured findings |
| NM | Numeric | Measurements |
| ST | String | Short text |

### Common LOINC Codes for Radiology (OBX-3)

| LOINC | Description |
|-------|-------------|
| 18782-3 | Radiology Study Observation |
| 59776-5 | Procedure Findings |
| 19005-8 | Impression |
| 18834-2 | Recommendation |
| 29252-4 | CT Findings |
| 29253-2 | MR Findings |

### Example (Radiology Report)

```
OBX|1|TX|18782-3^RADIOLOGY STUDY OBSERVATION^LN||TECHNIQUE: Non-contrast CT of the chest was performed.||||||F|||20231115150000
OBX|2|TX|59776-5^FINDINGS^LN||LUNGS: Clear bilaterally. No infiltrates, masses, or nodules identified.~HEART: Normal size and configuration.~MEDIASTINUM: No lymphadenopathy.||||||F|||20231115150000
OBX|3|TX|19005-8^IMPRESSION^LN||1. No acute cardiopulmonary abnormality.~2. Normal CT chest examination.||||||F|||20231115150000
OBX|4|TX|18834-2^RECOMMENDATION^LN||No imaging follow-up required.||||||F|||20231115150000
```

---

## 7. ZDS - Custom DICOM Segment (Z-Segment)

Z-segments are custom segments for site-specific data. ZDS is commonly used for DICOM UIDs.

### Common ZDS Structure

| Field | Name | Description |
|-------|------|-------------|
| ZDS-1 | Study Instance UID | Pre-assigned DICOM UID |

### Example

```
ZDS|1.2.840.113619.2.55.3.604688119.929.1234567890.1^STUDY_UID
```

---

## 8. Segment Relationships in Radiology Messages

### ORM Order Message Structure

```
MSH                         Message Header
[PID]                       Patient Identification
[PV1]                       Patient Visit
{                           --- ORDER GROUP (repeat) ---
  ORC                       Common Order
  [TQ1]                     Timing/Quantity
  OBR                       Observation Request
  [{NTE}]                   Notes and Comments
  [{DG1}]                   Diagnosis
  [{OBX}]                   Observation/Result
}
[ZDS]                       Custom DICOM UIDs
```

### ORU Result Message Structure

```
MSH                         Message Header
[PID]                       Patient Identification
[PV1]                       Patient Visit
{                           --- ORDER GROUP (repeat) ---
  [ORC]                     Common Order
  OBR                       Observation Request
  [{NTE}]                   Notes and Comments
  {                         --- OBSERVATION GROUP ---
    OBX                     Observation/Result
    [{NTE}]                 Notes for Observation
  }
}
```

---

## References

- [HL7 v2.5.1 Chapter 3 - Patient Administration](https://www.hl7.eu/HL7v2x/v251/std251/ch03.html)
- [HL7 v2.5.1 Chapter 4 - Order Entry](https://www.hl7.eu/HL7v2x/v251/std251/ch04.html)
- [HL7 v2.5.1 Chapter 7 - Observation Reporting](https://www.vico.org/HL7_V2_5/v251/std251/ch07.html)
- [OBR Segment Reference](https://usnistgov.github.io/v2plusDemo/segment-definition/OBR.html)
- [ORC Segment Best Practices](https://www.tactionsoft.com/hl7-orc-segment-structure-standards-and-best-practices/)
