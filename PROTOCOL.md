# Acknowledgement

This document incorporates protocol reference material from the Chronos ESP32 ecosystem.

Acknowledged project: chronos-esp32 by fbiego
Repository: https://github.com/fbiego/chronos-esp32

OpelXID uses the same underlying wire protocols for compatibility with the Chronos mobile protocol behavior.

---

# ChronosESP32 Bluetooth Protocol

This document describes the Bluetooth Low Energy (BLE) protocol used by this library to communicate with the Chronos phone app.

It is derived from the implementation in:
- `src/ChronosESP32.h`
- `src/ChronosESP32.cpp`

## 1) BLE Layer Used

## 1.1 GATT profile

The library implements a custom BLE GATT server with one service and two characteristics:

- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX characteristic UUID (phone -> ESP32): `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
  - Properties: `WRITE | WRITE_NO_RESPONSE`
- TX characteristic UUID (ESP32 -> phone): `6e400003-b5a3-f393-e0a9-e50e24dcca9e`
  - Property: `NOTIFY`

This is the same UUID layout as Nordic UART Service style transports (custom, not SIG standard service semantics).

## 1.2 ATT/MTU behavior

- The server requests MTU 517 (`BLEDevice::setMTU(517)`).
- Despite high MTU, application payload handling keeps a 20-byte packet model for chunking logic.

## 1.3 Advertising behavior

- Advertises the custom service UUID.
- Scan response enabled.
- Preferred connection params set to `0x06, 0x12` (comment says this helps iPhone connections).
- Device name set from the library watch name.

## 1.4 Subscription gate

- Data from ESP32 to phone uses notifications on TX.
- On TX subscription, library starts an info timer and then sends watch info + battery + phone battery subscription request.

## 2) Application Framing Over BLE

The BLE characteristics carry a custom framed binary protocol.

## 2.1 Frame header

Most complete application messages begin with:

- Byte 0: frame marker
  - `0xAB` for most commands/config/events
  - `0xEA` for extended weather/location payloads
- Byte 1..2: payload length field (`lenHi`, `lenLo`), interpreted as big-endian
- Byte 3: direction/class marker
  - `0xFE` and `0xFF` are used in parsed traffic
- Byte 4: primary opcode
- Byte 5+: opcode-specific subcommand/payload

Assembly rule used by library:

- `assembled_length = (byte1 * 256 + byte2) + 3`

## 2.2 Chunking format for long packets

When sending payloads longer than 20 bytes (or when forced):

1. First BLE notification carries bytes 0..19 of the original message unchanged.
2. Remaining bytes are sent as chunks of up to 19 payload bytes, prefixed with a 1-byte sequence number.
   - chunk format: `[seq][data19-or-less]`
   - `seq` starts at `0x00`, increments by one per chunk.
3. Receiver reconstructs by placing first continuation chunk at offset 20, then `20 + seq*19`.

This same continuation assembly logic is used for incoming writes as well.

## 2.3 Timing between notifications

Library inserts about 200 ms delay between notifications (`vTaskDelay(200 / portTICK_PERIOD_MS)`).

## 3) Shared Opcode Families

Across features, opcodes are grouped by `byte4`:

- `0x71..0x7F`: watch settings / controls / weather / sleep
- `0x88`, `0x8A`: additional weather details
- `0x91..0x93`: battery + time/info
- `0x9x`: media/font
- `0xA2..0xA8`: contacts + QR
- `0xCA`, `0xCC`: app version and transfer capability
- `0xEE`, `0xEF`: navigation icon + navigation text state
- `0x31`, `0x32`, `0x51`, `0x52`: health measurement and record requests

## 3.1 Quick Reference Table

Legend:
- Frame marker (`byte0`): `AB` = main protocol frame, `EA` = extended weather/location frame.
- Class/direction marker (`byte3`): `FE` and `FF` are protocol class values used by the app/library command families.
- Dir column: `Phone -> ESP32` means app writes RX characteristic; `ESP32 -> Phone` means watch notifies TX characteristic.

| Frame | Opcode (byte4) | Dir | Purpose | Key bytes after opcode |
| --- | --- | --- | --- | --- |
| AB | 0x20 | Phone -> ESP32 | Sync complete event | byte3=FE |
| AB | 0x23 | ESP32 -> Phone / Phone -> ESP32 | Sync request / reset callback path | TX uses `AB 00 03 FE 23 80` |
| AB | 0x31 | Both | Realtime health measure request/result | byte5 selects metric: 0A HR, 12 SpO2, 22 BP |
| AB | 0x32 | Both | Combined health measure request/result | byte5=80, then HR/SpO2/SYS/DIA |
| AB | 0x51 | Both | Steps and health records | byte5: 08 realtime steps, 11 HR rec, 12 SpO2 rec, 13 temp rec, 14 BP rec, 20 steps rec, 80 request |
| AB | 0x52 | Both | Sleep records | byte5=80 request or payload subtype |
| AB | 0x53 | Phone -> ESP32 | Water reminder config | enabled, start/end time, interval |
| AB | 0x71 | Phone -> ESP32 | Find-watch event | no extra structured payload used |
| AB | 0x72 | Phone -> ESP32 | Notification/ringer event | byte6 icon, byte7 state, byte8.. text |
| AB | 0x73 | Phone -> ESP32 | Alarm config | index, enabled, hour, minute, repeat |
| AB | 0x74 | Phone -> ESP32 | User profile config | step, age, height, weight, units, target, temp unit |
| AB | 0x75 | Phone -> ESP32 | Sedentary config | enabled, start/end time, interval |
| AB | 0x76 | Phone -> ESP32 | Quiet-hours config | enabled, start/end time |
| AB | 0x77 | Phone -> ESP32 | Raise-to-wake config | byte6 flag |
| AB | 0x78 | Phone -> ESP32 | Hourly measurement config | byte6 flag |
| AB | 0x79 | Both | Camera readiness / capture command family | RX byte6 ready; TX `... 79 80 01` capture |
| AB | 0x7B | Phone -> ESP32 | Language config | byte6 language id |
| AB | 0x7C | Phone -> ESP32 | 12/24h mode | byte6 (0 means 24h in current parser) |
| AB | 0x7D | ESP32 -> Phone | Find-phone control | byte6: 01 start, 00 stop |
| AB | 0x7E | Phone -> ESP32 | Compact weather | repeating 2-byte icon/temp entries |
| AB | 0x7F | Phone -> ESP32 | Sleep schedule config | enabled, start/end time |
| AB | 0x88 | Phone -> ESP32 | Weather hi/low | repeating 2-byte signed highs/lows |
| AB | 0x8A | Phone -> ESP32 | Weather UV/pressure | UV + pressure(2 bytes) |
| AB | 0x91 | Both | Battery exchange | TX watch battery, RX phone battery |
| AB | 0x92 | ESP32 -> Phone | Watch/device info | fixed 20-byte capability block |
| AB | 0x93 | Phone -> ESP32 | Time sync | year, month, day, hour, minute, second |
| AB | 0x99 / 0x9D | ESP32 -> Phone | Media controls | volume or transport command bytes |
| AB | 0x9C | Phone -> ESP32 | Font/color config | RGB + selector bytes |
| AB | 0xA2 | Phone -> ESP32 | Contact name entry | byte5 index, byte6.. name |
| AB | 0xA3 | Phone -> ESP32 | Contact number entry | byte5 index, byte6 length, packed digits |
| AB | 0xA5 | Phone -> ESP32 | Contact metadata | SOS index + count |
| AB | 0xA8 | Phone -> ESP32 | QR transfer | FF=data chunk, FE=end marker |
| AB | 0xBF | Phone -> ESP32 | Remote touch event | state + X/Y |
| AB | 0xCA | Phone -> ESP32 | App version info | version code + version string |
| AB | 0xCC | Phone -> ESP32 | Chunked-transfer capability | byte5 nonzero enables chunk mode |
| AB | 0xEE | Phone -> ESP32 | Navigation icon chunk | pos + CRC + 96 bytes data |
| AB | 0xEF | Phone -> ESP32 | Navigation state/text | mode + flags + CRC + 6 strings |
| EA | 0x7E | Phone -> ESP32 | Extended weather | byte5=01 city, byte5=02 hourly |
| EA | 0x7F | Phone -> ESP32 | Weather location | float lat/lon + city/region/country |

## 3.2 Feature-to-Opcode Lookup

| README feature | Primary opcodes | Direction | Notes |
| --- | --- | --- | --- |
| Time (auto sync) | AB:0x93 | Phone -> ESP32 | Phone pushes current date/time fields. |
| Notifications | AB:0x72 | Phone -> ESP32 | Includes app icon id, state, and text payload. |
| Weather | AB:0x7E, 0x88, 0x8A; EA:0x7E, 0x7F | Phone -> ESP32 | Compact forecast, hi/low, UV/pressure, city/hourly, geo payload. |
| Controls: Music | AB:0x9D, 0x99 | ESP32 -> Phone | 0x9D transport control, 0x99 family for volume operations. |
| Controls: Find Phone | AB:0x7D | ESP32 -> Phone | Toggle start/stop phone ringing from watch side. |
| Controls: Camera | AB:0x79 | Both | RX provides readiness, TX triggers capture. |
| Phone Battery | AB:0x91 | Both | TX sends watch battery, RX provides phone battery/charging. |
| Navigation | AB:0xEE, 0xEF | Phone -> ESP32 | 0xEE icon chunks, 0xEF status and route strings. |
| Contacts | AB:0xA2, 0xA3, 0xA5 | Phone -> ESP32 | Metadata + per-contact name and number records. |
| QR codes | AB:0xA8 | Phone -> ESP32 | FF carries entries, FE marks transfer completion. |
| Alarms | AB:0x73 | Phone -> ESP32 | Alarm index, enabled flag, time, repeat mask. |
| Sync lifecycle | AB:0x23, 0x20 | Both | TX sync request, RX sync-complete signal. |

## 4) Feature Mapping (from README)

This section maps each README feature to the protocol pieces it uses.

## 4.1 Time (auto sync)

Bluetooth aspects used:
- Phone writes framed command to RX.
- ESP32 parses one-shot frame (normally unchunked).

Protocol path:
- Incoming `0xAB ... byte4=0x93` updates RTC.
- Date/time fields parsed from payload into `setTime(second, minute, hour, day, month, year)`.

Data layout seen by parser (`0xAB`, opcode `0x93`):
- `byte7..8`: year (big-endian, full year)
- `byte9`: month
- `byte10`: day
- `byte11`: hour
- `byte12`: minute
- `byte13`: second

## 4.2 Notifications

Bluetooth aspects used:
- Phone writes notification events to RX.
- ESP32 stores in ring buffer and invokes callback.

Protocol path:
- Incoming `0xAB ... byte4=0x72`
  - `byte6`: app icon ID
  - `byte7`: state
  - `byte8..end`: UTF-8/ASCII text body

Special cases:
- icon `0x01`: incoming call/ringer start event
- icon `0x02`: ringer cancel event
- state `0x02`: normal notification inserted into notification buffer

Title/message split rule:
- If first `:` appears before index 30 and before any newline, title is left side and message is right side.
- Otherwise title defaults to app name from icon map.

## 4.3 Weather

Bluetooth aspects used:
- Uses both `0xAB` and `0xEA` frame markers.
- Supports compact current/weekly, hi/lo, UV/pressure, city name, hourly forecast, location payload.

Protocol paths:

1. Current/weekly compact weather
- Incoming `0xAB ... byte4=0x7E`
- Repeating 2-byte entries from `byte6`:
  - first byte: high nibble icon, low-bit sign flag
  - second byte: absolute temperature

2. High/low temperatures
- Incoming `0xAB ... byte4=0x88`
- Repeating pairs:
  - first byte: high temp (bit7 sign, bits0..6 magnitude)
  - second byte: low temp (bit7 sign, bits0..6 magnitude)

3. UV + pressure
- Incoming `0xAB ... byte4=0x8A`
  - `byte6`: UV
  - `byte7..8`: pressure big-endian

4. City name (extended frame)
- Incoming `0xEA ... byte4=0x7E, byte5=0x01`
  - city string from `byte7..end`

5. Hourly forecast (extended frame)
- Incoming `0xEA ... byte4=0x7E, byte5=0x02`
  - `byte6`: number of forecast records
  - `byte7`: starting hour index
  - each record is 6 bytes from `byte8`:
    - [0] icon/sign packed like compact weather
    - [1] temp magnitude
    - [2..3] wind big-endian
    - [4] humidity
    - [5] UV

6. Weather location (extended frame)
- Incoming `0xEA ... byte4=0x7F, byte3=0xFE`
  - `byte6`: payload length (`payloadLen`)
  - payload starts at `byte7`:
    - bytes 0..3: latitude float32 little-endian
    - bytes 4..7: longitude float32 little-endian
    - bytes 8..: city null-terminated, then region null-terminated, then country (rest)

## 4.4 Controls (Music, Find Phone, Camera)

Bluetooth aspects used:
- ESP32 sends short notify frames (`0xAB`, class `0xFF`), mostly unchunked.
- Phone returns camera readiness via incoming config frame.

Music controls (ESP32 -> phone):
- `musicControl(command)` sends:
  - `AB 00 04 FF [cmdHi] 80 [cmdLo]`
- Command IDs:
  - Play `0x9D00`
  - Pause `0x9D01`
  - Previous `0x9D02`
  - Next `0x9D03`
  - Toggle `0x9900`

Volume set (ESP32 -> phone):
- `setVolume(level)`:
  - `AB 00 05 FF 99 80 A0 [level]`

Capture photo (ESP32 -> phone):
- `capturePhoto()` (only if camera ready):
  - `AB 00 04 FF 79 80 01`

Find phone (ESP32 -> phone):
- `findPhone(true)`:
  - `AB 00 04 FF 7D 80 01`
- `findPhone(false)`:
  - `AB 00 04 FF 7D 80 00`
- Library auto-cancels after ~30 seconds.

Camera readiness (phone -> ESP32):
- Incoming `0xAB ... byte4=0x79`
  - `byte6 == 1` means camera ready.

## 4.5 Phone Battery

Bluetooth aspects used:
- Bidirectional exchange:
  - ESP32 sends watch battery to phone.
  - ESP32 asks phone to send phone battery updates.
  - Phone sends battery status back.

Watch battery report (ESP32 -> phone):
- `sendBattery()`:
  - `AB 00 05 FF 91 80 [chargingFlag] [level]`

Phone battery subscribe/request (ESP32 -> phone):
- `setNotifyBattery(state)`:
  - `AB 00 04 FE 91 80 [0|1]`

Phone battery status from phone:
- Incoming `0xAB ... byte4=0x91, byte3=0xFE`
  - `byte6`: phone charging flag (1 yes)
  - `byte7`: phone battery level

## 4.6 Navigation

Bluetooth aspects used:
- Uses chunk-capable transport due potentially large payloads.
- Separate opcodes for icon bitmap chunks and route text/status.

Navigation icon chunks (phone -> ESP32):
- Incoming `0xAB ... byte4=0xEE, byte3=0xFE`
  - `byte6`: icon chunk position index
  - `byte7..10`: icon CRC32-like identifier (big-endian in packet)
  - `byte11..106`: 96 bytes of icon data
- Icon buffer is 48x48 monochrome (`288` bytes total), split into 3 chunks x 96 bytes.

Navigation state/text (phone -> ESP32):
- Incoming `0xAB ... byte4=0xEF, byte3=0xFE`
- `byte5` mode:
  - `0x00`: navigation inactive
  - `0xFF`: navigation feature disabled in app settings
  - `0x80`: active navigation payload follows

Active navigation payload (`byte5=0x80`):
- `byte6`: hasIcon flag
- `byte7`: isNavigation flag
- `byte8..11`: icon CRC
- `byte12..`: six null-terminated strings in order:
  1. title
  2. duration
  3. distance
  4. eta
  5. directions
  6. speed

## 4.7 Contacts & QR codes

Bluetooth aspects used:
- Phone pushes contacts/QR data to ESP32.
- Contacts use multi-message records.

Contacts metadata:
- Incoming `0xAB ... byte4=0xA5`
  - `byte6`: SOS contact index
  - `byte7`: total contact count

Contact name record:
- Incoming `0xAB ... byte4=0xA2`
  - `byte5`: contact position
  - `byte6..end`: name text

Contact number record:
- Incoming `0xAB ... byte4=0xA3`
  - `byte5`: contact position
  - `byte6`: expected number length (`nSize`)
  - `byte7..end`: packed BCD-like nibbles
- Decoding in library:
  - each byte converted to hex text and nibbles swapped (e.g. `0x21` -> "12")
  - `A` replaced with `+`
  - final string trimmed to `nSize`

QR record transfer:
- Data frame: incoming `0xAB ... byte4=0xA8, byte3=0xFF`
  - `byte5`: QR index
  - `byte6..end`: QR string/link
- End marker: incoming `0xAB ... byte4=0xA8, byte3=0xFE`
  - `byte5`: number of links received

## 4.8 Alarms

Bluetooth aspects used:
- Phone sends watch alarm configuration packets to ESP32.
- Library stores alarm state and exposes getters/checkers.

Alarm config frame (phone -> ESP32):
- Incoming `0xAB ... byte4=0x73`
  - `byte6`: alarm index
  - `byte7`: enabled
  - `byte8`: hour
  - `byte9`: minute
  - `byte10`: repeat mask

Repeat mask interpretation in helper logic:
- `0x80` or `0x7F` treated as always/every-day active mode.
- Otherwise bits map to weekdays with Sunday on bit 6, Monday..Saturday on bits 0..5.

Note:
- Public API has local setters for alarms, but this implementation does not send alarm writes to phone.

## 5) Other Protocol Elements Used by Library

Not listed as top-level README features, but present in protocol:

- Sync request (ESP32 -> phone): `AB 00 03 FE 23 80`
- Sync completed (phone -> ESP32): incoming `byte4=0x20` with `byte3=0xFE`
- Watch info (ESP32 -> phone): opcode `0x92` fixed 20-byte capabilities block
- App version info (phone -> ESP32): opcode `0xCA`, then ESP32 sends long `0x92` text payload (`sendESP`)
- Chunk capability from phone: opcode `0xCC` (`byte5 != 0` enables chunk transfer mode)
- Touch forwarding from phone: opcode `0xBF`, includes touch state and XY
- Settings opcodes:
  - `0x74` user profile
  - `0x75` sedentary
  - `0x76` quiet hours
  - `0x77` raise-to-wake
  - `0x78` hourly measurement
  - `0x7B` language
  - `0x7C` 12/24h mode flag
  - `0x7F` sleep schedule
  - `0x9C` font/color

## 6) Direction Summary

- Phone -> ESP32
  - Always via RX characteristic writes.
  - Includes notifications/events, settings, weather, time, navigation, contacts/QR, battery info.

- ESP32 -> Phone
  - Always via TX characteristic notifications.
  - Includes watch info, watch battery, control actions, health/realtime records, requests/subscriptions.

## 7) Frame Examples (verbatim from implementation)

- Watch battery report:
  - `AB 00 05 FF 91 80 <charging> <level>`

- Ask phone battery updates:
  - `AB 00 04 FE 91 80 <0|1>`

- Request sync:
  - `AB 00 03 FE 23 80`

- Music next:
  - `AB 00 04 FF 9D 80 03`

- Find phone on:
  - `AB 00 04 FF 7D 80 01`

- Realtime HR upload:
  - `AB 00 05 FF 31 0A <hr> 1B`

## 8) Precision Notes / Caveats

- Length semantics are implementation-specific (`byte1..2 + 3`) and should be followed exactly for compatibility.
- Some fields in fixed command templates appear reserved/constant and are not decoded by this library.
- Endianness differs by field family:
  - Most integer fields are byte-packed manually (often big-endian semantics for multibyte integer reconstruction).
  - Weather location float coordinates are explicitly little-endian.
- Parser accepts both frame markers (`0xAB`, `0xEA`) and both class markers (`0xFE`, `0xFF`) for start-of-frame detection.
