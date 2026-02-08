# Backup and Recovery Guide

> **Version:** 0.2.0.0
> **Last Updated:** 2026-02-08

---

## Table of Contents

- [Backup Overview](#backup-overview)
- [Backup Procedures](#backup-procedures)
- [Recovery Procedures](#recovery-procedures)
- [Data Retention Policies](#data-retention-policies)
- [Testing Backups](#testing-backups)

---

## Backup Overview

### Components to Backup

| Component | Location | Priority | Frequency |
|-----------|----------|----------|-----------|
| Configuration | `/etc/pacs_bridge/` | Critical | On change |
| TLS Certificates | `/etc/pacs_bridge/certs/` | Critical | On change |
| Message Queue DB | `/var/lib/pacs_bridge/queue.db` | High | Continuous |
| Logs | `/var/log/pacs_bridge/` | Medium | Daily |

### Backup Strategy

```
┌────────────────────────────────────────────────────────────────┐
│                      Backup Strategy                            │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Configuration          Queue Database          Logs           │
│       │                      │                   │              │
│       ▼                      ▼                   ▼              │
│  ┌─────────┐           ┌─────────┐         ┌─────────┐         │
│  │  Git    │           │Litestream│        │Logrotate│         │
│  │ Version │           │Replication│       │  + Ship │         │
│  │ Control │           │          │        │         │         │
│  └────┬────┘           └────┬────┘         └────┬────┘         │
│       │                     │                   │               │
│       ▼                     ▼                   ▼               │
│  ┌──────────────────────────────────────────────────┐          │
│  │              Object Storage (S3/GCS)             │          │
│  └──────────────────────────────────────────────────┘          │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## Backup Procedures

### Configuration Backup

#### Git-Based Backup (Recommended)

```bash
# Initialize git repository for configuration
cd /etc/pacs_bridge
git init
git add .
git commit -m "Initial configuration"

# Add remote for backup
git remote add backup git@backup-server:pacs_bridge_config.git

# Push configuration changes
git push backup main
```

#### Manual Backup

```bash
#!/bin/bash
# backup_config.sh

BACKUP_DIR="/backup/pacs_bridge/config"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Create backup directory
mkdir -p "${BACKUP_DIR}"

# Backup configuration (excluding sensitive files)
tar -czf "${BACKUP_DIR}/config_${TIMESTAMP}.tar.gz" \
    --exclude='*.key' \
    -C /etc pacs_bridge

# Keep only last 30 backups
find "${BACKUP_DIR}" -name "config_*.tar.gz" -mtime +30 -delete

echo "Configuration backed up: ${BACKUP_DIR}/config_${TIMESTAMP}.tar.gz"
```

### TLS Certificate Backup

```bash
#!/bin/bash
# backup_certs.sh

BACKUP_DIR="/backup/pacs_bridge/certs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
ENCRYPTION_KEY="/root/.backup_key"

# Create backup directory
mkdir -p "${BACKUP_DIR}"

# Backup certificates with encryption
tar -cf - -C /etc/pacs_bridge certs | \
    openssl enc -aes-256-cbc -salt -pbkdf2 \
    -pass file:${ENCRYPTION_KEY} \
    -out "${BACKUP_DIR}/certs_${TIMESTAMP}.tar.enc"

# Verify backup
openssl enc -d -aes-256-cbc -pbkdf2 \
    -pass file:${ENCRYPTION_KEY} \
    -in "${BACKUP_DIR}/certs_${TIMESTAMP}.tar.enc" | tar -t > /dev/null

if [ $? -eq 0 ]; then
    echo "Certificate backup verified: ${BACKUP_DIR}/certs_${TIMESTAMP}.tar.enc"
else
    echo "ERROR: Certificate backup verification failed!"
    exit 1
fi

# Keep only last 10 backups
find "${BACKUP_DIR}" -name "certs_*.tar.enc" -mtime +90 -delete
```

### Message Queue Database Backup

#### Continuous Replication with Litestream

Install and configure Litestream for continuous SQLite replication:

```bash
# Install litestream
wget https://github.com/benbjohnson/litestream/releases/latest/download/litestream-linux-amd64.tar.gz
tar -xzf litestream-linux-amd64.tar.gz
sudo mv litestream /usr/local/bin/
```

Configure `/etc/litestream.yml`:

```yaml
dbs:
  - path: /var/lib/pacs_bridge/queue.db
    replicas:
      - type: s3
        bucket: pacs-bridge-backup
        path: queue
        region: us-east-1
        sync-interval: 1s
        snapshot-interval: 1h
        retention: 168h  # 7 days
```

Start as a service:

```bash
# /etc/systemd/system/litestream.service
[Unit]
Description=Litestream SQLite Replication
After=network.target

[Service]
Type=simple
User=pacs_bridge
ExecStart=/usr/local/bin/litestream replicate -config /etc/litestream.yml
Restart=always

[Install]
WantedBy=multi-user.target
```

#### Manual Backup

```bash
#!/bin/bash
# backup_queue.sh

BACKUP_DIR="/backup/pacs_bridge/queue"
DB_PATH="/var/lib/pacs_bridge/queue.db"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Create backup directory
mkdir -p "${BACKUP_DIR}"

# Use SQLite backup command (safe while database is in use)
sqlite3 "${DB_PATH}" ".backup '${BACKUP_DIR}/queue_${TIMESTAMP}.db'"

# Compress backup
gzip "${BACKUP_DIR}/queue_${TIMESTAMP}.db"

# Verify backup
gunzip -c "${BACKUP_DIR}/queue_${TIMESTAMP}.db.gz" | sqlite3 /dev/stdin "SELECT COUNT(*) FROM messages;" > /dev/null

if [ $? -eq 0 ]; then
    echo "Queue backup verified: ${BACKUP_DIR}/queue_${TIMESTAMP}.db.gz"
else
    echo "ERROR: Queue backup verification failed!"
    exit 1
fi

# Keep only last 7 days
find "${BACKUP_DIR}" -name "queue_*.db.gz" -mtime +7 -delete
```

### Log Backup

```bash
#!/bin/bash
# backup_logs.sh

BACKUP_DIR="/backup/pacs_bridge/logs"
LOG_DIR="/var/log/pacs_bridge"
TIMESTAMP=$(date +%Y%m%d)

# Create backup directory
mkdir -p "${BACKUP_DIR}"

# Compress and backup logs older than 1 day
find "${LOG_DIR}" -name "*.log.*" -mtime +1 -exec gzip {} \;
find "${LOG_DIR}" -name "*.gz" -exec mv {} "${BACKUP_DIR}/" \;

echo "Logs backed up to: ${BACKUP_DIR}"

# Keep only last 90 days
find "${BACKUP_DIR}" -name "*.gz" -mtime +90 -delete
```

### Automated Backup Schedule

Create cron jobs:

```bash
# /etc/cron.d/pacs_bridge_backup

# Configuration backup (on change detection or daily)
0 2 * * * root /usr/local/bin/backup_config.sh

# Certificate backup (weekly)
0 3 * * 0 root /usr/local/bin/backup_certs.sh

# Queue backup (hourly, if not using litestream)
0 * * * * root /usr/local/bin/backup_queue.sh

# Log backup (daily)
0 4 * * * root /usr/local/bin/backup_logs.sh
```

---

## Recovery Procedures

### Configuration Recovery

```bash
#!/bin/bash
# restore_config.sh

BACKUP_FILE="$1"

if [ -z "$BACKUP_FILE" ]; then
    echo "Usage: restore_config.sh <backup_file>"
    exit 1
fi

# Stop service
systemctl stop pacs_bridge

# Backup current configuration
mv /etc/pacs_bridge /etc/pacs_bridge.old

# Restore from backup
mkdir -p /etc/pacs_bridge
tar -xzf "${BACKUP_FILE}" -C /etc

# Set permissions
chown -R pacs_bridge:pacs_bridge /etc/pacs_bridge
chmod 600 /etc/pacs_bridge/config.yaml

# Validate configuration
/opt/pacs_bridge/bin/pacs_bridge --config /etc/pacs_bridge/config.yaml --validate

if [ $? -eq 0 ]; then
    # Start service
    systemctl start pacs_bridge
    echo "Configuration restored and service started"
else
    # Rollback
    rm -rf /etc/pacs_bridge
    mv /etc/pacs_bridge.old /etc/pacs_bridge
    systemctl start pacs_bridge
    echo "ERROR: Configuration validation failed, rolled back"
    exit 1
fi
```

### TLS Certificate Recovery

```bash
#!/bin/bash
# restore_certs.sh

BACKUP_FILE="$1"
ENCRYPTION_KEY="/root/.backup_key"

if [ -z "$BACKUP_FILE" ]; then
    echo "Usage: restore_certs.sh <backup_file>"
    exit 1
fi

# Stop service
systemctl stop pacs_bridge

# Backup current certificates
mv /etc/pacs_bridge/certs /etc/pacs_bridge/certs.old

# Restore from backup
mkdir -p /etc/pacs_bridge/certs
openssl enc -d -aes-256-cbc -pbkdf2 \
    -pass file:${ENCRYPTION_KEY} \
    -in "${BACKUP_FILE}" | tar -xf - -C /etc/pacs_bridge

# Set permissions
chown -R pacs_bridge:pacs_bridge /etc/pacs_bridge/certs
chmod 600 /etc/pacs_bridge/certs/*.key
chmod 644 /etc/pacs_bridge/certs/*.crt

# Verify certificates
openssl x509 -in /etc/pacs_bridge/certs/server.crt -noout -dates

if [ $? -eq 0 ]; then
    # Start service
    systemctl start pacs_bridge
    rm -rf /etc/pacs_bridge/certs.old
    echo "Certificates restored and service started"
else
    # Rollback
    rm -rf /etc/pacs_bridge/certs
    mv /etc/pacs_bridge/certs.old /etc/pacs_bridge/certs
    systemctl start pacs_bridge
    echo "ERROR: Certificate verification failed, rolled back"
    exit 1
fi
```

### Message Queue Recovery

#### From Litestream

```bash
#!/bin/bash
# restore_queue_litestream.sh

BUCKET="pacs-bridge-backup"
DB_PATH="/var/lib/pacs_bridge/queue.db"

# Stop service
systemctl stop pacs_bridge

# Backup current database
mv "${DB_PATH}" "${DB_PATH}.old"

# Restore from litestream
litestream restore -o "${DB_PATH}" "s3://${BUCKET}/queue"

# Set permissions
chown pacs_bridge:pacs_bridge "${DB_PATH}"

# Verify database
sqlite3 "${DB_PATH}" "PRAGMA integrity_check;"

if [ $? -eq 0 ]; then
    systemctl start pacs_bridge
    rm -f "${DB_PATH}.old"
    echo "Queue database restored from litestream"
else
    mv "${DB_PATH}.old" "${DB_PATH}"
    systemctl start pacs_bridge
    echo "ERROR: Database integrity check failed, rolled back"
    exit 1
fi
```

#### From Manual Backup

```bash
#!/bin/bash
# restore_queue.sh

BACKUP_FILE="$1"
DB_PATH="/var/lib/pacs_bridge/queue.db"

if [ -z "$BACKUP_FILE" ]; then
    echo "Usage: restore_queue.sh <backup_file>"
    exit 1
fi

# Stop service
systemctl stop pacs_bridge

# Backup current database
mv "${DB_PATH}" "${DB_PATH}.old"

# Restore from backup
gunzip -c "${BACKUP_FILE}" > "${DB_PATH}"

# Set permissions
chown pacs_bridge:pacs_bridge "${DB_PATH}"

# Verify database
sqlite3 "${DB_PATH}" "PRAGMA integrity_check;"

if [ $? -eq 0 ]; then
    systemctl start pacs_bridge
    rm -f "${DB_PATH}.old"
    echo "Queue database restored"
else
    mv "${DB_PATH}.old" "${DB_PATH}"
    systemctl start pacs_bridge
    echo "ERROR: Database integrity check failed, rolled back"
    exit 1
fi
```

### Point-in-Time Recovery

For Litestream, restore to a specific point in time:

```bash
# List available restore points
litestream snapshots s3://pacs-bridge-backup/queue

# Restore to specific timestamp
litestream restore -o /var/lib/pacs_bridge/queue.db \
    -timestamp "2025-12-10T12:00:00Z" \
    s3://pacs-bridge-backup/queue
```

---

## Data Retention Policies

### Retention Schedule

| Data Type | Hot Storage | Warm Storage | Archive | Total Retention |
|-----------|-------------|--------------|---------|-----------------|
| Configuration | Indefinite (Git) | - | - | Indefinite |
| Certificates | 90 days | 1 year | 7 years | 7 years |
| Queue DB | 7 days | 30 days | 90 days | 90 days |
| Logs | 14 days | 90 days | 1 year | 1 year |

### Storage Tiers

```yaml
# Lifecycle policy for S3/GCS
lifecycle_rules:
  - name: queue_lifecycle
    prefix: queue/
    transitions:
      - days: 7
        storage_class: STANDARD_IA
      - days: 30
        storage_class: GLACIER
    expiration:
      days: 90

  - name: logs_lifecycle
    prefix: logs/
    transitions:
      - days: 14
        storage_class: STANDARD_IA
      - days: 90
        storage_class: GLACIER
    expiration:
      days: 365
```

### Compliance Considerations

For healthcare environments:

| Requirement | Implementation |
|-------------|----------------|
| HIPAA | Encrypt backups at rest and in transit |
| Data Integrity | Verify checksums after backup/restore |
| Access Control | Restrict backup access to authorized personnel |
| Audit Trail | Log all backup/restore operations |

---

## Testing Backups

### Backup Verification Checklist

- [ ] Backup file exists and has expected size
- [ ] Backup can be decrypted (if encrypted)
- [ ] Backup can be extracted/decompressed
- [ ] Configuration validates successfully
- [ ] Certificates verify successfully
- [ ] Database integrity check passes
- [ ] Service starts with restored data

### Automated Verification Script

```bash
#!/bin/bash
# verify_backups.sh

BACKUP_DIR="/backup/pacs_bridge"
TEMP_DIR="/tmp/backup_verify"
ERRORS=0

mkdir -p "${TEMP_DIR}"

# Verify configuration backup
echo "Verifying configuration backup..."
LATEST_CONFIG=$(ls -t ${BACKUP_DIR}/config/config_*.tar.gz 2>/dev/null | head -1)
if [ -n "$LATEST_CONFIG" ]; then
    tar -tzf "$LATEST_CONFIG" > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: Configuration backup verification failed"
        ((ERRORS++))
    else
        echo "OK: Configuration backup verified"
    fi
else
    echo "WARNING: No configuration backup found"
fi

# Verify queue backup
echo "Verifying queue backup..."
LATEST_QUEUE=$(ls -t ${BACKUP_DIR}/queue/queue_*.db.gz 2>/dev/null | head -1)
if [ -n "$LATEST_QUEUE" ]; then
    gunzip -c "$LATEST_QUEUE" > "${TEMP_DIR}/test.db" 2>/dev/null
    sqlite3 "${TEMP_DIR}/test.db" "PRAGMA integrity_check;" > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: Queue backup verification failed"
        ((ERRORS++))
    else
        echo "OK: Queue backup verified"
    fi
else
    echo "WARNING: No queue backup found"
fi

# Cleanup
rm -rf "${TEMP_DIR}"

if [ $ERRORS -gt 0 ]; then
    echo "FAILED: ${ERRORS} verification errors"
    exit 1
else
    echo "PASSED: All backups verified"
    exit 0
fi
```

### Monthly Recovery Drill

Perform monthly recovery drills:

1. **Restore to test environment**
   ```bash
   # Create test environment
   docker run -d --name pacs_bridge_test -p 12575:2575 kcenon/pacs_bridge:latest

   # Restore configuration
   docker cp /backup/pacs_bridge/config/latest.tar.gz pacs_bridge_test:/tmp/
   docker exec pacs_bridge_test tar -xzf /tmp/latest.tar.gz -C /etc

   # Restore queue
   docker cp /backup/pacs_bridge/queue/latest.db.gz pacs_bridge_test:/tmp/
   docker exec pacs_bridge_test gunzip -c /tmp/latest.db.gz > /var/lib/pacs_bridge/queue.db

   # Restart and verify
   docker restart pacs_bridge_test
   curl http://localhost:12575/health
   ```

2. **Validate functionality**
   - Send test HL7 messages
   - Verify message processing
   - Check queue state

3. **Document results**
   - Recovery time
   - Any issues encountered
   - Improvement actions

### Recovery Time Objectives

| Recovery Type | RTO Target | Actual (Test) |
|---------------|------------|---------------|
| Configuration | < 5 min | Document |
| Queue (7 days) | < 15 min | Document |
| Full System | < 30 min | Document |

---

## Related Documentation

- [Deployment Guide](deployment.md) - Deployment options
- [High Availability](high-availability.md) - HA configuration
- [Operations Runbook](../operations/runbook.md) - Operational procedures
