#include "app.h"
#include <stdio.h>

App::App(AppState& state, IEncoder& encoder, OledUi& ui)
    : _app(state), _encoder(encoder), _ui(ui) {}

// ---------------------------------------------------------------------------
// Default CC assignments: General MIDI's undefined/free controller numbers
// (3, 9, 14, 15, 20-31) — 16 of them, one per pot. Placeholders — reassign
// by editing this array (a runtime CC-assignment menu is future work, see
// KEYBOARD_CONTEXT.md "Known gaps / next steps").
// ---------------------------------------------------------------------------
static void applyDefaultConfig(PersistentConfig& cfg) {
  cfg = PersistentConfig{}; // struct defaults (see keyboard_model.h)

  static const uint8_t kDefaultCc[HW::CC_CHANNELS] = {
    3, 9, 14, 15, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
  };
  for (uint8_t i = 0; i < HW::CC_CHANNELS; ++i) {
    cfg.ccMap[i].cc      = kDefaultCc[i];
    cfg.ccMap[i].channel = 1;
  }

  // Two organ manuals stacked by default: lower manual starting at C2,
  // upper manual starting an octave+ above it. Adjust to taste in Settings.
  cfg.manualA.startNote = 36;  // C2
  cfg.manualA.channel   = 1;
  cfg.manualB.startNote = 60;  // C4
  cfg.manualB.channel   = 2;

  cfg.velocity.startNote = 36;
  cfg.velocity.channel   = 1;
}

void App::loadPersistedState() {
  if (!persist.load(_app.cfg)) {
    applyDefaultConfig(_app.cfg);
  }
}

void App::persistNow() {
  persist.save(_app.cfg);
  _app.cfgDirty = false;
}

void App::begin() {
  keyMatrix.begin();
  ccMux.begin();
  loadPersistedState();
}

// ---------------------------------------------------------------------------
// Tick — mirrors picoST3215's App::tick() structure: input, then the two
// scanning subsystems, then a lazy persist write, then a rate-gated render,
// then drain incoming USB MIDI. No fixed rate — runs as fast as loop() spins.
// ---------------------------------------------------------------------------
void App::tick() {
  handleInput();

  keyMatrix.poll();
  drainKeyEvents();

  ccMux.tick(_app.cfg.ccMap, _app.ccLive);
  streamCcIfDue();

  if (_app.cfgDirty) {
    persistNow();
  }

  render();
  midiEngine.tick();
}

void App::drainKeyEvents() {
  KeyEvent e;
  while (keyMatrix.popEvent(e)) {
    int note = -1, vel = -1;
    if (keyboardEngine.onEvent(e, _app.cfg, note, vel)) {
      _app.lastNote     = note;
      _app.lastVelocity = vel;
    }
  }
  _app.droppedKeyEvents = keyMatrix.droppedEvents();
}

void App::onMidiCC(uint8_t channel, uint8_t cc, uint8_t value) {
  // Reserved for future use (e.g. remote mode switching via an incoming CC).
  (void)channel; (void)cc; (void)value;
}

void App::toggleMode() {
  _app.cfg.mode = (_app.cfg.mode == KeyboardMode::DoubleManual)
                    ? KeyboardMode::Velocity
                    : KeyboardMode::DoubleManual;
  keyboardEngine.reset();
  _app.cfgDirty = true;
  _app.menuIndex = 0; // field count differs per mode — avoid a stale out-of-range index
  Serial.print(F("[MODE] Now: "));
  Serial.println(_app.cfg.mode == KeyboardMode::DoubleManual ? "DoubleManual" : "Velocity");
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

void App::dumpStatus(Stream& out) {
  out.println(F("--- status ---"));
  out.print(F("mode: "));
  out.println(_app.cfg.mode == KeyboardMode::DoubleManual ? "DoubleManual" : "Velocity");
  out.print(F("usbMounted: ")); out.println(midiEngine.isMounted() ? "yes" : "no");
  out.print(F("lastNote: "));   out.println(_app.lastNote);
  out.print(F("lastVelocity: "));out.println(_app.lastVelocity);
  out.print(F("droppedKeyEvents: ")); out.println(_app.droppedKeyEvents);
  out.print(F("manualA: start=")); out.print(_app.cfg.manualA.startNote);
  out.print(F(" ch="));  out.print(_app.cfg.manualA.channel);
  out.print(F(" en="));  out.println(_app.cfg.manualA.enabled);
  out.print(F("manualB: start=")); out.print(_app.cfg.manualB.startNote);
  out.print(F(" ch="));  out.print(_app.cfg.manualB.channel);
  out.print(F(" en="));  out.println(_app.cfg.manualB.enabled);
  out.print(F("velocity: start=")); out.print(_app.cfg.velocity.startNote);
  out.print(F(" ch="));  out.print(_app.cfg.velocity.channel);
  out.print(F(" minMs=")); out.print(_app.cfg.velocity.minTravelMs);
  out.print(F(" maxMs=")); out.print(_app.cfg.velocity.maxTravelMs);
  out.print(F(" curve=")); out.println(_app.cfg.velocity.curveShape);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void App::handleInput() {
  _encoder.update();
  int  delta      = _encoder.readSteps();
  bool shortPress = _encoder.wasShortPressed();
  bool longPress  = _encoder.wasLongPressed();

  if (delta == 0 && !shortPress && !longPress) return;

  if (_app.screen == ScreenId::Home) {
    handleHomeInput(delta, shortPress, longPress);
  } else {
    handleSettingsInput(delta, shortPress, longPress);
  }
}

void App::handleHomeInput(int /*delta*/, bool shortPress, bool /*longPress*/) {
  if (shortPress) {
    _app.screen     = ScreenId::Settings;
    _app.menuIndex  = 0;
    _app.editing    = false;
  }
}

void App::handleSettingsInput(int delta, bool shortPress, bool longPress) {
  int fieldCount = settingsFieldCount();

  if (longPress) {
    if (_app.editing) {
      _app.editing = false;
    } else {
      persistNow();
      _app.screen = ScreenId::Home;
    }
    return;
  }

  if (shortPress) {
    _app.editing = !_app.editing;
    return;
  }

  if (delta != 0) {
    if (_app.editing) {
      adjustField(_app.menuIndex, delta);
      _app.cfgDirty = true;
    } else {
      int idx = _app.menuIndex + delta;
      if (idx < 0) idx = 0;
      if (idx >= fieldCount) idx = fieldCount - 1;
      _app.menuIndex = idx;
    }
  }
}

// ---------------------------------------------------------------------------
// Settings fields
//
// DoubleManual mode: 0=Mode 1-4=Manual A(start/chan/vel/on) 5-8=Manual B(...)
// Velocity mode:     0=Mode 1=Start 2=Chan 3=MinMs 4=MaxMs 5=Curve
//
// CC-pot assignment (ccMap) is not yet exposed here — see
// KEYBOARD_CONTEXT.md "Known gaps / next steps". Edit applyDefaultConfig()
// above (or ccMap directly) to change it for now.
// ---------------------------------------------------------------------------
int App::settingsFieldCount() const {
  return (_app.cfg.mode == KeyboardMode::DoubleManual) ? 9 : 6;
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void App::adjustField(int index, int delta) {
  PersistentConfig& c = _app.cfg;

  if (index == 0) {
    // Mode toggle — any nonzero delta flips it. toggleMode() also resets
    // menuIndex to 0, since the field count differs between modes.
    toggleMode();
    return;
  }

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
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void App::render() {
  if (!_app.oledOk) return;
  unsigned long now = millis();
  if (now - _lastUiMs < UI::UI_REFRESH_MS) return;
  _lastUiMs = now;

  if (_app.screen == ScreenId::Home) renderHome();
  else                                renderSettings();
}

void App::renderHome() {
  _ui.drawStatus(_app.cfg.mode, midiEngine.isMounted(),
                 _app.lastNote, _app.lastVelocity,
                 _app.droppedKeyEvents,
                 _app.ccLive, HW::CC_CHANNELS);
}

void App::renderSettings() {
  PersistentConfig& c = _app.cfg;
  int count = settingsFieldCount();
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

  _ui.drawMenu("Settings", items, count, _app.menuIndex, _app.editing,
               _app.editing ? "Short=Done Long=Cancel" : "Short=Edit Long=Back");
}