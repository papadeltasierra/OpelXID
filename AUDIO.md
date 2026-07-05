# Acknowledgement

This document incorporates audio protocol reference material from the Chronos ESP32 ecosystem.

Acknowledged project: chronos-esp32 by fbiego
Repository: https://github.com/fbiego/chronos-esp32

OpelXID uses the same underlying wire protocols for compatibility with the Chronos mobile protocol behavior.

---

# Audio Metadata Over BLE (Track + Artist)

This protocol can carry audio metadata, but indirectly.

There is no dedicated now-playing frame with explicit track, artist, album, and playback-state fields. Instead, media text is conveyed over the general notification event channel using frame marker AB and opcode 0x72.

## Scope for implementers

For devices implementing this protocol, there are two separate audio-related paths:

1. Device to phone control commands (play, pause, previous, next, volume).
2. Phone to device notification events that may contain track and artist text.

These paths are independent: control opcodes do not inherently return a synchronized playback-state response.

## Audio control command family

Common control values in this protocol family include:
- 0x9D00 play
- 0x9D01 pause
- 0x9D02 previous
- 0x9D03 next
- 0x9900 toggle play/pause
- 0x99 A0 <level> set volume

## Metadata transport path (notification opcode 0x72)

Use a phone to device notification event:

- byte0: AB
- byte1..2: length field (assembled length follows the protocol rule len = (b1 << 8 | b2) + 3)
- byte3: class marker (typically FE for incoming events)
- byte4: 0x72
- byte5: reserved
- byte6: app/icon id
- byte7: state (0x02 is commonly used for normal notification insertion)
- byte8..end: text payload

## Recommended text conventions

To maximize interoperability across different device parsers, use stable text conventions:

1. Track and artist split with one colon:
- Track Name:Artist Name

2. Playback-state prefix when needed:
- Playing:Track Name:Artist Name
- Paused:Track Name:Artist Name

3. Keep the first separator early in the payload:
- Some implementations only auto-split when the first colon appears near the beginning.

## Conceptual frame example

Example text payload:
- Blinding Lights:The Weeknd

Conceptual bytes:
- AB ?? ?? FE 72 00 <icon> 02 42 6C 69 6E 64 69 6E 67 20 4C 69 67 68 74 73 3A 54 68 65 20 57 65 65 6B 6E 64

Notes:
- ?? ?? must be populated with the correct length value for the full frame.
- <icon> should be set to the sender-selected app/media icon id.

## Playback status (Playing vs Paused)

This protocol does not define a first-class playback-state field in a dedicated audio metadata opcode.

If state must be shown, include it in text payload conventions, for example:
- Playing:Track Name:Artist Name
- Paused:Track Name:Artist Name

## Interoperability guidance

- Treat audio metadata as notification content, not guaranteed structured media schema.
- Do not assume all peers support the same title/message splitting rules.
- Prefer backward-compatible text formatting over introducing custom opcodes unless both peers are updated together.

## Optional extension direction

For ecosystems that need richer media sync, define a new opcode with explicit fields:
- playback state
- track
- artist
- album
- progress and duration

Maintain compatibility by continuing to support opcode 0x72 fallback text payloads.