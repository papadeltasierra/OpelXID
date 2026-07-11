# OpelXID Basic Text Example

This example demonstrates the simplest use of the OpelXID library: sending a fixed text message and symbols to an Opel/Vauxhall MID or TID display.

## What It Does

The example:
1. Initializes the display interface with GPIO pins
2. Powers on the display (self-test sequence)
3. Sends "HELLO" text to the display with RDS and STEREO symbols enabled
4. Waits indefinitely (the display retains the text)

This is ideal for understanding the basic API and validating hardware wiring.

## Building and Running

### Prerequisites

- ESP-IDF installed and set up
- ESP32 board connected to your computer
- Display properly wired to the ESP32 (see [WIRING.md](../../WIRING.md))

### Build Steps

```bash
cd examples/basic_text

# Open configuration menu (optional, to change GPIO pins or display type)
idf.py menuconfig
# Navigate: OpelXID Basic Text Configuration
# - Select display type (TID-8 or TID-10/MID)
# - Set GPIO pins if different from defaults (21=SDA, 22=SCL, 23=MRQ)

# Build the project
idf.py build

# Flash to ESP32
idf.py flash

# Monitor output
idf.py monitor
# (Press Ctrl+] to exit monitor)
```

### Default Configuration

- **Display Type**: TID-10/MID (10-digit display)
- **GPIO Pins**: SDA=21, SCL=22, MRQ=23

These defaults work with the standard OpelXID wiring. Adjust via `idf.py menuconfig` if needed.

## Expected Output

```
I (XXX) basic_text: Starting OpelXID Basic Text
I (XXX) opel_mid: Initialized device (address 0x4D, 10 chars)
I (XXX) opel_mid: Sent power-on sequence
I (XXX) opel_mid: Sent frame: "HELLO     " (address=0x4D, symbols=0x00/0x00/0x84)
```

The display should show "HELLO" with RDS and STEREO icons lit.

## See Also

- [WIRING.md](../../WIRING.md) — How to physically connect the ESP32 to the display
- [examples/demonstration/README.md](../demonstration/README.md) — Interactive example with full feature showcase
- Main project [README.md](../../README.md)
