# OpelXID Design

## 1. Purpose

Build a greenfield ESP32 application (ESP-IDF based) that synchronizes:

- Device time from a custom mobile app.
- Audio metadata from the mobile app (track title, artist, playback status).

All existing code in this repository is treated as scaffold only. New implementation is designed from first principles on top of ESP-IDF public APIs.

## 2. Hard Constraints

- Use ESP-IDF from `~/git/esp-idf`.
- Use only public interfaces documented in the ESP-IDF Programming Guide:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/index.html
- Do not use private headers, internal components, undocumented symbols, or copied logic from ChronosESP32 implementation.
- Protocol behavior is defined by:
  - `~/git/ChronosESP32/PROTOCOL.md`
  - `~/git/ChronosESP32/AUDIO.md`
- ChronosESP32 source code is explicitly not a code reference.

## 3. Product Scope

### In Scope (Phase 1)

- BLE GATT server with one custom service and RX characteristic for phone-to-ESP32 writes.
- Framed protocol parser for incoming messages.
- Time synchronization handling (`opcode 0x93`).
- Audio metadata handling via notification channel (`opcode 0x72`) with text parsing for:
  - Track title
  - Artist
  - Playback state (Playing/Paused when prefixed)
- Internal state model and callback/event API for UI or other components.

### Out of Scope (Phase 1)

- Weather, contacts, QR, navigation, health, alarms.
- Any display/UI framework specifics.
- OTA update flow.

## 4. Protocol Summary for This App

### BLE Transport

- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX characteristic (phone to ESP32): `6e400002-b5a3-f393-e0a9-e50e24dcca9e`

### Framing

- Primary marker: `0xAB` (and optionally `0xEA` for extended families, ignored in phase 1).
- Length bytes: big-endian at bytes 1..2 with assembled rule:
  - `assembled_len = ((b1 << 8) | b2) + 3`
- Opcode at byte 4.

### Audio Metadata Path

- Incoming event family: `opcode 0x72`.
- Text payload begins at byte 8.
- Conventions to support:
  - `Track:Artist`
  - `Playing:Track:Artist`
  - `Paused:Track:Artist`

### Time Sync Path

- Incoming frame family: `opcode 0x93`.
- Date/time fields from payload are converted to system time and committed via standard system time APIs.

## 5. ESP-IDF Public Components To Use

1. BLE stack (NimBLE, public ESP-IDF APIs)
2. FreeRTOS APIs exposed by ESP-IDF (`task`, `queue`, `event groups`, `timers` as needed)
3. System time APIs (`settimeofday`, `time`, `localtime_r`)
4. Logging (`esp_log.h`)
5. Non-volatile storage (`nvs_flash`) for persisted configuration (device name, pairing preferences)
6. Optional security and bonding via documented BLE security configuration APIs

No internal/private headers are permitted.

## 6. High-Level Architecture

1. BLE Transport Layer
2. Frame Reassembly Layer
3. Protocol Decoder Layer
4. Domain State Layer
5. Application Event API

### 6.1 BLE Transport Layer

- Owns GATT service registration and advertising lifecycle.
- Receives writes on RX and forwards bytes to frame reassembler.
- Uses a receive-only protocol path in phase 1 (phone to ESP32 only).

### 6.2 Frame Reassembly Layer

- Reassembles complete application frames from BLE writes.
- Validates marker and assembled length.
- Drops malformed or oversized frames with diagnostics.

### 6.3 Protocol Decoder Layer

- Parses frame header and opcode.
- Routes only supported opcodes in phase 1 (`0x72`, `0x93`).
- Converts payload to typed events:
  - `TIME_SYNC_EVENT`
  - `AUDIO_METADATA_EVENT`

### 6.4 Domain State Layer

Keeps authoritative runtime state:

- Connection state.
- Last synchronized UTC timestamp.
- Audio state:
  - `playback_state` (`unknown`, `playing`, `paused`)
  - `track_title`
  - `artist`
  - `last_update_ms`

### 6.5 Application Event API

- Exposes callbacks or queue-driven events for the rest of firmware.
- Ensures BLE callbacks remain lightweight and non-blocking.

## 7. Data Model

```c
typedef enum {
    AUDIO_PLAYBACK_UNKNOWN = 0,
    AUDIO_PLAYBACK_PLAYING,
    AUDIO_PLAYBACK_PAUSED
} audio_playback_state_t;

typedef struct {
    audio_playback_state_t playback_state;
    char track[96];
    char artist[96];
    int64_t updated_ms;
} audio_metadata_t;
```

Buffers are fixed-size and bounds-checked to avoid heap fragmentation and overflow risk.

## 8. Parsing Rules

### 8.1 Metadata Text Parsing

1. Decode payload as UTF-8 bytes (best effort; preserve printable subset if invalid).
2. Split by `:`.
3. If first token is `Playing` or `Paused`, map playback state and parse next two tokens as track/artist.
4. Otherwise parse first token as track, second as artist.
5. Trim whitespace around tokens.
6. Missing fields become empty strings; event still emitted.

### 8.2 Time Parsing

1. Validate minimum payload length for year/month/day/hour/minute/second.
2. Validate numeric ranges.
3. Convert to `struct tm` and commit with `settimeofday`.
4. Emit synchronization result event (success/failure).

## 9. Concurrency Model

- BLE callback context only copies raw bytes into a queue/ring buffer.
- A dedicated protocol task performs reassembly, decode, and state update.
- Another optional application task consumes domain events.

This prevents long operations in BLE callbacks and keeps timing stable.

## 10. Security and Robustness

- Enforce max frame size and reject oversized packets.
- Validate every frame length before payload access.
- Use watchdog-friendly task loops (no unbounded blocking).
- Use BLE pairing/bonding as configurable policy.
- Log malformed frame metrics for diagnostics.

## 11. Suggested Project Layout (Greenfield)

Within `main/`:

- `app_main.c` (startup and wiring)
- `ble_transport.c/.h` (GATT, advertising, RX)
- `frame_reassembly.c/.h`
- `protocol_decoder.c/.h`
- `audio_state.c/.h`
- `time_sync.c/.h`
- `app_events.c/.h`
- `config.h` (compile-time limits)

## 12. Implementation Plan

1. Initialize NVS and BLE stack.
2. Bring up custom GATT service and characteristics.
3. Implement RX byte ingestion and full-frame reassembly.
4. Implement opcode routing for `0x93` and `0x72`.
5. Implement time update path and validation.
6. Implement audio metadata parsing and state store.
7. Add structured logs and error counters.
8. Add unit tests for frame parsing and tokenization.
9. Run hardware integration test with mobile app.

## 13. Test Strategy

### Unit Tests

- Frame length validation and malformed packet rejection.
- Parser cases for:
  - `Track:Artist`
  - `Playing:Track:Artist`
  - `Paused:Track:Artist`
  - Missing separators and empty values
- Time field range checks.

### Integration Tests

- Connect/disconnect/reconnect BLE cycles.
- Time sync correctness and persistence over reboot.
- Metadata update latency and ordering under rapid updates.
- Invalid frame flood handling.

## 14. Deliverables for Phase 1

- Working BLE transport and parser for time + audio metadata.
- Stable public API for reading latest audio and time state.
- Buildable ESP-IDF project with tests and logs.
- No dependency on ChronosESP32 source internals.

## 15. Future Extensions

- Add rich media metadata opcode if phone app evolves.
- Add playback progress and duration support.
- Add multi-language text normalization.
- Add optional display integration module.