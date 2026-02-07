# PACS Bridge Documentation

Welcome to the PACS Bridge documentation. PACS Bridge is an integration gateway that connects Hospital Information Systems (HIS) and Radiology Information Systems (RIS) with Picture Archiving and Communication Systems (PACS).

## What is PACS Bridge?

PACS Bridge translates between HL7 v2.x/FHIR messaging protocols and DICOM services, enabling seamless workflow integration in radiology departments.

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   HIS / RIS     │     │   PACS Bridge   │     │   PACS System   │
│                 │◄───►│                 │◄───►│                 │
│  HL7 / FHIR     │     │  Gateway Layer  │     │  DICOM Services │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

## Key Features

| Feature | Description |
|---------|-------------|
| **HL7 v2.x Gateway** | ADT, ORM, ORU, SIU message support via MLLP |
| **FHIR R4 Gateway** | RESTful API integration with modern EHR systems |
| **Modality Worklist Bridge** | MWL C-FIND to RIS/HIS query translation |
| **MPPS Notification** | Exam status propagation to RIS |
| **Flexible Routing** | Multi-gateway support with failover |
| **IHE SWF Compliant** | Follows IHE Scheduled Workflow integration profile |
| **Prometheus Metrics** | Built-in metrics export for monitoring |

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/kcenon/pacs_bridge.git
cd pacs_bridge

# Build
cmake -B build -DBRIDGE_STANDALONE_BUILD=ON
cmake --build build
```

### Configuration

```yaml
# pacs_bridge.yaml
server:
  name: "PACS_BRIDGE"

hl7:
  listener:
    port: 2575

pacs:
  host: "pacs.hospital.local"
  port: 11112
  ae_title: "PACS_BRIDGE"
  called_ae: "PACS_SCP"
```

### Run

```bash
./build/bin/pacs_bridge --config /etc/pacs_bridge/config.yaml
```

## Documentation Sections

<div class="grid cards" markdown>

-   :material-book-open-variant:{ .lg .middle } **User Guide**

    ---

    Getting started, configuration, and troubleshooting.

    [:octicons-arrow-right-24: User Guide](user-guide/getting-started.md)

-   :material-server:{ .lg .middle } **Administrator Guide**

    ---

    Deployment, high availability, monitoring, and backup.

    [:octicons-arrow-right-24: Admin Guide](admin-guide/deployment.md)

-   :material-api:{ .lg .middle } **API Reference**

    ---

    HL7 messages, FHIR API, and error codes.

    [:octicons-arrow-right-24: API Reference](api/hl7-messages.md)

-   :material-code-braces:{ .lg .middle } **Developer Guide**

    ---

    Architecture, modules, and contributing guidelines.

    [:octicons-arrow-right-24: Developer Guide](developer/architecture.md)

</div>

## Getting Help

- **GitHub Issues:** [Report bugs or request features](https://github.com/kcenon/pacs_bridge/issues)
- **Documentation:** [Full documentation](https://kcenon.github.io/pacs_bridge/)

## License

PACS Bridge is licensed under the BSD 3-Clause License. See the [LICENSE](https://github.com/kcenon/pacs_bridge/blob/main/LICENSE) file for details.
