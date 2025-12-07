# HL7 v2.x Overview

## Introduction

HL7 Version 2 (HL7 v2.x) is one of the most widely adopted healthcare messaging standards worldwide. Since its introduction in the 1980s, it has powered countless integrations between healthcare systems including HIS, RIS, LIS, EMR, and PACS.

## Key Characteristics

- **Text-based format**: Pipe-delimited (`|`) message structure
- **Event-driven**: Messages are triggered by specific healthcare events
- **Flexible**: Supports optional segments and custom extensions (Z-segments)
- **Backward compatible**: Newer versions maintain compatibility with older ones

## Version History

| Version | Year | Key Features |
|---------|------|--------------|
| 2.1 | 1990 | Initial widespread adoption |
| 2.2 | 1994 | Enhanced segments |
| 2.3 | 1997 | Improved order entry |
| 2.3.1 | 1999 | Bug fixes and clarifications |
| 2.4 | 2000 | Enhanced clinical content |
| 2.5 | 2003 | Improved conformance |
| 2.5.1 | 2007 | **Recommended for new implementations** |
| 2.6 | 2007 | Additional segments |
| 2.7 | 2011 | Latest version |
| 2.8 | 2014 | Minor updates |
| 2.9 | 2019 | Current latest |

> **Recommendation**: HL7 v2.5.1 is the most widely implemented version and recommended for new radiology integrations.

## Message Structure

### Basic Format

```
MSH|^~\&|SendingApp|SendingFac|ReceivingApp|ReceivingFac|20231115103000||ADT^A04|MSG00001|P|2.5.1
EVN|A04|20231115103000
PID|1||12345^^^Hospital^MR||DOE^JOHN^A||19800115|M|||123 MAIN ST^^CITY^ST^12345
...
```

### Structure Hierarchy

```
Message
├── Segment 1 (MSH - always first)
├── Segment 2 (e.g., EVN, PID)
├── ...
├── Segment Group (repeating)
│   ├── Segment A
│   └── Segment B
└── Segment N
```

### Delimiters

| Character | Name | Purpose | ASCII |
|-----------|------|---------|-------|
| `\|` | Pipe | Field separator | 0x7C |
| `^` | Caret | Component separator | 0x5E |
| `~` | Tilde | Repetition separator | 0x7E |
| `\` | Backslash | Escape character | 0x5C |
| `&` | Ampersand | Subcomponent separator | 0x26 |

> These are defined in MSH-1 (Field Separator) and MSH-2 (Encoding Characters).

## Message Types Relevant to Radiology

### ADT (Admit, Discharge, Transfer)

Patient demographic and visit management:

| Trigger | Description | Use Case |
|---------|-------------|----------|
| A01 | Admit/Visit | Patient admitted |
| A02 | Transfer | Patient transferred |
| A03 | Discharge | Patient discharged |
| A04 | Register | Outpatient registration |
| A08 | Update Info | Demographics changed |
| A40 | Merge | Patient records merged |

### ORM (Order Message)

Radiology order management:

| Trigger | Description | Use Case |
|---------|-------------|----------|
| O01 | Order Message | New order, update, cancel |

### ORU (Observation Result)

Radiology report delivery:

| Trigger | Description | Use Case |
|---------|-------------|----------|
| R01 | Unsolicited Result | Report transmission |

### SIU (Scheduling)

Appointment and scheduling:

| Trigger | Description | Use Case |
|---------|-------------|----------|
| S12 | New Appointment | Exam scheduled |
| S13 | Reschedule | Appointment changed |
| S14 | Modification | Details modified |
| S15 | Cancellation | Appointment cancelled |

## Acknowledgment (ACK)

Every HL7 message should receive an acknowledgment:

```
MSH|^~\&|ReceivingApp|ReceivingFac|SendingApp|SendingFac|20231115103001||ACK^A04|ACK00001|P|2.5.1
MSA|AA|MSG00001
```

### ACK Codes

| Code | Name | Description |
|------|------|-------------|
| AA | Application Accept | Message processed successfully |
| AE | Application Error | Error in message content |
| AR | Application Reject | Message rejected |
| CA | Commit Accept | (Enhanced mode) Commit accepted |
| CE | Commit Error | (Enhanced mode) Commit error |
| CR | Commit Reject | (Enhanced mode) Commit rejected |

## Processing Modes

### Original Mode
- Simple request/response
- Single ACK per message
- Most common in radiology

### Enhanced Mode
- Two-phase commit
- Commit ACK + Application ACK
- Used for critical transactions

## References

- [HL7 International](https://www.hl7.org/)
- [HL7 v2.5.1 Specification](https://www.hl7.org/implement/standards/)
- [HL7 V2 Messages Tutorial](https://healthcareintegrations.com/hl7-v2-messages-explained-adt-orm-and-oru-tutorial/)
- [InterfaceWare HL7 Resources](https://www.interfaceware.com/)
