# Wiring Guide

This document describes how to physically connect an ESP32 to an Opel/Vauxhall MID or TID display via the factory radio socket.

## Overview

The MID/TID display communicates via a 3-wire open-drain bus (SDA, SCL, MRQ) integrated into the factory radio connector, which provides: **GND, SDA, SCL, MRQ, D1, and no 5V output**. The display is powered externally by the vehicle; only signal lines are available at the radio socket.

To interface the ESP32 (3.3V logic) with the MID/TID (5V logic), a **bidirectional level shifter** is required. Power for the ESP32 and level shifter comes from a **12V → 5V buck converter** connected to the vehicle's power supply.

**Recommended Architecture** (using ESP32-S3-Zero):
- Vehicle 12V → Buck Converter (12V → 5V) → ESP32-S3-Zero USB input
- ESP32-S3-Zero 3.3V output → Level Shifter VCC-A (+ internal ESP32 use)
- Buck Converter 5V output → Level Shifter VCC-B
- Radio socket signals (SDA, SCL, MRQ) → Level Shifter A-side → B-side → Display

**Note**: Other power configurations are possible (detailed in "Alternative Power Configurations" section below).

## Recommended Hardware

### ESP32 Variant

We recommend the **ESP32-S3-Zero** as the base microcontroller:
- Very compact (25mm × 23mm)
- Native USB-C UART (no external serial adapter needed)
- Dual-core processor with FreeRTOS support
- GPIO pins suitable for open-drain mode
- Wide availability and low cost

Alternative: Any ESP32-S3 or standard ESP32 board will work, but may require an external USB-to-Serial adapter for console output.

### Level Shifter

Use a **bidirectional I2C level shifter IC** rated for 3.3V ↔ 5V conversion:

**Recommended**: TXS0108E (8-channel)
- **Why it works for open-drain**: The TXS0108E includes internal sensing logic that detects when a bus line is pulled low by the slave and automatically releases its output to let the pull-up resistor take over. This is exactly what open-drain buses need.
- Fast switching (~5ns propagation delay)
- Bus speeds: Rated for I2C up to 400 kHz; the MID/TID bus runs at ~20 kHz, well within margin
- **Important**: Buy from reputable distributors (TI, Digi-Key, Mouser, etc.) to avoid counterfeits

**Alternative: Resistive Voltage Dividers** (simpler, no IC needed)
- For 5V → 3.3V: Use a 22kΩ pull-down resistor from the 5V signal to GND. Output = ~3V (safe for ESP32)
- For 3.3V → 5V: Direct open-drain connection; slave's pull-up brings line to 5V
- Pros: Simple, cheap, no counterfeits
- Cons: Slower rise time (~10–100µs), more susceptible to noise over long cables
- Suitable for this application (bus speed is very slow)

**Connections per channel** (if using TXS0108E):
- A-side: ESP32 GPIO (3.3V)
- B-side: MID/TID signal (5V, pulled to 5V via slave resistors)
- GND: Common ground between level shifter, ESP32, and display
- OE (Output Enable): Connect to 3.3V (always enabled)

### Power Supply

**Primary Design** (ESP32-S3-Zero with USB input):
- **12V → 5V Buck Converter**:
  - Input: 10–16V (automotive 12V variation)
  - Output: 5V (carries ~200mA for ESP32 via USB + 100mA for level shifter B-side = ~300mA total)
  - Connect directly to vehicle's ignition or accessory power (via optional 3A fuse)
  - Popular modules: LM2596-based, MP1584-based, or pre-built "12V to 5V" modules

- **ESP32-S3-Zero Configuration**:
  - Power via USB input from the 5V buck converter (clean way to power a USB-capable board)
  - Internally regulates 5V → 3.3V
  - 3.3V output pin supplies the level shifter VCC-A

- **Level Shifter Power**:
  - VCC-A (3.3V): Directly from ESP32-S3-Zero 3.3V output pin
  - VCC-B (5V): Directly from the buck converter 5V output (same source as ESP32's USB input)
  - Decoupling: 0.1µF ceramic caps on both VCC-A and VCC-B, placed near the level shifter IC

**Fuse Protection** (Optional but recommended):
- Place a 3A fast-blow fuse on the 12V input to the buck converter
- Protects against short circuits in the ESP32, wiring, or buck converter
- Use a standard automotive fuse holder

**[Note: Test the buck converter before installation. Apply 12V and verify ~5V output with no load, then under 300mA load (e.g., ESP32 + test resistor)]**

## Pin Connections

### Radio Socket Wiring (TID-10/MID at Address 0x4D)

The factory radio socket provides **6 pins**: GND, SDA, SCL, MRQ, D1, and Shield/GND. Only GND, SDA, SCL, and MRQ are used for this project.

**Bus signals (5V radio socket ↔ 3.3V ESP32 via level shifter)**:

| Signal | Radio Socket | Level Shifter B (5V) | Level Shifter A (3.3V) | ESP32 GPIO |
|--------|--------------|----------------------|------------------------|-----------|
| GND    | Pin 1 (GND)  | GND                  | GND                    | GND       |
| SDA    | Pin 2 (SDA)  | Channel 1-B (5V)     | Channel 1-A (3.3V)     | GPIO 21   |
| SCL    | Pin 3 (SCL)  | Channel 2-B (5V)     | Channel 2-A (3.3V)     | GPIO 22   |
| MRQ    | Pin 4 (MRQ)  | Channel 3-B (5V)     | Channel 3-A (3.3V)     | GPIO 23   |
| D1     | Pin 5        | (not used)           | (not used)             | (not used) |
| Shield | Pin 6        | GND                  | GND                    | GND       |

**Power connections**:

| Source | Destination |
|--------|-------------|
| Vehicle 12V | Buck Converter VIN |
| Buck Converter VOUT (5V) | ESP32-S3-Zero USB input (VCC) |
| Buck Converter VOUT (5V) | Level Shifter VCC-B (+ 0.1µF cap) |
| ESP32-S3-Zero 3.3V out | Level Shifter VCC-A (+ 0.1µF cap) |
| Vehicle GND | All GND connections (radio socket, ESP32, level shifter) |

**Notes**:
- **No 5V available from the radio socket**; the 5V rail is generated locally by the buck converter
- All ESP32 GPIO pins should support **open-drain mode** (virtually all do; avoid strapping pins)
- GND must be common between: vehicle ground, radio socket, ESP32, level shifter, and buck converter
- **TID-8 variant** uses the same radio socket pinout but different protocol addressing (0x4A vs 0x4D)

### Full Schematic Diagram

```
Vehicle 12V Supply
        │
    [3A fuse]
        │
   ┌────┴──────────────────────────────────┐
   │  Buck Converter (12V → 5V)            │
   │  (LM2596, MP1584, or similar)         │
   │                                       │
   │ VIN (12V)                             │
   │ VOUT (5V) ──┬───[10µF + 0.1µF]───┐    │
   │ GND ──┐     │                    │    │
   └───────┼─────┼────────────────────┼────┘
           │     │                    │
   GND ────┼─────┼────────────────────┴─────┬──────────────────┐
           │     │                          │                  │
           │  [5V]                      Level Shifter          │
           │     │                      (TXS0108E)             │
           │     │                     ┌──────────────────┐    │
           │     └─────────────────────┤ VCC-B (5V)       │    │
           │                           │ [0.1µF cap]      │    │
           │   ESP32-S3-Zero           │                  │    │
           │   ┌─────────────────────┐ │ VCC-A (3.3V) ←───┼────┤┐
           │   │ USB (5V in) ←───────┼─┤ OE (enable):3.3V │    ││
           │   │ 3.3V out ───┬──[0.1µF cap]───────────┐   │    ││
           │   │             │                        │   │    ││
           │   │ GPIO 21 ────┼────────────────────────┤ A1↔B1 (SDA)──→ Radio
           │   │ GPIO 22 ────┼────────────────────────┤ A2↔B2 (SCL)    Socket
           │   │ GPIO 23 ────┼────────────────────────┤ A3↔B3 (MRQ)    (5V)
           │   │             │                        │   │    ││
           │   │ GND ────────┼────────────────────────┤ GND    ││
           │   └─────────────┼────────────────────────┘        ││
           │                 │                                 ││
           │                 └─────────────────────────────────┘│
           │                                                    │
           └────────────────────────────────────────────────────┘
                            │
                      Radio Socket
                   (GND, SDA, SCL, MRQ, D1)
```

**Key signal flow**:
1. Vehicle 12V → Buck Converter → 5V
2. 5V → ESP32-S3-Zero USB input (provides power)
3. 5V → Level Shifter VCC-B (powers B-side for 5V signals)
4. ESP32-S3-Zero 3.3V output → Level Shifter VCC-A (powers A-side for 3.3V logic)
5. GPIO 21/22/23 → Level Shifter A-side → B-side → Radio socket (open-drain signals)
6. Common GND throughout

### Alternative Power Configurations

The primary design above uses an ESP32-S3-Zero with a single 12V→5V buck converter. However, other configurations are possible depending on your ESP32 variant and specific circumstances:

### Option A: Single 12V→5V Buck Converter (Recommended for S3-Zero)
- **Best for**: ESP32-S3-Zero, ESP32-S3 with USB input, or any board with integrated 5V→3.3V regulator
- **Advantages**: Simple, one buck converter, clean USB power input
- **Implementation**: See "Primary Design" section above

### Option B: 12V→3.3V Buck Converter (if ESP32 has no USB input)
- **Best for**: Standard ESP32 (non-S3), older boards without USB regulators
- **Setup**:
  - 12V → 3.3V buck converter powers ESP32 VCC directly
  - For level shifter VCC-B (5V): Use a separate 12V→5V module OR use a voltage doubler circuit on the 3.3V output (more complex, not recommended)
  - Alternative: Use resistive dividers instead of a dedicated level shifter IC (trades simplicity for slower bus speed)
- **Advantages**: Single 3.3V rail, simpler power distribution
- **Disadvantages**: No USB power convenience, may need additional voltage generation for the 5V rail

### Option C: Two Buck Converters (if board integration is not available)
- **Best for**: Custom ESP32 boards, breadboard prototyping, maximum flexibility
- **Setup**:
  - 12V → 5V buck converter (300mA) for ESP32 USB + level shifter VCC-B
  - 12V → 3.3V buck converter (100mA) alternative for boards that prefer dedicated 3.3V input
  - Or: 12V → 5V → onboard regulator → 3.3V (if your board has integrated regulation)
- **Advantages**: Highest flexibility, independent power rails
- **Disadvantages**: More complex, more ICs, takes up more space

### Option D: Resistive Divider for Level Shifter (if simplicity is priority)
- **Best for**: Prototype/development, non-critical applications, low noise environments
- **Setup**:
  - Use single 12V→3.3V buck converter for ESP32
  - For 5V logic signaling: Use resistive voltage dividers (22kΩ pull-down from 5V signal to GND for 3.3V→5V safety; open-drain output naturally raises to 5V via slave pull-up)
  - Level shifter IC not needed
- **Advantages**: Fewest components, cheapest, still works at ~20 kHz bus speed
- **Disadvantages**: Slower signal rise time, more susceptible to noise, unsuitable for long cable runs

**Recommendation**: Use **Option A** (12V→5V buck converter + S3-Zero) for the best balance of simplicity, reliability, and integration. Other options work but add complexity or limitations.

### Display Specifications

| Variant | Address | Radio Socket Pinout | Pull-up Resistors* | Notes |
|---------|---------|-------|-----|-------|
| TID-8 (Astra F, Corsa B, Tigra) | 0x4A | 6-pin (GND, SDA, SCL, MRQ, D1, Shield) | ~10kΩ to 5V (display side) | [Verify pinout against your display] |
| TID-10 / MID (Astra G, Corsa C) | 0x4D | 6-pin (GND, SDA, SCL, MRQ, D1, Shield) | ~10kΩ to 5V (display side) | [Verify specifications; may vary by model] |

*Pull-up resistance on the display's slave side (inside the display module); affects bus rise time and current draw

**[Important]**: The radio socket provides only GND, SDA, SCL, MRQ, and D1. No 5V is available from the socket; all 5V power must be generated locally by the buck converter.

### Capacitor Placement (Decoupling & Stability)

Decoupling capacitors are **critical for stable operation**. Place them close to the power pins of each IC:

**Buck Converter (12V → 5V output)**:
- Input side: Follow datasheet (typically 10µF + 0.1µF)
- Output side: 10µF electrolytic + 0.1µF ceramic capacitors placed within 1 cm of output
  - 10µF: Large capacitance for bulk energy storage
  - 0.1µF: High-frequency noise filtering
  - Verify capacitor voltage ratings (16V or higher for safety margin on 5V rail)

**Level Shifter (TXS0108E)**:
- 0.1µF ceramic capacitor on VCC-A (3.3V) pin, placed within 0.5 cm
- 0.1µF ceramic capacitor on VCC-B (5V) pin, placed within 0.5 cm
- Both should be placed as close as possible to reduce ground loop inductance

**ESP32-S3-Zero**:
- Typically already has 0.1µF capacitor on USB VCC (verify on board schematic)
- Additional cap not usually needed, but verify

**Common Ground**: Solder all GND connections together at a single point (preferably between the buck converter and level shifter), then run a single ground wire back to the vehicle GND.

### Cable & Connector Specifications

**Radio Socket Connector**:
- **Type**: [Verify against your specific vehicle's factory radio connector; likely ISO/DIN or proprietary 6-pin]
- **Pins used**: GND (pin 1), SDA (pin 2), SCL (pin 3), MRQ (pin 4), Shield/GND (pin 6)
- **Pin 5 (D1)**: Not used for this project
- **Note**: Check your vehicle's wiring diagram; pin assignments may vary by model year

**Signal Cable**:
- **Wire gauge**:
  - For distances up to 1 meter: 22 AWG (0.6 mm²) is adequate
  - For distances 1–2 meters: 20 AWG (0.5 mm²) recommended
  - Ground return: Same gauge as signal wires
- **Signal integrity**:
  - Keep SDA, SCL, MRQ wires together (not necessarily twisted for this slow 20 kHz bus speed)
  - Optional shielding: Not required, but recommended if running cable through engine bay near ignition/alternator
- **Maximum cable length**: Up to several meters should work at this bus speed; verify with oscilloscope if you see timing issues

**Power Cable** (Vehicle 12V to Buck Converter):
- **Wire gauge**: 18–20 AWG (1.0–0.5 mm²) for ~3A max current
- **Protection**: Fuse holder rated for 3A @ 12V, placed immediately after the battery/power source
- **Connectors**: Use crimp terminals or solder + heat shrink; automotive-grade for reliability
- MID/TID protocol timing: 50µs minimum high/low period
- **Conclusion**: Cable length is non-critical for this slow bus; lengths up to several meters should work. Verify with oscilloscope if you experience timing issues.

### Serial Console (Optional)

If your ESP32 lacks native USB UART (e.g., standard ESP32 or custom board):

| Signal | ESP32     | USB-to-Serial Adapter |
|--------|-----------|----------------------|
| TX     | GPIO 1*   | RX                   |
| RX     | GPIO 3*   | TX                   |
| GND    | GND       | GND                  |

*Default ESP-IDF UART pins; check your board's pinout or reconfigure in menuconfig if different.

### Power Connections

```
Vehicle 12V ──[Fuse 1-2A]──┬─→ Buck Converter (12V input)
                           │
                           └──[GND return to vehicle]

Buck Converter (5V output) ──┬─→ Level Shifter VCC-B
                           └──→ MID/TID Pin 5 (5V supply)

ESP32 3.3V supply ────────────→ Level Shifter VCC-A

Common GND:
  - ESP32 GND
  - Level Shifter GND
  - Buck Converter GND
  - Vehicle 12V GND (tied together)
  - MID/TID Pin 1 (GND)
```

## GPIO Pin Constraints

**ESP32 standard (ESP32-WROOM, ESP32-DevKit)**:
- GPIO 0, 2, 4, 5, 12, 15: Strapping pins; may have boot-time side effects
- GPIO 34–39 (ADC2 pins on regular ESP32): Input-only, cannot be driven as outputs
- **Recommended for open-drain**: GPIO 18, 19, 21, 22, 23, 25, 26, 27 (all support output and open-drain mode)

**ESP32-S3 (including S3-Zero)**:
- GPIO 0: Download mode (typically low to enter bootloader)
- GPIO 46: Output-only, cannot be used as input
- GPIO 19, 20: USB-UART (don't use if you need console)
- **Recommended for open-drain**: GPIO 21, 22, 23, 24, 25 (same as above for safety)

**[Verify against your specific board's schematic and datasheet]**

The default example uses GPIO 21 (SDA), GPIO 22 (SCL), GPIO 23 (MRQ), which are safe on virtually all ESP32/S3 variants.

To use different pins (e.g., if your board has different pin labels or conflicts):

```bash
cd examples/basic_text    # or examples/demonstration
idf.py menuconfig
# Navigate: OpelXID Basic Text Configuration → GPIO Pins
# Select pins that are output-capable and not strapping pins
# (Verify against your specific board's documentation)
idf.py build && idf.py flash
```

You'll need to update your physical wiring to match the new GPIO assignments.

**[Important]**: When selecting pins, consult your board's schematic to ensure:
- The pin is output-capable
- The pin is not a strapping pin (especially GPIO 0, 2, 12, 15 for regular ESP32)
- The pin is not needed for USB or other critical functions

## Troubleshooting

### No Response from Display

1. **Verify voltage levels**:
   - Measure 12V at the vehicle power source (should be 12V ±1V)
   - Check buck converter 5V output (should be 5.0V ±0.1V under load)
   - Measure ESP32-S3-Zero 3.3V output (should be 3.3V ±0.1V)
   - Measure radio socket signals: idle state should show 5V on SDA/SCL/MRQ at the socket

2. **Check GPIO pin configuration and radio socket connection**:
   - Verify menuconfig settings match your physical wiring (GPIO 21/22/23 by default)
   - Verify radio socket is properly connected (pin order: GND=1, SDA=2, SCL=3, MRQ=4, D1=5, Shield=6)
   - Re-flash after any pin changes: `idf.py build && idf.py flash`
   - Check serial console output during initialization (should log "Initialized device")

3. **Inspect level shifter and connections**:
   - Verify all connections are soldered correctly (no cold solder joints)
   - Test continuity of each signal path: radio socket → level shifter → ESP32 GPIO
   - Confirm GND is tied together: vehicle GND, fuse holder, buck converter, ESP32, level shifter, radio socket
   - Measure <1Ω resistance between any two GND points
   - If using TXS0108E, verify OE (Output Enable) pin is connected to 3.3V (should be logic high)

4. **Test the radio socket independently**:
   - Disconnect level shifter from radio socket temporarily
   - Use a voltmeter to measure radio socket pins directly (SDA/SCL/MRQ should idle at ~5V if the display is powered)
   - If socket pins read 0V or are missing, the display may not be powered or the connector contact is bad

### Partial or Garbled Display Output

- **Timing issues**: Use oscilloscope to verify the bus signals are rising cleanly (no slow rise time, no ringing). If rise time is >1ms, add a smaller pull-up resistor (try 4.7kΩ instead of ~10kΩ, but verify the display can supply the resulting current).
  - **[Verify pull-up values against your display's datasheet]**
- **Level shifter propagation delay**: TXS0108E adds ~5ns delay (negligible). If you're using a different shifter or resistive dividers, verify the data sheet.
- **Short circuits**: Check for stray solder, overlapping traces, or bent pins causing bus contention (lines stuck low)

### "Handshake timeout" Errors

- **Missing pull-ups**: Confirm the MID/TID display is providing its slave pull-up resistors (typical ~10kΩ to 5V, actual value varies by model)
  - **[Verify pull-up values for your specific display]**
  - Measure B-side idle voltage: should be 5V, not lower (if <3V, suspect missing pull-ups)
- **Loose connections**: Reseat all wiring, especially on open-drain pins. Check for corrosion on connectors.
- **Level shifter not powered**:
  - Verify VCC-A (3.3V) is connected and stable
  - Verify VCC-B (5V) is connected and stable
  - Check that decoupling capacitors are soldered in place
- **Display not powered or defective**:
  - Measure voltage at display Pin 5 (should be 5V)
  - If the display has an on/off switch or indicator, verify it's powered
  - **[Consult your display's documentation for power-up sequences or self-test modes]**

### Boot or Stability Issues

- **ESP32-S3-Zero won't boot**: May indicate power supply issue or strapping pin conflict
  - Verify GPIO pins are not strapping pins (GPIO 0, 46 for S3-Zero; GPIO 0, 2, 12, 15 for regular ESP32)
  - Measure 5V at ESP32 USB input (should be 5.0V ±0.2V); if <4.5V or >5.5V, the buck converter is out of spec
  - Measure 3.3V output from ESP32 (should be 3.3V ±0.2V); if missing, the board's internal regulator may be damaged
  - Check for solder bridges or loose wires on power pins

- **Random resets or glitches on the bus**: Often caused by weak decoupling or power supply noise
  - Verify 10µF + 0.1µF capacitors are soldered on buck converter output
  - Verify 0.1µF capacitors are soldered on both VCC-A and VCC-B pins of the level shifter
  - Ensure GND connections are solid and short (measure <1Ω resistance between GND points)
  - Check that the buck converter can supply 300mA under load (some cheap modules can't)
  - In vehicle: Keep 12V power wire away from signal wires; poor shielding can couple engine noise

### Debugging with Serial Monitor

Enable verbose logging in ESP-IDF to see detailed protocol messages:

```bash
idf.py menuconfig
# Navigate: Component config → Log output → Default log verbosity
# Set to DEBUG or VERBOSE
idf.py build && idf.py flash && idf.py monitor
```

Watch for timeout messages and the byte where transmission fails.

## Important Notes & Disclaimers

### Hardware Verification Required

The specifications in this document are based on typical Opel/Vauxhall MID/TID displays and factory radio socket wiring. **Your specific vehicle may differ**. Before wiring to a live vehicle, you should:

1. **Test the buck converter first** (critical):
   - Apply 12V to the input and measure 5.0V ±0.2V at the output (no-load)
   - Add a 100Ω dummy load (~50mA) and remeasure (should stay within 5.0V ±0.2V)
   - Add a 50Ω dummy load (~100mA) and remeasure (should stay within 5.0V ±0.2V)
   - Verify output voltage stability and no unusual noise/ripple on an oscilloscope if available
   - **Do not proceed with installation if the converter fails any of these tests**

2. **Consult your vehicle's wiring diagram** (if available):
   - Verify the radio socket pinout matches documentation (pin assignments may vary by year/model)
   - Check if the radio is always powered or only when ignition is on
   - Confirm GND connection path (through the radio chassis or dedicated wire)

3. **Verify GPIO pin compatibility** with your specific ESP32 board:
   - Check the board's schematic for pin assignments and any special constraints
   - Avoid strapping pins and pins used for USB/console
   - Measure actual voltage levels during operation (3.3V on GPIO side, 5V at radio socket side)

### Timing Assumptions

This document and the library assume:
- **Display protocol timing**: 50µs minimum SCL high/low period (~20 kHz clock)
- **MRQ handshake timing**: 100µs pulse width, 2ms gaps
- **Bus pull-up time constant (RC)**: ~1ms rise time (τ = R × C with ~10kΩ pull-ups and typical PCB capacitance)

If your display operates at significantly different speeds, the library timing constants (T_* macros in opel_mid.c) may need adjustment. **[Verify against actual oscilloscope measurements on your hardware]**

### Component Selection Notes

- **Buck Converter (12V → 5V)**: Select a quality module rated for 12V automotive input and 5V output with ≥300mA capacity. Popular options:
  - **LM2596-based modules** — Classic, widely available, reliable, needs some careful layout for stability
  - **MP1584-based modules** — More modern, better efficiency, very compact, but more critical layout
  - **Pre-built "12V to 5V" modules** — Search online for USB car charger-style modules; many work well but test output voltage first
  - Recommended: Modules with integrated input/output capacitors and tested specs
  - **Test before installation**: Apply 12V and verify ~5.0V ±0.1V output with no load; retest under 300mA load (e.g., ESP32 powered)

- **Level Shifter (TXS0108E)**: Counterfeit rates are high. Buy from reputable distributors (TI, Digi-Key, Mouser) and verify package markings.

- **Fuse**: Use a 3A fast-blow automotive fuse in a proper fuse holder (ATC/ATO style). Never improvise with wire or foil.

- **Capacitors**: Use high-quality ceramic capacitors (X7R or better). For electrolytic caps on the buck converter, ensure ratings of 16V or higher.

### Known Limitations

- **This library does not support multiple displays on a single bus** (each would need its own GPIO pins for SDA/SCL/MRQ)
- **Display text and symbols are non-persistent** across power loss; you'll need to resend after power-up
- **No rate limiting**: You can send updates as fast as the ESP32 can transmit (likely >100 Hz), but the display may not keep up

## Safety Considerations

- **Voltage mismatch**: The radio socket operates at 5V and the ESP32 at 3.3V. **Always use a level shifter** (TXS0108E) to convert between them. Never connect 5V signals directly to ESP32 GPIO pins—this will damage the ESP32.

- **Buck converter selection**: Use a quality 12V→5V converter rated for automotive input. Verify:
  - Input voltage range: 10–16V minimum (covers typical automotive 12V variation)
  - Output voltage accuracy: 5.0V ±0.3V or better
  - Sufficient current capacity: ≥300mA (for ESP32 ~80mA + level shifter <10mA)
  - Thermal protection and short-circuit protection (check datasheet)
  - **Test on a bench supply before vehicle installation**

- **Fuse protection**: A 3A fast-blow fuse on the 12V input is recommended to protect against short circuits in the buck converter, ESP32, or wiring. Use a proper automotive fuse holder; never improvise.

- **Common ground is critical**: **Ensure GND is connected between vehicle 12V return, buck converter, ESP32, level shifter, and radio socket GND**. This is the reference for all signal levels. A missing or weak GND connection is a leading cause of communication failures.

- **Do not connect to a live vehicle until bench testing is complete**:
  - Test the buck converter with a bench supply (12V input, verify 5V output)
  - Test the ESP32 and level shifter on the bench before soldering to the vehicle
  - Only connect to the vehicle's radio socket after all individual components have been validated

- **Do not power-cycle the ESP32 while connected to the radio socket**: If the ESP32 reboots or enters bootloader mode, GPIO pins may briefly output undefined logic levels, causing bus contention (both master and slave pulling lines). Always ensure the ESP32 is booted and stable before risking the display.

- **Cable routing**: Keep the 12V power cable separate from signal wires (SDA/SCL/MRQ) to minimize electromagnetic interference. In-vehicle, use shielded twisted pair or conduit for signal wires running through the engine bay.

- **Capacitor safety**: Double-check capacitor orientation when soldering (electrolytic capacitors are polarized). Verify capacitor voltage ratings: use 16V or higher for capacitors on the 5V rail.

- **Disclaimer**: This library and wiring guide are provided as-is. **You are responsible for verifying compatibility with your specific vehicle and display**. Improper wiring can damage the display, ESP32, or vehicle electronics. If you're uncomfortable with automotive electrical work, consult a professional.

## See Also

- [README.md](README.md) — Project overview
- [examples/basic_text/README.md](examples/basic_text/README.md) — Building and running the minimal example
- [examples/demonstration/README.md](examples/demonstration/README.md) — Interactive feature demonstration
