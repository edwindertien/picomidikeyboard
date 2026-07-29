# Developer Context — Tonewheel Organ Keyboard Controller

Companion to [README.md](README.md), same split as picoST3215: README is
how to build and use it, this is why it's built the way it is — assumptions
made without hardware in hand, decisions and their reasoning, and a log of
what's been found and fixed during bring-up.

**Status as of the last bring-up session:** both manuals confirmed working
in DoubleManual (fixed-velocity organ) mode, on real hardware, over USB
MIDI. OLED (128x32 SSD1306) and encoder confirmed working. CC pots still
untested beyond floating-pin diagnostics; Velocity mode untested.

**Not yet tested: the dual-core split** (see "Dual-core split" below) —
this is a substantial restructure (core0/core1, the `SharedConfig` mutex,
`setup1()`/`loop1()`) done in response to a real latency finding, but
written without hardware in hand. First things to check after flashing:
that the Settings menu still edits and persists correctly (this now crosses
the core boundary via `SharedConfig` where it didn't before), that mode
changes made via the menu still take effect on core0 (via the
`_lastSeenMode` detection in `tickCore0()`), and that the boot sequence's
core0/core1 handshake actually completes (watch for the
"WARNING: core1 init did not finish within 2s" message).

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

## Dual-core split

Core0 owns everything that isn't I2C: USB MIDI/CDC, the DIN UART, the ADC
(CC pots), and the digital GPIO matrix scan. Core1 owns I2C exclusively —
OLED render and the M5Stack encoder (Settings menu input). This is the same
reasoning as the BTstack/TinyUSB isolation in picoST3215: one peripheral,
one owning core, no exceptions — here the OLED render's ~11.5ms blocking I2C
transfer (see "Latency" below) was stalling the key-scan loop, and CC pot
CC-send goes through the same USB/DIN transport as note messages, so it had
to stay put on core0 rather than move alongside the "less time-critical"
OLED/encoder side.

**`App` splits into `tickCore0()` and `tickCore1()`.** `main.cpp` gets
`setup1()`/`loop1()` (picked up automatically by the earlephilhower core,
no manual `multicore_launch_core1()` needed) — all I2C init (`Wire`, OLED,
encoder) moved there from `setup()`.

**What crosses the core boundary, and how:**

- `PersistentConfig` — read every tick by core0 (`KeyboardEngine`, `CcMux`),
  read/written by core1 (Settings menu). Wrapped in `SharedConfig`, a raw
  pico-sdk `mutex_t` behind a `SharedConfigGuard` RAII lock. Critical
  sections are always just a few struct-field reads/writes — never LittleFS
  I/O, which stays core0-exclusive (`persist.save()`), per the same
  "LittleFS must never run on core 1" rule as picoST3215.
- **Mode changes are the one subtlety.** The Settings menu's mode field
  (core1) only flips the bit under lock and marks the config dirty — it
  does NOT call `KeyboardEngine::reset()` directly, because `KeyboardEngine`
  is core0-owned state and core1 must never touch it. Instead, core0's
  `tickCore0()` compares the snapshotted mode against its own cached
  `_lastSeenMode` every tick and calls `reset()` itself the instant it
  detects a change — regardless of whether that change came from the menu
  or the serial `m` command. One detection path, two possible triggers.
- **Persistence is no longer tied to "leaving Settings."** `SharedConfig`
  has a lock-free `volatile bool _dirty` flag — single writer (core1's
  `markDirty()`), single reader/clearer (core0's `takeDirty()`), so no mutex
  needed just for that. Core0 checks it every tick and saves within one
  tick of any edit, whichever core triggered it.
- `MonitorState` (`lastNote`, `lastVelocity`, `droppedKeyEvents`, `ccLive[]`)
  — written by core0, read by core1 for the Home-screen status display.
  Plain `volatile` fields, deliberately NOT mutex-protected: the worst case
  from a torn/stale read is a single display frame briefly showing a
  mismatched note+velocity pair or a half-updated CC bar — cosmetic, not a
  hazard — and locking on every scan-loop update would cost far more than
  that's worth protecting against.
- **`setup1()` runs concurrently with `setup()`, not after it — this
  matters.** Checked against the earlephilhower core's own source: core1 is
  launched via `multicore_launch_core1()` *before* `setup()` is even called,
  and the docs confirm setup()/setup1() start at the same time, with only
  loop()/loop1() waiting for their own core's setup to finish. First draft
  of this got that backwards and assumed `sharedConfig.begin()` (core0)
  would always complete before core1 could touch `SharedConfig` — false,
  and a real race given core0's USB-mount wait can take up to 1.5s while
  core1's I2C init is comparatively fast. Fixed with a second flag:
  `setup1()` busy-waits on `g_sharedConfigReady` (set by core0 immediately
  after `sharedConfig.begin()`, before the slow USB wait) before touching
  `SharedConfig` at all, or doing anything else.
- **Serial/CDC stays core0-exclusive, including at boot.** Given the above,
  a naive "print the boot summary from wherever finishes init first" risks
  two cores writing to the same USB CDC endpoint at once. Core0's `setup()`
  waits (with a 2s timeout) on a separate one-time `g_core1Ready` flag, set
  at the very end of `setup1()`, before printing anything about OLED/encoder
  status. After that handshake, core1 never touches Serial again, ever —
  matching "one peripheral, one core" even for something as low-stakes as a
  debug print.

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

**Resolved: the OLED render used to block the scan loop.** `_display.display()`
pushes the full framebuffer over I2C every call — for the 128x32 SSD1306
(512 bytes of GDDRAM) at 400kHz, that's roughly **11.5ms** per call,
throttled to once per `UI::UI_REFRESH_MS` (50ms). With everything on one
core, that meant `tick()` was blocked for ~11.5ms roughly 23% of the time,
and a key pressed during that window wouldn't be scanned until it finished.
Moving OLED render (and the encoder, since they share one I2C bus) to core1
— see "Dual-core split" above — removes this entirely rather than just
shrinking it: core0's scan loop never touches I2C at all now, so there's no
render call in its path to block on. The three options considered (raising
`HW::I2C_HZ`, reducing render frequency, partial display updates) are all
moot as a result — none of them were as clean a fix as just not sharing the
loop in the first place.

---

## Drawbar visualization

The Home screen shows the first `HW::DRAWBAR_COUNT` (9) CC-pot channels as
Hammond-style vertical tracks — an outlined slot per drawbar, filled from
the **top down** in proportion to value (0 = empty/silent, matching the bar
pushed all the way in; 127 = filled the full track height, matching it
pulled all the way out). This is deliberately the opposite of a typical
bottom-up volume-meter fill, matching the actual physical action ("you pull
the bars" down/out to increase them) rather than a generic bar-chart
convention.

`OledUi::drawDrawbarTracks()` is a from-scratch helper (`drawStatus()`'s
CC section used to be a generic bottom-up 16-channel bar graph) — it takes
a bounding box and count, so it's reused for both panel sizes with
different boxes: the 128x32 panel gives it the whole content area below the
header row. The 128x64 panel keeps USB/dropped-event/velocity text and
gives the drawbars a shorter strip below it.

**Which channels are "drawbars" is a hardcoded assumption, not derived from
anything in `CcSlot`.** `HW::DRAWBAR_COUNT = 9` in `config.h` just says
"the first N logical positions are drawbars" — there's no per-channel
metadata distinguishing a drawbar pot from a volume/drive/chorus pot. A more
general "channel type" tag on `CcSlot` would remove this assumption but
felt like over-engineering for what's being asked right now — worth
reconsidering if the physical assignment turns out to change often.

### Drawbar remap: physical wiring doesn't match mux-channel order

The 9 drawbars aren't wired to mux channels 0-8 in order — confirmed
against the actual hardware (2026-07): drawbar 1 (16') is physically on mux
channel 3, drawbar 2 on channel 0, and so on, a full derangement (no
drawbar sits on its "obvious" channel). `HW::DRAWBAR_MUX_CHANNEL[9]` in
`config.h` is the correction table, indexed by logical position (0 =
drawbar 1) giving the mux channel that actually carries it:
`{3, 0, 4, 8, 1, 7, 5, 2, 6}`.

This table is used in two places that both have to agree, for two different
reasons:

- **Display** (`OledUi::drawDrawbarTracks()`) reads
  `values[DRAWBAR_MUX_CHANNEL[i]]` for logical position `i`, instead of
  `values[i]` directly — this is a genuine runtime indirection, since
  `ccLive[]` is filled in mux-scan order (a hardware fact, not something
  software controls) and has to be permuted for the display to show
  physical drawbar `i+1` at screen position `i+1`.
- **CC assignment** doesn't need a runtime indirection — `shared_config.cpp`'s
  `kDefaultCc[]` is indexed by mux channel (matching how `CcMux::tick()`
  looks up `map[_channel]`), and its *values* are simply computed so that
  whichever mux channel carries logical drawbar `i+1` sends CC `12+i`. The
  two tables were derived together and cross-checked against each other —
  if you ever touch one, recheck the other, or the display and the CC
  stream for the same physical drawbar will disagree.

### CC assignment and the top-row header

Default CC numbers for the drawbars are 12-20 (one per logical position,
per your request) — chosen to match whatever convention your target
plugin/instrument expects, not a General MIDI meaning (CC12/13 are
technically GM "Effect Control 1/2", but that's irrelevant for a custom
controller talking to a specific plugin). The remaining 7 (unwired)
channels shifted to CC 21-27 so nothing collides with the drawbar range.

`drawStatus()`'s top row was rebuilt to pack in more than just the mode:
abbreviated mode text (`DblMan`/`Veloc`, matching the Settings menu's
abbreviations — now used on **both** panel sizes, since the row is 128px
wide either way and only panel *height* differs), the last note sent, and a
MIDI activity indicator (small square, top-right corner) that fills solid
when a message went out within `UI::MIDI_BLINK_WINDOW_MS` (80ms) and shows
as an outline when idle. `MidiEngine::recentlyActive()` backs this — a
`volatile uint32_t` timestamp updated by every `sendNoteOn`/`sendNoteOff`/
`sendCC`/`panic()` call (core0), read by core1's render — same single-
writer/single-reader reasoning as the rest of `MonitorState`, and the same
"simple read-only status query" precedent as `isMounted()` already being
called from core1. The 80ms window is deliberately longer than
`UI::UI_REFRESH_MS` (50ms) so a single, isolated MIDI message is very likely
to show as "on" for at least one render frame, even in the worst-case
timing alignment between when it's sent and when render() next samples it.
One tiny, harmless quirk: `_lastActivityMs` starts at 0, so the indicator
reads as "recently active" for the first ~80ms after boot regardless of
whether anything's actually been sent — never actually visible in practice
since OLED/I2C init takes longer than that anyway.

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