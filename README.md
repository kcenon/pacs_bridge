# PACS Bridge

[![CI](https://github.com/kcenon/pacs_bridge/actions/workflows/ci.yml/badge.svg)](https://github.com/kcenon/pacs_bridge/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)

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
- **IHE SWF Compliant** - Follows IHE Scheduled Workflow integration profile

## Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   HIS / RIS     │     │   PACS Bridge   │     │   PACS System   │
│                 │◄───►│                 │◄───►│                 │
│  HL7 / FHIR     │     │  Gateway Layer  │     │  DICOM Services │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

## Requirements

- C++20 compatible compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.20+
- [pacs_system](https://github.com/kcenon/pacs_system) v0.2.0+
- [kcenon ecosystem](https://github.com/kcenon) dependencies:
  - common_system
  - thread_system
  - logger_system
  - container_system
  - network_system
  - monitoring_system
- Optional: OpenSSL (for TLS support)

## Building

```bash
# Clone the repository
git clone https://github.com/kcenon/pacs_bridge.git
cd pacs_bridge

# Clone dependencies (as siblings)
cd ..
git clone https://github.com/kcenon/common_system.git
git clone https://github.com/kcenon/thread_system.git
git clone https://github.com/kcenon/logger_system.git
git clone https://github.com/kcenon/container_system.git
git clone https://github.com/kcenon/network_system.git
git clone https://github.com/kcenon/monitoring_system.git
git clone https://github.com/kcenon/pacs_system.git

# Build
cd pacs_bridge
mkdir build && cd build
cmake .. -DBRIDGE_BUILD_HL7=ON -DBRIDGE_BUILD_FHIR=OFF
cmake --build .
```

## Configuration

```yaml
# pacs_bridge.yaml
server:
  name: "PACS_BRIDGE"

hl7:
  listener:
    port: 2575
    tls: false
    max_connections: 50

  outbound:
    - name: "RIS"
      host: "ris.hospital.local"
      port: 2576
      retry_count: 3

pacs_system:
  host: "localhost"
  port: 11112
  ae_title: "PACS_BRIDGE"
  called_ae: "PACS_SCP"
```

## Documentation

### Specifications
- [Product Requirements (PRD)](docs/PRD.md) | [한국어](docs/PRD_KO.md)
- [Software Requirements (SRS)](docs/SRS.md) | [한국어](docs/SRS_KO.md)
- [Software Design (SDS)](docs/SDS.md) | [한국어](docs/SDS_KO.md)

### Reference Materials
- [HL7 v2.x Overview](docs/reference_materials/01_hl7_v2x_overview.md)
- [HL7 Message Types](docs/reference_materials/02_hl7_message_types.md)
- [HL7 Segments](docs/reference_materials/03_hl7_segments.md)
- [MLLP Protocol](docs/reference_materials/04_mllp_protocol.md)
- [FHIR Radiology](docs/reference_materials/05_fhir_radiology.md)
- [IHE SWF Profile](docs/reference_materials/06_ihe_swf_profile.md)
- [DICOM-HL7 Mapping](docs/reference_materials/07_dicom_hl7_mapping.md)
- [MWL-HL7 Integration](docs/reference_materials/08_mwl_hl7_integration.md)

### Verification & Validation
- [Verification Report](docs/VERIFICATION_REPORT.md) | [한국어](docs/VERIFICATION_REPORT_KO.md)
- [Validation Report](docs/VALIDATION_REPORT.md) | [한국어](docs/VALIDATION_REPORT_KO.md)

## Development Status

This project is currently in the **documentation and design phase**. Source code implementation will follow the phased approach outlined in the PRD:

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 1 | Core HL7 Gateway & MWL Integration | Planning |
| Phase 2 | MPPS and Bidirectional Flow | Planning |
| Phase 3 | FHIR Gateway and Reporting | Planning |
| Phase 4 | Production Hardening | Planning |

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.

## Related Projects

- [pacs_system](https://github.com/kcenon/pacs_system) - Core PACS implementation
- [common_system](https://github.com/kcenon/common_system) - Common utilities
- [network_system](https://github.com/kcenon/network_system) - Network communication
- [thread_system](https://github.com/kcenon/thread_system) - Thread management
- [logger_system](https://github.com/kcenon/logger_system) - Logging infrastructure
- [container_system](https://github.com/kcenon/container_system) - Data containers
- [monitoring_system](https://github.com/kcenon/monitoring_system) - System monitoring

## Author

- **kcenon** - [GitHub](https://github.com/kcenon) - kcenon@naver.com
