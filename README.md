# PACS Bridge

HIS/RIS integration bridge for PACS System - providing seamless connectivity between PACS and Hospital/Radiology Information Systems.

## Overview

PACS Bridge enables healthcare facilities to integrate their PACS (Picture Archiving and Communication System) with:
- **HIS** (Hospital Information System) - Patient demographics, ADT events
- **RIS** (Radiology Information System) - Worklist, scheduling, reporting
- **EMR/EHR** (Electronic Medical/Health Records) - Clinical data exchange

## Features

- **HL7 v2.x Gateway** - ADT, ORM, ORU, SIU message support via MLLP
- **FHIR R4/R5 Gateway** - RESTful API integration with modern EHR systems
- **Modality Worklist Bridge** - MWL C-FIND to RIS/HIS query translation
- **MPPS Notification** - Exam status propagation to RIS
- **Flexible Routing** - Multi-gateway support with failover

## Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   HIS / RIS     │     │   PACS Bridge   │     │   PACS System   │
│                 │◄───►│                 │◄───►│                 │
│  HL7 / FHIR     │     │  Gateway Layer  │     │  DICOM Services │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

## Requirements

- C++20 compatible compiler
- CMake 3.20+
- pacs_system v0.2.0+
- Optional: OpenSSL (for TLS support)

## Building

```bash
mkdir build && cd build
cmake .. -DBRIDGE_BUILD_HL7=ON -DBRIDGE_BUILD_FHIR=OFF
cmake --build .
```

## Documentation

- [Reference Materials](docs/reference_materials/) - Standards and specifications
- [Design Documents](docs/design/) - Architecture and design decisions
- [API Documentation](docs/api/) - Public API reference

## License

[To be determined]

## Related Projects

- [pacs_system](../pacs_system/) - Core PACS implementation
