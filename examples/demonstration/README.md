# OpelXID Demonstration Example

This example provides an interactive demonstration of all OpelXID library features, designed for exploring the display capabilities and documenting 7-bit code mappings.

## What It Does

The demonstration offers an interactive menu with five demonstrations:

1. **Character Set** — Displays all printable ASCII characters (0x20–0x7E) in groups matching your display width. Useful for verifying character rendering.

2. **Non-printable Codes** — Tests non-printable 7-bit payload values (0x00–0x1F and 0x7F). As you observe the hardware behavior, the built-in table documents which codes display correctly and what they show.

3. **All Symbols** — Toggles each symbol individually to verify all symbol flags work correctly (RDS, TP, STEREO, AS, etc.). Press return between each test.

4. **Edge Cases** — Tests boundary conditions like empty strings, text longer than display width, and non-printable character substitution.

5. **Run All** — Executes all demonstrations in sequence.

6. **Time Sync (UTC)** — Prompts for a UTC timestamp in `YYYYMMDDTHHmmss` format and sends a hardware time-sync update (`0x60` command). Century (`YYYY` -> `YY`) and seconds are ignored by protocol.

## Building and Running

### Prerequisites

- ESP-IDF installed and set up
- ESP32 board connected to your computer
- Display properly wired to the ESP32 (see [WIRING.md](../../WIRING.md))
- Serial connection to monitor output and provide interactive input

### Build Steps

```bash
cd examples/demonstration

# Open configuration menu to match your hardware
idf.py menuconfig
# Navigate: OpelXID Demonstration Configuration
# - Select display type (TID-8 or TID-10/MID)
# - Set GPIO pins to match your wiring (defaults: 21=SDA, 22=SCL, 23=MRQ)

# Build the project
idf.py build

# Flash to ESP32
idf.py flash

# Start interactive monitoring
idf.py monitor
# (Press Ctrl+] to exit monitor)
```

### Default Configuration

- **Display Type**: TID-10/MID (10-digit display)
- **GPIO Pins**: SDA=21, SCL=22, MRQ=23

Modify via `idf.py menuconfig` if your hardware differs.

## Interactive Menu

After flashing, the demonstration displays a menu:

```
╔════════════════════════════════════════╗
║   OpelXID MID/TID Demonstration Menu   ║
╠════════════════════════════════════════╣
║ 1. Character Set (all printable ASCII) ║
║ 2. Non-printable codes (0x00–0x1F,7F) ║
║ 3. All Symbols (Radio/Tape/CD)        ║
║ 4. Edge Cases                          ║
║ 5. Run All Demonstrations             ║
║ 6. Time Sync (UTC timestamp)          ║
║ 7. Exit                                ║
╚════════════════════════════════════════╝

Select (1–7): _
```

Select a demonstration by number and press return. Each test prompts you to press return before proceeding to the next action, giving you time to observe the display output.

## Discovering Non-Printable Codes

As you test characters with option 2, the code includes a table structure ready to document findings:

```c
struct {
    uint8_t code;
    uint8_t display_output;
    const char *note;
} char_map[];
```

When you discover which control/DEL codes produce useful or unusual output, you can add entries to this table to build a mapping for your specific hardware.

## Example Usage

```
Select (1–7): 1
Showing all printable characters in groups of 10:

0x20-0x29: ' !"#$%&' ()'
Press return to continue... _

0x2A-0x33:  *+,-./0123
Press return to continue... _

...
```

### Time Sync Example

```
Select (1–7): 6

=== HARDWARE TIME SYNC (0x60) ===
Enter UTC timestamp as YYYYMMDDTHHmmss (example: 20260714T154500).
Century and seconds are ignored by the display protocol.

Timestamp: 20260714T154500
Time sync sent: input=20260714T154500 -> day=14 month=07 year=26 hour=15 minute=45 (seconds ignored: 00)
```

## See Also

- [WIRING.md](../../WIRING.md) — How to physically connect the ESP32 to the display
- [examples/basic_text/README.md](../basic_text/README.md) — Minimal "Hello World" example
- Main project [README.md](../../README.md)
