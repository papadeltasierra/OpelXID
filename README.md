# OpelXID

ESP-IDF component for driving Vauxhall/Opel MID and TID displays from an ESP32.

## Display variants

Three display types are supported. They share the same 3-wire bus (SDA, SCL, MRQ)
and the same underlying protocol, but differ in slave address, frame layout, and
the number of physical character positions.

### 8-digit TID (`OPEL_MID_TYPE_TID_8`)

Fitted to the **Astra F** and **Corsa B / Tigra**.

| Property | Value |
|---|---|
| Slave address | `0x4A` (74 decimal) |
| Symbol bytes per frame | 2 (Radio Status, Tape Status) |
| Data bytes per frame | 8 |
| Total bytes per frame | 1 (address) + 2 (symbols) + 8 (data) = **11** |

The two symbol bytes carry icons for RDS, TP, Stereo, AS, Dolby B/C, CPS, cr,
and CD-In. There is no CD Status byte. Any `cd` field in `opel_mid_symbols_t`
is silently ignored for this type.

### 10-digit TID (`OPEL_MID_TYPE_TID_10`)

Fitted to the **Astra G** and **Corsa C**.

| Property | Value |
|---|---|
| Slave address | `0x4D` (77 decimal) |
| Symbol bytes per frame | 3 (Radio Status, Tape Status, CD Status) |
| Data bytes per frame | 10 |
| Total bytes per frame | 1 (address) + 3 (symbols) + 10 (data) = **14** |

The third symbol byte adds CD-specific icons (Track, RDM, PGM, DISC). The two
extra data character positions mean the display can show wider text than the
8-digit variant without truncation or padding.

### MID (Multi Information Display)

The MID is the larger, dot-matrix instrument-cluster display found in some Astra G
and Zafira A models. It communicates on the **same 3-wire bus** and uses the
**same `0x4D` slave address and 10-byte frame format** as the 10-digit TID.

From the protocol perspective the MID and the 10-digit TID are therefore
**identical**; use `OPEL_MID_TYPE_TID_10` for both. The visual difference
(dot-matrix vs. segmented characters) is entirely internal to the display unit
and requires no driver changes.

### Summary

| Constant | Display | Address | Symbol bytes | Data bytes |
|---|---|---|---|---|
| `OPEL_MID_TYPE_TID_8` | Astra F, Corsa B, Tigra | `0x4A` | 2 | 8 |
| `OPEL_MID_TYPE_TID_10` | Astra G, Corsa C, MID | `0x4D` | 3 | 10 |
