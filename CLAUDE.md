# AetherSDR — Project Context for Claude

## Project Goal

Replicate the **Windows-only FlexRadio SmartSDR client** (written in C#) as a
**Linux-native C++ application** using Qt6 and C++20. The aim is to mirror the
look, feel, and every function SmartSDR is capable of. The reference radio is a
**FLEX-8600 running firmware v1.4.0.0**.

## AI Agent Guidelines

When helping with AetherSDR:
- Prefer C++20 / Qt6 idioms (std::ranges, concepts if clean, Qt signals/slots over lambdas when possible)
- Keep classes small and single-responsibility
- Use RAII everywhere (no naked new/delete)
- Comment non-obvious protocol decisions with firmware version
- When suggesting code: show **diff-style** changes or full function/class if small
- Test suggestions locally if possible (assume Arch Linux build env)
- Never suggest Wine/Crossover workarounds — goal is native
- Flag any proposal that would break slice 0 RX flow
- If unsure about protocol behavior → ask for logs/wireshark captures first

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/AetherSDR
```

Dependencies (Arch): `qt6-base qt6-multimedia cmake ninja pkgconf`

Current version: **0.1.8** (set in both `CMakeLists.txt` and `README.md`).

---

## Architecture Overview

```
src/
├── main.cpp
├── core/
│   ├── RadioDiscovery      — UDP 4992 broadcast listener, emits radioDiscovered/Lost
│   ├── RadioConnection     — TCP 4992 command channel, V/H/R/S/M SmartSDR protocol
│   ├── CommandParser       — Stateless protocol line parser + command builder
│   ├── PanadapterStream    — VITA-49 UDP receiver: routes FFT, waterfall, audio by PCC
│   └── AudioEngine         — QAudioSink push-fed by PanadapterStream (RX); TX stub
├── models/
│   ├── RadioModel          — Central state: owns connection, slices, panadapter config
│   ├── SliceModel          — Per-slice state (freq, mode, filter, DSP, RIT/XIT, etc.)
│   ├── MeterModel          — Meter definition registry + VITA-49 value conversion
│   └── TransmitModel       — Transmit state, internal ATU, TX profile management
└── gui/
    ├── MainWindow          — Dark-themed QMainWindow, wires everything together
    ├── ConnectionPanel     — Radio list + connect/disconnect button
    ├── FrequencyDial       — Custom 9-digit MHz display with click/scroll/keyboard tuning
    ├── SpectrumWidget      — FFT spectrum + scrolling waterfall + frequency scale
    ├── AppletPanel         — Toggle-button column of applet panels (ANLG, RX, TX, etc.)
    ├── SMeterWidget        — Analog S-Meter gauge with peak hold (toggled by ANLG button)
    ├── RxApplet            — Full RX controls: antenna, filter, AGC, AF gain, pan, DSP, RIT/XIT
    ├── TxApplet            — TX controls: power gauges/sliders, profiles, ATU, TUNE/MOX
    ├── TunerApplet         — 4o3a TGXL tuner: gauges, relay bars, TUNE/OPERATE
    ├── PhoneCwApplet       — P/CW mic controls: level/compression gauges, mic profile, PROC/DAX/MON
    └── HGauge.h            — Shared horizontal gauge widget (header-only)
```

### Data Flow

```
UDP bcast (4992)  →  RadioDiscovery  →  ConnectionPanel (GUI)
TCP (4992)        →  RadioConnection →  RadioModel → SliceModel → GUI
UDP VITA-49 (4991)→  PanadapterStream
                       ├── PCC 0x8003 (FFT bins)      → SpectrumWidget.updateSpectrum()
                       ├── PCC 0x8004 (waterfall tiles)→ SpectrumWidget.updateWaterfallRow()
                       ├── PCC 0x8002 (meter data)     → MeterModel.updateValues()
                       ├── PCC 0x03E3 (audio float32)  → AudioEngine.feedAudioData()
                       └── PCC 0x0123 (audio int16)    → AudioEngine.feedAudioData()
```

---

## SmartSDR Protocol (Firmware v1.4.0.0)

### Message Types

| Prefix | Dir | Meaning |
|--------|-----|---------|
| `V` | Radio→Client | Firmware version |
| `H` | Radio→Client | Hex client handle |
| `C` | Client→Radio | Command: `C<seq>\|<cmd>\n` |
| `R` | Radio→Client | Response: `R<seq>\|<hex_code>\|<body>` |
| `S` | Radio→Client | Status: `S<handle>\|<object> key=val ...` |
| `M` | Radio→Client | Informational message |

### Status Object Names

Object names are **multi-word**: `slice 0`, `display pan 0x40000000`,
`display waterfall 0x42000000`, `interlock band 9`. The parser finds the split
between object name and key=value pairs by locating the last space before the
first `=` sign.

### Connection Sequence

1. TCP connect → radio sends `V<version>` then `H<handle>`
2. Subscribe: `sub slice all`, `sub pan all`, `sub tx all`, `sub atu all`,
   `sub meter all`, `sub audio all`
3. `client gui` + `client program AetherSDR` + `client station AetherSDR`
4. Bind UDP socket, send `\x00` to radio:4992 (port registration)
5. `client udpport <port>` (returns error 0x50001000 on v1.4.0.0 — expected)
6. `slice list` → if empty, create default slice (14.225 MHz USB ANT1)
7. `stream create type=remote_audio_rx compression=none` → radio starts sending
   VITA-49 audio to our UDP port

### Firmware v1.4.0.0 Quirks

- `client set udpport` returns `0x50001000` — use the one-byte UDP packet method
- `display panafall create` returns `0x50000016` — use `panadapter create`
- Slice frequency is `RF_frequency` (not `freq`) in status messages
- All VITA-49 streams use `ExtDataWithStream` (type 3, top nibble `0x3`)
- Streams are discriminated by **PacketClassCode** (PCC), NOT by packet type

---

## VITA-49 Packet Format

### Header (28 bytes)

Words 0–6 of the VITA-49 header. Key field: **PCC** in lower 16 bits of word 3.

### Packet Class Codes

| PCC | Content | Payload Format |
|------|---------|---------------|
| `0x8003` | FFT panadapter bins | uint16 big-endian, linear map to dBm |
| `0x8004` | Waterfall tiles | 36-byte sub-header + uint16 bins |
| `0x03E3` | RX audio (uncompressed) | float32 stereo, big-endian |
| `0x0123` | DAX audio (reduced BW) | int16 mono, big-endian |
| `0x8002` | Meter data | N × (uint16 meter_id, int16 raw_value), big-endian |

### FFT Bin Conversion

```
dBm = min_dbm + (sample / 65535.0) × (max_dbm − min_dbm)
```

`min_dbm` / `max_dbm` come from `display pan` status messages (typically -135 / -40).

### FFT Frame Assembly

FFT data may span multiple VITA-49 packets. A 12-byte sub-header at offset 28
contains: `start_bin_index`, `num_bins`, `bin_size`, `total_bins_in_frame`,
`frame_index`. `PanadapterStream::FrameAssembler` stitches partial frames.

### Waterfall Tile Format

36-byte sub-header at offset 28:

| Offset | Type | Field |
|--------|------|-------|
| 0 | int64 | FrameLowFreq |
| 8 | int64 | BinBandwidth |
| 16 | uint32 | LineDurationMS |
| 20 | uint16 | Width |
| 22 | uint16 | Height |
| 24 | uint32 | Timecode |
| 28 | uint32 | AutoBlackLevel |
| 32 | uint16 | TotalBinsInFrame |
| 34 | uint16 | FirstBinIndex |

Payload: `Width × Height` uint16 bins (big-endian). Conversion:

```
intensity = static_cast<int16>(raw_uint16) / 128.0f
```

This yields an **arbitrary positive intensity scale** (NOT actual dBm).
Observed values: noise floor ~96–106, signal peaks ~110–115 on HF.
The waterfall colour range is calibrated to [104, 120] by default and is
**decoupled** from the FFT spectrum's dBm range.

### Audio Payload

- PCC 0x03E3: big-endian float32 stereo → byte-swap uint32, memcpy to float,
  scale to int16 for QAudioSink (24 kHz stereo)
- PCC 0x0123: big-endian int16 mono → byte-swap, duplicate to stereo

### Meter Data Payload (PCC 0x8002)

Payload is N × 4-byte pairs: `(uint16 meter_id, int16 raw_value)`, big-endian.
Value conversion depends on unit type (from FlexLib Meter.cs):

| Unit | Conversion |
|------|-----------|
| dBm, dB, dBFS, SWR | `raw / 128.0f` |
| Volts, Amps | `raw / 1024.0f` (v1.4.0.0) |
| degF, degC | `raw / 64.0f` |

### Meter Status (TCP)

Meter definitions arrive via TCP status messages with `#` as KV separator
(NOT spaces like other status objects). Format:
`S<handle>|meter 7.src=SLC#7.num=0#7.nam=LEVEL#7.unit=dBm#7.low=-150.0#7.hi=20.0`

The S-Meter is the "LEVEL" meter from source "SLC" (slice).

### Stream IDs (observed)

- `0x40000000` — panadapter FFT (same as pan object ID)
- `0x42000000` — waterfall tiles
- `0x04xxxxxx` — remote audio RX (dynamically assigned)
- `0x00000700` — meter data

---

## GUI Design

### Theme

Dark theme: background `#0f0f1a`, text `#c8d8e8`, accent `#00b4d8`, borders `#203040`.

### Layout

Three-pane horizontal splitter:
1. **ConnectionPanel** (left) — radio list, connect/disconnect
2. **Center** — SpectrumWidget (top: FFT 40%, bottom: waterfall 60%, frequency
   scale bar 20px), FrequencyDial below, mode selector, TX button, volume controls
3. **AppletPanel** (right, 260px fixed) — toggle-button row (ANLG, RX, TX, PHNE, P/CW, EQ),
   S-Meter gauge (toggled by ANLG), scrollable applet stack below

### SpectrumWidget

- FFT spectrum: exponential smoothing (α=0.35), dB grid every 20 dB, freq grid ~50 kHz
- Waterfall: 7-stop colour gradient (black→dark blue→blue→cyan→green→yellow→red)
- Overlays: filter passband (semi-transparent), slice center line (orange + triangle)
- Mouse: click-to-tune (snapped to step size), scroll-wheel tunes by step size
- Native waterfall tiles (PCC 0x8004) suppress FFT-derived waterfall rows

### FrequencyDial

- 9 digits: `XXX.XXX.XXX` (MHz.kHz.Hz)
- Click top/bottom half of digit to tune up/down by that place value
- Scroll wheel over digit tunes that specific digit
- Scroll wheel elsewhere tunes by step size
- Double-click for direct text entry
- Range: 0.001–54.0 MHz

### SMeterWidget (ANLG applet)

- Analog gauge: 180° arc, S0 (left) to S9+60 (right)
- S0–S9 white markings (6 dB per S-unit), S9+10/+20/+40/+60 red markings
- Needle with shadow, center dot, exponential smoothing (α=0.3)
- Peak hold marker (orange triangle) with decay (0.5 dB/50ms) and 10s hard reset
- Text readouts: S-units (top-left, cyan), source label (top-center), dBm (top-right)
- Scale mapping: S0–S9 occupies left 60% of arc, S9–S9+60 occupies right 40%
- Toggled by ANLG button (visible by default)

### RxApplet Controls

Header row → step stepper → filter presets → AGC mode+threshold →
AF gain + audio pan → squelch → NB/NR/ANF DSP toggles → RIT → XIT.

Step sizes: 10, 50, 100, 250, 500, 1000, 2500, 5000, 10000 Hz.

---

## Key Implementation Patterns

### GUI↔Radio Sync (No Feedback Loops)

- `SliceModel` setters emit `commandReady(cmd)` → `RadioModel` sends to radio
- Radio status pushes update `SliceModel` via `applyStatus(kvs)`
- `MainWindow` uses `m_updatingFromModel` guard to prevent echoing model updates
  back to the radio
- `RxApplet` uses `QSignalBlocker` extensively when updating UI from model state

### Auto-Reconnect

`RadioModel` has a 3-second `m_reconnectTimer` for unexpected disconnects.
Disabled by `m_intentionalDisconnect` flag on user-initiated disconnect.

### SmartConnect

If the radio already has slices (`slice list` returns IDs), `RadioModel` fetches
them with `slice get <id>` rather than creating new ones.

---

## Known Bugs

- **Tuner applet SWR capture**: The final SWR displayed after a TGXL autotune
  cycle is inaccurate. During tuning the SWR meter streams via VITA-49 UDP while
  relay status arrives via TCP — there is a race between tuning=0 (TCP) and the
  final settled SWR meter reading (UDP). Current approach tracks the minimum SWR
  seen during tuning, but this captures mid-search transients (~1.5x) rather than
  the actual settled result (~1.01–1.15). Needs investigation with timestamped
  meter logging to understand the exact arrival order of SWR values relative to
  the tuning=0 status change. See `TunerApplet::updateMeters()` and the
  `tuningChanged` lambda in `TunerApplet::setTunerModel()`.

---

## Known Quirks / Lessons Learned

- `QMap<K,V>` needs `#include <QMap>` in headers — forward-declaration in
  `qcontainerfwd.h` leaves the field as incomplete type
- `static constexpr` class members are private by default; file-scope
  `constexpr` copies needed for free functions in the same .cpp
- Qt6: `Qt::AA_UseHighDpiPixmaps` removed — do not use
- Qt6: `QMenu::addAction(text, obj, slot, shortcut)` deprecated — use
  `addAction(text)` + `setShortcut()` + `connect()` separately
- Filter width presets are mode-aware: LSB/DIGL/CWL use negative low offset,
  CW centers 200 Hz above, others use 0 to high

---

## What's Implemented (v0.1.8)

- UDP radio discovery and TCP command/control
- SmartSDR V/H/R/S/M protocol parsing
- Panadapter VITA-49 FFT spectrum display with dBm calibration
- Native VITA-49 waterfall tiles (PCC 0x8004) with colour mapping
- VITA-49 meter data decode (PCC 0x8002) with unit-aware conversion
- MeterModel: meter definition registry from TCP `#`-separated status messages
- Analog S-Meter gauge (ANLG applet): needle, peak hold, S-unit + dBm readout
- Audio RX (float32 stereo + int16 mono) via VITA-49 → QAudioSink
- Volume / mute control with RMS level meter
- Full RX applet: antennas, filter presets, AGC, AF gain, pan, squelch,
  NB/NR/ANF, RIT/XIT, tuning step stepper, tune lock
- Frequency dial: click, scroll, keyboard, direct entry
- Spectrum: click-to-tune, scroll-to-tune, filter passband overlay
- AppletPanel: toggle-button row (ANLG, RX, TUNE, TX, PHNE, P/CW, EQ)
- Tuner applet (4o3a TGXL): Fwd Power/SWR gauges, C1/L/C2 relay bars,
  TUNE (autotune) and OPERATE/BYPASS/STANDBY buttons
- Tuner auto-detect: hidden when no TGXL, appears on amplifier subscription
- Fwd Power gauge auto-scales: barefoot (0–200 W) vs PGXL (0–2000 W)
- TX applet: Fwd Power/SWR gauges, RF Power/Tune Power sliders,
  TX profile dropdown, TUNE/MOX/ATU/MEM buttons, ATU status indicators, APD
- TransmitModel: transmit state, internal ATU state, TX profile management
- P/CW applet: mic level gauge (-40 to +10 dB, 3-zone cyan/yellow/red) with
  peak-hold marker, compression gauge (reversed red fill with slow decay)
- P/CW applet: mic profile dropdown, mic source selector, mic level slider,
  +ACC toggle, PROC/NOR-DX-DX+, DAX toggle, MON + monitor volume
- HGauge shared header: reusable horizontal gauge widget with three-zone fill,
  peak-hold markers, and reversed fill mode
- TX button (sends `xmit 1` / `xmit 0`)
- Persistent window geometry

## What's NOT Yet Implemented

- Slice filter passband shading on spectrum
- Multi-slice support (slice tabs or overlaid markers)
- Audio TX (microphone → radio, full VITA-49 framing)
- Band stacking / band map
- CW keyer / memories
- Phone applet (PHNE)
- Equalizer (EQ applet)
- Full meter display (SWR, ALC, power, etc. — MeterModel has the data)
- Meter scaling review across all gauges (S-meter, TX power, mic level, compression)
- DAX / CAT interface
- Spot / DX cluster integration
- Memory channels
- Macro / voice keyer
- Network audio (Opus compression)
- TNF (tracking notch filter) management
