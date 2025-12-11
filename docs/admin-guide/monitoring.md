# Monitoring Guide

> **Version:** 0.1.0.0
> **Last Updated:** 2025-12-10

---

## Table of Contents

- [Overview](#overview)
- [Prometheus Metrics](#prometheus-metrics)
- [Grafana Dashboards](#grafana-dashboards)
- [Alerting Rules](#alerting-rules)
- [Log Aggregation](#log-aggregation)
- [Health Checks](#health-checks)

---

## Overview

PACS Bridge provides comprehensive monitoring capabilities through:

- **Prometheus Metrics** - Time-series metrics for all components
- **Health Endpoints** - Real-time health status
- **Structured Logging** - JSON-formatted logs for analysis
- **Grafana Dashboards** - Pre-built visualization dashboards

### Monitoring Architecture

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   PACS Bridge   │     │   Prometheus    │     │     Grafana     │
│   :8081/metrics │────►│   (Scraper)     │────►│   (Dashboard)   │
└─────────────────┘     └─────────────────┘     └─────────────────┘
         │                                               │
         │                                               │
         ▼                                               ▼
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Log Files     │────►│   Promtail/     │────►│      Loki       │
│                 │     │   Fluentd       │     │                 │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

---

## Prometheus Metrics

### Metrics Endpoint

The metrics endpoint is enabled by default:

```yaml
monitoring:
  health:
    port: 8081
    base_path: /health
    enable_metrics_endpoint: true
    metrics_path: /metrics
```

Access metrics:

```bash
curl http://localhost:8081/metrics
```

### Available Metrics

#### HL7 Message Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `hl7_messages_received_total` | Counter | `message_type` | Total HL7 messages received |
| `hl7_messages_sent_total` | Counter | `message_type` | Total HL7 messages sent |
| `hl7_message_processing_duration_seconds` | Histogram | `message_type` | Message processing latency |
| `hl7_message_errors_total` | Counter | `message_type`, `error_type` | Total message errors |
| `hl7_ack_sent_total` | Counter | `ack_code` | Total ACK messages sent (AA, AE, AR) |

#### MWL Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `mwl_entries_created_total` | Counter | - | Total MWL entries created |
| `mwl_entries_updated_total` | Counter | - | Total MWL entries updated |
| `mwl_entries_cancelled_total` | Counter | - | Total MWL entries cancelled |
| `mwl_query_duration_seconds` | Histogram | - | MWL query latency |
| `mwl_query_results_total` | Counter | - | Total MWL query results |

#### Queue Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `queue_depth` | Gauge | `destination` | Current queue depth |
| `queue_messages_enqueued_total` | Counter | `destination` | Total messages enqueued |
| `queue_messages_delivered_total` | Counter | `destination` | Total messages delivered |
| `queue_delivery_failures_total` | Counter | `destination` | Total delivery failures |
| `queue_dead_letters_total` | Counter | `destination` | Total dead letters |
| `queue_retry_total` | Counter | `destination` | Total retry attempts |

#### Connection Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `mllp_active_connections` | Gauge | - | Current active MLLP connections |
| `mllp_total_connections` | Counter | - | Total MLLP connections |
| `mllp_connection_errors_total` | Counter | `error_type` | Connection errors |
| `fhir_active_requests` | Gauge | - | Current active FHIR requests |
| `fhir_requests_total` | Counter | `method`, `resource` | Total FHIR requests |
| `pacs_connection_status` | Gauge | - | PACS connection status (1=up, 0=down) |

#### System Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `process_cpu_seconds_total` | Counter | - | Total CPU time in seconds |
| `process_resident_memory_bytes` | Gauge | - | Resident memory size |
| `process_virtual_memory_bytes` | Gauge | - | Virtual memory size |
| `process_open_fds` | Gauge | - | Number of open file descriptors |
| `process_start_time_seconds` | Gauge | - | Process start time |

#### Patient Cache Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `patient_cache_entries` | Gauge | - | Current cache entries |
| `patient_cache_hits_total` | Counter | - | Cache hits |
| `patient_cache_misses_total` | Counter | - | Cache misses |
| `patient_cache_evictions_total` | Counter | - | Cache evictions |

### Prometheus Configuration

Add PACS Bridge as a scrape target:

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'pacs_bridge'
    scrape_interval: 15s
    scrape_timeout: 10s
    static_configs:
      - targets:
          - 'pacs-bridge-1:8081'
          - 'pacs-bridge-2:8081'
          - 'pacs-bridge-3:8081'
    metrics_path: /metrics
    relabel_configs:
      - source_labels: [__address__]
        target_label: instance
        regex: '([^:]+):\d+'
        replacement: '${1}'
```

### Prometheus Recording Rules

Create recording rules for common queries:

```yaml
# prometheus_rules.yml
groups:
  - name: pacs_bridge_recording
    rules:
      # Message rate per second
      - record: pacs_bridge:hl7_messages_received:rate5m
        expr: rate(hl7_messages_received_total[5m])

      # Error rate percentage
      - record: pacs_bridge:error_rate:ratio
        expr: |
          sum(rate(hl7_message_errors_total[5m]))
          /
          sum(rate(hl7_messages_received_total[5m]))

      # P95 processing latency
      - record: pacs_bridge:processing_latency:p95
        expr: |
          histogram_quantile(0.95,
            rate(hl7_message_processing_duration_seconds_bucket[5m])
          )

      # Cache hit ratio
      - record: pacs_bridge:cache_hit_ratio
        expr: |
          sum(rate(patient_cache_hits_total[5m]))
          /
          (sum(rate(patient_cache_hits_total[5m])) +
           sum(rate(patient_cache_misses_total[5m])))
```

---

## Grafana Dashboards

### Importing the Dashboard

1. Navigate to **Dashboards** > **Import** in Grafana
2. Upload `docs/monitoring/grafana/pacs_bridge_dashboard.json`
3. Select your Prometheus datasource
4. Click **Import**

### Dashboard Panels

#### Overview Row

| Panel | Metrics | Description |
|-------|---------|-------------|
| Message Rate | `hl7_messages_received_total` | Messages per second |
| Active Connections | `mllp_active_connections` | Current connections |
| Queue Depth | `queue_depth` | Total queue depth |
| Error Rate | `hl7_message_errors_total` | Errors per second |
| P95 Latency | `hl7_message_processing_duration_seconds` | 95th percentile latency |
| Memory Usage | `process_resident_memory_bytes` | Memory in MB |

#### HL7 Messages Row

| Panel | Description |
|-------|-------------|
| Message Throughput | Messages received/sent over time |
| Message Types Distribution | Breakdown by message type |
| Processing Latency | Latency percentiles over time |
| Error Breakdown | Errors by type |

#### Queue Row

| Panel | Description |
|-------|-------------|
| Queue Depth | Queue depth over time |
| Queue Operations | Enqueue/dequeue rate |
| Delivery Success Rate | Successful deliveries vs failures |
| Dead Letters | Dead letter count over time |

#### MWL Row

| Panel | Description |
|-------|-------------|
| MWL Operations | Create/update/cancel rate |
| MWL Query Latency | Query response time |
| MWL Query Results | Results per query |

#### System Row

| Panel | Description |
|-------|-------------|
| CPU Usage | Process CPU utilization |
| Memory Usage | Resident memory over time |
| Open File Descriptors | FD count over time |

### Custom Dashboard Creation

Example PromQL queries for custom panels:

```promql
# Message rate by type
sum by (message_type) (rate(hl7_messages_received_total[5m]))

# Average processing time
avg(rate(hl7_message_processing_duration_seconds_sum[5m])
    / rate(hl7_message_processing_duration_seconds_count[5m]))

# Queue depth trend
avg_over_time(queue_depth[1h])

# Uptime
time() - process_start_time_seconds
```

---

## Alerting Rules

### Alert Configuration

Deploy alerting rules to Prometheus:

```bash
cp docs/monitoring/prometheus/alerts.yml /etc/prometheus/rules/pacs_bridge_alerts.yml
```

### Critical Alerts

| Alert | Condition | Severity | Description |
|-------|-----------|----------|-------------|
| `PACBridgeDown` | Instance unreachable | Critical | Service is not responding |
| `PACBridgeHighErrorRate` | Error rate > 5% | Critical | Too many processing errors |
| `PACBridgeQueueCritical` | Queue depth > 10000 | Critical | Queue is severely backed up |
| `PACBridgePACSDown` | PACS connection down | Critical | Cannot connect to PACS |
| `PACBridgeDeadLettersGrowing` | >10 dead letters in 15m | Critical | Messages failing permanently |

### Warning Alerts

| Alert | Condition | Severity | Description |
|-------|-----------|----------|-------------|
| `PACBridgeHighLatency` | P95 latency > 500ms | Warning | Processing latency elevated |
| `PACBridgeHighMemory` | Memory > 800MB | Warning | Memory usage high |
| `PACBridgeQueueWarning` | Queue depth > 1000 | Warning | Queue starting to back up |
| `PACBridgeNoMessages` | No messages in 10m | Warning | Not receiving messages |
| `PACBridgeCacheEvictions` | Evictions > 100/min | Warning | Cache under pressure |
| `PACBridgeConnectionDrop` | Lost >5 connections in 5m | Warning | Connections dropping |

### Alert Examples

```yaml
groups:
  - name: pacs_bridge
    rules:
      - alert: PACBridgeDown
        expr: up{job="pacs_bridge"} == 0
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "PACS Bridge instance {{ $labels.instance }} is down"
          description: "PACS Bridge has been unreachable for more than 1 minute."
          runbook_url: "https://docs/operations/runbook.md#service-down"

      - alert: PACBridgeHighErrorRate
        expr: |
          sum(rate(hl7_message_errors_total[5m]))
          / sum(rate(hl7_messages_received_total[5m])) > 0.05
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "PACS Bridge error rate is above 5%"
          description: "Error rate is {{ $value | humanizePercentage }}"
          runbook_url: "https://docs/operations/runbook.md#high-error-rate"

      - alert: PACBridgeHighLatency
        expr: |
          histogram_quantile(0.95,
            rate(hl7_message_processing_duration_seconds_bucket[5m])
          ) > 0.5
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "PACS Bridge P95 latency is above 500ms"
          description: "P95 latency is {{ $value | humanizeDuration }}"
```

### Alertmanager Configuration

```yaml
# alertmanager.yml
global:
  smtp_smarthost: 'smtp.hospital.local:587'
  smtp_from: 'alertmanager@hospital.local'

route:
  receiver: 'pacs-team'
  group_by: ['alertname', 'severity']
  group_wait: 30s
  group_interval: 5m
  repeat_interval: 4h
  routes:
    - match:
        severity: critical
      receiver: 'pacs-oncall'
      continue: true

receivers:
  - name: 'pacs-team'
    email_configs:
      - to: 'pacs-team@hospital.local'
        send_resolved: true

  - name: 'pacs-oncall'
    pagerduty_configs:
      - service_key: '<pagerduty-service-key>'
        severity: critical
    email_configs:
      - to: 'pacs-oncall@hospital.local'
        send_resolved: true
```

---

## Log Aggregation

### Log Format

PACS Bridge outputs JSON-formatted logs:

```json
{
  "timestamp": "2025-12-10T12:00:00.000Z",
  "level": "info",
  "logger": "hl7.gateway",
  "message": "Message processed",
  "message_type": "ADT^A01",
  "message_id": "MSG001",
  "source": "HIS",
  "processing_time_ms": 15,
  "trace_id": "abc123"
}
```

### Promtail Configuration

```yaml
# promtail.yml
server:
  http_listen_port: 9080
  grpc_listen_port: 0

positions:
  filename: /tmp/positions.yaml

clients:
  - url: http://loki:3100/loki/api/v1/push

scrape_configs:
  - job_name: pacs_bridge
    static_configs:
      - targets:
          - localhost
        labels:
          job: pacs_bridge
          __path__: /var/log/pacs_bridge/*.log
    pipeline_stages:
      - json:
          expressions:
            level: level
            message_type: message_type
            error_type: error_type
            processing_time: processing_time_ms
      - labels:
          level:
          message_type:
          error_type:
      - metrics:
          processing_time:
            type: Histogram
            description: "Processing time from logs"
            source: processing_time
            config:
              buckets: [10, 50, 100, 250, 500, 1000]
```

### Fluentd Configuration

```xml
# fluent.conf
<source>
  @type tail
  path /var/log/pacs_bridge/bridge.log
  pos_file /var/log/fluentd/pacs_bridge.log.pos
  tag pacs_bridge
  <parse>
    @type json
    time_key timestamp
    time_format %Y-%m-%dT%H:%M:%S.%LZ
  </parse>
</source>

<filter pacs_bridge>
  @type record_transformer
  <record>
    hostname ${hostname}
    environment production
  </record>
</filter>

<match pacs_bridge>
  @type elasticsearch
  host elasticsearch
  port 9200
  index_name pacs_bridge
  type_name _doc
  <buffer>
    @type file
    path /var/log/fluentd/buffer/pacs_bridge
    flush_interval 5s
  </buffer>
</match>
```

### Useful Log Queries

**Loki/LogQL:**

```logql
# Errors in last hour
{job="pacs_bridge"} |= "error" | json | level="error"

# Slow messages (>100ms)
{job="pacs_bridge"} | json | processing_time_ms > 100

# Messages by type
sum by (message_type) (count_over_time({job="pacs_bridge"} | json [1h]))

# Error rate over time
sum(rate({job="pacs_bridge"} | json | level="error" [5m]))
```

---

## Health Checks

### Health Endpoints

| Endpoint | Purpose | Response |
|----------|---------|----------|
| `GET /health` | Overall health | 200 if healthy |
| `GET /health/live` | Liveness probe | 200 if running |
| `GET /health/ready` | Readiness probe | 200 if ready |

### Health Response Format

```json
{
  "status": "healthy",
  "timestamp": "2025-12-10T12:00:00Z",
  "version": "1.0.0",
  "uptime_seconds": 86400,
  "components": {
    "hl7_listener": {
      "status": "up",
      "details": {
        "active_connections": 5,
        "max_connections": 100
      }
    },
    "pacs_connection": {
      "status": "up",
      "details": {
        "last_successful": "2025-12-10T11:59:55Z",
        "latency_ms": 15
      }
    },
    "patient_cache": {
      "status": "up",
      "details": {
        "entries": 5432,
        "hit_rate": 0.95
      }
    },
    "message_queue": {
      "status": "up",
      "details": {
        "depth": 23,
        "dead_letters": 0
      }
    }
  }
}
```

### Kubernetes Probes

```yaml
livenessProbe:
  httpGet:
    path: /health/live
    port: 8081
  initialDelaySeconds: 10
  periodSeconds: 10
  timeoutSeconds: 5
  failureThreshold: 3

readinessProbe:
  httpGet:
    path: /health/ready
    port: 8081
  initialDelaySeconds: 5
  periodSeconds: 10
  timeoutSeconds: 5
  failureThreshold: 3

startupProbe:
  httpGet:
    path: /health/live
    port: 8081
  initialDelaySeconds: 0
  periodSeconds: 5
  timeoutSeconds: 5
  failureThreshold: 30
```

---

## Related Documentation

- [Deployment Guide](deployment.md) - Deployment options
- [High Availability](high-availability.md) - HA configuration
- [Operations Runbook](../operations/runbook.md) - Operational procedures
- [Monitoring README](../monitoring/README.md) - Detailed metrics reference
