#pragma once
#include <Arduino.h>
#include "../config.h"
#include "../model/keyboard_model.h"

// ---------------------------------------------------------------------------
// KeyMatrix — raw scanner for the 12-row x (8-col x 2-bank) contact matrix.
//
// This class knows NOTHING about MIDI, notes, or KeyboardMode — it only
// scans the physical grid and emits debounced Make/Break KeyEvents. All
// mode-dependent interpretation (which note, on/off vs. velocity timing)
// lives one layer up in KeyboardEngine. This mirrors the IServoBus split in
// picoST3215: a dumb, reusable driver underneath a mode-aware App layer.
//
// poll() is non-blocking and cheap — call it every App::tickCore0()
// iteration, same "just as fast as possible" philosophy as picoST3215's
// App::tick(). A full 16-column x 12-row pass costs roughly
// 16 * (SETTLE_US + ~12 digitalReads) — on the order of a few hundred
// microseconds, which is what gives Velocity mode its contact-timing
// resolution (see context.md).
// ---------------------------------------------------------------------------
class KeyMatrix {
public:
  void begin();
  void poll();

  // Drain one pending debounced event. Returns false when the queue is empty.
  bool popEvent(KeyEvent& out);

  // Diagnostic: sweeps all 16 columns fresh, bypassing debounce and the
  // event queue entirely. out16[c] (c = bank*8+col, 0..15) has bit r set if
  // row r currently reads pressed (raw, unfiltered) for that column. Meant
  // to show exactly what's on the pins right now — see App's 'k' serial
  // stream command.
  void readRawColumns(uint16_t* out16);

  // Count of events silently dropped because the queue was full (diagnostic
  // only — should stay at 0 in normal operation; a rising count means
  // App::tickCore0() isn't being called often enough relative to key traffic).
  uint32_t droppedEvents() const { return _dropped; }

private:
  // TUNE ON REAL HARDWARE — starting points only, see context.md.
  static constexpr uint16_t SETTLE_US   = 8;     // mux propagation + wiring settle
  static constexpr uint32_t DEBOUNCE_US = 4000;  // post-transition lockout (leading-edge
                                                  // debounce — see updateCell() in the .cpp)

  void selectColumn(uint8_t bank, uint8_t col);
  void updateCell(uint8_t row, uint8_t col, uint8_t bank, bool raw, uint32_t nowUs);
  void pushEvent(uint8_t row, uint8_t col, uint8_t bank, KeyEventType type, uint32_t t);

  bool     _stable    [HW::MATRIX_ROWS][HW::MATRIX_COLS_PER_BANK][HW::MATRIX_BANKS] = {};
  uint32_t _lastChange[HW::MATRIX_ROWS][HW::MATRIX_COLS_PER_BANK][HW::MATRIX_BANKS] = {};

  static constexpr uint8_t QUEUE_SIZE = 64;
  KeyEvent _queue[QUEUE_SIZE];
  uint8_t  _qHead = 0, _qTail = 0;
  uint32_t _dropped = 0;
};

extern KeyMatrix keyMatrix;
