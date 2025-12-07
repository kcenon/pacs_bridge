# FHIR R4 Radiology Resources

## Overview

FHIR (Fast Healthcare Interoperability Resources) is a modern healthcare data exchange standard developed by HL7. This document covers the FHIR R4 resources most relevant to radiology integration.

## Key Resources for Radiology

### Resource Relationships

```
┌─────────────────┐
│    Patient      │
└────────┬────────┘
         │ subject
         ▼
┌─────────────────┐     basedOn      ┌─────────────────┐
│ ServiceRequest  │◄─────────────────│  ImagingStudy   │
│ (Order)         │                  │  (Images)       │
└────────┬────────┘                  └────────┬────────┘
         │ basedOn                            │ imagingStudy
         ▼                                    ▼
┌─────────────────┐                  ┌─────────────────┐
│DiagnosticReport │◄─────────────────│   Observation   │
│  (Report)       │     derivedFrom  │  (Findings)     │
└─────────────────┘                  └─────────────────┘
```

---

## 1. Patient Resource

The Patient resource contains demographic information.

### Key Elements

```json
{
  "resourceType": "Patient",
  "id": "example-patient",
  "identifier": [
    {
      "use": "usual",
      "type": {
        "coding": [
          {
            "system": "http://terminology.hl7.org/CodeSystem/v2-0203",
            "code": "MR"
          }
        ]
      },
      "system": "http://hospital.example.org/patients",
      "value": "12345"
    }
  ],
  "name": [
    {
      "use": "official",
      "family": "Doe",
      "given": ["John", "Andrew"]
    }
  ],
  "gender": "male",
  "birthDate": "1980-01-15",
  "address": [
    {
      "use": "home",
      "line": ["123 Main St"],
      "city": "City",
      "state": "ST",
      "postalCode": "12345",
      "country": "USA"
    }
  ],
  "telecom": [
    {
      "system": "phone",
      "value": "555-123-4567",
      "use": "home"
    }
  ]
}
```

### Mapping to DICOM

| FHIR Element | DICOM Tag | DICOM Keyword |
|--------------|-----------|---------------|
| identifier[MR].value | (0010,0020) | PatientID |
| name.family + given | (0010,0010) | PatientName |
| birthDate | (0010,0030) | PatientBirthDate |
| gender | (0010,0040) | PatientSex |

---

## 2. ServiceRequest Resource

ServiceRequest represents the imaging order/request.

### Key Elements

```json
{
  "resourceType": "ServiceRequest",
  "id": "imaging-order-1",
  "identifier": [
    {
      "type": {
        "coding": [
          {
            "system": "http://terminology.hl7.org/CodeSystem/v2-0203",
            "code": "PLAC"
          }
        ]
      },
      "value": "ORD001"
    },
    {
      "type": {
        "coding": [
          {
            "system": "http://terminology.hl7.org/CodeSystem/v2-0203",
            "code": "ACSN"
          }
        ]
      },
      "value": "ACC001"
    }
  ],
  "status": "active",
  "intent": "order",
  "category": [
    {
      "coding": [
        {
          "system": "http://snomed.info/sct",
          "code": "363679005",
          "display": "Imaging"
        }
      ]
    }
  ],
  "code": {
    "coding": [
      {
        "system": "http://loinc.org",
        "code": "24627-2",
        "display": "CT Chest"
      },
      {
        "system": "http://www.ama-assn.org/go/cpt",
        "code": "71260",
        "display": "CT thorax w/o contrast"
      }
    ]
  },
  "subject": {
    "reference": "Patient/example-patient"
  },
  "encounter": {
    "reference": "Encounter/example-encounter"
  },
  "occurrenceDateTime": "2023-11-15T14:00:00Z",
  "requester": {
    "reference": "Practitioner/ordering-physician"
  },
  "reasonCode": [
    {
      "coding": [
        {
          "system": "http://hl7.org/fhir/sid/icd-10-cm",
          "code": "R07.9",
          "display": "Chest pain, unspecified"
        }
      ]
    }
  ],
  "priority": "routine"
}
```

### Status Values

| Status | Description | Workflow Stage |
|--------|-------------|----------------|
| draft | Order being prepared | Pre-submission |
| active | Order submitted | Pending scheduling |
| on-hold | Order paused | Held |
| completed | Order fulfilled | Exam done |
| cancelled | Order cancelled | Terminated |
| entered-in-error | Erroneous entry | Invalid |

### Mapping to DICOM MWL

| FHIR Element | DICOM Tag | DICOM Keyword |
|--------------|-----------|---------------|
| identifier[PLAC] | (0040,2016) | PlacerOrderNumberIS |
| identifier[ACSN] | (0008,0050) | AccessionNumber |
| code.coding | (0008,1032) | RequestedProcedureCodeSequence |
| subject | (0010,0020) | PatientID |
| occurrenceDateTime | (0040,0002/0003) | ScheduledProcedureStepStartDate/Time |
| requester | (0008,0090) | ReferringPhysicianName |
| reasonCode | (0040,1002) | ReasonForRequestedProcedure |

---

## 3. ImagingStudy Resource

ImagingStudy represents a DICOM imaging study.

### Key Elements

```json
{
  "resourceType": "ImagingStudy",
  "id": "imaging-study-1",
  "identifier": [
    {
      "system": "urn:dicom:uid",
      "value": "urn:oid:1.2.840.113619.2.55.3.604688119.929.1234567890.1"
    }
  ],
  "status": "available",
  "subject": {
    "reference": "Patient/example-patient"
  },
  "started": "2023-11-15T14:05:00Z",
  "basedOn": [
    {
      "reference": "ServiceRequest/imaging-order-1"
    }
  ],
  "referrer": {
    "reference": "Practitioner/ordering-physician"
  },
  "endpoint": [
    {
      "reference": "Endpoint/dicom-wado"
    }
  ],
  "numberOfSeries": 2,
  "numberOfInstances": 150,
  "procedureCode": [
    {
      "coding": [
        {
          "system": "http://loinc.org",
          "code": "24627-2",
          "display": "CT Chest"
        }
      ]
    }
  ],
  "modality": [
    {
      "system": "http://dicom.nema.org/resources/ontology/DCM",
      "code": "CT"
    }
  ],
  "description": "CT Chest without contrast",
  "series": [
    {
      "uid": "1.2.840.113619.2.55.3.604688119.929.1234567890.2",
      "number": 1,
      "modality": {
        "system": "http://dicom.nema.org/resources/ontology/DCM",
        "code": "CT"
      },
      "description": "Axial Images",
      "numberOfInstances": 100,
      "bodySite": {
        "system": "http://snomed.info/sct",
        "code": "51185008",
        "display": "Thorax"
      },
      "instance": [
        {
          "uid": "1.2.840.113619.2.55.3.604688119.929.1234567890.3",
          "sopClass": {
            "system": "urn:ietf:rfc:3986",
            "code": "urn:oid:1.2.840.10008.5.1.4.1.1.2"
          },
          "number": 1
        }
      ]
    }
  ]
}
```

### Status Values

| Status | Description |
|--------|-------------|
| registered | Study record created, no images |
| available | Images available for viewing |
| cancelled | Study cancelled |
| entered-in-error | Erroneous entry |
| unknown | Status unknown |

### Mapping to DICOM

| FHIR Element | DICOM Tag | DICOM Keyword |
|--------------|-----------|---------------|
| identifier.value | (0020,000D) | StudyInstanceUID |
| started | (0008,0020/0030) | StudyDate/Time |
| numberOfSeries | (0020,1206) | NumberOfStudyRelatedSeries |
| numberOfInstances | (0020,1208) | NumberOfStudyRelatedInstances |
| modality | (0008,0061) | ModalitiesInStudy |
| series.uid | (0020,000E) | SeriesInstanceUID |
| series.modality | (0008,0060) | Modality |
| instance.uid | (0008,0018) | SOPInstanceUID |
| instance.sopClass | (0008,0016) | SOPClassUID |

---

## 4. DiagnosticReport Resource

DiagnosticReport contains the radiology report.

### Key Elements

```json
{
  "resourceType": "DiagnosticReport",
  "id": "radiology-report-1",
  "identifier": [
    {
      "system": "http://hospital.example.org/reports",
      "value": "RPT001"
    }
  ],
  "basedOn": [
    {
      "reference": "ServiceRequest/imaging-order-1"
    }
  ],
  "status": "final",
  "category": [
    {
      "coding": [
        {
          "system": "http://terminology.hl7.org/CodeSystem/v2-0074",
          "code": "RAD",
          "display": "Radiology"
        }
      ]
    }
  ],
  "code": {
    "coding": [
      {
        "system": "http://loinc.org",
        "code": "24627-2",
        "display": "CT Chest"
      }
    ]
  },
  "subject": {
    "reference": "Patient/example-patient"
  },
  "effectiveDateTime": "2023-11-15T15:00:00Z",
  "issued": "2023-11-15T16:00:00Z",
  "performer": [
    {
      "reference": "Practitioner/radiologist"
    }
  ],
  "imagingStudy": [
    {
      "reference": "ImagingStudy/imaging-study-1"
    }
  ],
  "conclusion": "No acute cardiopulmonary abnormality.",
  "conclusionCode": [
    {
      "coding": [
        {
          "system": "http://snomed.info/sct",
          "code": "17621005",
          "display": "Normal"
        }
      ]
    }
  ],
  "presentedForm": [
    {
      "contentType": "text/html",
      "data": "PGh0bWw+Li4uPC9odG1sPg=="
    }
  ]
}
```

### Status Values

| Status | Description | Equivalent |
|--------|-------------|------------|
| registered | Report registered | - |
| partial | Partial results | Preliminary |
| preliminary | Preliminary report | P |
| final | Final report | F |
| amended | Amended report | A |
| corrected | Corrected report | C |
| cancelled | Report cancelled | X |
| entered-in-error | Erroneous entry | - |

---

## 5. Task Resource (Worklist Management)

Task can represent workflow items like worklist entries.

### Key Elements

```json
{
  "resourceType": "Task",
  "id": "mwl-task-1",
  "identifier": [
    {
      "system": "http://hospital.example.org/tasks",
      "value": "SPS001"
    }
  ],
  "status": "requested",
  "intent": "order",
  "code": {
    "coding": [
      {
        "system": "http://loinc.org",
        "code": "24627-2",
        "display": "CT Chest"
      }
    ]
  },
  "focus": {
    "reference": "ServiceRequest/imaging-order-1"
  },
  "for": {
    "reference": "Patient/example-patient"
  },
  "executionPeriod": {
    "start": "2023-11-15T14:00:00Z"
  },
  "owner": {
    "reference": "Device/ct-scanner-1"
  },
  "location": {
    "reference": "Location/radiology-ct-1"
  }
}
```

### Task Status for Radiology Workflow

| Status | Description | DICOM MPPS Equivalent |
|--------|-------------|----------------------|
| requested | Scheduled | - |
| received | Acknowledged | - |
| accepted | Ready to perform | - |
| in-progress | Exam started | IN PROGRESS |
| completed | Exam completed | COMPLETED |
| cancelled | Cancelled | DISCONTINUED |

---

## 6. FHIR API Operations

### RESTful Endpoints

| Operation | HTTP Method | Endpoint | Description |
|-----------|-------------|----------|-------------|
| Read | GET | /Patient/{id} | Get patient by ID |
| Search | GET | /Patient?name=Doe | Search patients |
| Create | POST | /ServiceRequest | Create order |
| Update | PUT | /ServiceRequest/{id} | Update order |
| Patch | PATCH | /Task/{id} | Partial update |
| Delete | DELETE | /ImagingStudy/{id} | Delete study |

### Common Search Parameters

```http
# Search for imaging studies for a patient
GET /ImagingStudy?subject=Patient/12345&status=available

# Search for pending orders
GET /ServiceRequest?status=active&category=imaging

# Search for final reports
GET /DiagnosticReport?status=final&issued=ge2023-11-01
```

### Subscription for Events

```json
{
  "resourceType": "Subscription",
  "status": "active",
  "criteria": "DiagnosticReport?status=final",
  "channel": {
    "type": "rest-hook",
    "endpoint": "https://pacs.example.org/webhook/report",
    "payload": "application/fhir+json"
  }
}
```

---

## 7. FHIR-DICOM Integration Patterns

### Worklist Query Flow

```
RIS (FHIR)                    PACS Bridge                    Modality
    │                              │                              │
    │  GET /Task?status=requested  │                              │
    │◄─────────────────────────────│                              │
    │                              │                              │
    │  Bundle of Task resources    │                              │
    │─────────────────────────────►│                              │
    │                              │  Convert to DICOM MWL        │
    │                              │                              │
    │                              │  C-FIND Response             │
    │                              │─────────────────────────────►│
```

### Study Notification Flow

```
Modality                     PACS Bridge                    EMR (FHIR)
    │                              │                              │
    │  C-STORE (images)            │                              │
    │─────────────────────────────►│                              │
    │                              │  Create ImagingStudy         │
    │                              │                              │
    │                              │  POST /ImagingStudy          │
    │                              │─────────────────────────────►│
    │                              │                              │
    │                              │  201 Created                 │
    │                              │◄─────────────────────────────│
```

---

## References

- [FHIR R4 Specification](https://hl7.org/fhir/R4/)
- [ImagingStudy Resource](https://hl7.org/fhir/R4/imagingstudy.html)
- [ServiceRequest Resource](https://hl7.org/fhir/R4/servicerequest.html)
- [DiagnosticReport Resource](https://hl7.org/fhir/R4/diagnosticreport.html)
- [Task Resource](https://hl7.org/fhir/R4/task.html)
- [DICOM SR to FHIR Mapping](https://confluence.hl7.org/display/IMIN/Mapping+of+DICOM+SR+to+FHIR)
