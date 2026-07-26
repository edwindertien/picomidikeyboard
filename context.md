# Developer Context — pico MIDI keyboard

Companion to [README.md](README.md), same split as picoST3215: README is
how to build and use it, this is why it's built the way it is — assumptions
made without hardware in hand, decisions and their reasoning, and a log of
what's been found and fixed during bring-up.

**Status as of the last bring-up session:** builds cleanly, boots, OLED
(128x32 SSD1306), encoder, and USB MIDI/CDC all confirmed working on real
hardware. Key matrix and CC pots have not yet been tested with anything
actually wired to them (floating-pin diagnostics only so far) — the
assumptions below are what to check first once they are.

---

## Architecture

`KeyMatrix` is a dumb, mode-agnostic scanner — it only emits raw Make/Break
events per (row, col, bank), with per-cell debounce. `KeyboardEngine` is
where DoubleManual vs. Velocity interpretation lives — which note, on/off
vs. contact-timing velocity. This mirrors the `IServoBus` / `App` split in
picoST3215: a reusable driver underneath a mode-aware policy layer, kept out
of both the driver and the UI.

`CcMux` follows the same split internally: `tick()` is the MIDI-facing path
(filtered, hysteresis-gated, one channel per call); `readAllRaw()` is a
separate unfiltered full sweep for diagnostics, sharing the hardware access
but none of the filter/hysteresis state.

## Matrix interpretation

The 12x8x2 matrix (12 receive lines, 8 columns per bank via the shared
74HC238 A/B/C address, 2 banks via GPIO16) is read identically regardless of
mode:

- **DoubleManual** — bank0 and bank1 are two *independent* 96-key sets
  (`manualA`/`manualB`), each with its own start note, channel, and a fixed
  velocity (organ contacts are on/off, no velocity electronics — this is
  deliberately authentic tonewheel-organ behaviour, not a shortcut).
- **Velocity** — bank0 and bank1 are the top/bottom contact of the *same*
  96 physical keys. The time between top-Make and bottom-Make maps to
  velocity via `travelTimeToVelocity()`.

The 16 analog CC pots (drawbars, volume, drive, chorus) are **not** sharing
the key matrix — they're the separate 74HC4067 analog bank, always active
regardless of `KeyboardMode`. If any drawbars are actually matrix switches
rather than pots, that's a different shape of change — flag it before
wiring more of it up.

## Physical key mapping

`KeyboardEngine::keyIndex(row, col)` assumes: the 12 receive lines are one
chromatic octave (row 0..11 = 12 consecutive semitones), and the 74HC238
column address selects which octave (col 0..7) — 8 octaves x 12 semitones =
96 keys per bank:

```cpp
return col * HW::MATRIX_ROWS + row;   // col = octave, row = semitone
```

This replaced an earlier row-major guess (`row * 8 + col`) once it became
clear 12 receive lines matching one chromatic octave wasn't a coincidence.
Still assumes row 0 is the lowest semitone and col 0 is the lowest octave —
if either is reversed on the PCB, this is still the one place to fix it.
Verify with `dumpStatus()`/the OLED "Last:" readout while pressing keys up a
single octave in order.

## Contact polarity — CHECK THIS

`KeyMatrix::begin()` configures all 12 row pins `INPUT_PULLUP` and treats a
closed contact as the row reading LOW (contact shorts the row to GND when
pressed). If your PCB instead pulls rows high through the contact, flip the
`== 0` bit test in `KeyMatrix::poll()` and switch `pinMode()` to
`INPUT`/`INPUT_PULLDOWN` accordingly. Everything downstream (debounce, event
queue, KeyboardEngine) is polarity-agnostic — one-line change.

## Velocity contact order — CHECK THIS

For Velocity mode, the conventional dual-contact keybed geometry is assumed:

- **Press**: bank0 (top) contact closes first, bank1 (bottom) closes
  slightly later as the key continues down. Velocity = f(time between them).
- **Release**: bank1 (bottom) opens first as the key rises, bank0 (top)
  opens last as the key fully returns to rest. **Note Off is driven
  entirely by bank0's Break** — bank1's Break is ignored.

If your keybed's contact order is reversed, swap the bank checks in
`KeyboardEngine::handleVelocity()`. Can't really be verified without the
actual keybed in hand — flag it if the first hardware test shows notes never
turning on, or turning off before the key is released.

`releaseVelocity` in `VelocityConfig` is currently unused (Note Off always
sends velocity 0) — most synths/organs ignore release velocity anyway, but
the field is there if you want to wire in break-contact release timing
later.

## Timing constants — tune on hardware

- `KeyMatrix::SETTLE_US = 8` — time after switching the mux address before
  reading rows. Should comfortably cover 74HC238 propagation delay plus
  wiring capacitance; raise it if you see spurious reads on columns adjacent
  to a just-scanned one.
- `KeyMatrix::DEBOUNCE_US = 4000` (4ms) — standard mechanical debounce
  window. This is a *ceiling* on velocity resolution too — see below.
- `VelocityConfig::minTravelMs/maxTravelMs` default to 1ms/60ms — placeholder
  guesses, not measured. Play a key as hard and as soft as feels realistic,
  watch the OLED "Last: note vNN" readout, adjust via Settings (persisted,
  no reflash needed).

Because `DEBOUNCE_US` delays when a Make event is *accepted* as stable, it
adds up to 4ms of jitter to the measured top-to-bottom travel time in
Velocity mode — a real chunk of the default 1-60ms range. If velocity feels
coarse/steppy on hardware, lowering `DEBOUNCE_US` (more contact-bounce risk)
or widening the travel-time range are the two levers.

## Hardware MIDI DIN out — mirrors everything

`MidiEngine` sends every Note On/Off/CC over **both** USB MIDI and the
GPIO12 hardware DIN out, unconditionally — DIN doesn't check `isMounted()`
since it's a separate physical link with its own consumer. Narrowing this
(e.g. CC only over USB) is a small change to `MidiEngine::sendCC`/
`sendNoteOn`/`sendNoteOff` if wanted later.

Serial1 (UART0) is TX-only — RX is parked on GPIO29 purely so
`Serial1.begin()` doesn't reclaim GPIO1/13/17, which the key/CC matrices
already own. Nothing is ever read from Serial1.

---

## Bring-up log

### OLED: SH1107 build targeting an SSD1306 panel (fixed)

First hardware test: OLED completely blank. Root cause — `platformio.ini`
was still building `DISPLAY_SH1107` (linking the SH110X driver) against an
actual 128x32 SSD1306 panel; the SH1107 init command sequence doesn't match
SSD1306 silicon, so nothing displayed at all rather than something garbled.
Also had the wrong panel size assumed (128x64 instead of the real 128x32).

Fixed: `platformio.ini` now defaults to `DISPLAY_SSD1306` with the matching
lib_dep; `config.h` corrected to 128x32. Since the original menu/status
layout was sized for a 64px-tall canvas (4 menu rows + footer + a 14px CC
bar graph — none of which fits in 32px), added `HW::OLED_CANVAS_H` and a
compile-time `kCompact` flag in `oled_ui.cpp`: 2 menu rows with no footer
text on a compact panel (the `>`/`*` selection marker still carries edit
state), and the status screen drops to two lines with no bar graph. Taller
panels (SH1107, still selectable via the build flag) keep the original
4-row/footer/bar-graph layout.

### Key matrix: digitalRead() loop -> gpio_get_all()

Original `KeyMatrix::poll()` did 12 sequential `digitalRead()` calls per
column (192 per full 16-column scan pass). Each carries real Arduino-wrapper
overhead (pin table lookup, mode branch) on top of the actual read. Since
GPIO0-11 map directly to bits 0-11 of the RP2040 SIO input register,
switched to a single `gpio_get_all()` per column plus a shift/mask per row —
same pattern as `rcpwm_bus.cpp`'s direct `hardware/pwm.h` use in picoST3215
(reach for the raw SDK when a hot path's speed actually matters). This
matters here specifically because scan-pass speed sets Velocity mode's
timing resolution.

### CC mux: settle time and resolution

Original `CcMux::tick()` used a 15µs settle then a single `analogRead()`
immediately after switching the 74HC4067 channel — no discard read, so a
channel could pick up residual charge from whichever pot was selected right
before it (all 16 pot wipers share one analog bus trace through the mux).
Also capped at 10-bit ADC resolution despite the RP2040's ADC being native
12-bit.

Fixed: `readSettled()` now does a throwaway conversion immediately after
switching, then the real sample; resolution raised to 12-bit; the MIDI-facing
`tick()` path added a light EMA filter (`FILTER_SHIFT`, default /4) feeding
the hysteresis gate. `readAllRaw()` was added as a separate unfiltered sweep
specifically so the `p` diagnostic stream shows genuine pin noise rather than
something already smoothed away — useful for confirming 16 independently-
floating channels (vs. crosstalk or a stuck channel) before anything's wired.

---

## Known gaps / next steps

- **CC-pot assignment isn't in the OLED Settings menu yet.** `ccMap[16]`
  (which CC number + channel each pot sends) only has the
  General-MIDI-undefined-block defaults set in `applyDefaultConfig()`
  (`app.cpp`) — edit that array directly for now. A "select pot, then edit"
  sub-screen is a natural follow-up once matrix/pot scanning is validated on
  real hardware.
- **Disabling a manual mid-hold leaves a stuck note.** `ManualConfig::enabled
  = false` short-circuits both Make and Break in `handleDoubleManual()`, so
  a key held down when you disable its manual never gets its Note Off. Low
  priority, but worth fixing if it bites.
- **No dedicated mode-toggle shortcut on the Home screen** — reachable via
  Settings field 0 or the `m` serial command; a direct shortcut might be
  nicer once this is in daily use rather than on the bench.