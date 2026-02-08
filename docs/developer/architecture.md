# Architecture Overview

> **Version:** 0.2.0.0
> **Last Updated:** 2026-02-08

---

## Table of Contents

- [System Architecture](#system-architecture)
- [Component Architecture](#component-architecture)
- [Data Flow](#data-flow)
- [Technology Stack](#technology-stack)
- [Design Principles](#design-principles)
- [Integration Points](#integration-points)

---

## System Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Healthcare Network                              │
├─────────────┬─────────────────────────────────────────────────┬─────────────┤
│             │                                                 │             │
│   ┌─────────▼─────────┐                           ┌───────────▼───────┐     │
│   │   HIS / RIS       │                           │    PACS System    │     │
│   │   ─────────       │                           │    ────────────   │     │
│   │   HL7 v2.x        │                           │    DICOM          │     │
│   │   FHIR R4         │                           │    MWL/MPPS       │     │
│   └─────────┬─────────┘                           └───────────┬───────┘     │
│             │                                                 │             │
│             │              ┌─────────────────┐                │             │
│             │              │   PACS Bridge   │                │             │
│             └─────────────►│   ───────────   │◄───────────────┘             │
│                            │                 │                               │
│                            │  ┌───────────┐  │                               │
│                            │  │ HL7       │  │                               │
│                            │  │ Gateway   │  │                               │
│                            │  └─────┬─────┘  │                               │
│                            │        │        │                               │
│                            │  ┌─────▼─────┐  │                               │
│                            │  │ Message   │  │                               │
│                            │  │ Router    │  │                               │
│                            │  └─────┬─────┘  │                               │
│                            │        │        │                               │
│                            │  ┌─────▼─────┐  │                               │
│                            │  │ Protocol  │  │                               │
│                            │  │ Translator│  │                               │
│                            │  └─────┬─────┘  │                               │
│                            │        │        │                               │
│                            │  ┌─────▼─────┐  │                               │
│                            │  │ PACS      │  │                               │
│                            │  │ Adapter   │  │                               │
│                            │  └───────────┘  │                               │
│                            │                 │                               │
│                            └─────────────────┘                               │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Layered Architecture

PACS Bridge follows a layered architecture pattern:

```
┌─────────────────────────────────────────────────────────────┐
│                     Presentation Layer                       │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │   MLLP Server   │  │   FHIR REST    │  │ Health/Metrics│ │
│  │   (HL7 v2.x)    │  │   (HTTP/S)     │  │  (HTTP)       │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                     Application Layer                        │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │  Message Router │  │  Event Handlers │  │ Mapping Engine│ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                      Domain Layer                            │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │  HL7 Protocol   │  │  FHIR Resources │  │ DICOM Objects│ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                   Infrastructure Layer                       │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │  Patient Cache  │  │  Message Queue  │  │ PACS Adapter │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                   External Systems Layer                     │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │   pacs_system   │  │ network_system  │  │ logger_system│ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## Component Architecture

### Core Components

| Component | Responsibility | Location |
|-----------|---------------|----------|
| **HL7 Gateway** | HL7 v2.x message processing | `src/protocol/hl7/` |
| **MLLP Transport** | HL7 message framing | `src/mllp/` |
| **FHIR Gateway** | FHIR REST API | `src/fhir/` |
| **Message Router** | Message routing logic | `src/router/` |
| **Protocol Translator** | HL7↔DICOM mapping | `src/mapping/` |
| **PACS Adapter** | DICOM integration | `src/pacs_adapter/` |
| **Patient Cache** | Demographics caching | `src/cache/` |
| **Message Queue** | Reliable delivery | `src/queue/` |
| **Configuration** | Config management | `src/config/` |
| **Security** | TLS, auth, audit | `src/security/` |
| **Monitoring** | Health, metrics | `src/monitoring/` |

### Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           PACS Bridge                                    │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                      Transport Layer                              │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌──────────────┐              │   │
│  │  │ MLLP Server │  │ MLLP Client │  │ HTTP Server  │              │   │
│  │  └──────┬──────┘  └──────┬──────┘  └──────┬───────┘              │   │
│  └─────────┼────────────────┼────────────────┼──────────────────────┘   │
│            │                │                │                           │
│  ┌─────────▼────────────────▼────────────────▼──────────────────────┐   │
│  │                      Protocol Layer                               │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌──────────────┐              │   │
│  │  │ HL7 Parser  │  │ HL7 Builder │  │ FHIR Handler │              │   │
│  │  └──────┬──────┘  └──────┬──────┘  └──────┬───────┘              │   │
│  └─────────┼────────────────┼────────────────┼──────────────────────┘   │
│            │                │                │                           │
│  ┌─────────▼────────────────▼────────────────▼──────────────────────┐   │
│  │                      Handler Layer                                │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐          │   │
│  │  │ADT Handler│  │ORM Handler│ │SIU Handler│ │MPPS Handler│        │   │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘          │   │
│  └───────┼─────────────┼─────────────┼─────────────┼────────────────┘   │
│          │             │             │             │                     │
│  ┌───────▼─────────────▼─────────────▼─────────────▼────────────────┐   │
│  │                      Service Layer                                │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐         │   │
│  │  │ Message Router│  │ Patient Cache │  │ Message Queue │         │   │
│  │  └───────┬───────┘  └───────┬───────┘  └───────┬───────┘         │   │
│  └──────────┼──────────────────┼──────────────────┼─────────────────┘   │
│             │                  │                  │                      │
│  ┌──────────▼──────────────────▼──────────────────▼─────────────────┐   │
│  │                      Adapter Layer                                │   │
│  │  ┌───────────────────────────────────────────────────────────┐   │   │
│  │  │                     PACS Adapter                          │   │   │
│  │  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐    │   │   │
│  │  │  │ MWL Client  │  │MPPS Handler │  │ Query Handler   │    │   │   │
│  │  │  └─────────────┘  └─────────────┘  └─────────────────┘    │   │   │
│  │  └───────────────────────────────────────────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key Interfaces

```cpp
// Message handler interface
class message_handler {
public:
    virtual ~message_handler() = default;
    virtual result<hl7_message> handle(const hl7_message& message) = 0;
    virtual std::vector<std::string> supported_message_types() const = 0;
};

// Router interface
class message_router {
public:
    virtual ~message_router() = default;
    virtual void add_handler(std::unique_ptr<message_handler> handler) = 0;
    virtual result<hl7_message> route(const hl7_message& message) = 0;
};

// PACS adapter interface
class pacs_adapter {
public:
    virtual ~pacs_adapter() = default;
    virtual result<void> update_worklist(const mwl_item& item) = 0;
    virtual result<std::vector<mwl_item>> query_worklist(const mwl_query& query) = 0;
    virtual result<void> process_mpps(const mpps_event& event) = 0;
};
```

---

## Data Flow

### Inbound Message Flow

```
                             ┌─────────────────┐
                             │   HIS / RIS     │
                             └────────┬────────┘
                                      │
                                      │ HL7 Message
                                      ▼
                             ┌─────────────────┐
                             │   MLLP Server   │
                             └────────┬────────┘
                                      │
                                      │ Raw Message
                                      ▼
                             ┌─────────────────┐
                             │   HL7 Parser    │
                             └────────┬────────┘
                                      │
                                      │ Parsed Message
                                      ▼
                             ┌─────────────────┐
                             │  Message Router │
                             └────────┬────────┘
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                    ▼                 ▼                 ▼
           ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
           │ ADT Handler  │  │ ORM Handler  │  │ SIU Handler  │
           └──────┬───────┘  └──────┬───────┘  └──────┬───────┘
                  │                 │                 │
                  ▼                 ▼                 ▼
           ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
           │Patient Cache │  │ HL7-DICOM    │  │ HL7-DICOM    │
           │              │  │ Mapper       │  │ Mapper       │
           └──────────────┘  └──────┬───────┘  └──────┬───────┘
                                    │                 │
                                    ▼                 ▼
                             ┌─────────────────────────────┐
                             │        PACS Adapter         │
                             └─────────────────────────────┘
```

### Outbound Message Flow

```
                             ┌─────────────────┐
                             │   PACS System   │
                             └────────┬────────┘
                                      │
                                      │ MPPS Event
                                      ▼
                             ┌─────────────────┐
                             │  MPPS Handler   │
                             └────────┬────────┘
                                      │
                                      │ MPPS Data
                                      ▼
                             ┌─────────────────┐
                             │  DICOM-HL7      │
                             │  Mapper         │
                             └────────┬────────┘
                                      │
                                      │ HL7 Message
                                      ▼
                             ┌─────────────────┐
                             │  Message Queue  │
                             └────────┬────────┘
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                    ▼                 ▼                 ▼
           ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
           │ RIS Primary  │  │ RIS Secondary│  │ RIS Tertiary │
           │ (Priority 1) │  │ (Priority 2) │  │ (Priority 3) │
           └──────────────┘  └──────────────┘  └──────────────┘
```

---

## Technology Stack

### Core Technologies

| Technology | Version | Purpose |
|------------|---------|---------|
| C++ | C++23 | Core language |
| CMake | 3.20+ | Build system |
| vcpkg | Latest | Package management |
| Google Test | Latest | Unit testing |
| OpenSSL | 1.1+ | TLS/cryptography |
| SQLite | 3.x | Queue persistence |
| YAML-cpp | 0.7+ | Configuration parsing |

### kcenon Ecosystem Dependencies

| Library | Purpose |
|---------|---------|
| `common_system` | Common utilities |
| `thread_system` | Thread management |
| `logger_system` | Logging infrastructure |
| `container_system` | Data containers |
| `network_system` | Network communication |
| `monitoring_system` | System monitoring |
| `pacs_system` | DICOM integration |

### Dependency Graph

```
                    ┌───────────────┐
                    │  pacs_bridge  │
                    └───────┬───────┘
                            │
          ┌─────────────────┼─────────────────┐
          │                 │                 │
          ▼                 ▼                 ▼
   ┌─────────────┐  ┌──────────────┐  ┌────────────────┐
   │ pacs_system │  │network_system│  │monitoring_system│
   └──────┬──────┘  └──────┬───────┘  └────────┬───────┘
          │                │                   │
          └────────────────┼───────────────────┘
                           │
                    ┌──────▼──────┐
                    │logger_system│
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │thread_system│
                    └──────┬──────┘
                           │
                ┌──────────┼──────────┐
                │          │          │
         ┌──────▼──────┐ ┌─▼───────────▼─┐
         │container_sys│ │  common_system │
         └─────────────┘ └───────────────┘
```

---

## Design Principles

### SOLID Principles

| Principle | Application in PACS Bridge |
|-----------|---------------------------|
| **Single Responsibility** | Each handler processes one message type |
| **Open/Closed** | Handlers extensible via interface |
| **Liskov Substitution** | Handler implementations interchangeable |
| **Interface Segregation** | Small, focused interfaces |
| **Dependency Inversion** | Depend on abstractions, not concretions |

### Design Patterns Used

| Pattern | Usage |
|---------|-------|
| **Factory** | Message handler creation |
| **Strategy** | Routing algorithms |
| **Chain of Responsibility** | Message processing pipeline |
| **Observer** | Event notification |
| **Adapter** | Protocol translation |
| **Singleton** | Configuration, metrics |
| **Builder** | HL7 message construction |

### C++20/23 Features

| Feature | Usage |
|---------|-------|
| **Concepts** | Type constraints for handlers |
| **Coroutines** | Async I/O operations |
| **Ranges** | Data transformation |
| **std::format** | String formatting |
| **Modules** | (Future) Code organization |

---

## Integration Points

### External System Integration

```
┌────────────────────────────────────────────────────────────────────────┐
│                         External Systems                                │
│                                                                         │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐      │
│  │       HIS        │  │       RIS        │  │      PACS        │      │
│  │  ─────────────   │  │  ─────────────   │  │  ─────────────   │      │
│  │  Port: 2575      │  │  Port: 2576      │  │  Port: 11112     │      │
│  │  Protocol: MLLP  │  │  Protocol: MLLP  │  │  Protocol: DICOM │      │
│  └────────┬─────────┘  └────────┬─────────┘  └────────┬─────────┘      │
│           │                     │                     │                 │
│           │                     │                     │                 │
│  ┌────────▼─────────────────────▼─────────────────────▼─────────┐      │
│  │                       PACS Bridge                             │      │
│  │  ┌─────────────────────────────────────────────────────────┐ │      │
│  │  │                 Integration Layer                        │ │      │
│  │  │  ┌───────────┐  ┌───────────┐  ┌───────────────────────┐│ │      │
│  │  │  │MLLP Server│  │MLLP Client│  │    PACS Adapter       ││ │      │
│  │  │  │(Inbound)  │  │(Outbound) │  │    (pacs_system)      ││ │      │
│  │  │  └───────────┘  └───────────┘  └───────────────────────┘│ │      │
│  │  └─────────────────────────────────────────────────────────┘ │      │
│  └───────────────────────────────────────────────────────────────┘      │
│                                                                         │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐      │
│  │    Prometheus    │  │     Grafana      │  │     EHR/EMR      │      │
│  │  ─────────────   │  │  ─────────────   │  │  ─────────────   │      │
│  │  Port: 8081      │  │  Dashboard       │  │  Port: 8080      │      │
│  │  Path: /metrics  │  │                  │  │  Protocol: FHIR  │      │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘      │
│                                                                         │
└────────────────────────────────────────────────────────────────────────┘
```

### API Contracts

| Interface | Protocol | Port | Direction |
|-----------|----------|------|-----------|
| HIS/RIS HL7 | MLLP | 2575 | Inbound |
| RIS Outbound | MLLP | 2576 | Outbound |
| FHIR API | HTTP/S | 8080 | Inbound |
| PACS MWL | DICOM | 11112 | Outbound |
| PACS MPPS | DICOM | 11112 | Inbound |
| Health/Metrics | HTTP | 8081 | Inbound |

---

## Related Documentation

- [Module Descriptions](modules.md) - Detailed module documentation
- [Contributing Guidelines](contributing.md) - How to contribute
- [API Reference](../api/hl7-messages.md) - API specifications
