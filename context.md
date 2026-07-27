# Developer Context — Tonewheel Organ Keyboard Controller

Companion to [README.md](README.md), same split as picoST3215: README is
how to build and use it, this is why it's built the way it is — assumptions
made without hardware in hand, decisions and their reasoning, and a log of
what's been found and fixed during bring-up.

**Status as of the last bring-up session:** builds cleanly, boots, OLED
(128x32 SSD1306), encoder, and USB MIDI/CDC all confirmed working on real
hardware. Key matrix scanning is confirmed working via a hardware trace
(pull-down/active-HIGH polarity fix + the column-inversion fix below) on one
manual (bank1) — the other manual/bank and velocity mode are still
unverified. CC pots still untested beyond floating-pin diagnostics.

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

## Physical key mapping — confirmed via hardware trace

Confirmed by streaming `k` while playing chromatically from the lowest key
(an F) to the highest reached (a C) on one manual:

- **Row = semitone, row 0 = C, increasing row = increasing pitch.** Exactly
  as originally assumed — the trace showed a clean `bit0..bit11` climb in
  step with each keypress within a stable column, and the very first
  (partial) column correctly started at bit 5 (F), not bit 0, since the
  lowest physical key isn't a C.
- **Column (the 74HC238 address, 0..7) runs the other way.** Pitch went up
  while the raw column address went *down* (bank1 col 6 -> 5 -> 4 -> 3 -> 2
  as the run climbed) — the lowest octave sits at the highest column
  address, not the lowest.

`KeyboardEngine::keyIndex()` now inverts the column before combining it with
row: `octave = MATRIX_COLS_PER_BANK - 1 - col`, then `octave*12 + row`. Row
is left untouched.

This was diagnosed with the raw `k` stream (see README's Serial Debug
Commands) rather than guessed — worth reaching for that tool again if a
similar "which order is X actually in" question comes up (e.g. once the
second manual/bank is tested, or if velocity-mode contact order turns out
to need the same kind of check).

## Contact polarity — fixed (was: CHECK THIS)

Confirmed on hardware: rows now use `INPUT_PULLDOWN` and a closed contact
reads HIGH, not the original `INPUT_PULLUP`/LOW assumption. Two things
combine to explain why:

- The diodes are wired column-side anode, row-side cathode (current flows
  column -> row when pressed) — the opposite of what was originally assumed.
- The 74HC238 in use here is the **non-inverting** member of the '138/'238
  decoder family — its selected output drives HIGH, not LOW like the far
  more common 74HC138. So the selected column is actually at logic HIGH
  when addressed.

Those two facts are self-consistent: column HIGH (selected) forward-biases
the column->row diode against a row idling LOW (pull-down), pulling the row
HIGH when pressed — clean detection, no crosstalk from the 15 unselected
(LOW) columns on the same row. Everything downstream of the raw bit read in
`KeyMatrix::poll()` (debounce, event queue, `KeyboardEngine`) never assumed
a polarity, so this was a two-line fix (`pinMode` + the bit test).

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
- `KeyMatrix::DEBOUNCE_US = 4000` (4ms) — a post-transition **lockout**, not
  a pre-acceptance wait (see "Debounce: leading vs. trailing edge" below).
  Since it no longer costs latency, it's safe to raise this further if real
  contact chatter ever shows up — there's no UX downside to a larger value
  now, only a slightly longer minimum time between two genuinely separate
  taps on the same key.
- `VelocityConfig::minTravelMs/maxTravelMs` default to 1ms/60ms — placeholder
  guesses, not measured. Play a key as hard and as soft as feels realistic,
  watch the OLED "Last: note vNN" readout, adjust via Settings (persisted,
  no reflash needed).

### Debounce: leading vs. trailing edge

Originally implemented as trailing-edge debounce: wait for the raw signal
to hold *stable* for `DEBOUNCE_US` before accepting it as a real transition.
That adds a fixed `DEBOUNCE_US` of latency to every single Make and Break —
noticeable on a fixed-velocity organ manual, where debounce buys nothing
(there's no timing to protect) and is pure added delay.

Switched to leading-edge debounce: accept a transition immediately, the
instant it's first seen, then ignore any further change on that cell for
`DEBOUNCE_US` afterward. Real contact bounce happens right at the edge, so
it's still caught by the lockout; a genuine press or release is no longer
delayed at all — bounded only by the scan pass itself (currently ~150-200µs
for a full 16-column sweep, not milliseconds). This also removes the
"up to 4ms of jitter added to Velocity mode's measured travel time" caveat
that used to be documented here — Make is now accepted at the true first
contact instant on both the top and bottom contact, so the timing window
comes out cleaner too.

One edge case worth knowing: a press-and-release faster than `DEBOUNCE_US`
(sub-4ms) has its release delayed until the lockout expires, rather than
registering instantly. This is the standard tradeoff of leading-edge
debounce and not something a human playing a physical keyboard is likely to
hit in practice.

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

## Latency

With leading-edge debounce (above), the firmware-side floor for detecting a
key press is roughly one scan pass — ~150-200µs for the full 16-column
sweep — plus whatever USB Full Speed (RP2040 has no High Speed capability;
~1ms frame interval) or the DIN's fixed 31250 baud (~1ms to shift out a
3-byte message) themselves cost. Those two are protocol/hardware floors,
not something firmware can improve.

**One real remaining latency source: the OLED render blocks the scan loop.**
`_display.display()` pushes the full framebuffer over I2C every call. For
the 128x32 SSD1306 (512 bytes of GDDRAM) at the current 400kHz I2C clock,
that's roughly **11.5ms** per call — and it happens inline in `App::tick()`,
throttled to once per `UI::UI_REFRESH_MS` (50ms). So roughly 23% of the time,
`tick()` is blocked for ~11.5ms rendering the display; a key pressed during
that window won't be scanned until it finishes. `keyMatrix.poll()` and
`drainKeyEvents()` (which is what actually sends the MIDI message) both run
*before* `render()` each tick, so this doesn't delay an already-detected
note going out — it only delays *detecting* a press that happens to land
inside that ~11.5ms window.

Options, not yet applied — pick one before I touch this, since OLED and the
M5Stack encoder share one I2C bus (GPIO20/21) and this is a shared-clock
change:

- **Raise `HW::I2C_HZ`** (400kHz -> 1MHz, "Fast Mode Plus") — would cut the
  render block to roughly a quarter, ~3ms. Simplest change, but it's not
  guaranteed the encoder's I2C-to-I2C bridge tolerates 1MHz reliably — this
  needs testing on the real bus, not something safe to assume from here.
- **Reduce render frequency** (raise `UI::UI_REFRESH_MS`) — fewer stalls,
  same duration each, and a less responsive-feeling status screen.
- **Partial/windowed display updates** — only push the changed region
  instead of the whole framebuffer. Real fix, but a bigger lift: the
  Adafruit driver's base `display()` doesn't expose a dirty-rect API, so
  this would mean either a different display call pattern or accepting the
  added complexity.

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