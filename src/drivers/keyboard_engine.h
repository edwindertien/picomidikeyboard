#pragma once
#include <Arduino.h>
#include "../config.h"
#include "../model/keyboard_model.h"
#include "persist.h"

// ---------------------------------------------------------------------------
// KeyboardEngine — turns raw KeyEvents from KeyMatrix into MIDI note
// messages, according to the active KeyboardMode. Sits at the same
// architectural layer as busForProtocol() in picoST3215: KeyMatrix is the
// dumb reusable driver, KeyboardEngine is where the mode-specific policy
// lives, kept out of both the driver and the App/UI layer.
// ---------------------------------------------------------------------------
class KeyboardEngine {
public:
  // Feed one raw event. If it results in a MIDI Note On being sent, returns
  // true and fills outNote/outVelocity (used by the status screen); returns
  // false for Note Off and for events that don't (yet) complete a note.
  bool onEvent(const KeyEvent& e, const PersistentConfig& cfg,
               int& outNote, int& outVelocity);

  // Clears all in-flight velocity-mode "waiting for second contact" state.
  // Call this whenever the mode is switched at runtime, so a half-completed
  // press from the old mode can't leak into the new one.
  void reset();

private:
  bool handleDoubleManual(const KeyEvent& e, const PersistentConfig& cfg,
                           int& outNote, int& outVelocity);
  bool handleVelocity(const KeyEvent& e, const PersistentConfig& cfg,
                       int& outNote, int& outVelocity);

  static uint8_t keyIndex(uint8_t row, uint8_t col);
  static uint8_t travelTimeToVelocity(uint32_t dtUs, const VelocityConfig& v);

  struct PendingKey {
    bool     topMade  = false; // bank0 (top) Make seen, waiting for bank1 (bottom)
    uint32_t topMakeUs = 0;
    bool     sounding = false; // Note On already sent, waiting for release
  };
  PendingKey _pending[HW::MATRIX_ROWS][HW::MATRIX_COLS_PER_BANK];
};

extern KeyboardEngine keyboardEngine;
