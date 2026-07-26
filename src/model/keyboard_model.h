#pragma once
#include <Arduino.h>
#include "../config.h"

// =============================================================================
// KeyboardMode — the two hardware-supported ways of reading the same 12x8x2
// matrix. See KEYBOARD_CONTEXT.md "Matrix interpretation" for the reasoning.
// =============================================================================
enum class KeyboardMode : uint8_t {
  DoubleManual = 0,  // bank0 and bank1 = two independent 96-key organ manuals
                      // (on/off contacts, no velocity — authentic tonewheel
                      // organ behaviour). "Flexible note range" mode.
  Velocity     = 1,  // bank0/bank1 = top/bottom contact of the SAME 96 keys;
                      // contact-to-contact time -> MIDI velocity.
};

// One manual's configuration in DoubleManual mode.
struct ManualConfig {
  uint8_t startNote = 36;   // MIDI note for physical key (row0,col0) of this bank
  uint8_t channel   = 1;    // 1-16
  uint8_t velocity  = 100;  // fixed velocity — organ contacts are on/off only
  bool    enabled   = true;
};

// The single manual's configuration in Velocity mode.
struct VelocityConfig {
  uint8_t  startNote       = 36;
  uint8_t  channel         = 1;
  uint16_t minTravelMs     = 1;     // fastest realistic press -> velocity 127
  uint16_t maxTravelMs     = 60;    // slowest realistic press -> velocity 1
  uint8_t  curveShape      = 0;     // 0=linear, 1=exponential, 2=logarithmic
  uint8_t  releaseVelocity = 64;    // fixed — no break-contact release sensing (v1)
};

// One CC pot's MIDI assignment.
struct CcSlot {
  uint8_t cc      = 0xFF;   // 0xFF = unassigned (no message sent)
  uint8_t channel = 1;
};

// ---------------------------------------------------------------------------
// Raw physical matrix event, produced by KeyMatrix::poll()/popEvent().
// Interpretation into MIDI (which note, on/off vs. velocity timing) happens
// one layer up, in KeyboardEngine — KeyMatrix itself knows nothing about
// MIDI or the current KeyboardMode.
// ---------------------------------------------------------------------------
enum class KeyEventType : uint8_t { Make, Break };

struct KeyEvent {
  uint8_t      row;    // 0..11
  uint8_t      col;    // 0..7
  uint8_t      bank;   // 0..1
  KeyEventType type;
  uint32_t     timestampUs;
};
