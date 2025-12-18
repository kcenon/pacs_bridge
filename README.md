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
- **C++20 Concepts** - Type-safe templates with clear compile-time constraints
- **Prometheus Metrics** - Built-in metrics export for monitoring and alerting

## Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   HIS / RIS     │     │   PACS Bridge   │     │   PACS System   │
│                 │◄───►│                 │◄───►│                 │
│  HL7 / FHIR     │     │  Gateway Layer  │     │  DICOM Services │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

## Project Structure

```
pacs_bridge/
├── include/pacs/bridge/
│   ├── hl7/              # HL7 Gateway (convenience header)
│   ├── protocol/hl7/     # HL7 v2.x message handling
│   ├── mllp/             # MLLP transport layer
│   ├── fhir/             # FHIR R4 Gateway (Phase 3)
│   ├── emr/              # EMR client (FHIR R4 client, Phase 5)
│   ├── mapping/          # HL7-DICOM translation
│   ├── router/           # Message routing
│   ├── pacs_adapter/     # PACS system integration
│   ├── config/           # Configuration management
│   ├── cache/            # Patient demographics cache
│   ├── security/         # TLS, audit, OAuth2/Smart-on-FHIR
│   ├── monitoring/       # Health checks, metrics
│   ├── performance/      # Performance utilities
│   ├── testing/          # Test utilities
│   ├── integration/      # System adapters
│   └── concepts/         # C++20 Concepts for type constraints
├── src/                  # Implementation files
├── tests/
│   ├── unit/             # Unit tests
│   └── integration/      # Integration tests
├── examples/             # Configuration examples
├── docs/                 # Documentation
├── cmake/                # CMake modules
└── benchmarks/           # Performance benchmarks
```

## CI/CD Pipeline

The project uses GitHub Actions for continuous integration with multi-platform support:

| Platform | Compiler | Status |
|----------|----------|--------|
| Ubuntu 24.04 | GCC 13+ | ✓ std::format support |
| Ubuntu 24.04 | Clang | ✓ |
| macOS 14 (ARM64) | Apple Clang | ✓ |
| Windows 2022 | MSVC | ✓ vcpkg integration |

### Code Coverage

Code coverage reports are generated on Ubuntu GCC builds using lcov and uploaded to Codecov.
Target coverage: 80% by Phase 5.

```bash
# Build with coverage enabled
cmake -B build -DBRIDGE_BUILD_TESTS=ON -DBRIDGE_ENABLE_COVERAGE=ON
cmake --build build

# Run tests to generate coverage data
ctest --test-dir build

# Generate coverage report
lcov --capture --directory build --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/tests/*' '*/build/_deps/*' --output-file coverage.info
lcov --list coverage.info
```

### Test Framework

Unit tests use [Google Test](https://github.com/google/googletest) (gtest/gmock):
- Automatic test discovery via `gtest_discover_tests()`
- Rich assertion macros and matchers
- Mock support for dependency injection
- Parallel test execution support

### CI Features

- **Dependency Caching**: kcenon ecosystem dependencies are cached for faster builds
- **Parallel Builds**: All platform builds run in parallel
- **Artifact Upload**: Test results and coverage reports are archived

## Requirements

- **C++23 compatible compiler**:
  - GCC 13+ (Linux) - Required for `std::format` support
  - Clang 14+ (macOS)
  - MSVC 2022+ (Windows)
- CMake 3.20+
- Ninja (recommended for faster builds)
- Optional: OpenSSL 1.1+ (for TLS support)
- Optional: [kcenon ecosystem](https://github.com/kcenon) dependencies (for full integration)

## Building

### Quick Start (Standalone Mode)

Build without external dependencies using internal stubs:

```bash
# Clone the repository
git clone https://github.com/kcenon/pacs_bridge.git
cd pacs_bridge

# Build in standalone mode (default)
cmake -B build -DBRIDGE_STANDALONE_BUILD=ON
cmake --build build
```

### Full Build (With kcenon Dependencies)

For production use with full kcenon ecosystem integration:

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

# Build with full integration
cd pacs_bridge
cmake -B build -DBRIDGE_STANDALONE_BUILD=OFF
cmake --build build
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BRIDGE_STANDALONE_BUILD` | `ON` | Build without external kcenon dependencies |
| `BRIDGE_BUILD_PACS_INTEGRATION` | `ON` | Enable pacs_system for DICOM MWL/MPPS (requires full build) |
| `BRIDGE_BUILD_MONITORING` | `ON` | Enable monitoring_system for Prometheus metrics (requires full build) |
| `BRIDGE_BUILD_HL7` | `ON` | Build HL7 gateway module |
| `BRIDGE_BUILD_FHIR` | `OFF` | Build FHIR gateway module (future) |
| `BRIDGE_BUILD_TESTS` | `ON` | Build unit tests (uses Google Test) |
| `BRIDGE_BUILD_EXAMPLES` | `ON` | Build example applications |
| `BRIDGE_BUILD_BENCHMARKS` | `OFF` | Build performance benchmarks |
| `BRIDGE_ENABLE_TLS` | `ON` | Enable TLS support with OpenSSL |
| `BRIDGE_ENABLE_COVERAGE` | `OFF` | Enable code coverage instrumentation (GCC/Clang only) |

### vcpkg Integration

The project uses vcpkg manifest mode (`vcpkg.json`) for managing standard third-party
dependencies (OpenSSL, GTest, fmt, spdlog, etc.). The kcenon ecosystem packages are
managed via CMake FetchContent.

```bash
# With vcpkg (recommended for cross-platform builds)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build

# Or using VCPKG_ROOT environment variable
export VCPKG_ROOT=/path/to/vcpkg
cmake -B build
cmake --build build
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

### User Guides
- [Getting Started](docs/user-guide/getting-started.md) - Quick start guide
- [Configuration Guide](docs/user-guide/configuration.md) - Configuration options
- [Message Flows](docs/user-guide/message-flows.md) - HL7/DICOM message flows
- [Troubleshooting](docs/user-guide/troubleshooting.md) - Common issues and solutions

### Administrator Guides
- [Deployment Guide](docs/admin-guide/deployment.md) - Docker, bare metal, Kubernetes
- [High Availability](docs/admin-guide/high-availability.md) - HA configuration
- [Monitoring Guide](docs/admin-guide/monitoring.md) - Prometheus, Grafana, alerting
- [Backup & Recovery](docs/admin-guide/backup-recovery.md) - Data protection

### API Reference
- [HL7 Messages](docs/api/hl7-messages.md) - HL7 v2.x message specifications
- [FHIR API](docs/api/fhir-api.yaml) - OpenAPI 3.0 specification
- [Error Codes](docs/api/error-codes.md) - Error code reference

### Developer Guides
- [Architecture](docs/developer/architecture.md) - System architecture
- [Modules](docs/developer/modules.md) - Module descriptions
- [Contributing](docs/developer/contributing.md) - Contribution guidelines

### Operations
- [Runbook](docs/operations/runbook.md) - Operational procedures

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

### Monitoring & Metrics
- [Monitoring Guide](docs/monitoring/README.md) - Prometheus metrics, Grafana dashboard, alerting rules

### Verification & Validation
- [Verification Report](docs/VERIFICATION_REPORT.md) | [한국어](docs/VERIFICATION_REPORT_KO.md)
- [Validation Report](docs/VALIDATION_REPORT.md) | [한국어](docs/VALIDATION_REPORT_KO.md)

## Development Status

Source code implementation follows the phased approach outlined in the PRD:

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 1 | Core HL7 Gateway & MWL Integration | **Implemented** |
| Phase 2 | MPPS and Bidirectional Flow | **Implemented** |
| Phase 3 | FHIR Gateway and Reporting | Planning |
| Phase 4 | Production Hardening | Planning |
| Phase 5 | EMR Integration (FHIR R4 Client) | **In Progress** |

### Phase 1 Implementation Status

| Module | Component | Status |
|--------|-----------|--------|
| HL7 Protocol | Message types, parser, builder | Implemented |
| HL7 ADT Handler | A01/A04/A08/A40 event processing | Implemented |
| HL7-DICOM Mapping | ORM to MWL mapper | Implemented |
| Message Routing | Pattern matching, handler chains | Implemented |
| Patient Cache | TTL/LRU cache with aliases | Implemented |
| Configuration | YAML config loader | Implemented |
| MLLP Transport | Client/Server with TLS | Implemented |
| PACS Adapter | MWL Client (pacs_system integration) | Implemented |
| Unit Tests | HL7, ADT, mapping, router, cache, MWL | Implemented |

### Phase 2 Implementation Status

| Module | Component | Status |
|--------|-----------|--------|
| Integration Tests | Test infrastructure | Implemented |
| MPPS Tests | N-CREATE/N-SET flows | Implemented |
| Queue Tests | Message persistence & recovery | Implemented |
| Failover Tests | RIS failover routing | Implemented |
| Stress Tests | High volume load testing | Implemented |
| Performance Tests | Benchmarks, concurrency, throughput | Implemented |
| Memory Safety | Leak detection, RAII compliance | Implemented |
| Monitoring | Prometheus metrics collector | Implemented |
| Monitoring | /metrics HTTP endpoint | Implemented |
| Monitoring | Grafana dashboard & alerts | Implemented |
| MPPS Handler | MPPS event processing (pacs_system) | Implemented |
| HL7 Mapper | MPPS to ORM^O01 conversion | Implemented |
| Message Queue | Outbound queue with SQLite persistence | Implemented |
| Message Router | Failover routing with retry/DLQ | Implemented |
| Bridge Server | Phase 2 workflow orchestration | Implemented |
| E2E Tests | MPPS→HL7→MLLP + recovery tests | Implemented |

### Phase 5 Implementation Status

| Module | Component | Status |
|--------|-----------|--------|
| EMR Client | FHIR R4 HTTP Client | Implemented |
| EMR Client | OAuth2/Smart-on-FHIR Auth | Implemented |
| EMR Client | Patient Demographics Query | Implemented |
| EMR Client | Patient Matching/Disambiguation | Implemented |
| EMR Client | DiagnosticReport Posting | Pending |
| Integration | EMR Integration Tests | Pending |

## Running Tests

Tests use Google Test framework with automatic test discovery.

```bash
# Build with tests enabled
cmake -B build -DBRIDGE_BUILD_TESTS=ON
cmake --build build

# Run all tests with CTest (recommended)
ctest --test-dir build --output-on-failure

# Run tests in parallel
ctest --test-dir build -j $(nproc) --output-on-failure

# Run specific test executables directly
./build/bin/hl7_test                    # HL7 protocol tests
./build/bin/adt_handler_test            # ADT message processing
./build/bin/mapping_test                # HL7-DICOM mapping
./build/bin/router_test                 # Message routing
./build/bin/cache_test                  # Patient cache
./build/bin/mllp_test                   # MLLP transport
./build/bin/config_test                 # Configuration
./build/bin/mwl_client_test             # MWL client
./build/bin/bridge_metrics_test         # Prometheus metrics
./build/bin/ecosystem_integration_test  # Verify dependency setup

# Run Phase 5 EMR client tests
./build/bin/fhir_client_test            # FHIR R4 HTTP client
./build/bin/patient_lookup_test         # Patient demographics query

# Run Phase 2 integration tests
./build/bin/mpps_integration_test       # MPPS N-CREATE/N-SET flows
./build/bin/queue_persistence_test      # Message queue recovery
./build/bin/failover_test               # RIS failover routing
./build/bin/stress_test                 # High volume load testing

# Run performance benchmarks and memory tests
./build/bin/benchmark_suite_test        # Message processing, concurrency, throughput
./build/bin/memory_safety_test          # Memory leak detection, RAII compliance

# Filter tests by label
ctest --test-dir build -L phase1      # Phase 1 unit tests only
ctest --test-dir build -L phase2      # Phase 2 integration tests only
ctest --test-dir build -L phase5      # Phase 5 EMR client tests
ctest --test-dir build -L integration # All integration tests
ctest --test-dir build -L stress      # Stress tests only

# Filter tests by name pattern (Google Test)
./build/bin/hl7_test --gtest_filter="HL7ParserTest.*"
./build/bin/hl7_test --gtest_filter="*Timestamp*"
```

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
