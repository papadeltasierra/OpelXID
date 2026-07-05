# OpelXID

OpelXID is a greenfield ESP-IDF firmware project for ESP32 that receives BLE data from a custom mobile application and synchronizes:

1. Device time
2. Audio metadata (track title, artist, playback state)

The protocol direction for this project is strictly phone to ESP32.

## Status

Phase 1 is implemented and builds successfully with ESP-IDF.

Implemented in Phase 1:

1. RX-only BLE transport using NimBLE
2. Custom service and RX characteristic for incoming phone writes
3. Frame reassembly for AB-framed payloads
4. Time sync handling for opcode 0x93
5. Audio metadata handling for opcode 0x72
6. Queue-based processing pipeline to keep BLE callbacks lightweight

## Repository Documents

1. DESIGN.md: architecture and implementation plan
2. PROTOCOL.md: protocol overview used for this project
3. AUDIO.md: audio-focused protocol behavior and conventions

## Build Requirements

1. ESP-IDF installed at ../esp-idf (as used in this workspace)
2. Supported target: esp32
3. Python environment required by ESP-IDF tools

## Build and Flash

1. Load ESP-IDF environment:

```bash
. ../esp-idf/export.sh
```

2. Select target (first run only):

```bash
idf.py set-target esp32
```

3. Build:

```bash
idf.py build
```

4. Flash and open monitor:

```bash
idf.py -p <PORT> flash monitor
```

## Configuration

Use menuconfig for project-specific options:

1. OpelXID Configuration -> BLE device name
2. OpelXID Configuration -> RX queue depth

Default Bluetooth mode is BLE-only NimBLE as defined in sdkconfig.defaults.

## Runtime Pipeline

1. NimBLE GATT RX callback receives write payload bytes
2. Payload chunk is copied into a FreeRTOS queue
3. Protocol task consumes queue entries and performs frame reassembly
4. Decoder routes by opcode
5. Time and audio state are updated and published as app events

## Source Layout

1. main/main.c: app bootstrap, queue setup, task creation
2. main/ble_transport.c: NimBLE setup, GATT registration, advertising, RX handling
3. main/frame_reassembly.c: stream-to-frame assembly logic
4. main/protocol_decoder.c: opcode routing and payload parsing
5. main/time_sync.c: time validation and settimeofday application
6. main/audio_state.c: thread-safe audio metadata state store
7. main/app_events.c: event callback registration and publishing

## Scope Constraints

1. No ESP32-to-phone protocol features are implemented
2. No AVRCP or Classic BT behavior is included
3. Only public ESP-IDF interfaces are used

## Notes

This repository intentionally replaces previous scaffold/example code with a clean implementation aligned to the design in DESIGN.md.
