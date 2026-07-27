#pragma once
#include <Arduino.h>
#include "../config.h"
#include "../model/keyboard_model.h"

// ---------------------------------------------------------------------------
// CcMux — reads the 16 analog CC pots (drawbars, volume, drive, chorus, ...)
// through a single 74HC4067 mux feeding ADC0 (GPIO26).
//
// tick() services the MIDI-facing path: one channel per call, round-robin,
// with a lightweight EMA filter and a hysteresis gate so idle/floating pots
// don't spam CC messages. readAllRaw() is a separate, unfiltered full sweep
// for diagnostics (see App's 'p' serial stream command) — it does NOT touch
// the round-robin position or the filter/hysteresis state tick() uses, so
// the two can run side by side without interfering with each other.
// ---------------------------------------------------------------------------
class CcMux {
public:
  void begin();

  // MIDI-facing: one channel per call. liveOut is updated every call (used
  // by the OLED status screen, read from core1 — see MonitorState), with
  // the filtered/hysteresis-gated 0-127 value; MIDI is only sent for the
  // channel whose turn it is, and only if it moved past the hysteresis band
  // since the last value actually sent.
  void tick(const CcSlot* map, volatile uint8_t* liveOut);

  // Diagnostic: reads all 16 channels fresh, bypassing the EMA filter and
  // hysteresis gate entirely. out16[i] = raw 12-bit ADC count (0-4095) for
  // channel i. Meant to show exactly what's on the pin, noise included —
  // e.g. to confirm 16 distinct floating channels before anything's wired.
  void readAllRaw(uint16_t* out16);

private:
  void selectChannel(uint8_t ch);
  int  readSettled();  // channel must already be selected: settle + discard + real read

  // TUNE ON REAL HARDWARE — starting points only, see context.md.
  static constexpr uint16_t SWITCH_SETTLE_US = 30;  // 4067 output + shared bus settle
  static constexpr uint8_t  HYSTERESIS       = 2;   // in 0-127 CC units
  static constexpr uint8_t  FILTER_SHIFT     = 2;   // EMA: filtered += (raw-filtered)>>SHIFT

  uint8_t _lastSent[HW::CC_CHANNELS];
  bool    _everSent[HW::CC_CHANNELS] = {};
  int16_t _filtered[HW::CC_CHANNELS];  // 12-bit EMA state; -1 = not yet initialised
  uint8_t _channel = 0;
};

extern CcMux ccMux;
