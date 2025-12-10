# Operations Runbook

> **Version:** 1.0.0
> **Last Updated:** 2025-12-10

---

## Table of Contents

- [Overview](#overview)
- [Start/Stop Procedures](#startstop-procedures)
- [Health Check Interpretation](#health-check-interpretation)
- [Common Issues and Solutions](#common-issues-and-solutions)
- [Escalation Procedures](#escalation-procedures)
- [Emergency Procedures](#emergency-procedures)
- [Maintenance Windows](#maintenance-windows)
- [Performance Troubleshooting](#performance-troubleshooting)

---

## Overview

This runbook provides operational procedures for PACS Bridge administrators and operators.

### Service Information

| Item | Value |
|------|-------|
| Service Name | PACS Bridge |
| Service Owner | Healthcare IT Team |
| Critical | Yes (Patient Safety) |
| SLA | 99.9% uptime |
| Support Hours | 24/7 |

### Contact Information

| Role | Contact |
|------|---------|
| Primary On-Call | pacs-oncall@hospital.local |
| Secondary On-Call | healthcare-it@hospital.local |
| Management | it-management@hospital.local |
| Vendor Support | https://github.com/kcenon/pacs_bridge/issues |

### Severity Levels

| Level | Description | Response Time | Escalation |
|-------|-------------|---------------|------------|
| P1 | Service down, patient impact | 15 minutes | Immediate |
| P2 | Degraded service | 1 hour | 30 minutes |
| P3 | Minor issue | 4 hours | 2 hours |
| P4 | Low priority | 24 hours | 8 hours |

---

## Start/Stop Procedures

### Starting the Service

#### Systemd (Linux)

```bash
# Check current status
sudo systemctl status pacs_bridge

# Start the service
sudo systemctl start pacs_bridge

# Verify startup
sudo systemctl status pacs_bridge
journalctl -u pacs_bridge -f

# Check health endpoint
curl http://localhost:8081/health
```

#### Docker

```bash
# Check current status
docker ps | grep pacs_bridge

# Start the container
docker start pacs_bridge

# Verify startup
docker logs -f pacs_bridge

# Check health endpoint
curl http://localhost:8081/health
```

#### Docker Compose

```bash
# Start services
docker-compose up -d

# Verify startup
docker-compose logs -f pacs_bridge

# Check health
curl http://localhost:8081/health
```

### Stopping the Service

#### Graceful Shutdown (Preferred)

```bash
# Systemd
sudo systemctl stop pacs_bridge

# Docker
docker stop pacs_bridge

# Docker Compose
docker-compose stop pacs_bridge
```

#### Force Stop (Emergency Only)

```bash
# Systemd
sudo systemctl kill pacs_bridge

# Docker
docker kill pacs_bridge

# Process
kill -9 $(pgrep pacs_bridge)
```

### Restarting the Service

```bash
# Systemd
sudo systemctl restart pacs_bridge

# Docker
docker restart pacs_bridge

# Docker Compose
docker-compose restart pacs_bridge
```

### Configuration Reload

```bash
# Systemd (if supported)
sudo systemctl reload pacs_bridge

# Or restart with new config
sudo systemctl restart pacs_bridge
```

---

## Health Check Interpretation

### Health Endpoint Response

```bash
curl http://localhost:8081/health | jq
```

### Status Interpretation

| Status | Meaning | Action |
|--------|---------|--------|
| `healthy` | All components operational | None |
| `degraded` | Some components impaired | Investigate |
| `unhealthy` | Critical component down | Immediate action |

### Component Health

| Component | Status | Meaning |
|-----------|--------|---------|
| `hl7_listener` | `up` | Accepting connections |
| `hl7_listener` | `down` | Port not listening |
| `pacs_connection` | `up` | Connected to PACS |
| `pacs_connection` | `down` | Cannot reach PACS |
| `patient_cache` | `up` | Cache operational |
| `patient_cache` | `degraded` | High eviction rate |
| `message_queue` | `up` | Queue operational |
| `message_queue` | `degraded` | Queue backing up |
| `message_queue` | `down` | Database error |

### Health Check Response Example

```json
{
  "status": "degraded",
  "timestamp": "2025-12-10T12:00:00Z",
  "components": {
    "hl7_listener": {
      "status": "up",
      "details": {
        "active_connections": 45,
        "max_connections": 100
      }
    },
    "pacs_connection": {
      "status": "up",
      "details": {
        "latency_ms": 15
      }
    },
    "patient_cache": {
      "status": "up",
      "details": {
        "entries": 8500,
        "hit_rate": 0.92
      }
    },
    "message_queue": {
      "status": "degraded",
      "details": {
        "depth": 1500,
        "threshold": 1000
      }
    }
  }
}
```

### Key Metrics to Monitor

```bash
# Get current metrics
curl http://localhost:8081/metrics
```

| Metric | Warning | Critical |
|--------|---------|----------|
| Queue Depth | > 1000 | > 10000 |
| Error Rate | > 1% | > 5% |
| P95 Latency | > 500ms | > 2000ms |
| Connections | > 80% max | > 95% max |
| Memory | > 800MB | > 1GB |
| CPU | > 70% | > 90% |

---

## Common Issues and Solutions

### Issue: Service Won't Start

**Symptoms:** Service fails to start, exits immediately

**Diagnosis:**
```bash
# Check logs
journalctl -u pacs_bridge --since "5 minutes ago"

# Check configuration
pacs_bridge --config /etc/pacs_bridge/config.yaml --validate
```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Config syntax error | Fix YAML syntax |
| Port in use | Change port or stop conflicting service |
| Permission denied | Fix file permissions |
| Missing dependency | Install required libraries |

---

### Issue: PACS Connection Failed

**Symptoms:** Health shows `pacs_connection: down`

**Diagnosis:**
```bash
# Test network connectivity
ping pacs.hospital.local
telnet pacs.hospital.local 11112

# Check PACS status
# (Hospital-specific command)
```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Network issue | Check network connectivity |
| PACS down | Contact PACS admin |
| Firewall | Check firewall rules |
| Wrong AE title | Verify AE title configuration |

**Escalation:** If PACS is down, escalate to PACS admin team.

---

### Issue: High Queue Depth

**Symptoms:** Queue depth exceeds threshold

**Diagnosis:**
```bash
# Check queue metrics
curl http://localhost:8081/metrics | grep queue

# Check destination availability
telnet ris.hospital.local 2576
```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Destination down | Check RIS availability |
| Network issue | Check network to destination |
| Slow consumer | Increase workers or investigate RIS |
| High volume | Scale horizontally |

**Immediate Actions:**
1. Check destination availability
2. Increase worker count if possible
3. Monitor dead letter queue

---

### Issue: High Error Rate

**Symptoms:** Error rate exceeds threshold

**Diagnosis:**
```bash
# Check error logs
grep '"level":"error"' /var/log/pacs_bridge/bridge.log | tail -50

# Check error metrics
curl http://localhost:8081/metrics | grep error
```

**Solutions:**

| Error Type | Solution |
|------------|----------|
| Parse errors | Check sender message format |
| Connection errors | Check network connectivity |
| Mapping errors | Verify mapping configuration |
| Timeout errors | Increase timeouts |

---

### Issue: High Memory Usage

**Symptoms:** Memory exceeds threshold

**Diagnosis:**
```bash
# Check memory usage
ps aux | grep pacs_bridge
curl http://localhost:8081/metrics | grep memory
```

**Solutions:**

| Cause | Solution |
|-------|----------|
| Large cache | Reduce cache size |
| Memory leak | Report bug, restart as workaround |
| High volume | Scale horizontally |

**Immediate Action:** If critical, restart service to reclaim memory.

---

### Issue: High Latency

**Symptoms:** P95 latency exceeds threshold

**Diagnosis:**
```bash
# Check latency metrics
curl http://localhost:8081/metrics | grep duration

# Check system resources
top -p $(pgrep pacs_bridge)
```

**Solutions:**

| Cause | Solution |
|-------|----------|
| CPU bottleneck | Scale horizontally |
| Slow PACS | Check PACS performance |
| Slow destination | Check RIS performance |
| Log level too verbose | Reduce log level |

---

## Escalation Procedures

### When to Escalate

| Condition | Escalate To |
|-----------|-------------|
| Service down > 5 minutes | On-call manager |
| Patient workflow impacted | On-call manager |
| Data loss suspected | On-call manager + Security |
| Security incident | Security team |
| Unable to diagnose | Vendor support |

### Escalation Steps

1. **Gather Information**
   ```bash
   # Collect diagnostics
   curl http://localhost:8081/health > health.json
   curl http://localhost:8081/metrics > metrics.txt
   tail -500 /var/log/pacs_bridge/bridge.log > logs.txt
   ```

2. **Document Timeline**
   - When issue started
   - What changed before issue
   - Actions taken
   - Current status

3. **Contact Escalation**
   - Call on-call phone
   - Send email with diagnostics
   - Update incident ticket

4. **Handoff Information**
   - Issue summary
   - Actions taken
   - Current status
   - Outstanding tasks

---

## Emergency Procedures

### Service Outage

**Priority:** P1

**Steps:**

1. **Assess Impact**
   ```bash
   curl http://localhost:8081/health
   ```

2. **Check Logs**
   ```bash
   journalctl -u pacs_bridge --since "10 minutes ago"
   ```

3. **Attempt Restart**
   ```bash
   sudo systemctl restart pacs_bridge
   ```

4. **If Restart Fails**
   - Check configuration
   - Check dependencies
   - Activate DR site if available

5. **Escalate**
   - Notify on-call manager
   - Open incident ticket
   - Update status page

### Data Corruption

**Priority:** P1

**Steps:**

1. **Stop Service**
   ```bash
   sudo systemctl stop pacs_bridge
   ```

2. **Preserve Evidence**
   ```bash
   cp /var/lib/pacs_bridge/queue.db /backup/queue.db.corrupted
   cp /var/log/pacs_bridge/bridge.log /backup/bridge.log.corrupted
   ```

3. **Restore from Backup**
   ```bash
   cp /backup/queue.db.latest /var/lib/pacs_bridge/queue.db
   ```

4. **Restart Service**
   ```bash
   sudo systemctl start pacs_bridge
   ```

5. **Notify**
   - Escalate to security if needed
   - Document data loss

### Security Incident

**Priority:** P1

**Steps:**

1. **Isolate System** (if instructed)
   ```bash
   # Disable network (extreme measure)
   # Only if instructed by security team
   ```

2. **Preserve Evidence**
   ```bash
   # Capture logs
   tar -czf /tmp/incident_$(date +%Y%m%d_%H%M%S).tar.gz \
     /var/log/pacs_bridge/ \
     /etc/pacs_bridge/
   ```

3. **Contact Security Team**
   - security@hospital.local
   - Security hotline

4. **Follow Security Team Instructions**

---

## Maintenance Windows

### Planned Maintenance

**Pre-Maintenance:**

1. Notify stakeholders (48 hours advance)
2. Schedule maintenance window
3. Prepare rollback plan
4. Test in staging if applicable

**During Maintenance:**

1. Announce start of maintenance
2. Perform maintenance tasks
3. Verify service health
4. Test functionality
5. Announce end of maintenance

**Post-Maintenance:**

1. Monitor for issues
2. Document changes
3. Close maintenance ticket

### Maintenance Checklist

```markdown
## Pre-Maintenance
- [ ] Stakeholders notified
- [ ] Maintenance window approved
- [ ] Rollback plan documented
- [ ] Backups verified
- [ ] Staging tested (if applicable)

## During Maintenance
- [ ] Maintenance started (notify)
- [ ] Service stopped (if required)
- [ ] Changes applied
- [ ] Service started
- [ ] Health verified
- [ ] Functionality tested
- [ ] Maintenance completed (notify)

## Post-Maintenance
- [ ] Monitoring active
- [ ] No issues for 30 minutes
- [ ] Changes documented
- [ ] Ticket closed
```

---

## Performance Troubleshooting

### CPU Troubleshooting

```bash
# Check CPU usage
top -p $(pgrep pacs_bridge)

# Check thread usage
ps -T -p $(pgrep pacs_bridge)

# Profile (if enabled)
perf record -p $(pgrep pacs_bridge) -g -- sleep 30
perf report
```

### Memory Troubleshooting

```bash
# Check memory usage
ps aux | grep pacs_bridge

# Check memory details
cat /proc/$(pgrep pacs_bridge)/status | grep -i mem

# Check for memory growth
watch -n 5 'ps aux | grep pacs_bridge'
```

### Network Troubleshooting

```bash
# Check connections
netstat -an | grep ESTABLISHED | wc -l

# Check connection states
netstat -an | grep 2575

# Capture traffic (if needed)
tcpdump -i any port 2575 -w capture.pcap
```

### Database Troubleshooting

```bash
# Check queue database
sqlite3 /var/lib/pacs_bridge/queue.db "SELECT COUNT(*) FROM messages;"

# Check database size
du -h /var/lib/pacs_bridge/queue.db

# Check integrity
sqlite3 /var/lib/pacs_bridge/queue.db "PRAGMA integrity_check;"
```

---

## Quick Reference

### Common Commands

```bash
# Start
sudo systemctl start pacs_bridge

# Stop
sudo systemctl stop pacs_bridge

# Restart
sudo systemctl restart pacs_bridge

# Status
sudo systemctl status pacs_bridge

# Logs
journalctl -u pacs_bridge -f

# Health
curl http://localhost:8081/health

# Metrics
curl http://localhost:8081/metrics
```

### Important Paths

| Path | Purpose |
|------|---------|
| `/etc/pacs_bridge/config.yaml` | Configuration |
| `/etc/pacs_bridge/certs/` | TLS certificates |
| `/var/lib/pacs_bridge/queue.db` | Queue database |
| `/var/log/pacs_bridge/bridge.log` | Application logs |

### Important Ports

| Port | Service |
|------|---------|
| 2575 | MLLP (HL7) |
| 2576 | MLLPS (HL7 TLS) |
| 8080 | FHIR REST API |
| 8081 | Health/Metrics |

---

## Related Documentation

- [Deployment Guide](../admin-guide/deployment.md) - Deployment procedures
- [Monitoring Guide](../admin-guide/monitoring.md) - Monitoring setup
- [Backup and Recovery](../admin-guide/backup-recovery.md) - Backup procedures
- [Troubleshooting Guide](../user-guide/troubleshooting.md) - Common issues
