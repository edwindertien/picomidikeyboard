#include "keyboard_engine.h"
#include "midi_engine.h"
#include <math.h>

KeyboardEngine keyboardEngine;

// Physical (row,col) -> logical key index 0..95 within a bank.
// Confirmed against a hardware trace (chromatic run, see context.md
// "Physical key mapping"): row = semitone, increasing row = increasing
// pitch, row 0 = C — exactly as assumed. But col (the 74HC238 address,
// 0..7) runs the OTHER way: the lowest octave sits at the highest column
// address, not the lowest, so it's inverted here before combining with row.
uint8_t KeyboardEngine::keyIndex(uint8_t row, uint8_t col) {
  uint8_t octave = (uint8_t)(HW::MATRIX_COLS_PER_BANK - 1 - col);
  return (uint8_t)(octave * HW::MATRIX_ROWS + row);
}

void KeyboardEngine::reset() {
  for (auto& row : _pending)
    for (auto& cell : row)
      cell = PendingKey{};
}

bool KeyboardEngine::onEvent(const KeyEvent& e, const PersistentConfig& cfg,
                              int& outNote, int& outVelocity) {
  if (cfg.mode == KeyboardMode::DoubleManual) {
    return handleDoubleManual(e, cfg, outNote, outVelocity);
  }
  return handleVelocity(e, cfg, outNote, outVelocity);
}

bool KeyboardEngine::handleDoubleManual(const KeyEvent& e, const PersistentConfig& cfg,
                                         int& outNote, int& outVelocity) {
  const ManualConfig& m = (e.bank == 0) ? cfg.manualA : cfg.manualB;
  if (!m.enabled) return false;

  int note = (int)m.startNote + keyIndex(e.row, e.col);
  if (note < 0 || note > 127) return false;

  if (e.type == KeyEventType::Make) {
    midiEngine.sendNoteOn((uint8_t)note, m.velocity, m.channel);
    outNote = note;
    outVelocity = m.velocity;
    return true;
  }
  midiEngine.sendNoteOff((uint8_t)note, 0, m.channel);
  return false;
}

bool KeyboardEngine::handleVelocity(const KeyEvent& e, const PersistentConfig& cfg,
                                     int& outNote, int& outVelocity) {
  const VelocityConfig& v = cfg.velocity;
  PendingKey& p = _pending[e.row][e.col];

  int note = (int)v.startNote + keyIndex(e.row, e.col);
  if (note < 0 || note > 127) return false;

  if (e.bank == 0) {
    // Top contact — touched first on the way down, released last on the way
    // back up. See context.md "Velocity contact order" for why Note
    // Off is driven from this contact rather than the bottom one.
    if (e.type == KeyEventType::Make) {
      p.topMade  = true;
      p.topMakeUs = e.timestampUs;
    } else {
      if (p.sounding) {
        midiEngine.sendNoteOff((uint8_t)note, 0, v.channel);
      }
      p.sounding = false;
      p.topMade  = false;
    }
    return false;
  }

  // Bottom contact — only its Make matters for v1; its Break is intentionally
  // ignored (release is entirely driven by the top contact opening, above).
  if (e.type == KeyEventType::Make && p.topMade && !p.sounding) {
    uint32_t dtUs = e.timestampUs - p.topMakeUs;
    uint8_t vel = travelTimeToVelocity(dtUs, v);
    midiEngine.sendNoteOn((uint8_t)note, vel, v.channel);
    p.sounding = true;
    outNote = note;
    outVelocity = vel;
    return true;
  }
  return false;
}

uint8_t KeyboardEngine::travelTimeToVelocity(uint32_t dtUs, const VelocityConfig& v) {
  uint32_t dtMs = dtUs / 1000UL;

  if (dtMs <= v.minTravelMs) return 127;
  if (dtMs >= v.maxTravelMs) return 1;

  float span = (float)(v.maxTravelMs - v.minTravelMs);
  float t = (float)(dtMs - v.minTravelMs) / span;  // 0 (fast/hard) .. 1 (slow/soft)

  float shaped;
  switch (v.curveShape) {
    case 1:  shaped = t * t;      break;  // exponential — favours high velocities
    case 2:  shaped = sqrtf(t);   break;  // logarithmic — favours low velocities
    default: shaped = t;          break;  // linear
  }

  int vel = 127 - (int)(shaped * 126.0f);
  if (vel < 1)   vel = 1;
  if (vel > 127) vel = 127;
  return (uint8_t)vel;
}
