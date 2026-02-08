# High Availability Guide

> **Version:** 0.2.0.0
> **Last Updated:** 2026-02-08

---

## Table of Contents

- [HA Architecture](#ha-architecture)
- [Load Balancing](#load-balancing)
- [Failover Configuration](#failover-configuration)
- [Disaster Recovery](#disaster-recovery)
- [Data Replication](#data-replication)
- [Health Monitoring](#health-monitoring)

---

## HA Architecture

### Overview

PACS Bridge supports high availability deployments to ensure continuous operation of critical healthcare workflows.

```
                         ┌─────────────────┐
                         │   Load Balancer │
                         │    (HAProxy)    │
                         └────────┬────────┘
                                  │
              ┌───────────────────┼───────────────────┐
              │                   │                   │
     ┌────────▼────────┐ ┌────────▼────────┐ ┌────────▼────────┐
     │  PACS Bridge 1  │ │  PACS Bridge 2  │ │  PACS Bridge 3  │
     │    (Active)     │ │    (Active)     │ │    (Active)     │
     └────────┬────────┘ └────────┬────────┘ └────────┬────────┘
              │                   │                   │
              └───────────────────┼───────────────────┘
                                  │
                         ┌────────▼────────┐
                         │   Shared Queue  │
                         │    (Redis)      │
                         └─────────────────┘
```

### Deployment Topologies

| Topology | Description | Use Case |
|----------|-------------|----------|
| Active-Passive | One active, one standby | Simple HA |
| Active-Active | Multiple active instances | High throughput |
| Multi-Region | Cross-datacenter | Disaster recovery |

---

## Load Balancing

### HAProxy Configuration

```
# /etc/haproxy/haproxy.cfg

global
    log /dev/log local0
    maxconn 4096
    user haproxy
    group haproxy
    daemon

defaults
    log     global
    mode    tcp
    option  tcplog
    option  dontlognull
    timeout connect 5000ms
    timeout client  50000ms
    timeout server  50000ms

# MLLP Load Balancing
frontend mllp_frontend
    bind *:2575
    mode tcp
    default_backend mllp_backend

backend mllp_backend
    mode tcp
    balance roundrobin
    option tcp-check

    # Health check: TCP connection
    server pacs_bridge_1 10.0.0.11:2575 check inter 5000 rise 2 fall 3
    server pacs_bridge_2 10.0.0.12:2575 check inter 5000 rise 2 fall 3
    server pacs_bridge_3 10.0.0.13:2575 check inter 5000 rise 2 fall 3

# FHIR API Load Balancing
frontend fhir_frontend
    bind *:8080
    mode http
    default_backend fhir_backend

backend fhir_backend
    mode http
    balance roundrobin
    option httpchk GET /health
    http-check expect status 200

    server pacs_bridge_1 10.0.0.11:8080 check inter 5000 rise 2 fall 3
    server pacs_bridge_2 10.0.0.12:8080 check inter 5000 rise 2 fall 3
    server pacs_bridge_3 10.0.0.13:8080 check inter 5000 rise 2 fall 3

# Health/Metrics Endpoint
frontend metrics_frontend
    bind *:8081
    mode http
    default_backend metrics_backend

backend metrics_backend
    mode http
    balance roundrobin
    option httpchk GET /health

    server pacs_bridge_1 10.0.0.11:8081 check inter 5000 rise 2 fall 3
    server pacs_bridge_2 10.0.0.12:8081 check inter 5000 rise 2 fall 3
    server pacs_bridge_3 10.0.0.13:8081 check inter 5000 rise 2 fall 3

# HAProxy Statistics
listen stats
    bind *:8404
    mode http
    stats enable
    stats uri /stats
    stats refresh 10s
```

### Load Balancing Strategies

| Strategy | Description | Best For |
|----------|-------------|----------|
| Round Robin | Equal distribution | Homogeneous instances |
| Least Connections | Route to least busy | Variable load |
| Source Hash | Same source to same server | Session affinity |
| Weighted | Based on server capacity | Mixed capacity |

### Session Affinity Considerations

For MLLP connections, session affinity is typically not required because:
- Each HL7 message is independent
- No session state between messages
- Patient cache is shared or synchronized

For FHIR API, session affinity may be needed if:
- Using server-side sessions
- Long-running transactions

---

## Failover Configuration

### Outbound Message Failover

Configure multiple outbound destinations with priorities:

```yaml
hl7:
  outbound:
    # Primary RIS
    - name: "RIS_PRIMARY"
      host: "ris-1.hospital.local"
      port: 2576
      priority: 1
      enabled: true
      retry_count: 3
      retry_delay: 1000

    # Secondary RIS (failover)
    - name: "RIS_SECONDARY"
      host: "ris-2.hospital.local"
      port: 2576
      priority: 2
      enabled: true
      retry_count: 3
      retry_delay: 1000

    # Tertiary RIS (failover)
    - name: "RIS_TERTIARY"
      host: "ris-3.hospital.local"
      port: 2576
      priority: 3
      enabled: true
      retry_count: 3
      retry_delay: 1000
```

### Failover Behavior

```
┌───────────────────────────────────────────────────────────┐
│                    Failover Flow                          │
└───────────────────────────────────────────────────────────┘

Message Send Attempt
        │
        ▼
┌───────────────┐
│  RIS Primary  │
│   Priority 1  │
└───────┬───────┘
        │
   Success? ────Yes──► Done
        │
       No
        │
        ▼
   Retry (up to retry_count)
        │
   Success? ────Yes──► Done
        │
       No
        │
        ▼
┌───────────────┐
│ RIS Secondary │
│   Priority 2  │
└───────┬───────┘
        │
   Success? ────Yes──► Done
        │
       No
        │
        ▼
   Retry (up to retry_count)
        │
   Success? ────Yes──► Done
        │
       No
        │
        ▼
┌───────────────┐
│  RIS Tertiary │
│   Priority 3  │
└───────┬───────┘
        │
   Success? ────Yes──► Done
        │
       No
        │
        ▼
    Queue for retry / Dead letter
```

### PACS Connection Failover

```yaml
pacs:
  primary:
    host: "pacs-1.hospital.local"
    port: 11112
    ae_title: "PACS_BRIDGE"
    called_ae: "PACS_SCP"
    timeout: 30s

  secondary:
    host: "pacs-2.hospital.local"
    port: 11112
    ae_title: "PACS_BRIDGE"
    called_ae: "PACS_SCP_DR"
    timeout: 30s

  failover:
    enabled: true
    check_interval: 30s
    failback_delay: 300s
```

---

## Disaster Recovery

### Recovery Point Objective (RPO)

| Component | RPO | Method |
|-----------|-----|--------|
| Configuration | 0 | Version control |
| Message Queue | Near-zero | Replication |
| Patient Cache | 1 hour | Periodic sync |
| Logs | 1 hour | Log shipping |

### Recovery Time Objective (RTO)

| Scenario | RTO | Method |
|----------|-----|--------|
| Single node failure | < 30 seconds | Auto-failover |
| Multiple node failure | < 5 minutes | Manual intervention |
| Data center failure | < 15 minutes | DR site activation |

### DR Site Configuration

```yaml
# Primary Site Configuration
server:
  name: "PACS_BRIDGE_PRIMARY"
  site: "DATACENTER_1"

# DR Site Configuration
server:
  name: "PACS_BRIDGE_DR"
  site: "DATACENTER_2"
  dr_mode: true
```

### Failover Procedures

#### Automatic Failover

1. Health check detects primary failure
2. Load balancer removes primary from pool
3. Traffic routes to remaining nodes
4. Alert sent to operations team

#### Manual DR Activation

```bash
# 1. Verify primary is down
curl http://primary:8081/health

# 2. Update DNS or load balancer
# Point PACS Bridge FQDN to DR site

# 3. Start DR instances
systemctl start pacs_bridge

# 4. Verify DR operation
curl http://dr-site:8081/health

# 5. Monitor for issues
tail -f /var/log/pacs_bridge/bridge.log
```

#### Failback Procedures

```bash
# 1. Verify primary is restored
curl http://primary:8081/health

# 2. Sync any pending data from DR
# (Queue replication, cache sync)

# 3. Update DNS or load balancer
# Point PACS Bridge FQDN back to primary

# 4. Monitor for issues
tail -f /var/log/pacs_bridge/bridge.log

# 5. Stop DR instances (optional)
systemctl stop pacs_bridge
```

---

## Data Replication

### Message Queue Replication

For shared queue across instances, use Redis:

```yaml
queue:
  type: redis
  redis:
    host: "redis-cluster.hospital.local"
    port: 6379
    password: "${REDIS_PASSWORD}"
    db: 0
    cluster: true
    sentinel:
      enabled: true
      master: "pacs-bridge-master"
      nodes:
        - "redis-sentinel-1:26379"
        - "redis-sentinel-2:26379"
        - "redis-sentinel-3:26379"
```

### Patient Cache Synchronization

Options for cache synchronization:

1. **Shared Cache (Redis)**
   ```yaml
   patient_cache:
     type: redis
     redis:
       host: "redis-cache.hospital.local"
       port: 6379
   ```

2. **Cache-Aside with TTL**
   - Each instance maintains local cache
   - Short TTL ensures consistency
   - Cache miss queries source system

3. **Event-Driven Sync**
   - Publish cache updates to message bus
   - All instances subscribe and update

### Database Replication (SQLite Queue)

For SQLite-based queue persistence:

```bash
# Primary: Enable WAL mode for better concurrency
sqlite3 /var/lib/pacs_bridge/queue.db "PRAGMA journal_mode=WAL;"

# Backup: Use litestream for continuous replication
litestream replicate /var/lib/pacs_bridge/queue.db s3://backup-bucket/queue.db
```

---

## Health Monitoring

### Health Check Endpoints

| Endpoint | Purpose | Check Interval |
|----------|---------|----------------|
| `/health` | Overall health | 10-30 seconds |
| `/health/live` | Liveness (is running) | 5-10 seconds |
| `/health/ready` | Readiness (can serve) | 10-30 seconds |

### Component Health Checks

```json
{
  "status": "healthy",
  "timestamp": "2025-12-10T12:00:00Z",
  "components": {
    "hl7_listener": {
      "status": "up",
      "active_connections": 5,
      "max_connections": 100
    },
    "pacs_connection": {
      "status": "up",
      "last_successful": "2025-12-10T11:59:55Z",
      "latency_ms": 15
    },
    "patient_cache": {
      "status": "up",
      "entries": 5432,
      "hit_rate": 0.95
    },
    "message_queue": {
      "status": "up",
      "depth": 23,
      "dead_letters": 0
    }
  }
}
```

### Alerting Rules

```yaml
# Prometheus alerting rules
groups:
  - name: pacs_bridge_ha
    rules:
      # Too few instances running
      - alert: PACBridgeInsufficientReplicas
        expr: count(up{job="pacs_bridge"}) < 2
        for: 2m
        labels:
          severity: critical
        annotations:
          summary: "PACS Bridge has fewer than 2 running instances"

      # Instance down
      - alert: PACBridgeInstanceDown
        expr: up{job="pacs_bridge"} == 0
        for: 1m
        labels:
          severity: warning
        annotations:
          summary: "PACS Bridge instance {{ $labels.instance }} is down"

      # Queue depth imbalance
      - alert: PACBridgeQueueImbalance
        expr: |
          max(queue_depth) / min(queue_depth) > 2
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Queue depth imbalance between instances"
```

### Monitoring Dashboard

Key metrics to monitor for HA:

| Metric | Description | Alert Threshold |
|--------|-------------|-----------------|
| Instance count | Running instances | < 2 |
| Health status | Component health | Any unhealthy |
| Failover count | Failovers per hour | > 5 |
| Queue depth variance | Difference across instances | > 2x |
| Latency variance | P99 difference across instances | > 100ms |

---

## Best Practices

### Deployment

1. Deploy minimum 2 instances for HA
2. Spread across availability zones
3. Use health checks at all layers
4. Test failover regularly

### Configuration

1. Use identical configuration across instances
2. Externalize secrets
3. Version control all configs
4. Document all customizations

### Operations

1. Monitor all instances
2. Set up alerting for failures
3. Document failover procedures
4. Practice DR drills quarterly

---

## Related Documentation

- [Deployment Guide](deployment.md) - Deployment options
- [Monitoring Guide](monitoring.md) - Monitoring setup
- [Backup and Recovery](backup-recovery.md) - Data protection
- [Operations Runbook](../operations/runbook.md) - Operational procedures
