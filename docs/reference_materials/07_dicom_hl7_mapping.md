# DICOM to HL7 Attribute Mapping

## Overview

This document provides comprehensive mapping tables between DICOM attributes and HL7 v2.x fields for radiology integration. These mappings are essential for the PACS Bridge to translate data between DICOM-based systems (modalities, PACS) and HL7-based systems (HIS, RIS).

## Patient Demographics Mapping

### PID Segment to DICOM Patient Module

| HL7 Field | HL7 Name | DICOM Tag | DICOM Keyword | Notes |
|-----------|----------|-----------|---------------|-------|
| PID-3 | Patient Identifier List | (0010,0020) | PatientID | Primary identifier |
| PID-3.4 | Assigning Authority | (0010,0021) | IssuerOfPatientID | |
| PID-5 | Patient Name | (0010,0010) | PatientName | PN format |
| PID-7 | Date/Time of Birth | (0010,0030) | PatientBirthDate | DA format |
| PID-8 | Administrative Sex | (0010,0040) | PatientSex | M/F/O |
| PID-10 | Race | (0010,2160) | EthnicGroup | |
| PID-11 | Patient Address | (0010,1040) | PatientAddress | |
| PID-13 | Phone Number - Home | (0010,2154) | PatientTelephoneNumbers | |
| PID-18 | Patient Account Number | (0010,0050) | PatientInsurancePlanCodeSequence | |
| PID-19 | SSN Number | (0010,0020) | PatientID | Alternative ID |

### Name Format Conversion

**HL7 XPN Format**:
```
FamilyName^GivenName^MiddleName^Suffix^Prefix^Degree
DOE^JOHN^ANDREW^JR^MR^MD
```

**DICOM PN Format**:
```
FamilyName^GivenName^MiddleName^Prefix^Suffix
DOE^JOHN^ANDREW^MR^JR
```

> **Note**: HL7 has Suffix before Prefix; DICOM has Prefix before Suffix.

### Sex Code Mapping

| HL7 Value | Description | DICOM Value |
|-----------|-------------|-------------|
| M | Male | M |
| F | Female | F |
| O | Other | O |
| U | Unknown | (empty) |
| A | Ambiguous | O |
| N | Not Applicable | O |

---

## Order/Request Mapping

### ORC/OBR to DICOM Scheduled Procedure Step

| HL7 Field | HL7 Name | DICOM Tag | DICOM Keyword |
|-----------|----------|-----------|---------------|
| ORC-2 | Placer Order Number | (0040,2016) | PlacerOrderNumberImagingServiceRequest |
| ORC-3 | Filler Order Number | (0008,0050) | AccessionNumber |
| ORC-5 | Order Status | (0040,0020) | ScheduledProcedureStepStatus |
| ORC-9 | Date/Time of Transaction | (0040,0002) | ScheduledProcedureStepStartDate |
| ORC-12 | Ordering Provider | (0008,0090) | ReferringPhysicianName |
| OBR-2 | Placer Order Number | (0040,2016) | PlacerOrderNumberImagingServiceRequest |
| OBR-3 | Filler Order Number | (0040,1001) | RequestedProcedureID |
| OBR-4 | Universal Service ID | (0032,1064) | RequestedProcedureCodeSequence |
| OBR-4 | Universal Service ID | (0040,0008) | ScheduledProtocolCodeSequence |
| OBR-5 | Priority | (0040,1003) | RequestedProcedurePriority |
| OBR-7 | Observation Date/Time | (0040,0002) | ScheduledProcedureStepStartDate |
| OBR-7 | Observation Date/Time | (0040,0003) | ScheduledProcedureStepStartTime |
| OBR-13 | Relevant Clinical Info | (0040,1002) | ReasonForTheRequestedProcedure |
| OBR-16 | Ordering Provider | (0008,0090) | ReferringPhysicianName |
| OBR-18 | Placer Field 1 | (0040,0009) | ScheduledProcedureStepID |
| OBR-24 | Diagnostic Service ID | (0008,0060) | Modality |
| OBR-31 | Reason for Study | (0040,1002) | ReasonForTheRequestedProcedure |
| OBR-44 | Procedure Code | (0032,1064) | RequestedProcedureCodeSequence |

### Order Status Mapping

| ORC-5 Value | Description | DICOM SPS Status |
|-------------|-------------|------------------|
| SC | Scheduled | SCHEDULED |
| IP | In Progress | STARTED |
| CM | Completed | COMPLETED |
| CA | Cancelled | CANCELLED |
| HD | On Hold | SCHEDULED |
| DC | Discontinued | DISCONTINUED |

### Priority Mapping

| OBR-5 Value | Description | DICOM Priority |
|-------------|-------------|----------------|
| S | Stat | HIGH |
| A | ASAP | HIGH |
| R | Routine | ROUTINE |
| P | Preoperative | HIGH |
| C | Callback | ROUTINE |
| T | Timing Critical | HIGH |

---

## Modality Worklist (MWL) Mapping

### Complete MWL Attribute Mapping

| DICOM Tag | DICOM Keyword | HL7 Source | Notes |
|-----------|---------------|------------|-------|
| **Patient Module** |
| (0010,0010) | PatientName | PID-5 | |
| (0010,0020) | PatientID | PID-3 | |
| (0010,0021) | IssuerOfPatientID | PID-3.4 | |
| (0010,0030) | PatientBirthDate | PID-7 | DA format |
| (0010,0040) | PatientSex | PID-8 | |
| (0010,1000) | OtherPatientIDs | PID-3 (repeating) | |
| (0010,2160) | EthnicGroup | PID-10 | |
| **Visit Module** |
| (0008,0080) | InstitutionName | PV1-3.4 or MSH-4 | |
| (0008,1040) | InstitutionalDepartmentName | PV1-3.1 | |
| (0038,0010) | AdmissionID | PV1-19 | |
| **Imaging Service Request** |
| (0008,0050) | AccessionNumber | ORC-3 or OBR-3 | |
| (0008,0090) | ReferringPhysicianName | ORC-12 or OBR-16 | |
| (0008,1110) | ReferencedStudySequence | - | Pre-assigned |
| (0020,000D) | StudyInstanceUID | ZDS-1 | Pre-assigned or generated |
| (0032,1032) | RequestingPhysician | ORC-12 | |
| (0032,1033) | RequestingService | OBR-24 | |
| (0032,1060) | RequestedProcedureDescription | OBR-4.2 | |
| (0040,1001) | RequestedProcedureID | OBR-3 | |
| (0040,1002) | ReasonForRequestedProcedure | OBR-13 or OBR-31 | |
| (0040,1003) | RequestedProcedurePriority | OBR-5 | |
| (0040,2016) | PlacerOrderNumberIS | ORC-2 or OBR-2 | |
| (0040,2017) | FillerOrderNumberIS | ORC-3 | |
| **Scheduled Procedure Step Sequence (0040,0100)** |
| (0008,0060) | Modality | OBR-24 | |
| (0040,0001) | ScheduledStationAETitle | Lookup table | Based on modality |
| (0040,0002) | ScheduledProcedureStepStartDate | OBR-7 | DA format |
| (0040,0003) | ScheduledProcedureStepStartTime | OBR-7 | TM format |
| (0040,0006) | ScheduledPerformingPhysicianName | OBR-34 | |
| (0040,0007) | ScheduledProcedureStepDescription | OBR-4.2 | |
| (0040,0008) | ScheduledProtocolCodeSequence | OBR-4 | |
| (0040,0009) | ScheduledProcedureStepID | OBR-18 or generated | |
| (0040,0010) | ScheduledStationName | Lookup table | |
| (0040,0011) | ScheduledProcedureStepLocation | OBR-20 | |
| (0040,0012) | PreMedication | OBR-13 | |
| (0040,0020) | ScheduledProcedureStepStatus | ORC-5 | |
| **Requested Procedure Code Sequence (0032,1064)** |
| (0008,0100) | CodeValue | OBR-4.1 | |
| (0008,0102) | CodingSchemeDesignator | OBR-4.3 | |
| (0008,0104) | CodeMeaning | OBR-4.2 | |

---

## MPPS to HL7 ORM Mapping

### MPPS N-CREATE to HL7 ORM

| DICOM Tag | DICOM Keyword | HL7 Field | Notes |
|-----------|---------------|-----------|-------|
| (0008,0050) | AccessionNumber | ORC-3, OBR-3 | |
| (0008,1115) | ReferencedSeriesSequence | - | Not mapped |
| (0010,0010) | PatientName | PID-5 | |
| (0010,0020) | PatientID | PID-3 | |
| (0040,0241) | PerformedStationAETitle | OBR-24 | Via lookup |
| (0040,0242) | PerformedStationName | OBR-20 | |
| (0040,0244) | PerformedProcedureStepStartDate | OBR-7 (part 1) | DA format |
| (0040,0245) | PerformedProcedureStepStartTime | OBR-7 (part 2) | TM format |
| (0040,0252) | PerformedProcedureStepStatus | ORC-1, ORC-5 | IN PROGRESS → SC, IP |
| (0040,0253) | PerformedProcedureStepID | OBR-3 | |
| (0040,0254) | PerformedProcedureStepDescription | OBR-4.2 | |
| (0040,0270) | ScheduledStepAttributesSequence | - | Reference to original order |

### MPPS N-SET to HL7 ORM

| DICOM Tag | DICOM Keyword | HL7 Field | Notes |
|-----------|---------------|-----------|-------|
| (0040,0250) | PerformedProcedureStepEndDate | OBR-8 (part 1) | |
| (0040,0251) | PerformedProcedureStepEndTime | OBR-8 (part 2) | |
| (0040,0252) | PerformedProcedureStepStatus | ORC-1, ORC-5 | See table below |
| (0040,0340) | PerformedSeriesSequence | OBX segments | Optional |

### MPPS Status to HL7 Order Control

| MPPS Status | ORC-1 | ORC-5 | Description |
|-------------|-------|-------|-------------|
| IN PROGRESS | SC | IP | Exam started |
| COMPLETED | SC | CM | Exam completed |
| DISCONTINUED | DC | CA | Exam discontinued |

---

## Report Mapping (ORU)

### HL7 ORU to DICOM SR (Basic)

| HL7 Field | HL7 Name | DICOM Concept | Notes |
|-----------|----------|---------------|-------|
| OBR-4 | Universal Service ID | Procedure Code | |
| OBR-22 | Results Rpt/Status Change | Content Date/Time | |
| OBR-25 | Result Status | Completion Flag | |
| OBR-32 | Principal Result Interpreter | Verifying Observer | |
| OBX-3 | Observation Identifier | Concept Name Code | LOINC |
| OBX-5 | Observation Value | Text Value | |
| OBX-11 | Observation Result Status | Verification Flag | |
| OBX-14 | Date/Time of Observation | Observation DateTime | |

### Common LOINC Codes for Radiology Reports

| LOINC Code | Description | DICOM Concept |
|------------|-------------|---------------|
| 18782-3 | Radiology Study Observation | Study Observation |
| 59776-5 | Procedure Findings | Findings |
| 19005-8 | Radiology Impression | Impression |
| 18834-2 | Radiology Recommendation | Recommendation |
| 24627-2 | CT Chest | Procedure |
| 24628-0 | CT Abdomen | Procedure |
| 30746-2 | MR Head | Procedure |

---

## Date/Time Format Conversion

### HL7 DTM to DICOM DA/TM

**HL7 DTM Format**: `YYYYMMDDHHMMSS.SSSS[+/-ZZZZ]`

**DICOM DA Format**: `YYYYMMDD`

**DICOM TM Format**: `HHMMSS.FFFFFF`

```cpp
// Conversion example
std::pair<std::string, std::string> hl7_to_dicom_datetime(const std::string& hl7_dtm) {
    // Input: "20231115143052.1234+0000"
    // Output: {"20231115", "143052.123400"}

    std::string da = hl7_dtm.substr(0, 8);  // YYYYMMDD
    std::string tm;

    if (hl7_dtm.length() >= 14) {
        tm = hl7_dtm.substr(8, 6);  // HHMMSS

        // Handle fractional seconds
        if (hl7_dtm.length() > 14 && hl7_dtm[14] == '.') {
            size_t frac_end = hl7_dtm.find_first_of("+-", 15);
            if (frac_end == std::string::npos) frac_end = hl7_dtm.length();
            tm += hl7_dtm.substr(14, frac_end - 14);
        }
    }

    return {da, tm};
}
```

### DICOM DA/TM to HL7 DTM

```cpp
std::string dicom_to_hl7_datetime(const std::string& da, const std::string& tm) {
    // Input: da="20231115", tm="143052.123456"
    // Output: "20231115143052.1234"

    std::string dtm = da;

    if (!tm.empty()) {
        // Take HHMMSS
        dtm += tm.substr(0, 6);

        // Handle fractional seconds (HL7 uses 4 digits max)
        if (tm.length() > 6 && tm[6] == '.') {
            dtm += tm.substr(6, 5);  // .SSSS
        }
    }

    return dtm;
}
```

---

## Modality Code Mapping

### OBR-24 to DICOM Modality

| OBR-24 Value | DICOM Modality | Description |
|--------------|----------------|-------------|
| CT | CT | Computed Tomography |
| MR | MR | Magnetic Resonance |
| US | US | Ultrasound |
| XR | CR, DX | Computed/Digital Radiography |
| RF | RF | Radiofluoroscopy |
| NM | NM | Nuclear Medicine |
| PT | PT | PET |
| MG | MG | Mammography |
| XA | XA | X-Ray Angiography |
| ES | ES | Endoscopy |

---

## Code System Mapping

### Common Code Systems

| HL7 Coding System | DICOM Coding Scheme Designator | Description |
|-------------------|--------------------------------|-------------|
| I9C, I9 | ICD9CM | ICD-9-CM Diagnosis |
| I10 | ICD10 | ICD-10 Diagnosis |
| C4 | CPT | CPT-4 Procedure Codes |
| LN | LN | LOINC |
| SNM | SNM3 | SNOMED |
| SCT | SCT | SNOMED CT |
| L | 99LOCAL | Local codes |

### Code Element Mapping

```
HL7 CE: Identifier^Text^CodingSystem^AltId^AltText^AltCodingSystem
        71260^CT CHEST W/O^CPT

DICOM Code Sequence:
  (0008,0100) CodeValue = "71260"
  (0008,0102) CodingSchemeDesignator = "CPT"
  (0008,0104) CodeMeaning = "CT CHEST W/O"
```

---

## References

- [IHE Technical Framework Vol. 2 Appendix B](https://www.ihe.net/resources/technical_frameworks/#radiology)
- [DICOM Part 3 - IOD Definitions](https://dicom.nema.org/medical/dicom/current/output/chtml/part03/PS3.3.html)
- [HL7 v2.5.1 Data Types](https://www.hl7.eu/HL7v2x/v251/std251/ch02.html)
- [dcm4chee HL7 Conformance](https://dcm4chee-arc-hl7cs.readthedocs.io/)
- [DICOM Supplement 101 - HL7 Structured Document References](https://www.dicomstandard.org/News-dir/ftsup/docs/sups/sup101.pdf)
