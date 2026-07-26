# Tonewheel Keyboard Scanner

Key scanner firmware for a two-manual tonewheel organ, running on a Raspberry Pi Pico (RP2040).
Scans a diode-isolated key matrix and sends MIDI over both UART and USB simultaneously.

## Hardware

### Key matrix (one manual, 44 keys, F4–C8)

| Signal | Pins | Direction |
|--------|------|-----------|
| Strobe outputs | GP16–GP21 | OUTPUT — one HIGH at a time |
| Read inputs | GP2–GP13 | INPUT_PULLDOWN — HIGH when key closed |
| UART MIDI out | GP0 (Serial1) | TX → tone-generator Pico GP5 |
| USB | native | CDC serial (debug) + USB MIDI device |

GP16 is not connected. Active strobes are GP17–GP21:

| Strobe | Keys | MIDI notes (physical) | Read direction |
|--------|------|-----------------------|----------------|
| GP17 | 7 (F4–B4) | 71..65 | high→low |
| GP18 | 12 (C5–B5) | 83..72 | high→low |
| GP19 | 12 (C6–B6) | 95..84 | high→low |
| GP20 | 12 (C7–B7) | 107..96 | high→low |
| GP21 | 1 (C8) | 108, bit 11 (GP13) | — |

### Planned hardware (not yet built)

**Second manual** — two 74HC238 (active-high 3-to-8 decoders) replace the direct strobe lines:

| Signal | Pins |
|--------|------|
| Address A0–A2 | GP16–GP18 (shared both decoders) |
| Enable 238-A (bottom manual) | GP14 |
| Enable 238-B (top manual) | GP15 |
| Read inputs | GP2–GP13 (shared both manuals) |

74HC238 chosen over 74HC138: outputs are active-HIGH, matching INPUT_PULLDOWN read lines.

**Analogue mux (74HC4067, 16 channels)**

| Signal | Pin |
|--------|-----|
| S0–S3 | GP21, GP22, GP26, GP27 |
| ADC input | GP28 (ADC2) |
| /EN | GND (always enabled) |

Channel assignments: drawbars 1–9 (CC 12–20), volume (CC 7), leslie (CC 82), 5 spare.

GP29 is ADC VREF — do not use as general ADC input.

## Software

### Configuration (`key_scanner.h`)

```cpp
#define MIDI_BASE_NOTE   41   // transposition: lowest key sends this note
#define MIDI_CHANNEL      1
#define DEBOUNCE_CYCLES  50   // ~5 ms at ~10 000 scans/sec
#define DEBUG_LEVEL       1   // 0=silent 1=note on/off 2=+raw 3=+dump
#define HAS_ADC_MUX       0   // flip to 1 when mux board is ready
```

`MIDI_BASE_NOTE` transposes the whole keyboard at send time.
`MIDI_TABLE_BASE` (65 = F4) is the lowest note in the physical table — do not change.
Transposed note = `table_note - MIDI_TABLE_BASE + MIDI_BASE_NOTE`.

### MIDI output

Every note-on/off is sent to **both** outputs simultaneously:
- UART on GP0 at 31250 baud → tone-generator Pico
- USB MIDI device (appears as "Tonewheel Keyboard" by "Tonewheel")

### Build

```
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = pico
board_build.core = earlephilhower
framework = arduino
build_flags = -O2 -DUSE_TINYUSB -DCFG_TUD_MIDI=1
lib_deps = https://github.com/adafruit/Adafruit_TinyUSB_Arduino.git
```

## File structure

```
src/
  main.cpp          — setup/loop, keyscan_init before Serial.begin
  key_scanner.h     — everything: matrix scan, debounce, MIDI out, USB MIDI
platformio.ini
README.md
CONTEXT.md
```