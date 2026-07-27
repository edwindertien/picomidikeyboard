#include "app.h"
#include <stdio.h>

App::App(AppState& state, IEncoder& encoder, OledUi& ui)
    : _app(state), _encoder(encoder), _ui(ui) {}

// =============================================================================
// core0: matrix scan, CC mux, MIDI, persistence, debug commands
// =============================================================================

void App::beginCore0() {
  keyMatrix.begin();
  ccMux.begin();
}

void App::persistNow() {
  PersistentConfig snap;
  { SharedConfigGuard g(sharedConfig); snap = *g; }
  persist.save(snap);
}

// ---------------------------------------------------------------------------
// tickCore0 — takes one snapshot of the shared config per call (a single
// short-held lock, cheap struct copy), then does everything that isn't I2C:
// matrix scan, MIDI send, CC pots, persistence. Runs as fast as loop() spins
// — no fixed rate, same philosophy as picoST3215's App::tick().
// ---------------------------------------------------------------------------
void App::tickCore0() {
  PersistentConfig localCfg;
  {
    SharedConfigGuard g(sharedConfig);
    localCfg = *g;
  }
  bool needsSave = sharedConfig.takeDirty();

  // Detect a mode change made by EITHER the serial 'm' command (this core)
  // or the Settings menu's mode field (core1, via toggleModeFromMenu()) and
  // react exactly once — KeyboardEngine is core0-owned state, so only core0
  // ever touches it, regardless of which side actually flipped the mode.
  if (localCfg.mode != _lastSeenMode) {
    keyboardEngine.reset();
    _lastSeenMode = localCfg.mode;
  }

  keyMatrix.poll();
  drainKeyEvents(localCfg);
  streamKeyIfDue();

  ccMux.tick(localCfg.ccMap, _app.monitor.ccLive);
  streamCcIfDue();

  if (needsSave) {
    persist.save(localCfg);
  }

  midiEngine.tick();
}

void App::drainKeyEvents(const PersistentConfig& cfg) {
  KeyEvent e;
  while (keyMatrix.popEvent(e)) {
    int note = -1, vel = -1;
    if (keyboardEngine.onEvent(e, cfg, note, vel)) {
      _app.monitor.lastNote     = note;
      _app.monitor.lastVelocity = vel;
    }
  }
  _app.monitor.droppedKeyEvents = keyMatrix.droppedEvents();
}

void App::onMidiCC(uint8_t channel, uint8_t cc, uint8_t value) {
  // Reserved for future use (e.g. remote mode switching via an incoming CC).
  (void)channel; (void)cc; (void)value;
}

void App::toggleMode() {
  KeyboardMode newMode;
  {
    SharedConfigGuard g(sharedConfig);
    g->mode = (g->mode == KeyboardMode::DoubleManual)
                ? KeyboardMode::Velocity
                : KeyboardMode::DoubleManual;
    newMode = g->mode;
  }
  keyboardEngine.reset();
  _lastSeenMode = newMode;  // avoid tickCore0() redundantly re-detecting this
  persistNow();
  Serial.print(F("[MODE] Now: "));
  Serial.println(newMode == KeyboardMode::DoubleManual ? "DoubleManual" : "Velocity");
}

void App::factoryReset() {
  { SharedConfigGuard g(sharedConfig); applyDefaultConfig(*g); }
  persistNow();
  keyboardEngine.reset();
  _lastSeenMode = KeyboardMode::DoubleManual;  // matches applyDefaultConfig()'s default
  _app.ui.menuIndex = 0;
  Serial.println(F("[RESET] Config overwritten with the compiled-in defaults from applyDefaultConfig() and saved."));
}

void App::dumpStatus(Stream& out) {
  PersistentConfig c;
  { SharedConfigGuard g(sharedConfig); c = *g; }

  out.println(F("--- status ---"));
  out.print(F("mode: "));
  out.println(c.mode == KeyboardMode::DoubleManual ? "DoubleManual" : "Velocity");
  out.print(F("usbMounted: ")); out.println(midiEngine.isMounted() ? "yes" : "no");
  out.print(F("lastNote: "));   out.println(_app.monitor.lastNote);
  out.print(F("lastVelocity: "));out.println(_app.monitor.lastVelocity);
  out.print(F("droppedKeyEvents: ")); out.println(_app.monitor.droppedKeyEvents);
  out.print(F("manualA: start=")); out.print(c.manualA.startNote);
  out.print(F(" ch="));  out.print(c.manualA.channel);
  out.print(F(" en="));  out.println(c.manualA.enabled);
  out.print(F("manualB: start=")); out.print(c.manualB.startNote);
  out.print(F(" ch="));  out.print(c.manualB.channel);
  out.print(F(" en="));  out.println(c.manualB.enabled);
  out.print(F("velocity: start=")); out.print(c.velocity.startNote);
  out.print(F(" ch="));  out.print(c.velocity.channel);
  out.print(F(" minMs=")); out.print(c.velocity.minTravelMs);
  out.print(F(" maxMs=")); out.print(c.velocity.maxTravelMs);
  out.print(F(" curve=")); out.println(c.velocity.curveShape);
}

void App::toggleCcStream() {
  _ccStreamOn = !_ccStreamOn;
  Serial.println(_ccStreamOn
    ? F("[CCSTREAM] on — 16 raw ADC values (0-4095), CSV, ~20Hz")
    : F("[CCSTREAM] off"));
}

// ---------------------------------------------------------------------------
// Streams all 16 raw (unfiltered) CC-pot ADC readings as one CSV line every
// ~50ms while enabled — Arduino Serial Plotter and most quick CSV-over-serial
// scripts read this directly. Deliberately reads via CcMux::readAllRaw(),
// NOT the ccLive[] the OLED shows — that's the filtered/hysteresis-gated
// MIDI-facing value, which would hide exactly the floating-pin noise this
// is meant to show before anything's wired up.
// ---------------------------------------------------------------------------
void App::streamCcIfDue() {
  if (!_ccStreamOn) return;
  unsigned long now = millis();
  if (now - _lastCcStreamMs < 50) return;
  _lastCcStreamMs = now;

  uint16_t raw[HW::CC_CHANNELS];
  ccMux.readAllRaw(raw);

  for (uint8_t i = 0; i < HW::CC_CHANNELS; ++i) {
    if (i > 0) Serial.print(',');
    Serial.print(raw[i]);
  }
  Serial.println();
}

void App::toggleKeyStream() {
  _keyStreamOn = !_keyStreamOn;
  if (_keyStreamOn) {
    Serial.println(F("[KEYSTREAM] on — 16 columns x 12-row raw bitmap, hex, ~10Hz"));
    Serial.println(F("[KEYSTREAM] each line: col00..col15, each a 3-digit hex row mask (bit0=row0..bit11=row11, 1=pressed)"));
  } else {
    Serial.println(F("[KEYSTREAM] off"));
  }
}

// ---------------------------------------------------------------------------
// Streams the raw (un-debounced) state of all 16 columns as one line every
// ~100ms — deliberately bypasses KeyMatrix's debounce/event-queue path
// entirely (KeyMatrix::readRawColumns()), same reasoning as the CC-pot 'p'
// stream: shows exactly what's on the pins right now, not an interpretation
// of it. Column c = bank*8+col (0..15); each value is the 12-bit row mask
// for that column, printed as 3 hex digits.
// ---------------------------------------------------------------------------
void App::streamKeyIfDue() {
  if (!_keyStreamOn) return;
  unsigned long now = millis();
  if (now - _lastKeyStreamMs < 100) return;
  _lastKeyStreamMs = now;

  uint16_t cols[16];
  keyMatrix.readRawColumns(cols);

  char line[16 * 4 + 1];
  int pos = 0;
  for (uint8_t c = 0; c < 16; ++c) {
    pos += snprintf(line + pos, sizeof(line) - pos, "%s%03X", (c > 0 ? " " : ""), cols[c]);
  }
  Serial.println(line);
}

// =============================================================================
// core1: encoder input + OLED render (the only thing on the I2C bus)
// =============================================================================

void App::tickCore1() {
  handleInput();
  render();
}

void App::handleInput() {
  _encoder.update();
  int  delta      = _encoder.readSteps();
  bool shortPress = _encoder.wasShortPressed();
  bool longPress  = _encoder.wasLongPressed();

  if (delta == 0 && !shortPress && !longPress) return;

  if (_app.ui.screen == ScreenId::Home) {
    handleHomeInput(delta, shortPress, longPress);
  } else {
    handleSettingsInput(delta, shortPress, longPress);
  }
}

void App::handleHomeInput(int /*delta*/, bool shortPress, bool /*longPress*/) {
  if (shortPress) {
    _app.ui.screen    = ScreenId::Settings;
    _app.ui.menuIndex = 0;
    _app.ui.editing   = false;
  }
}

void App::handleSettingsInput(int delta, bool shortPress, bool longPress) {
  KeyboardMode mode;
  { SharedConfigGuard g(sharedConfig); mode = g->mode; }
  int fieldCount = settingsFieldCount(mode);

  if (longPress) {
    if (_app.ui.editing) {
      _app.ui.editing = false;
    } else {
      // Persistence is no longer tied to "leaving Settings" — core0 saves
      // within one tick of any edit, via SharedConfig's dirty flag. This
      // just navigates back.
      _app.ui.screen = ScreenId::Home;
    }
    return;
  }

  if (shortPress) {
    _app.ui.editing = !_app.ui.editing;
    return;
  }

  if (delta != 0) {
    if (_app.ui.editing) {
      adjustField(_app.ui.menuIndex, delta);
    } else {
      int idx = _app.ui.menuIndex + delta;
      if (idx < 0) idx = 0;
      if (idx >= fieldCount) idx = fieldCount - 1;
      _app.ui.menuIndex = idx;
    }
  }
}

// ---------------------------------------------------------------------------
// Settings fields
//
// DoubleManual mode: 0=Mode 1-4=Manual A(start/chan/vel/on) 5-8=Manual B(...)
// Velocity mode:     0=Mode 1=Start 2=Chan 3=MinMs 4=MaxMs 5=Curve
//
// CC-pot assignment (ccMap) is not yet exposed here — see context.md
// "Known gaps / next steps". Edit applyDefaultConfig() (shared_config.cpp)
// or ccMap directly to change it for now.
// ---------------------------------------------------------------------------
int App::settingsFieldCount(KeyboardMode mode) const {
  return (mode == KeyboardMode::DoubleManual) ? 9 : 6;
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void App::toggleModeFromMenu() {
  // Core1's half of a mode toggle: flip the bit under lock and mark dirty,
  // nothing else. No Serial (core0-exclusive) and no KeyboardEngine::reset()
  // (core0-owned state) — core0's tickCore0() detects the change itself via
  // _lastSeenMode and reacts there. See "Dual-core split" in context.md.
  {
    SharedConfigGuard g(sharedConfig);
    g->mode = (g->mode == KeyboardMode::DoubleManual)
                ? KeyboardMode::Velocity
                : KeyboardMode::DoubleManual;
  }
  sharedConfig.markDirty();
  _app.ui.menuIndex = 0;  // field count differs per mode — stay on the Mode row
}

void App::adjustField(int index, int delta) {
  if (index == 0) {
    if (delta != 0) toggleModeFromMenu();
    return;
  }

  SharedConfigGuard g(sharedConfig);
  PersistentConfig& c = *g;

  if (c.mode == KeyboardMode::DoubleManual) {
    ManualConfig& m = (index <= 4) ? c.manualA : c.manualB;
    int field = (index <= 4) ? index : index - 4; // 1=start 2=chan 3=vel 4=on
    switch (field) {
      case 1: m.startNote = (uint8_t)clampi(m.startNote + delta, 0, 127); break;
      case 2: m.channel   = (uint8_t)clampi(m.channel + delta, 1, 16);    break;
      case 3: m.velocity  = (uint8_t)clampi(m.velocity + delta, 1, 127);  break;
      case 4: if (delta != 0) m.enabled = !m.enabled;                    break;
    }
  } else {
    VelocityConfig& v = c.velocity;
    switch (index) {
      case 1: v.startNote   = (uint8_t)clampi(v.startNote + delta, 0, 127);        break;
      case 2: v.channel     = (uint8_t)clampi(v.channel + delta, 1, 16);           break;
      case 3: v.minTravelMs = (uint16_t)clampi(v.minTravelMs + delta, 0, v.maxTravelMs); break;
      case 4: v.maxTravelMs = (uint16_t)clampi(v.maxTravelMs + delta, v.minTravelMs, 500); break;
      case 5: {
        int nv = ((int)v.curveShape + delta) % 3;
        if (nv < 0) nv += 3;
        v.curveShape = (uint8_t)nv;
        break;
      }
    }
  }

  sharedConfig.markDirty();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void App::render() {
  if (!_app.ui.oledOk) return;
  unsigned long now = millis();
  if (now - _lastUiMs < UI::UI_REFRESH_MS) return;
  _lastUiMs = now;

  if (_app.ui.screen == ScreenId::Home) renderHome();
  else                                   renderSettings();
}

void App::renderHome() {
  KeyboardMode mode;
  { SharedConfigGuard g(sharedConfig); mode = g->mode; }

  _ui.drawStatus(mode, midiEngine.isMounted(),
                 _app.monitor.lastNote, _app.monitor.lastVelocity,
                 _app.monitor.droppedKeyEvents,
                 _app.monitor.ccLive, HW::CC_CHANNELS);
}

void App::renderSettings() {
  PersistentConfig c;
  { SharedConfigGuard g(sharedConfig); c = *g; }

  int count = settingsFieldCount(c.mode);
  char nameBuf[6];

  snprintf(_fieldBuf[0], sizeof(_fieldBuf[0]), "Mode:%s",
           c.mode == KeyboardMode::DoubleManual ? "DblMan" : "Veloc");

  if (c.mode == KeyboardMode::DoubleManual) {
    midiNoteName(c.manualA.startNote, nameBuf, sizeof(nameBuf));
    snprintf(_fieldBuf[1], sizeof(_fieldBuf[1]), "A Start:%s", nameBuf);
    snprintf(_fieldBuf[2], sizeof(_fieldBuf[2]), "A Chan:%d", c.manualA.channel);
    snprintf(_fieldBuf[3], sizeof(_fieldBuf[3]), "A Vel:%d", c.manualA.velocity);
    snprintf(_fieldBuf[4], sizeof(_fieldBuf[4]), "A On:%s", c.manualA.enabled ? "Yes" : "No");

    midiNoteName(c.manualB.startNote, nameBuf, sizeof(nameBuf));
    snprintf(_fieldBuf[5], sizeof(_fieldBuf[5]), "B Start:%s", nameBuf);
    snprintf(_fieldBuf[6], sizeof(_fieldBuf[6]), "B Chan:%d", c.manualB.channel);
    snprintf(_fieldBuf[7], sizeof(_fieldBuf[7]), "B Vel:%d", c.manualB.velocity);
    snprintf(_fieldBuf[8], sizeof(_fieldBuf[8]), "B On:%s", c.manualB.enabled ? "Yes" : "No");
  } else {
    midiNoteName(c.velocity.startNote, nameBuf, sizeof(nameBuf));
    snprintf(_fieldBuf[1], sizeof(_fieldBuf[1]), "Start:%s", nameBuf);
    snprintf(_fieldBuf[2], sizeof(_fieldBuf[2]), "Chan:%d", c.velocity.channel);
    snprintf(_fieldBuf[3], sizeof(_fieldBuf[3]), "MinMs:%d", c.velocity.minTravelMs);
    snprintf(_fieldBuf[4], sizeof(_fieldBuf[4]), "MaxMs:%d", c.velocity.maxTravelMs);
    static const char* kCurves[3] = {"Linear","Expo","Log"};
    snprintf(_fieldBuf[5], sizeof(_fieldBuf[5]), "Curve:%s", kCurves[c.velocity.curveShape % 3]);
  }

  const char* items[MAX_SETTINGS_FIELDS];
  for (int i = 0; i < count; ++i) items[i] = _fieldBuf[i];

  _ui.drawMenu("Settings", items, count, _app.ui.menuIndex, _app.ui.editing,
               _app.ui.editing ? "Short=Done Long=Cancel" : "Short=Edit Long=Back");
}
