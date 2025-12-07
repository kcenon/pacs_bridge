# Modality Worklist and HL7 ORM Integration

## Overview

This document details the integration between DICOM Modality Worklist (MWL) and HL7 Order Management (ORM) messages. This is the core functionality of the PACS Bridge for RIS/HIS integration.

## Integration Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        MWL-HL7 Integration Flow                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   RIS/HIS                    PACS Bridge                    Modality     │
│      │                           │                             │         │
│      │  ORM^O01 (New Order)      │                             │         │
│      │──────────────────────────►│                             │         │
│      │                           │  Parse ORM                  │         │
│      │                           │  Extract MWL data           │         │
│      │                           │  Store in MWL DB            │         │
│      │  ACK                      │                             │         │
│      │◄──────────────────────────│                             │         │
│      │                           │                             │         │
│      │                           │◄─── C-FIND MWL Query ───────│         │
│      │                           │                             │         │
│      │                           │  Query MWL DB               │         │
│      │                           │  Build C-FIND Response      │         │
│      │                           │                             │         │
│      │                           │──── C-FIND Response ───────►│         │
│      │                           │                             │         │
│      │                           │                    [Patient │         │
│      │                           │                     selects │         │
│      │                           │                     exam]   │         │
│      │                           │                             │         │
│      │                           │◄─── MPPS N-CREATE ──────────│         │
│      │                           │     (IN PROGRESS)           │         │
│      │                           │                             │         │
│      │  ORM^O01 (Status Update)  │                             │         │
│      │◄──────────────────────────│                             │         │
│      │  ORC-5 = IP              │                             │         │
│      │                           │                             │         │
│      │                           │◄─── MPPS N-SET ─────────────│         │
│      │                           │     (COMPLETED)             │         │
│      │                           │                             │         │
│      │  ORM^O01 (Completed)      │                             │         │
│      │◄──────────────────────────│                             │         │
│      │  ORC-5 = CM              │                             │         │
│      │                           │                             │         │
└─────────────────────────────────────────────────────────────────────────┘
```

## ORM to MWL Conversion

### ORM^O01 Message Processing

When an ORM^O01 message is received, the PACS Bridge:

1. **Validates** the message structure
2. **Extracts** patient and order information
3. **Creates/Updates** the MWL entry
4. **Sends** ACK response

### Sample ORM^O01 (New Order)

```
MSH|^~\&|RIS|HOSPITAL|PACS|RADIOLOGY|20231115130000||ORM^O01^ORM_O01|MSG00001|P|2.5.1|||AL|NE
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^ANDREW||19800115|M|||123 MAIN ST^^CITY^ST^12345||555-123-4567
PV1|1|O|RAD^WAITING^01||||1234^SMITH^ROBERT^J^MD|||RAD||||||||V00001
ORC|NW|ORD001^RIS|ACC001^PACS||SC|||1|||20231115130000|CLERK^JANE|1234^SMITH^ROBERT^J^MD
TQ1|1||||20231115140000||R
OBR|1|ORD001^RIS|ACC001^PACS|71260^CT CHEST W/O CONTRAST^CPT|||20231115140000||||||||1234^SMITH^ROBERT^J^MD||ACC001||||CT|SC|||||||5678^TECH^JOHN
ZDS|1.2.840.113619.2.55.3.604688119.929.1234567890.1
```

### Resulting MWL Entry

```
# Patient Module
(0010,0010) PatientName = "DOE^JOHN^ANDREW"
(0010,0020) PatientID = "12345"
(0010,0021) IssuerOfPatientID = "HOSPITAL"
(0010,0030) PatientBirthDate = "19800115"
(0010,0040) PatientSex = "M"

# Imaging Service Request
(0008,0050) AccessionNumber = "ACC001"
(0008,0090) ReferringPhysicianName = "SMITH^ROBERT^J"
(0020,000D) StudyInstanceUID = "1.2.840.113619.2.55.3.604688119.929.1234567890.1"
(0032,1032) RequestingPhysician = "SMITH^ROBERT^J"
(0032,1060) RequestedProcedureDescription = "CT CHEST W/O CONTRAST"
(0040,1001) RequestedProcedureID = "ACC001"
(0040,2016) PlacerOrderNumberImagingServiceRequest = "ORD001"
(0040,2017) FillerOrderNumberImagingServiceRequest = "ACC001"

# Scheduled Procedure Step Sequence (0040,0100)
> (0008,0060) Modality = "CT"
> (0040,0001) ScheduledStationAETitle = "CT_SCANNER_1"
> (0040,0002) ScheduledProcedureStepStartDate = "20231115"
> (0040,0003) ScheduledProcedureStepStartTime = "140000"
> (0040,0007) ScheduledProcedureStepDescription = "CT CHEST W/O CONTRAST"
> (0040,0008) ScheduledProtocolCodeSequence
>> (0008,0100) CodeValue = "71260"
>> (0008,0102) CodingSchemeDesignator = "CPT"
>> (0008,0104) CodeMeaning = "CT CHEST W/O CONTRAST"
> (0040,0009) ScheduledProcedureStepID = "SPS001"
> (0040,0020) ScheduledProcedureStepStatus = "SCHEDULED"
```

## Order Control Processing

### ORC-1 and ORC-5 Combinations

| ORC-1 | ORC-5 | Action | MWL Status |
|-------|-------|--------|------------|
| NW | SC | Create new MWL entry | SCHEDULED |
| NW | IP | Create, mark in progress | STARTED |
| XO | SC | Update existing entry | SCHEDULED |
| XO | IP | Update, mark in progress | STARTED |
| CA | CA | Cancel/remove entry | (deleted) |
| DC | CA | Discontinue entry | DISCONTINUED |
| SC | IP | Status change to started | STARTED |
| SC | CM | Status change to completed | COMPLETED |

### Processing Logic

```cpp
void process_orc(const hl7_segment& orc, const hl7_segment& obr) {
    std::string order_control = orc.get_field(1);  // ORC-1
    std::string order_status = orc.get_field(5);   // ORC-5
    std::string placer_order = orc.get_field(2);   // ORC-2
    std::string filler_order = orc.get_field(3);   // ORC-3 (Accession)

    if (order_control == "NW") {
        // New Order
        if (order_status == "SC" || order_status.empty()) {
            create_mwl_entry(orc, obr, "SCHEDULED");
        } else if (order_status == "IP") {
            create_mwl_entry(orc, obr, "STARTED");
        }
    }
    else if (order_control == "XO") {
        // Change Order
        update_mwl_entry(filler_order, orc, obr);
    }
    else if (order_control == "CA" || order_control == "DC") {
        // Cancel/Discontinue
        delete_mwl_entry(filler_order);
    }
    else if (order_control == "SC") {
        // Status Change
        update_mwl_status(filler_order, order_status);
    }
}
```

## MWL C-FIND Query Handling

### Query Matching

When a modality sends a C-FIND query, the PACS Bridge:

1. **Receives** the C-FIND request
2. **Extracts** query keys
3. **Queries** the MWL database
4. **Returns** matching entries

### Common Query Keys

| DICOM Tag | Keyword | Matching Type |
|-----------|---------|---------------|
| (0008,0050) | AccessionNumber | Single Value |
| (0008,0060) | Modality | Single Value |
| (0010,0010) | PatientName | Wild Card |
| (0010,0020) | PatientID | Single Value |
| (0040,0001) | ScheduledStationAETitle | Single Value |
| (0040,0002) | ScheduledProcedureStepStartDate | Range |
| (0040,0003) | ScheduledProcedureStepStartTime | Range |
| (0040,0020) | ScheduledProcedureStepStatus | Single Value |

### Query Example

**C-FIND Request** (from modality):
```
(0008,0005) SpecificCharacterSet = "ISO_IR 100"
(0008,0052) QueryRetrieveLevel = "WORKLIST"
(0008,0060) Modality = "CT"
(0010,0010) PatientName = ""           # Return key
(0010,0020) PatientID = ""             # Return key
(0040,0001) ScheduledStationAETitle = "CT_SCANNER_1"
(0040,0002) ScheduledProcedureStepStartDate = "20231115"
(0040,0100) ScheduledProcedureStepSequence
```

**Database Query**:
```sql
SELECT * FROM worklist_items
WHERE modality = 'CT'
  AND station_ae = 'CT_SCANNER_1'
  AND scheduled_date = '20231115'
  AND status = 'SCHEDULED'
ORDER BY scheduled_time;
```

## MPPS to HL7 Notification

### MPPS N-CREATE Processing

When a modality sends MPPS N-CREATE (exam started):

```
# MPPS N-CREATE Content
(0008,0050) AccessionNumber = "ACC001"
(0010,0010) PatientName = "DOE^JOHN^ANDREW"
(0010,0020) PatientID = "12345"
(0040,0241) PerformedStationAETitle = "CT_SCANNER_1"
(0040,0244) PerformedProcedureStepStartDate = "20231115"
(0040,0245) PerformedProcedureStepStartTime = "140523"
(0040,0252) PerformedProcedureStepStatus = "IN PROGRESS"
(0040,0253) PerformedProcedureStepID = "MPPS001"
(0040,0270) ScheduledStepAttributesSequence
> (0008,0050) AccessionNumber = "ACC001"
> (0040,0009) ScheduledProcedureStepID = "SPS001"
```

**Generated HL7 ORM^O01**:

```
MSH|^~\&|PACS|RADIOLOGY|RIS|HOSPITAL|20231115140530||ORM^O01^ORM_O01|MSG00010|P|2.5.1
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^ANDREW
ORC|SC|ORD001^RIS|ACC001^PACS||IP|||||||
OBR|1|ORD001^RIS|ACC001^PACS|71260^CT CHEST^CPT|||20231115140523||||||||||ACC001||||CT|SC
```

### MPPS N-SET Processing (Completed)

When a modality sends MPPS N-SET (exam completed):

```
# MPPS N-SET Content
(0040,0250) PerformedProcedureStepEndDate = "20231115"
(0040,0251) PerformedProcedureStepEndTime = "141532"
(0040,0252) PerformedProcedureStepStatus = "COMPLETED"
(0040,0340) PerformedSeriesSequence
> (0020,000E) SeriesInstanceUID = "1.2.3.4.5.6.7.8.9"
> (0008,0060) Modality = "CT"
> (0008,103E) SeriesDescription = "Axial Images"
```

**Generated HL7 ORM^O01**:

```
MSH|^~\&|PACS|RADIOLOGY|RIS|HOSPITAL|20231115141540||ORM^O01^ORM_O01|MSG00011|P|2.5.1
PID|1||12345^^^HOSPITAL^MR||DOE^JOHN^ANDREW
ORC|SC|ORD001^RIS|ACC001^PACS||CM|||||||
OBR|1|ORD001^RIS|ACC001^PACS|71260^CT CHEST^CPT|||20231115140523|20231115141532|||||||||ACC001||||CT|F
```

## Station AE Title Mapping

### Modality to AE Title Lookup

The PACS Bridge maintains a mapping table:

| OBR-24 Modality | Station AE Title | Station Name | Location |
|-----------------|------------------|--------------|----------|
| CT | CT_SCANNER_1 | CT Scanner Room 1 | RAD-CT-01 |
| CT | CT_SCANNER_2 | CT Scanner Room 2 | RAD-CT-02 |
| MR | MR_SCANNER_1 | MR Suite 1 | RAD-MR-01 |
| MR | MR_SCANNER_2 | MR Suite 2 | RAD-MR-02 |
| US | US_ROOM_1 | Ultrasound Room 1 | RAD-US-01 |
| XR | DR_ROOM_1 | Digital X-Ray Room 1 | RAD-XR-01 |
| XR | CR_ROOM_1 | CR Room 1 | RAD-XR-02 |

### Configuration Example

```yaml
# station_mapping.yaml
modality_mapping:
  CT:
    - ae_title: CT_SCANNER_1
      station_name: "CT Scanner Room 1"
      location: "RAD-CT-01"
      default: true
    - ae_title: CT_SCANNER_2
      station_name: "CT Scanner Room 2"
      location: "RAD-CT-02"
  MR:
    - ae_title: MR_SCANNER_1
      station_name: "MR Suite 1"
      location: "RAD-MR-01"
      default: true
  US:
    - ae_title: US_ROOM_1
      station_name: "Ultrasound Room 1"
      location: "RAD-US-01"
      default: true
```

## Error Handling

### ORM Processing Errors

| Error Condition | HL7 ACK | Action |
|-----------------|---------|--------|
| Invalid message format | AE | Reject message |
| Missing required field | AE | Reject with error details |
| Patient not found | AA | Create temporary patient |
| Duplicate order | AA | Update existing if different |
| Database error | AR | Retry or reject |

### ACK Response Examples

**Success (AA)**:
```
MSH|^~\&|PACS|RADIOLOGY|RIS|HOSPITAL|20231115130001||ACK^O01|ACK00001|P|2.5.1
MSA|AA|MSG00001|Message accepted
```

**Error (AE)**:
```
MSH|^~\&|PACS|RADIOLOGY|RIS|HOSPITAL|20231115130001||ACK^O01|ACK00001|P|2.5.1
MSA|AE|MSG00001|Missing required field OBR-4
ERR|||207^Application internal error^HL70357|E|||Required field missing
```

### MWL Query Errors

| Error Condition | DICOM Status | Action |
|-----------------|--------------|--------|
| Query timeout | 0xA702 | Unable to process |
| No matches | 0x0000 | Success with no results |
| Database error | 0xC001 | Processing failure |

## Best Practices

### Performance Optimization

1. **Index** frequently queried fields (AccessionNumber, PatientID, Modality, Date)
2. **Cache** recently accessed MWL entries
3. **Batch** HL7 notifications when possible
4. **Limit** query results (e.g., max 100 entries)

### Data Integrity

1. **Validate** all incoming HL7 messages
2. **Track** message IDs for duplicate detection
3. **Log** all transactions for audit
4. **Sync** patient demographics periodically

### Reliability

1. **Queue** outbound HL7 messages for retry
2. **Monitor** connection status
3. **Alert** on repeated failures
4. **Fallback** to cached data if RIS unavailable

---

## References

- [dcm4chee MWL SCP Configuration](https://dcm4che.atlassian.net/wiki/spaces/ee2/pages/2555955/Modality+Worklist+SCP)
- [dcm4chee ORM Service](https://dcm4che.atlassian.net/wiki/spaces/ee2/pages/2555988/ORM+Service)
- [DICOM Part 4 - K.6 Modality Worklist](https://dicom.nema.org/medical/dicom/current/output/chtml/part04/sect_K.6.html)
- [IHE Radiology Technical Framework Vol. 2](https://www.ihe.net/resources/technical_frameworks/#radiology)
