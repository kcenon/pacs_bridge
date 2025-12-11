# Getting Started with PACS Bridge

> **Version:** 0.1.0.0
> **Last Updated:** 2025-12-10

---

## Table of Contents

- [Introduction](#introduction)
- [System Overview](#system-overview)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Basic Concepts](#basic-concepts)
- [Next Steps](#next-steps)

---

## Introduction

PACS Bridge is a C++20 integration gateway that connects Hospital Information Systems (HIS) and Radiology Information Systems (RIS) with Picture Archiving and Communication Systems (PACS). It translates between HL7 v2.x/FHIR messaging protocols and DICOM services, enabling seamless workflow integration in radiology departments.

### Key Features

| Feature | Description |
|---------|-------------|
| **HL7 v2.x Gateway** | ADT, ORM, ORU, SIU message support via MLLP |
| **FHIR R4/R5 Gateway** | RESTful API integration with modern EHR systems |
| **Modality Worklist Bridge** | MWL C-FIND to RIS/HIS query translation |
| **MPPS Notification** | Exam status propagation to RIS |
| **Flexible Routing** | Multi-gateway support with failover |
| **IHE SWF Compliant** | Follows IHE Scheduled Workflow integration profile |
| **Prometheus Metrics** | Built-in metrics export for monitoring and alerting |

---

## System Overview

### Architecture Diagram

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   HIS / RIS     │     │   PACS Bridge   │     │   PACS System   │
│                 │◄───►│                 │◄───►│                 │
│  HL7 / FHIR     │     │  Gateway Layer  │     │  DICOM Services │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

### Component Overview

| Component | Purpose |
|-----------|---------|
| **HL7 Gateway** | Receives and processes HL7 v2.x messages via MLLP |
| **FHIR Gateway** | Provides RESTful API for FHIR R4 resources |
| **Message Router** | Routes messages to appropriate handlers |
| **Protocol Translator** | Converts between HL7/FHIR and DICOM formats |
| **PACS Adapter** | Communicates with pacs_system for MWL/MPPS |
| **Patient Cache** | Caches patient demographics for efficient lookup |
| **Message Queue** | Provides reliable message delivery with persistence |

### Supported Protocols

#### HL7 v2.x Message Types

| Message Type | Description |
|--------------|-------------|
| ADT^A01 | Patient Admit |
| ADT^A04 | Patient Registration |
| ADT^A08 | Patient Information Update |
| ADT^A40 | Patient Merge |
| ORM^O01 | Order Message |
| ORU^R01 | Observation Result |
| SIU^S12 | Schedule Information Update |

#### FHIR R4 Resources

| Resource | Operations |
|----------|------------|
| Patient | Read, Search, Create, Update |
| ServiceRequest | Read, Search, Create, Update |
| ImagingStudy | Read, Search |
| Task | Read, Search, Create, Update |

#### DICOM Services

| Service | Role |
|---------|------|
| MWL C-FIND | SCU (Query worklist) |
| MPPS N-CREATE | SCP (Receive procedure start) |
| MPPS N-SET | SCP (Receive procedure complete/discontinue) |

---

## Prerequisites

### Hardware Requirements

| Resource | Minimum | Recommended |
|----------|---------|-------------|
| CPU | 2 cores | 4+ cores |
| RAM | 4 GB | 8+ GB |
| Disk | 20 GB | 100+ GB |
| Network | 100 Mbps | 1 Gbps |

### Software Requirements

| Software | Version | Notes |
|----------|---------|-------|
| Operating System | Linux, macOS, Windows | Ubuntu 22.04+ recommended |
| C++ Compiler | GCC 13+, Clang 14+, MSVC 2022+ | C++23 support required |
| CMake | 3.20+ | Build system |
| OpenSSL | 1.1+ | Optional, for TLS support |

### Network Requirements

| Port | Protocol | Purpose |
|------|----------|---------|
| 2575 | TCP | MLLP HL7 (default) |
| 2576 | TCP | MLLPS HL7 with TLS |
| 8080 | TCP | FHIR REST API |
| 8081 | TCP | Health check / Metrics |
| 11112 | TCP | DICOM (outbound to PACS) |

---

## Quick Start

### Step 1: Install PACS Bridge

#### Option A: Build from Source

```bash
# Clone the repository
git clone https://github.com/kcenon/pacs_bridge.git
cd pacs_bridge

# Build in standalone mode (no external dependencies)
cmake -B build -DBRIDGE_STANDALONE_BUILD=ON
cmake --build build
```

#### Option B: Docker Deployment

```bash
# Pull the latest image
docker pull kcenon/pacs_bridge:latest

# Run with default configuration
docker run -d \
  --name pacs_bridge \
  -p 2575:2575 \
  -p 8080:8080 \
  -p 8081:8081 \
  kcenon/pacs_bridge:latest
```

### Step 2: Configure the System

Create a configuration file based on the example:

```bash
cp examples/config/config.yaml /etc/pacs_bridge/config.yaml
```

Edit the configuration file to match your environment:

```yaml
# /etc/pacs_bridge/config.yaml
server:
  name: "MY_PACS_BRIDGE"

hl7:
  listener:
    port: 2575
    max_connections: 50

pacs:
  host: "pacs.hospital.local"
  port: 11112
  ae_title: "PACS_BRIDGE"
  called_ae: "PACS_SCP"
```

### Step 3: Start the Service

```bash
# Start PACS Bridge
./build/bin/pacs_bridge --config /etc/pacs_bridge/config.yaml
```

### Step 4: Verify Operation

Check the health endpoint:

```bash
curl http://localhost:8081/health
```

Expected response:

```json
{
  "status": "healthy",
  "components": {
    "hl7_listener": "up",
    "pacs_connection": "up",
    "patient_cache": "up"
  }
}
```

---

## Basic Concepts

### Message Flow

The following diagram illustrates the typical message flow through PACS Bridge:

```
HIS/RIS                    PACS Bridge                    PACS
   │                            │                           │
   │──── ADT^A04 ──────────────►│                           │
   │                            │── Update Patient Cache    │
   │                            │                           │
   │──── ORM^O01 ──────────────►│                           │
   │                            │── Convert to MWL          │
   │                            │── MWL C-FIND ────────────►│
   │                            │◄─ MWL Response ───────────│
   │                            │                           │
   │                            │◄─ MPPS N-CREATE ──────────│
   │◄─── ORU^R01 ───────────────│                           │
   │                            │                           │
   │                            │◄─ MPPS N-SET ─────────────│
   │◄─── ORU^R01 ───────────────│                           │
```

### Patient Registration

When a patient is registered in HIS:

1. HIS sends an ADT^A04 (Patient Registration) message
2. PACS Bridge parses the message and extracts patient demographics
3. Patient data is stored in the patient cache
4. An ACK message is returned to HIS

### Order Processing

When a radiology order is placed:

1. RIS sends an ORM^O01 (Order) message
2. PACS Bridge converts the order to MWL format
3. PACS Bridge updates the PACS worklist
4. An ACK message is returned to RIS

### Procedure Status

When a procedure is started or completed on a modality:

1. Modality sends MPPS N-CREATE (procedure started)
2. PACS Bridge converts the status to ORU^R01
3. RIS receives the status update
4. Modality sends MPPS N-SET (procedure completed)
5. PACS Bridge converts the final status to ORU^R01
6. RIS receives the completion notification

### Configuration Hierarchy

PACS Bridge uses a hierarchical configuration system:

```
Environment Variables (highest priority)
         │
         ▼
   Configuration File
         │
         ▼
   Default Values (lowest priority)
```

Environment variables can override configuration file settings using the `${VAR}` syntax:

```yaml
server:
  name: "${PACS_BRIDGE_NAME:-DEFAULT_BRIDGE}"
```

---

## Next Steps

- [Configuration Guide](configuration.md) - Detailed configuration options
- [Message Flows](message-flows.md) - Complete message flow documentation
- [Troubleshooting](troubleshooting.md) - Common issues and solutions

---

## Related Documentation

- [Administrator Guide](../admin-guide/deployment.md) - Deployment and operations
- [API Documentation](../api/hl7-messages.md) - HL7 message specifications
- [Developer Guide](../developer/architecture.md) - Architecture details

---

*For support, please open an issue at https://github.com/kcenon/pacs_bridge/issues*
