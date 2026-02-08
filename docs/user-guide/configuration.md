# Configuration Guide

> **Version:** 0.2.0.0
> **Last Updated:** 2026-02-08

---

## Table of Contents

- [Configuration File Format](#configuration-file-format)
- [Environment Variables](#environment-variables)
- [Server Configuration](#server-configuration)
- [HL7/MLLP Configuration](#hl7mllp-configuration)
- [FHIR Gateway Configuration](#fhir-gateway-configuration)
- [PACS Integration](#pacs-integration)
- [Mapping Configuration](#mapping-configuration)
- [Routing Rules](#routing-rules)
- [Message Queue](#message-queue)
- [Patient Cache](#patient-cache)
- [Logging](#logging)
- [Monitoring](#monitoring)
- [Distributed Tracing](#distributed-tracing)
- [Configuration Examples](#configuration-examples)

---

## Configuration File Format

PACS Bridge uses YAML format for configuration files. The default configuration file location is:

- Linux: `/etc/pacs_bridge/config.yaml`
- macOS: `/usr/local/etc/pacs_bridge/config.yaml`
- Windows: `C:\ProgramData\pacs_bridge\config.yaml`

### Configuration Loading Order

1. Default values (compiled into the application)
2. Configuration file specified by `--config` argument
3. Environment variables (override file settings)

---

## Environment Variables

Environment variables can be used to override configuration values. Use the `${VAR}` syntax in the configuration file:

| Syntax | Description |
|--------|-------------|
| `${VAR}` | Required variable (error if not set) |
| `${VAR:-default}` | Optional with default value |

### Common Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `PACS_BRIDGE_NAME` | Instance name | `PACS_BRIDGE` |
| `PACS_BRIDGE_MLLP_PORT` | MLLP listener port | `2575` |
| `PACS_HOST` | PACS server hostname | `localhost` |
| `PACS_PORT` | PACS server port | `11112` |
| `RIS_HOST` | RIS server hostname | `ris.hospital.local` |
| `RIS_PORT` | RIS server port | `2576` |
| `LOG_LEVEL` | Logging level | `info` |
| `PACS_BRIDGE_LOG_FILE` | Log file path | `/var/log/pacs_bridge/bridge.log` |
| `PACS_BRIDGE_QUEUE_DB` | Queue database path | `/var/lib/pacs_bridge/queue.db` |

---

## Server Configuration

```yaml
server:
  # Unique name for this PACS Bridge instance
  # Used in logging and message identification
  name: "${PACS_BRIDGE_NAME:-PACS_BRIDGE}"
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | No | `PACS_BRIDGE` | Instance identifier |

---

## HL7/MLLP Configuration

### Listener Configuration

```yaml
hl7:
  listener:
    port: 2575
    bind_address: "0.0.0.0"
    max_connections: 50
    idle_timeout: 300s
    max_message_size: 10485760
    tls:
      enabled: false
      cert_path: "/etc/pacs_bridge/certs/server.crt"
      key_path: "/etc/pacs_bridge/certs/server.key"
      ca_path: "/etc/pacs_bridge/certs/ca.crt"
      client_auth: optional
      min_version: tls_1_2
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `port` | integer | No | `2575` | MLLP listener port |
| `bind_address` | string | No | `0.0.0.0` | Network interface to bind |
| `max_connections` | integer | No | `50` | Maximum concurrent connections |
| `idle_timeout` | duration | No | `300s` | Connection idle timeout |
| `max_message_size` | integer | No | `10485760` | Maximum message size in bytes |

#### TLS Configuration

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `tls.enabled` | boolean | No | `false` | Enable TLS encryption |
| `tls.cert_path` | string | If TLS | - | Server certificate path |
| `tls.key_path` | string | If TLS | - | Private key path |
| `tls.ca_path` | string | No | - | CA certificate for client auth |
| `tls.client_auth` | string | No | `none` | `none`, `optional`, `required` |
| `tls.min_version` | string | No | `tls_1_2` | Minimum TLS version |

### Outbound Configuration

```yaml
hl7:
  outbound:
    - name: "RIS_PRIMARY"
      host: "ris.hospital.local"
      port: 2576
      message_types:
        - "ORM^O01"
        - "ORU^R01"
      priority: 1
      enabled: true
      retry_count: 3
      retry_delay: 1000
      tls:
        enabled: false
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Destination identifier |
| `host` | string | Yes | - | Remote hostname |
| `port` | integer | Yes | - | Remote port |
| `message_types` | list | No | All | Message types to route |
| `priority` | integer | No | `1` | Failover priority (lower = higher) |
| `enabled` | boolean | No | `true` | Enable this destination |
| `retry_count` | integer | No | `3` | Retry attempts on failure |
| `retry_delay` | integer | No | `1000` | Delay between retries (ms) |

---

## FHIR Gateway Configuration

```yaml
fhir:
  enabled: true
  server:
    port: 8080
    base_path: "/fhir/r4"
    max_connections: 100
    request_timeout: 60s
    page_size: 100
    tls:
      enabled: false
      cert_path: "/etc/pacs_bridge/certs/server.crt"
      key_path: "/etc/pacs_bridge/certs/server.key"
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `enabled` | boolean | No | `false` | Enable FHIR gateway |
| `server.port` | integer | No | `8080` | HTTP server port |
| `server.base_path` | string | No | `/fhir/r4` | FHIR endpoint base path |
| `server.max_connections` | integer | No | `100` | Maximum concurrent requests |
| `server.request_timeout` | duration | No | `60s` | Request timeout |
| `server.page_size` | integer | No | `100` | Default pagination size |

---

## PACS Integration

```yaml
pacs:
  host: "localhost"
  port: 11112
  ae_title: "PACS_BRIDGE"
  called_ae: "PACS_SCP"
  timeout: 30s
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `host` | string | Yes | - | PACS server hostname |
| `port` | integer | No | `11112` | DICOM port |
| `ae_title` | string | No | `PACS_BRIDGE` | Our AE title (SCU) |
| `called_ae` | string | No | `PACS_SCP` | PACS AE title (SCP) |
| `timeout` | duration | No | `30s` | Connection timeout |

---

## Mapping Configuration

### Modality AE Title Mapping

```yaml
mapping:
  modality_ae_titles:
    CT:
      - "CT_SCANNER_1"
      - "CT_SCANNER_2"
    MR:
      - "MR_SCANNER_1"
    XR:
      - "XRAY_ROOM_1"
      - "XRAY_ROOM_2"
    US:
      - "US_SCANNER_1"
```

### Procedure Code Mapping

```yaml
mapping:
  procedure_to_modality:
    "CT001": "CT"
    "CT002": "CT"
    "MR001": "MR"
    "XR001": "XR"
    "US001": "US"

  default_patient_id_issuer: "HOSPITAL_MRN"
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `modality_ae_titles` | map | No | - | Modality to AE title mapping |
| `procedure_to_modality` | map | No | - | Procedure code to modality |
| `default_patient_id_issuer` | string | No | `HOSPITAL_MRN` | Default issuer |

---

## Routing Rules

```yaml
routing_rules:
  - name: "ADT to Patient Cache"
    message_type_pattern: "ADT^A*"
    source_pattern: "HIS_*"
    destination: "patient_cache"
    priority: 10
    enabled: true

  - name: "New Orders to MWL"
    message_type_pattern: "ORM^O01"
    destination: "mwl_manager"
    priority: 20
    enabled: true
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Rule name for logging |
| `message_type_pattern` | string | Yes | - | Message type pattern (supports `*`) |
| `source_pattern` | string | No | - | Source facility pattern |
| `destination` | string | Yes | - | Handler destination |
| `priority` | integer | No | `0` | Rule evaluation order |
| `enabled` | boolean | No | `true` | Enable this rule |

### Built-in Destinations

| Destination | Purpose |
|-------------|---------|
| `patient_cache` | Store patient demographics |
| `mwl_manager` | Update modality worklist |
| `status_handler` | Process status updates |
| `patient_merge_handler` | Handle patient merges |

---

## Message Queue

```yaml
queue:
  database_path: "/var/lib/pacs_bridge/queue.db"
  max_queue_size: 50000
  max_retry_count: 5
  initial_retry_delay: 5s
  retry_backoff_multiplier: 2.0
  message_ttl: 24h
  worker_count: 4
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `database_path` | string | No | `/var/lib/pacs_bridge/queue.db` | SQLite database path |
| `max_queue_size` | integer | No | `50000` | Maximum queue depth |
| `max_retry_count` | integer | No | `5` | Retries before dead-letter |
| `initial_retry_delay` | duration | No | `5s` | Initial retry delay |
| `retry_backoff_multiplier` | float | No | `2.0` | Exponential backoff factor |
| `message_ttl` | duration | No | `24h` | Message time-to-live |
| `worker_count` | integer | No | `4` | Delivery worker threads |

---

## Patient Cache

```yaml
patient_cache:
  max_entries: 10000
  ttl: 3600s
  evict_on_full: true
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `max_entries` | integer | No | `10000` | Maximum cache entries |
| `ttl` | duration | No | `3600s` | Entry time-to-live |
| `evict_on_full` | boolean | No | `true` | Enable LRU eviction |

---

## Logging

```yaml
logging:
  level: "info"
  format: "json"
  file: "/var/log/pacs_bridge/bridge.log"
  max_file_size_mb: 100
  max_files: 5
  include_source_location: false
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `level` | string | No | `info` | Log level |
| `format` | string | No | `json` | Output format (`json`, `text`) |
| `file` | string | No | - | Log file path (empty = stdout) |
| `max_file_size_mb` | integer | No | `100` | Maximum file size |
| `max_files` | integer | No | `5` | Rotated files to keep |
| `include_source_location` | boolean | No | `false` | Include file:line info |

### Log Levels

| Level | Description |
|-------|-------------|
| `trace` | Most detailed logging |
| `debug` | Debugging information |
| `info` | Normal operational messages |
| `warn` | Warning conditions |
| `error` | Error conditions |
| `fatal` | Critical errors |

---

## Monitoring

```yaml
monitoring:
  health:
    port: 8081
    base_path: /health
    enable_metrics_endpoint: true
    metrics_path: /metrics
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `health.port` | integer | No | `8081` | Health server port |
| `health.base_path` | string | No | `/health` | Health endpoint path |
| `health.enable_metrics_endpoint` | boolean | No | `true` | Enable Prometheus metrics |
| `health.metrics_path` | string | No | `/metrics` | Metrics endpoint path |

---

## Distributed Tracing

PACS Bridge supports distributed tracing with W3C Trace Context standard. Traces can be exported to various backends including Jaeger, Zipkin, and OpenTelemetry collectors.

**See also:** [GitHub Issue #144](https://github.com/kcenon/pacs_bridge/issues/144)

```yaml
tracing:
  enabled: true
  service_name: "pacs_bridge"
  format: "otlp_grpc"
  endpoint: "http://jaeger:4317"
  sample_rate: 1.0

  batch:
    max_batch_size: 512
    max_export_delay: 5s
    max_queue_size: 2048
    retry_count: 3
    retry_delay: 1s

  propagation:
    hl7:
      enabled: true
      strategy: "z_segment"
      segment_name: "ZTR"
```

### General Settings

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `enabled` | boolean | No | `false` | Enable distributed tracing |
| `service_name` | string | No | `pacs_bridge` | Service name in traces |
| `format` | string | No | `otlp_grpc` | Export format (see below) |
| `endpoint` | string | If enabled | - | Trace backend endpoint |
| `sample_rate` | float | No | `1.0` | Sampling rate (0.0-1.0) |

### Export Formats

| Format | Description | Example Endpoint |
|--------|-------------|------------------|
| `jaeger_thrift` | Jaeger Thrift over HTTP | `http://jaeger:14268/api/traces` |
| `jaeger_grpc` | Jaeger gRPC (OTLP compatible) | `http://jaeger:14250` |
| `zipkin_json` | Zipkin JSON v2 format | `http://zipkin:9411/api/v2/spans` |
| `otlp_grpc` | OpenTelemetry Protocol gRPC | `http://collector:4317` |
| `otlp_http_json` | OTLP JSON over HTTP | `http://collector:4318/v1/traces` |

### Batch Export Settings

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `batch.max_batch_size` | integer | No | `512` | Maximum spans per batch |
| `batch.max_export_delay` | duration | No | `5s` | Maximum delay before export |
| `batch.max_queue_size` | integer | No | `2048` | Maximum pending spans |
| `batch.retry_count` | integer | No | `3` | Retry attempts on failure |
| `batch.retry_delay` | duration | No | `1s` | Delay between retries |

### HL7 Trace Propagation

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `propagation.hl7.enabled` | boolean | No | `false` | Enable HL7 trace propagation |
| `propagation.hl7.strategy` | string | No | `z_segment` | Propagation strategy |
| `propagation.hl7.segment_name` | string | No | `ZTR` | Custom Z-segment name |
| `propagation.hl7.msh_field_index` | integer | No | `25` | MSH field for custom field strategy |

### Propagation Strategies

| Strategy | Description |
|----------|-------------|
| `z_segment` | Use custom Z-segment (recommended) |
| `msh_control_id` | Append to MSH-10 Message Control ID |
| `msh_custom_field` | Use custom MSH field |

### Example: Jaeger Integration

```yaml
tracing:
  enabled: true
  service_name: "pacs_bridge_prod"
  format: "jaeger_thrift"
  endpoint: "http://jaeger.monitoring:14268/api/traces"
  sample_rate: 0.1  # Sample 10% in production

  batch:
    max_batch_size: 256
    max_export_delay: 10s
```

### Example: OpenTelemetry Collector

```yaml
tracing:
  enabled: true
  service_name: "pacs_bridge"
  format: "otlp_grpc"
  endpoint: "http://otel-collector:4317"

  propagation:
    hl7:
      enabled: true
      strategy: "z_segment"
```

---

## Configuration Examples

### Minimal Development Configuration

```yaml
server:
  name: "DEV_BRIDGE"

hl7:
  listener:
    port: 2575
    max_connections: 10

pacs:
  host: "localhost"
  port: 11112
  ae_title: "DEV_BRIDGE"
  called_ae: "PACS_SCP"

logging:
  level: "debug"
  format: "text"
```

### Production Configuration with TLS

See `examples/config/config-production.yaml` in the repository root for a complete production configuration example.

### High Availability Configuration

```yaml
server:
  name: "HA_BRIDGE_01"

hl7:
  listener:
    port: 2576
    max_connections: 200
    tls:
      enabled: true
      cert_path: "/etc/pacs_bridge/certs/server.crt"
      key_path: "/etc/pacs_bridge/certs/server.key"

  outbound:
    - name: "RIS_PRIMARY"
      host: "ris-1.hospital.local"
      port: 2576
      priority: 1
      retry_count: 5

    - name: "RIS_SECONDARY"
      host: "ris-2.hospital.local"
      port: 2576
      priority: 2
      retry_count: 5

queue:
  database_path: "/var/lib/pacs_bridge/queue.db"
  max_queue_size: 100000
  max_retry_count: 10
  worker_count: 8

patient_cache:
  max_entries: 50000
  ttl: 7200s
```

---

## Configuration Validation

PACS Bridge validates configuration on startup. Common validation errors:

| Error | Cause | Solution |
|-------|-------|----------|
| `Missing required field` | Required configuration not provided | Add the required field |
| `Invalid port number` | Port out of range (1-65535) | Use valid port number |
| `File not found` | Certificate or key file missing | Check file paths |
| `Invalid duration format` | Wrong duration syntax | Use format: `30s`, `5m`, `1h` |

### Validate Configuration

```bash
# Check configuration syntax
pacs_bridge --config /etc/pacs_bridge/config.yaml --validate
```

---

## Related Documentation

- [Getting Started](getting-started.md) - Quick start guide
- [Deployment Guide](../admin-guide/deployment.md) - Deployment options
- [Monitoring Guide](../admin-guide/monitoring.md) - Metrics and alerting
