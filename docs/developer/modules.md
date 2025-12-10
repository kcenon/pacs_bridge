# Module Descriptions

> **Version:** 1.0.0
> **Last Updated:** 2025-12-10

---

## Table of Contents

- [Module Overview](#module-overview)
- [Protocol Modules](#protocol-modules)
- [Transport Modules](#transport-modules)
- [Service Modules](#service-modules)
- [Infrastructure Modules](#infrastructure-modules)
- [Extension Points](#extension-points)

---

## Module Overview

PACS Bridge is organized into modular components for maintainability and extensibility.

### Module Structure

```
pacs_bridge/
├── include/pacs/bridge/
│   ├── protocol/hl7/         # HL7 message handling
│   ├── mllp/                 # MLLP transport
│   ├── fhir/                 # FHIR gateway
│   ├── mapping/              # Protocol translation
│   ├── router/               # Message routing
│   ├── pacs_adapter/         # PACS integration
│   ├── cache/                # Patient cache
│   ├── config/               # Configuration
│   ├── security/             # Security features
│   ├── monitoring/           # Health and metrics
│   ├── testing/              # Test utilities
│   └── concepts/             # C++20 concepts
├── src/
│   └── [corresponding implementations]
└── tests/
    └── [module tests]
```

---

## Protocol Modules

### HL7 Protocol Module (`protocol/hl7/`)

**Purpose:** Parse, build, and validate HL7 v2.x messages.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `hl7_message` | Message container with segments |
| `hl7_parser` | Parse raw HL7 to structured message |
| `hl7_builder` | Build HL7 messages programmatically |
| `hl7_validator` | Validate message structure and content |
| `hl7_types` | Type definitions and enumerations |

**Key Headers:**

```cpp
#include <pacs/bridge/protocol/hl7/hl7_message.h>
#include <pacs/bridge/protocol/hl7/hl7_parser.h>
#include <pacs/bridge/protocol/hl7/hl7_builder.h>
```

**Usage Example:**

```cpp
// Parse an HL7 message
hl7_parser parser;
auto result = parser.parse(raw_message);
if (result) {
    auto& message = result.value();
    auto msh = message.get_segment("MSH");
    auto message_type = msh.get_field(9);  // "ADT^A01"
}

// Build an ACK message
hl7_builder builder;
auto ack = builder
    .set_message_type("ACK", "A01")
    .set_message_control_id("ACK001")
    .add_msa_segment("AA", original_message_id)
    .build();
```

### ADT Handler (`protocol/hl7/adt_handler.h`)

**Purpose:** Process ADT (Admission/Discharge/Transfer) messages.

**Supported Events:**

| Event | Description | Action |
|-------|-------------|--------|
| A01 | Patient Admit | Cache patient |
| A04 | Patient Registration | Cache patient |
| A08 | Patient Update | Update cache |
| A40 | Patient Merge | Update aliases |

**Usage:**

```cpp
adt_handler handler(patient_cache);
auto result = handler.handle(adt_message);
```

### ORM Handler (`protocol/hl7/orm_handler.h`)

**Purpose:** Process ORM (Order) messages.

**Supported Events:**

| Event | Description | Action |
|-------|-------------|--------|
| O01 | New Order | Create MWL entry |

**Usage:**

```cpp
orm_handler handler(hl7_dicom_mapper, pacs_adapter);
auto result = handler.handle(orm_message);
```

### SIU Handler (`protocol/hl7/siu_handler.h`)

**Purpose:** Process SIU (Scheduling) messages.

**Supported Events:**

| Event | Description | Action |
|-------|-------------|--------|
| S12 | Scheduled | Create worklist |
| S13 | Modified | Update worklist |
| S14 | Cancelled | Cancel worklist |

---

## Transport Modules

### MLLP Module (`mllp/`)

**Purpose:** Handle Minimal Lower Layer Protocol for HL7 transport.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `mllp_server` | TCP server for inbound connections |
| `mllp_client` | TCP client for outbound connections |
| `mllp_connection` | Individual connection handler |
| `mllp_frame` | Message framing utilities |

**Configuration:**

```yaml
hl7:
  listener:
    port: 2575
    max_connections: 100
    idle_timeout: 300s
```

**Usage:**

```cpp
// Create server
mllp_server server(config);
server.set_message_handler([&](const std::string& message) {
    return router.route(parser.parse(message));
});
server.start();

// Create client
mllp_client client("ris.hospital.local", 2576);
auto result = client.send(hl7_message);
```

### FHIR Module (`fhir/`)

**Purpose:** RESTful FHIR R4 API gateway.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `fhir_server` | HTTP server for FHIR API |
| `resource_handler` | Base class for resource handlers |
| `patient_handler` | Patient resource operations |
| `service_request_handler` | ServiceRequest operations |
| `imaging_study_handler` | ImagingStudy operations |

**Endpoints:**

| Path | Methods | Resource |
|------|---------|----------|
| `/fhir/r4/Patient` | GET, POST | Patient |
| `/fhir/r4/Patient/{id}` | GET, PUT | Patient |
| `/fhir/r4/ServiceRequest` | GET, POST | ServiceRequest |
| `/fhir/r4/ImagingStudy` | GET | ImagingStudy |

---

## Service Modules

### Message Router (`router/`)

**Purpose:** Route messages to appropriate handlers based on type and rules.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `message_router` | Main routing engine |
| `routing_rule` | Individual routing rule |
| `pattern_matcher` | Message type pattern matching |

**Configuration:**

```yaml
routing_rules:
  - name: "ADT to Cache"
    message_type_pattern: "ADT^A*"
    destination: "patient_cache"
    priority: 10
```

**Usage:**

```cpp
message_router router;
router.add_rule(routing_rule{
    .name = "ADT to Cache",
    .message_type_pattern = "ADT^A*",
    .destination = "patient_cache",
    .priority = 10
});
router.add_handler("patient_cache", std::make_unique<adt_handler>(cache));

auto result = router.route(message);
```

### Protocol Mapping (`mapping/`)

**Purpose:** Translate between HL7, FHIR, and DICOM formats.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `hl7_dicom_mapper` | HL7 to DICOM conversion |
| `dicom_hl7_mapper` | DICOM to HL7 conversion |
| `fhir_dicom_mapper` | FHIR to DICOM conversion |

**Mappings:**

| Source | Target | Use Case |
|--------|--------|----------|
| ORM^O01 | MWL Item | Order to worklist |
| MPPS | ORU^R01 | Status notification |
| Patient | MWL Patient | Demographics |
| ServiceRequest | MWL Item | FHIR order |

**Usage:**

```cpp
hl7_dicom_mapper mapper(config.mapping);
auto mwl_item = mapper.map_orm_to_mwl(orm_message);

dicom_hl7_mapper reverse_mapper(config.mapping);
auto oru = reverse_mapper.map_mpps_to_oru(mpps_event);
```

### PACS Adapter (`pacs_adapter/`)

**Purpose:** Interface with pacs_system for DICOM operations.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `mwl_client` | Modality Worklist operations |
| `mpps_handler` | MPPS event handling |

**Operations:**

| Operation | DICOM Service | Direction |
|-----------|---------------|-----------|
| Query Worklist | MWL C-FIND | Outbound |
| Update Worklist | MWL Update | Outbound |
| Receive MPPS | MPPS N-CREATE/N-SET | Inbound |

**Usage:**

```cpp
mwl_client client(pacs_config);
auto result = client.update_worklist(mwl_item);

mpps_handler handler(dicom_hl7_mapper, message_queue);
handler.on_mpps_event(mpps_event);
```

---

## Infrastructure Modules

### Patient Cache (`cache/`)

**Purpose:** Cache patient demographics for efficient lookup.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `patient_cache` | LRU cache with TTL |
| `patient_record` | Patient data structure |
| `patient_alias` | Alias management |

**Features:**

- LRU eviction
- Configurable TTL
- Patient ID aliasing (for merges)
- Thread-safe access

**Configuration:**

```yaml
patient_cache:
  max_entries: 10000
  ttl: 3600s
  evict_on_full: true
```

**Usage:**

```cpp
patient_cache cache(config);
cache.store(patient);
auto patient = cache.lookup("12345");
cache.add_alias("12345", "67890");  // Merge alias
```

### Message Queue (`queue/`)

**Purpose:** Reliable message delivery with persistence.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `message_queue` | Queue interface |
| `sqlite_queue` | SQLite-backed implementation |
| `queue_worker` | Delivery worker thread |

**Features:**

- SQLite persistence
- Retry with exponential backoff
- Dead letter queue
- Multiple workers

**Configuration:**

```yaml
queue:
  database_path: "/var/lib/pacs_bridge/queue.db"
  max_queue_size: 50000
  max_retry_count: 5
  worker_count: 4
```

**Usage:**

```cpp
sqlite_queue queue(config);
queue.enqueue(message, "RIS_PRIMARY");

queue.set_delivery_handler([](const message& msg, const std::string& dest) {
    return mllp_client(dest).send(msg);
});
queue.start_workers();
```

### Configuration (`config/`)

**Purpose:** Load and manage configuration.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `config_loader` | YAML configuration loading |
| `config_manager` | Runtime config management |
| `bridge_config` | Configuration data structure |

**Usage:**

```cpp
config_loader loader;
auto config = loader.load("/etc/pacs_bridge/config.yaml");

config_manager manager(config);
auto& hl7_config = manager.get_hl7_config();
```

### Security (`security/`)

**Purpose:** Security features including TLS, authentication, and auditing.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `tls_context` | TLS configuration |
| `tls_socket` | TLS-enabled socket |
| `audit_logger` | Security event logging |
| `access_control` | Authorization checks |
| `input_validator` | Input sanitization |
| `rate_limiter` | Rate limiting |

**Usage:**

```cpp
tls_context context;
context.load_certificate("/path/to/cert.pem");
context.load_private_key("/path/to/key.pem");

audit_logger audit;
audit.log_access(user, resource, action);
```

### Monitoring (`monitoring/`)

**Purpose:** Health checks and Prometheus metrics.

**Key Classes:**

| Class | Purpose |
|-------|---------|
| `health_server` | Health endpoint server |
| `bridge_metrics_collector` | Metrics collection |
| `health_checker` | Component health checks |

**Metrics:**

| Metric | Type | Description |
|--------|------|-------------|
| `hl7_messages_received_total` | Counter | Received messages |
| `hl7_message_processing_duration_seconds` | Histogram | Processing latency |
| `queue_depth` | Gauge | Queue depth |
| `mllp_active_connections` | Gauge | Active connections |

**Usage:**

```cpp
auto& metrics = bridge_metrics_collector::instance();
metrics.record_hl7_message_received("ADT");

{
    PACS_BRIDGE_TIME_HL7_PROCESSING("ADT");
    // Process message
}
```

---

## Extension Points

### Adding a New Message Handler

1. **Create handler class:**

```cpp
// include/pacs/bridge/protocol/hl7/my_handler.h
class my_handler : public message_handler {
public:
    result<hl7_message> handle(const hl7_message& message) override;
    std::vector<std::string> supported_message_types() const override {
        return {"MYT^M01", "MYT^M02"};
    }
};
```

2. **Register with router:**

```cpp
router.add_handler("my_destination", std::make_unique<my_handler>());
```

3. **Add routing rule:**

```yaml
routing_rules:
  - name: "My Messages"
    message_type_pattern: "MYT^*"
    destination: "my_destination"
```

### Adding a New FHIR Resource

1. **Create resource handler:**

```cpp
class my_resource_handler : public resource_handler {
public:
    http_response handle_get(const http_request& request) override;
    http_response handle_post(const http_request& request) override;
};
```

2. **Register with FHIR server:**

```cpp
fhir_server.register_handler("/fhir/r4/MyResource",
    std::make_unique<my_resource_handler>());
```

### Adding Custom Metrics

```cpp
// Register custom metric
auto& metrics = bridge_metrics_collector::instance();
metrics.register_counter("my_custom_metric", "Description", {"label1"});

// Record values
metrics.increment_counter("my_custom_metric", {{"label1", "value1"}});
```

---

## Module Dependencies

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Internal Dependencies                         │
│                                                                       │
│  ┌─────────────┐                                                     │
│  │   config    │◄─────────────────────────────────────────┐         │
│  └─────────────┘                                           │         │
│         ▲                                                  │         │
│         │                                                  │         │
│  ┌──────┴──────┐    ┌─────────────┐    ┌─────────────┐    │         │
│  │   security  │    │  monitoring │    │   testing   │    │         │
│  └─────────────┘    └─────────────┘    └─────────────┘    │         │
│         ▲                  ▲                               │         │
│         │                  │                               │         │
│  ┌──────┴──────────────────┴────────────────────────────┐ │         │
│  │                    Core Modules                       │ │         │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────────┐  │ │         │
│  │  │  protocol  │  │    mllp    │  │      fhir      │  │ │         │
│  │  └─────┬──────┘  └─────┬──────┘  └────────┬───────┘  │ │         │
│  │        │               │                  │          │ │         │
│  │  ┌─────▼───────────────▼──────────────────▼───────┐  │ │         │
│  │  │                   router                        │  │ │         │
│  │  └────────────────────┬───────────────────────────┘  │ │         │
│  │                       │                               │ │         │
│  │  ┌────────────────────▼───────────────────────────┐  │ │         │
│  │  │                  mapping                        │  │ │         │
│  │  └────────────────────┬───────────────────────────┘  │ │         │
│  │                       │                               │ │         │
│  │  ┌──────────┬─────────▼─────────┬──────────────────┐ │ │         │
│  │  │  cache   │    pacs_adapter   │      queue       │ │ │         │
│  │  └──────────┘───────────────────┘──────────────────┘ │ │         │
│  └───────────────────────────────────────────────────────┘ │         │
│                                                            │         │
└────────────────────────────────────────────────────────────┘         │
                                                                       │
┌─────────────────────────────────────────────────────────────────────┐
│                     External Dependencies                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │
│  │ pacs_system │  │network_system│ │logger_system│  │ common_system│ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Related Documentation

- [Architecture Overview](architecture.md) - System architecture
- [Contributing Guidelines](contributing.md) - How to contribute
- [API Reference](../api/hl7-messages.md) - API specifications
