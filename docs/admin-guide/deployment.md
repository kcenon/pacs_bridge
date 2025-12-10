# Deployment Guide

> **Version:** 1.0.0
> **Last Updated:** 2025-12-10

---

## Table of Contents

- [Deployment Overview](#deployment-overview)
- [Docker Deployment](#docker-deployment)
- [Bare Metal Deployment](#bare-metal-deployment)
- [Kubernetes Deployment](#kubernetes-deployment)
- [Configuration Management](#configuration-management)
- [Security Considerations](#security-considerations)
- [Post-Deployment Verification](#post-deployment-verification)

---

## Deployment Overview

PACS Bridge supports multiple deployment methods:

| Method | Use Case | Complexity |
|--------|----------|------------|
| Docker | Development, Single Instance | Low |
| Docker Compose | Small/Medium Production | Medium |
| Bare Metal | Legacy Environments | Medium |
| Kubernetes | Enterprise, High Availability | High |

### Deployment Checklist

- [ ] Network ports configured (2575, 8080, 8081)
- [ ] TLS certificates prepared (if using MLLPS/HTTPS)
- [ ] PACS system connectivity verified
- [ ] RIS/HIS connectivity verified
- [ ] Configuration file prepared
- [ ] Log directory created
- [ ] Database directory created (for queue persistence)
- [ ] Monitoring infrastructure ready

---

## Docker Deployment

### Quick Start

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

### With Custom Configuration

```bash
# Create configuration directory
mkdir -p /opt/pacs_bridge/{config,certs,data,logs}

# Copy configuration file
cp config.yaml /opt/pacs_bridge/config/

# Run with mounted volumes
docker run -d \
  --name pacs_bridge \
  --restart unless-stopped \
  -p 2575:2575 \
  -p 8080:8080 \
  -p 8081:8081 \
  -v /opt/pacs_bridge/config:/etc/pacs_bridge:ro \
  -v /opt/pacs_bridge/certs:/etc/pacs_bridge/certs:ro \
  -v /opt/pacs_bridge/data:/var/lib/pacs_bridge \
  -v /opt/pacs_bridge/logs:/var/log/pacs_bridge \
  kcenon/pacs_bridge:latest
```

### Docker Compose

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  pacs_bridge:
    image: kcenon/pacs_bridge:latest
    container_name: pacs_bridge
    restart: unless-stopped
    ports:
      - "2575:2575"     # MLLP
      - "2576:2576"     # MLLPS (TLS)
      - "8080:8080"     # FHIR API
      - "8081:8081"     # Health/Metrics
    volumes:
      - ./config:/etc/pacs_bridge:ro
      - ./certs:/etc/pacs_bridge/certs:ro
      - pacs_bridge_data:/var/lib/pacs_bridge
      - pacs_bridge_logs:/var/log/pacs_bridge
    environment:
      - PACS_HOST=pacs.hospital.local
      - PACS_PORT=11112
      - RIS_HOST=ris.hospital.local
      - RIS_PORT=2576
      - LOG_LEVEL=info
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8081/health"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 10s
    networks:
      - healthcare

networks:
  healthcare:
    driver: bridge

volumes:
  pacs_bridge_data:
  pacs_bridge_logs:
```

Run with Docker Compose:

```bash
# Start services
docker-compose up -d

# View logs
docker-compose logs -f pacs_bridge

# Stop services
docker-compose down
```

### Docker Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `PACS_BRIDGE_NAME` | Instance name | `PACS_BRIDGE` |
| `PACS_BRIDGE_MLLP_PORT` | MLLP listener port | `2575` |
| `PACS_HOST` | PACS server hostname | `localhost` |
| `PACS_PORT` | PACS server port | `11112` |
| `RIS_HOST` | RIS server hostname | - |
| `RIS_PORT` | RIS server port | `2576` |
| `LOG_LEVEL` | Logging level | `info` |

---

## Bare Metal Deployment

### Prerequisites

- C++23 compatible compiler (GCC 13+, Clang 14+)
- CMake 3.20+
- OpenSSL 1.1+ (for TLS)
- systemd (for service management)

### Build from Source

```bash
# Clone repository
git clone https://github.com/kcenon/pacs_bridge.git
cd pacs_bridge

# Clone dependencies
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
cmake -B build -DBRIDGE_STANDALONE_BUILD=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Installation

```bash
# Create directories
sudo mkdir -p /opt/pacs_bridge/bin
sudo mkdir -p /etc/pacs_bridge/certs
sudo mkdir -p /var/lib/pacs_bridge
sudo mkdir -p /var/log/pacs_bridge

# Install binary
sudo cp build/bin/pacs_bridge /opt/pacs_bridge/bin/

# Install configuration
sudo cp examples/config/config-production.yaml /etc/pacs_bridge/config.yaml

# Set permissions
sudo chown -R pacs_bridge:pacs_bridge /opt/pacs_bridge
sudo chown -R pacs_bridge:pacs_bridge /var/lib/pacs_bridge
sudo chown -R pacs_bridge:pacs_bridge /var/log/pacs_bridge
sudo chmod 600 /etc/pacs_bridge/config.yaml
```

### Systemd Service

Create `/etc/systemd/system/pacs_bridge.service`:

```ini
[Unit]
Description=PACS Bridge HIS/RIS Integration Gateway
Documentation=https://github.com/kcenon/pacs_bridge
After=network.target

[Service]
Type=simple
User=pacs_bridge
Group=pacs_bridge
ExecStart=/opt/pacs_bridge/bin/pacs_bridge --config /etc/pacs_bridge/config.yaml
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal
SyslogIdentifier=pacs_bridge

# Security hardening
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadWritePaths=/var/lib/pacs_bridge /var/log/pacs_bridge

# Resource limits
LimitNOFILE=65536
LimitNPROC=4096

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable pacs_bridge
sudo systemctl start pacs_bridge
sudo systemctl status pacs_bridge
```

### Log Rotation

Create `/etc/logrotate.d/pacs_bridge`:

```
/var/log/pacs_bridge/*.log {
    daily
    rotate 14
    compress
    delaycompress
    missingok
    notifempty
    create 0640 pacs_bridge pacs_bridge
    sharedscripts
    postrotate
        systemctl reload pacs_bridge > /dev/null 2>&1 || true
    endscript
}
```

---

## Kubernetes Deployment

### ConfigMap

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: pacs-bridge-config
  namespace: healthcare
data:
  config.yaml: |
    server:
      name: "${POD_NAME}"

    hl7:
      listener:
        port: 2575
        max_connections: 100

    pacs:
      host: "${PACS_HOST}"
      port: 11112
      ae_title: "PACS_BRIDGE"
      called_ae: "PACS_SCP"

    logging:
      level: "info"
      format: "json"
```

### Secret

```yaml
apiVersion: v1
kind: Secret
metadata:
  name: pacs-bridge-tls
  namespace: healthcare
type: kubernetes.io/tls
data:
  tls.crt: <base64-encoded-certificate>
  tls.key: <base64-encoded-key>
```

### Deployment

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: pacs-bridge
  namespace: healthcare
  labels:
    app: pacs-bridge
spec:
  replicas: 2
  selector:
    matchLabels:
      app: pacs-bridge
  template:
    metadata:
      labels:
        app: pacs-bridge
      annotations:
        prometheus.io/scrape: "true"
        prometheus.io/port: "8081"
        prometheus.io/path: "/metrics"
    spec:
      serviceAccountName: pacs-bridge
      containers:
        - name: pacs-bridge
          image: kcenon/pacs_bridge:latest
          ports:
            - name: mllp
              containerPort: 2575
              protocol: TCP
            - name: mllps
              containerPort: 2576
              protocol: TCP
            - name: fhir
              containerPort: 8080
              protocol: TCP
            - name: metrics
              containerPort: 8081
              protocol: TCP
          env:
            - name: POD_NAME
              valueFrom:
                fieldRef:
                  fieldPath: metadata.name
            - name: PACS_HOST
              value: "pacs-service.healthcare.svc.cluster.local"
          volumeMounts:
            - name: config
              mountPath: /etc/pacs_bridge
              readOnly: true
            - name: tls
              mountPath: /etc/pacs_bridge/certs
              readOnly: true
            - name: data
              mountPath: /var/lib/pacs_bridge
          resources:
            requests:
              memory: "256Mi"
              cpu: "100m"
            limits:
              memory: "1Gi"
              cpu: "1000m"
          livenessProbe:
            httpGet:
              path: /health
              port: metrics
            initialDelaySeconds: 10
            periodSeconds: 30
          readinessProbe:
            httpGet:
              path: /health
              port: metrics
            initialDelaySeconds: 5
            periodSeconds: 10
          securityContext:
            runAsNonRoot: true
            runAsUser: 1000
            readOnlyRootFilesystem: true
      volumes:
        - name: config
          configMap:
            name: pacs-bridge-config
        - name: tls
          secret:
            secretName: pacs-bridge-tls
        - name: data
          persistentVolumeClaim:
            claimName: pacs-bridge-data
```

### Service

```yaml
apiVersion: v1
kind: Service
metadata:
  name: pacs-bridge
  namespace: healthcare
  labels:
    app: pacs-bridge
spec:
  type: ClusterIP
  ports:
    - name: mllp
      port: 2575
      targetPort: mllp
    - name: mllps
      port: 2576
      targetPort: mllps
    - name: fhir
      port: 8080
      targetPort: fhir
    - name: metrics
      port: 8081
      targetPort: metrics
  selector:
    app: pacs-bridge
```

### Horizontal Pod Autoscaler

```yaml
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: pacs-bridge-hpa
  namespace: healthcare
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: pacs-bridge
  minReplicas: 2
  maxReplicas: 10
  metrics:
    - type: Resource
      resource:
        name: cpu
        target:
          type: Utilization
          averageUtilization: 70
    - type: Pods
      pods:
        metric:
          name: hl7_messages_received_total
        target:
          type: AverageValue
          averageValue: "1000"
```

---

## Configuration Management

### Environment-Specific Configurations

```
/etc/pacs_bridge/
├── config.yaml           # Base configuration
├── config.development.yaml
├── config.staging.yaml
└── config.production.yaml
```

### Secrets Management

**Never store secrets in configuration files.** Use:

1. **Environment Variables**
   ```yaml
   pacs:
     host: "${PACS_HOST}"
   ```

2. **Kubernetes Secrets**
   ```yaml
   env:
     - name: PACS_PASSWORD
       valueFrom:
         secretKeyRef:
           name: pacs-credentials
           key: password
   ```

3. **HashiCorp Vault**
   ```bash
   vault kv get -field=password secret/pacs_bridge/pacs
   ```

---

## Security Considerations

### Network Security

1. **Firewall Rules**
   ```bash
   # Allow MLLP from HIS/RIS only
   iptables -A INPUT -p tcp --dport 2575 -s 10.0.0.0/24 -j ACCEPT
   iptables -A INPUT -p tcp --dport 2575 -j DROP
   ```

2. **TLS Configuration**
   - Use TLS 1.2 or higher
   - Use strong cipher suites
   - Rotate certificates regularly

3. **Network Segmentation**
   - Deploy in healthcare VLAN
   - Use network policies in Kubernetes

### Access Control

1. **File Permissions**
   ```bash
   chmod 600 /etc/pacs_bridge/config.yaml
   chmod 600 /etc/pacs_bridge/certs/*.key
   ```

2. **Service Account**
   - Run as non-root user
   - Use dedicated service account
   - Apply principle of least privilege

---

## Post-Deployment Verification

### Health Check

```bash
# Check service status
curl http://localhost:8081/health

# Expected response
{
  "status": "healthy",
  "components": {
    "hl7_listener": "up",
    "pacs_connection": "up",
    "patient_cache": "up"
  }
}
```

### Connectivity Tests

```bash
# Test MLLP listener
nc -vz localhost 2575

# Test FHIR endpoint
curl http://localhost:8080/fhir/r4/metadata

# Test metrics endpoint
curl http://localhost:8081/metrics | head -20
```

### Log Verification

```bash
# Check for startup messages
journalctl -u pacs_bridge --since "5 minutes ago"

# Or Docker logs
docker logs pacs_bridge --since 5m
```

### Integration Test

Send a test ADT message:

```bash
echo -e "\x0BMSH|^~\&|TEST|TEST|PACS_BRIDGE|PACS|$(date +%Y%m%d%H%M%S)||ADT^A04|TEST001|P|2.5\rEVN|A04|$(date +%Y%m%d%H%M%S)\rPID|1||TEST123^^^HOSPITAL^MR||TEST^PATIENT\x1C\x0D" | nc localhost 2575
```

---

## Related Documentation

- [High Availability](high-availability.md) - HA configuration
- [Monitoring](monitoring.md) - Monitoring setup
- [Backup and Recovery](backup-recovery.md) - Data protection
