# MLLP - Minimal Lower Layer Protocol

## Overview

The Minimal Lower Layer Protocol (MLLP), also known as LLP (Lower Layer Protocol), is the standard transport protocol for HL7 v2.x messages over TCP/IP. Since TCP is a continuous byte stream, MLLP provides message framing to identify the start and end of each HL7 message.

## Protocol Specification

### Message Framing

MLLP wraps each HL7 message with special framing characters:

```
<SB> HL7 Message Content <EB><CR>
```

| Character | Name | Hex Value | ASCII |
|-----------|------|-----------|-------|
| SB | Start Block | 0x0B | VT (Vertical Tab) |
| EB | End Block | 0x1C | FS (File Separator) |
| CR | Carriage Return | 0x0D | CR |

### Visual Representation

```
┌─────────────────────────────────────────────────────────────────┐
│  0x0B  │           HL7 Message           │  0x1C  │  0x0D  │
│  (VT)  │    (MSH|^~\&|....<CR>...)       │  (FS)  │  (CR)  │
└─────────────────────────────────────────────────────────────────┘
   1 byte         Variable length            1 byte   1 byte
```

### Example

Raw bytes for a simple ACK message:

```
0x0B M S H | ^ ~ \ & | A P P 1 | ... 0x0D ... 0x1C 0x0D
│    │                              │         │    │
│    └── HL7 message start          │         │    └── Final CR
│                                   │         └── End Block
└── Start Block                     └── Segment separator CR
```

## Connection Model

### Client-Server Architecture

```
┌─────────────────┐                    ┌─────────────────┐
│   HL7 Client    │                    │   HL7 Server    │
│   (Sender)      │                    │   (Receiver)    │
├─────────────────┤                    ├─────────────────┤
│                 │  1. TCP Connect    │                 │
│                 │───────────────────>│   Port 2575     │
│                 │                    │   (default)     │
│                 │  2. MLLP Message   │                 │
│                 │───────────────────>│                 │
│                 │                    │   Process       │
│                 │  3. MLLP ACK       │   Message       │
│                 │<───────────────────│                 │
│                 │                    │                 │
│                 │  4. (Optional)     │                 │
│                 │     Close/Reuse    │                 │
└─────────────────┘                    └─────────────────┘
```

### Connection Modes

#### Transient Connection
- New TCP connection per message
- Connection closed after ACK received
- Simple but higher overhead

#### Persistent Connection
- Connection kept open for multiple messages
- Lower overhead, faster throughput
- Requires keep-alive handling

## Message Flow

### Synchronous Model (Original Mode)

```
Sender                              Receiver
   │                                   │
   │  ────── MLLP(HL7 Message) ──────> │
   │                                   │ Process
   │  <────── MLLP(ACK/NAK) ────────── │
   │                                   │
```

### Asynchronous Model

```
Sender                              Receiver
   │                                   │
   │  ────── MLLP(Message 1) ────────> │
   │  ────── MLLP(Message 2) ────────> │
   │                                   │ Process
   │  <────── MLLP(ACK 1) ──────────── │
   │  <────── MLLP(ACK 2) ──────────── │
   │                                   │
```

## Implementation Details

### Reading MLLP Messages

```cpp
// Pseudocode for MLLP message reading
enum class mllp_state { waiting_start, reading_message, waiting_end };

class mllp_reader {
    mllp_state state_ = mllp_state::waiting_start;
    std::string buffer_;

public:
    std::optional<std::string> process_byte(uint8_t byte) {
        switch (state_) {
            case mllp_state::waiting_start:
                if (byte == 0x0B) {
                    state_ = mllp_state::reading_message;
                    buffer_.clear();
                }
                break;

            case mllp_state::reading_message:
                if (byte == 0x1C) {
                    state_ = mllp_state::waiting_end;
                } else {
                    buffer_ += static_cast<char>(byte);
                }
                break;

            case mllp_state::waiting_end:
                if (byte == 0x0D) {
                    state_ = mllp_state::waiting_start;
                    return buffer_;  // Complete message
                } else {
                    // Invalid: EB not followed by CR
                    // Add EB to buffer and continue
                    buffer_ += static_cast<char>(0x1C);
                    buffer_ += static_cast<char>(byte);
                    state_ = mllp_state::reading_message;
                }
                break;
        }
        return std::nullopt;
    }
};
```

### Writing MLLP Messages

```cpp
std::string wrap_mllp(const std::string& hl7_message) {
    std::string mllp;
    mllp.reserve(hl7_message.size() + 3);
    mllp += static_cast<char>(0x0B);  // Start Block
    mllp += hl7_message;
    mllp += static_cast<char>(0x1C);  // End Block
    mllp += static_cast<char>(0x0D);  // Carriage Return
    return mllp;
}
```

## Character Encoding

### Encoding Requirements

MLLP framing uses single-byte control characters (0x0B, 0x1C, 0x0D). The HL7 message content must use an encoding that does not conflict with these values.

### Safe Encodings

| Encoding | Safe | Notes |
|----------|------|-------|
| ASCII | Yes | Standard, 7-bit |
| ISO-8859-1 | Yes | Latin-1, 8-bit |
| UTF-8 | Yes | Variable length, ASCII compatible |
| UTF-16 | **No** | May contain 0x0B, 0x1C, 0x0D as part of characters |
| UTF-32 | **No** | May conflict with framing bytes |

> **Recommendation**: Use UTF-8 for international character support while maintaining MLLP compatibility.

## Error Handling

### Common Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| No Start Block | Missing 0x0B | Wait for valid start |
| Incomplete Message | Connection closed mid-message | Discard and reconnect |
| Missing End Block | Timeout without 0x1C | Timeout and discard |
| Invalid End Sequence | 0x1C not followed by 0x0D | Treat 0x1C as data, continue |

### Timeout Handling

```cpp
struct mllp_config {
    std::chrono::seconds connect_timeout{30};
    std::chrono::seconds read_timeout{60};
    std::chrono::seconds ack_timeout{30};
    size_t max_message_size{16 * 1024 * 1024};  // 16 MB
};
```

## Security Considerations

### MLLP over TLS (Secure MLLP)

For secure communication, MLLP can be wrapped in TLS:

```
┌─────────────────────────────────────────────────────────────────┐
│                        TLS Layer                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                     MLLP Layer                           │    │
│  │  ┌─────────────────────────────────────────────────┐    │    │
│  │  │              HL7 Message                         │    │    │
│  │  └─────────────────────────────────────────────────┘    │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

### Best Practices

1. **Use TLS** for production environments
2. **Authenticate** connections using certificates or IP whitelisting
3. **Validate** message content before processing
4. **Log** all connections and messages for audit
5. **Limit** message size to prevent DoS attacks

## MLLP Version 2

MLLP Version 2 (MLLP v2) extends the original specification with:

- Reliable delivery semantics
- Commit acknowledgments
- Better support for HL7 v3 content

However, MLLP v1 remains the most widely deployed version for HL7 v2.x messaging.

## Port Numbers

| Port | Usage |
|------|-------|
| 2575 | Default HL7/MLLP port |
| 2576 | Alternative MLLP port |
| 6661 | HL7 over TLS (common convention) |

> **Note**: Port 2575 is registered with IANA for HL7 but is not mandatory.

## Related Protocols

### HLLP (Hybrid Lower Layer Protocol)

HLLP adds checksums for data integrity verification:

```
<SB> HL7 Message <EB><CR> <BCC><BCC><CR>
```

Where BCC is a block check character (checksum).

### HTTP/REST

Modern alternatives to MLLP include:
- HL7 over HTTP
- FHIR REST API

## References

- [HL7 MLLP Transport Specification](https://www.hl7.org/implement/standards/product_brief.cfm?product_id=55)
- [InterfaceWare - LLP Protocol](https://www.interfaceware.com/hl7-transport-llp)
- [Google Cloud MLLP Adapter](https://github.com/GoogleCloudPlatform/mllp)
- [IBM MLLP Documentation](https://www.ibm.com/docs/en/imdm/11.6.0?topic=variables-minimal-lower-layer-protocol-mllp)
