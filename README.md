# Pico MIDI keyboard

A Raspberry Pi Pico (RP2040) keyboard/pot scanner for a tonewheel organ
build. Reads a 12x8x2 contact matrix through two 74HC238 decoders, either as
two independent non-velocity organ manuals or as one 96-key velocity-sensing
manual, plus 16 analog CC pots (drawbars, volume, drive, chorus, ...) through
a 74HC4067. Outputs USB MIDI, a USB CDC debug port, and a hardware MIDI DIN
line simultaneously.

Built on the same PlatformIO/Earlephilhower toolchain and several reused
driver patterns from the picoST3215 servo-tester project. See
[context.md](context.md) for hardware assumptions, decisions made along the
way, and what's still unverified on real hardware.

---

## Table of Contents

1. [Features](#features)
2. [Hardware / Pin Map](#hardware--pin-map)
3. [Software Setup](#software-setup)
4. [Usage Guide](#usage-guide)
5. [MIDI Reference](#midi-reference)
6. [Serial Debug Commands](#serial-debug-commands)
7. [Project File Structure](#project-file-structure)
8. [Troubleshooting](#troubleshooting)

---

## Features

- **Two keyboard modes**, switchable at runtime (persisted, no reflash):
  - **DoubleManual** — two independent 96-key (8 octave) manuals, on/off
    contacts, fixed velocity per manual — authentic organ behaviour
  - **Velocity** — one 96-key manual, dual top/bottom contacts per key,
    contact-travel time mapped to MIDI velocity via a selectable curve
- **Flexible note ranges** — start note and MIDI channel independently
  configurable per manual (DoubleManual) or for the single manual
  (Velocity), via the OLED + encoder Settings menu
- **16 analog CC pots** — drawbars, volume, drive, chorus, etc., each with
  an assignable CC number and channel; hysteresis-gated so idle pots
  generate zero MIDI traffic
- **Combined USB MIDI + CDC debug port**, plus a hardware MIDI DIN output on
  GPIO12 that mirrors everything sent over USB
- **OLED status screen** — mode, USB link state, last note/velocity played,
  dropped-event counter (auto-adapts to a 128x32 or 128x64 panel)
- **M5Stack encoder knob** — navigate/edit the Settings menu (rotate =
  move/adjust, short press = edit toggle, long press = back/cancel)
- **Persistent configuration** — mode, manual/velocity settings, and CC pot
  assignments saved to LittleFS, restored on boot

---

## Hardware / Pin Map

One fixed custom PCB, no board variants — all pins are set directly in
`config.h`.

| Function | GPIO(s) | Notes |
|---|---|---|
| Key matrix receive (rows) | 0-11 | 12 lines = one full chromatic octave |
| Hardware MIDI DIN out | 12 | UART0 TX, 31250 baud, TX-only |
| Key matrix send mux address (74HC238 A/B/C) | 13, 14, 15 | shared by both decoders |
| Key matrix send mux bank select | 16 | selects which 74HC238 is enabled — see below |
| CC pot mux address (74HC4067 S0-S2) | 17, 18, 19 | |
| I2C — OLED + M5Stack encoder (shared bus) | 20 (SDA), 21 (SCL) | |
| CC pot mux address (74HC4067 S3) | 22 | |
| CC pot mux common analog input | 26 (ADC0) | |
| MIDI DIN UART0 RX (unused, parked) | 29 | nothing is ever read from it |

**Key matrix addressing:** GPIO13-15 select 1-of-8 within whichever 74HC238
is enabled; GPIO16 selects which of the two decoders is enabled (their
enable pins are wired complementary to each other). So the full address is
effectively 4 bits — `{GPIO16, GPIO15, GPIO14, GPIO13}` — giving 16 send
lines with no separate "disabled" state: every valid code drives exactly one
line.

**Physical key mapping:** row = semitone within the octave (0-11), column =
which octave (0-7) — 8 octaves x 12 semitones = 96 keys per bank. See
[context.md](context.md) if this doesn't match your actual wiring.

---

## Software Setup

1. Open the project folder in VS Code with the PlatformIO extension (or
   `pio run` from the command line).
2. Default build target is `env:pico`, targeting a 128x32 SSD1306 OLED (same
   panel as picoControl). If you're using an SH1107 panel instead, swap
   `-D DISPLAY_SSD1306` for `-D DISPLAY_SH1107` in `platformio.ini`'s
   `build_flags`, and swap the `Adafruit SSD1306` lib_dep for
   `Adafruit SH110X` — see the comment block in `platformio.ini`.
3. Flash: `pio run -e pico -t upload`, or the VS Code PlatformIO upload
   button.
4. Connect a serial terminal at 115200 baud to the CDC port that appears
   (`pio device monitor`, or any terminal app) to see boot diagnostics and
   use the [debug commands](#serial-debug-commands).

---

## Usage Guide

### Home screen

Shows the active mode, USB MIDI link state, the last note+velocity played,
and a dropped-event counter (should stay at 0 — see
[Troubleshooting](#troubleshooting) if it isn't). Short-press the encoder to
enter Settings.

### Settings menu

Rotate to move the selection; short-press to toggle edit mode on the
selected field (rotate now changes its value); short-press again to commit
and return to browsing; long-press to go back to Home (saves to flash) or,
while editing, to cancel that field's edit.

Field 0 is always the mode toggle (**DblMan** / **Veloc**). The remaining
fields depend on the active mode:

**DoubleManual:**

| Field | Meaning |
|---|---|
| A Start / A Chan / A Vel / A On | Manual A: start note, MIDI channel, fixed velocity, enabled |
| B Start / B Chan / B Vel / B On | Manual B: same, for bank1 |

**Velocity:**

| Field | Meaning |
|---|---|
| Start / Chan | Start note, MIDI channel |
| MinMs / MaxMs | Contact-travel time (ms) mapped to velocity 127 / velocity 1 |
| Curve | Linear / Expo (favours high velocities) / Log (favours low velocities) |

CC-pot assignment (which CC number + channel each of the 16 pots sends)
isn't in this menu yet — see [context.md](context.md) "Known gaps".

---

## MIDI Reference

- **Note mapping:** `note = startNote + (octave * 12) + semitone`, where
  `octave` is the matrix column (0-7) and `semitone` is the matrix row
  (0-11).
- **DoubleManual velocity:** fixed per manual (`A Vel` / `B Vel` in
  Settings) — organ contacts are on/off, there's no velocity electronics in
  this mode by design.
- **Velocity mode:** computed from top-to-bottom contact travel time, mapped
  through `MinMs`/`MaxMs`/`Curve`.
- **CC pots:** default assignments use General MIDI's undefined/free
  controller numbers — CC 3, 9, 14, 15, and 20-31 (16 total, one per pot),
  all on channel 1. Edit `applyDefaultConfig()` in `app.cpp` to change them
  (see context.md for why this isn't a menu yet).
- **Outputs:** USB MIDI and the GPIO12 hardware MIDI DIN out both receive
  every message — DIN doesn't depend on USB being connected.
- **Panic:** the `z` serial command sends CC 120 (all sound off) + CC 123
  (all notes off) on all 16 channels, both outputs.

---

## Serial Debug Commands

Type a single character into the CDC serial terminal — no Enter needed.

| Key | Action |
|---|---|
| `m` | Toggle keyboard mode (DoubleManual <-> Velocity) |
| `s` | Dump current status (mode, USB state, last note, manual/velocity config) |
| `z` | MIDI panic — all sound/notes off, all channels, both outputs |
| `p` | Toggle a raw CC-pot CSV stream: all 16 ADC readings (0-4095, unfiltered), one comma-separated line every ~50ms — feed straight into Arduino Serial Plotter or any CSV-over-serial script |

---

## Project File Structure

```
picoKeyboard/
├── platformio.ini
├── README.md                       This file
├── context.md                      Hardware assumptions, decisions, bring-up log
└── src/
    ├── main.cpp                    Boot sequence · I2C recovery · serial debug commands
    ├── config.h                    Pin map, matrix/OLED/CC-mux constants
    ├── app_state.h                 AppState struct (runtime UI/monitoring state)
    ├── model/
    │   └── keyboard_model.h        KeyboardMode, ManualConfig, VelocityConfig, KeyEvent
    ├── app/
    │   ├── app.h                   App class declaration
    │   └── app.cpp                 Input handling · Settings menu · persistence · tick loop
    └── drivers/
        ├── key_matrix.h / .cpp     74HC238 matrix scan + per-cell debounce (raw events only)
        ├── keyboard_engine.h/.cpp  Mode-aware interpretation: raw events -> MIDI notes
        ├── cc_mux.h / .cpp         74HC4067 pot scan, EMA filter, hysteresis gate, raw diagnostic sweep
        ├── midi_engine.h / .cpp    USB MIDI+CDC init, hardware DIN mirror
        ├── oled_ui.h / .cpp        OLED rendering (SSD1306/SH1107, compact-layout aware)
        ├── persist.h / .cpp        LittleFS save/load
        ├── encoder_unit.h / .cpp   M5Stack Unit Encoder I2C driver (reused from picoST3215)
        └── iencoder.h              IEncoder pure-virtual interface (reused from picoST3215)
```

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| OLED blank | Wrong `DISPLAY_xxx` build flag for your actual panel | Check `platformio.ini`; see [context.md](context.md) bring-up log |
| Encoder unresponsive | I2C not initialised, or wrong address | `s` command shows `encoderOk`; boot log also prints it |
| No boot messages in terminal | Terminal opened after boot printed them | They only print once, right after boot — reset the board with the terminal already open |
| No MIDI/CC traffic from idle pots | Expected — hysteresis gate suppresses unchanged values | Use `p` to see raw values move as you turn a pot |
| `droppedKeyEvents` rising on Home screen | `App::tick()` not running often enough for the key traffic | Check nothing's blocking `loop()`; see context.md matrix timing notes |
| Notes at unexpected pitches | Physical key mapping or contact polarity assumption doesn't match your PCB | See [context.md](context.md) "Physical key mapping" / "Contact polarity" |
| Velocity mode: notes never turn on, or turn off before key release | Contact order assumption doesn't match your keybed | See [context.md](context.md) "Velocity contact order" |