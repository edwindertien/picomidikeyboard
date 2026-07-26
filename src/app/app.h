#pragma once
#include "../app_state.h"
#include "../drivers/iencoder.h"
#include "../drivers/oled_ui.h"
#include "../drivers/key_matrix.h"
#include "../drivers/cc_mux.h"
#include "../drivers/keyboard_engine.h"
#include "../drivers/midi_engine.h"
#include "../drivers/persist.h"

class App {
public:
  App(AppState& state, IEncoder& encoder, OledUi& ui);

  void begin();
  void tick();

  // Called by the MIDI engine CC callback (static trampoline -> this).
  // Currently just logged to CDC; reserved for e.g. remote mode switching.
  void onMidiCC(uint8_t channel, uint8_t cc, uint8_t value);

  // Serial debug commands (see main.cpp loop())
  void toggleMode();
  void toggleCcStream();
  void dumpStatus(Stream& out);

private:
  AppState& _app;
  IEncoder& _encoder;
  OledUi&   _ui;

  unsigned long _lastUiMs = 0;

  bool          _ccStreamOn    = false;
  unsigned long _lastCcStreamMs = 0;
  void streamCcIfDue();

  void loadPersistedState();     // called from begin() — restore or defaults
  void persistNow();             // immediate LittleFS write (called on Back)

  void drainKeyEvents();
  void handleInput();
  void handleHomeInput(int delta, bool shortPress, bool longPress);
  void handleSettingsInput(int delta, bool shortPress, bool longPress);

  int  settingsFieldCount() const;
  void adjustField(int index, int delta);

  static constexpr int MAX_SETTINGS_FIELDS = 9;
  char _fieldBuf[MAX_SETTINGS_FIELDS][24];

  void render();
  void renderHome();
  void renderSettings();
};