#pragma once
#include "../app_state.h"
#include "../drivers/iencoder.h"
#include "../drivers/oled_ui.h"
#include "../drivers/key_matrix.h"
#include "../drivers/cc_mux.h"
#include "../drivers/keyboard_engine.h"
#include "../drivers/midi_engine.h"
#include "../drivers/persist.h"
#include "../drivers/shared_config.h"

// ---------------------------------------------------------------------------
// App — split down the middle by which core owns which peripheral.
//
// tickCore0() / beginCore0(): matrix scan, CC mux, MIDI (USB+DIN), LittleFS
// persistence, serial debug commands. Everything that ISN'T I2C.
//
// tickCore1(): encoder input + OLED render. The only thing on the I2C bus,
// so it's the only thing that runs on core1 — see main.cpp's setup1()/
// loop1() and context.md "Dual-core split" for the full reasoning.
//
// The two sides only share PersistentConfig (via SharedConfig, mutex-
// guarded) and the plain-volatile MonitorState fields in AppState — nothing
// else crosses the core boundary.
// ---------------------------------------------------------------------------
class App {
public:
  App(AppState& state, IEncoder& encoder, OledUi& ui);

  void beginCore0();
  void tickCore0();

  void tickCore1();  // I2C (OLED+encoder) init happens directly in main.cpp's
                      // setup1(), not here — see main.cpp for why

  // Called by the MIDI engine CC callback (static trampoline -> this).
  // Currently just logged to CDC; reserved for e.g. remote mode switching.
  // Runs on core0 (MidiEngine::tick() -> _midiLib.read() is core0-only).
  void onMidiCC(uint8_t channel, uint8_t cc, uint8_t value);

  // Serial debug commands (see main.cpp loop()) — all core0-exclusive.
  void toggleMode();
  void toggleCcStream();
  void toggleKeyStream();
  void factoryReset();
  void dumpStatus(Stream& out);

private:
  AppState& _app;
  IEncoder& _encoder;
  OledUi&   _ui;

  unsigned long _lastUiMs = 0;

  // core0's own record of the last KeyboardMode it saw, so it can detect a
  // change made by EITHER toggleMode() (serial, core0) or the Settings
  // menu's mode field (core1) and react exactly once — see tickCore0().
  KeyboardMode _lastSeenMode = KeyboardMode::DoubleManual;

  void persistNow();  // core0 only: snapshot cfg under lock, then LittleFS write

  void drainKeyEvents(const PersistentConfig& cfg);

  bool          _ccStreamOn     = false;
  unsigned long _lastCcStreamMs = 0;
  void streamCcIfDue();

  bool          _keyStreamOn     = false;
  unsigned long _lastKeyStreamMs = 0;
  void streamKeyIfDue();

  // --- core1: input + render ---
  void handleInput();
  void handleHomeInput(int delta, bool shortPress, bool longPress);
  void handleSettingsInput(int delta, bool shortPress, bool longPress);
  void adjustField(int index, int delta);
  void toggleModeFromMenu();  // core1's mode-flip: locks + marks dirty ONLY,
                              // no Serial, no KeyboardEngine touch — see .cpp

  int  settingsFieldCount(KeyboardMode mode) const;

  static constexpr int MAX_SETTINGS_FIELDS = 9;
  char _fieldBuf[MAX_SETTINGS_FIELDS][24];

  void render();
  void renderHome();
  void renderSettings();
};
