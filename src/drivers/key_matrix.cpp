#include "key_matrix.h"
#include "hardware/gpio.h"

KeyMatrix keyMatrix;

void KeyMatrix::begin() {
  pinMode(HW::MUX_SEL_A,    OUTPUT);
  pinMode(HW::MUX_SEL_B,    OUTPUT);
  pinMode(HW::MUX_SEL_C,    OUTPUT);
  pinMode(HW::MUX_SEL_BANK, OUTPUT);

  // Rows idle LOW via internal pull-down; a closed contact pulls the row
  // HIGH. This matches the actual hardware: diodes point column->row, and
  // the 74HC238 (unlike the more common 74HC138) drives its SELECTED output
  // HIGH rather than LOW — so "selected column HIGH, key closed" needs to
  // read as the row going HIGH, not LOW. See context.md "Contact polarity".
  for (uint8_t r = 0; r < HW::MATRIX_ROWS; ++r) {
    pinMode(HW::MATRIX_ROW_PINS[r], INPUT_PULLDOWN);
  }
}

void KeyMatrix::selectColumn(uint8_t bank, uint8_t col) {
  digitalWrite(HW::MUX_SEL_A,    (col  >> 0) & 1);
  digitalWrite(HW::MUX_SEL_B,    (col  >> 1) & 1);
  digitalWrite(HW::MUX_SEL_C,    (col  >> 2) & 1);
  digitalWrite(HW::MUX_SEL_BANK, bank & 1);
}

void KeyMatrix::pushEvent(uint8_t row, uint8_t col, uint8_t bank, KeyEventType type, uint32_t t) {
  uint8_t next = (uint8_t)((_qHead + 1) % QUEUE_SIZE);
  if (next == _qTail) { ++_dropped; return; } // queue full — drop, keep scanning
  _queue[_qHead] = KeyEvent{row, col, bank, type, t};
  _qHead = next;
}

void KeyMatrix::updateCell(uint8_t row, uint8_t col, uint8_t bank, bool raw, uint32_t nowUs) {
  bool&     stable     = _stable[row][col][bank];
  uint32_t& lastChange = _lastChange[row][col][bank];

  if (raw == stable) return;  // matches the last accepted state — nothing to do

  if ((nowUs - lastChange) < DEBOUNCE_US) return;  // inside the post-transition
                                                    // lockout — this is bounce, ignore it

  // Leading-edge debounce: accept the transition right now, at the instant
  // it's first seen, rather than waiting for it to hold stable. This is
  // what makes a keypress feel instant instead of adding a fixed DEBOUNCE_US
  // to every note — see context.md "Debounce: leading vs. trailing edge".
  stable = raw;
  lastChange = nowUs;
  pushEvent(row, col, bank, raw ? KeyEventType::Make : KeyEventType::Break, nowUs);
}

void KeyMatrix::poll() {
  for (uint8_t bank = 0; bank < HW::MATRIX_BANKS; ++bank) {
    for (uint8_t col = 0; col < HW::MATRIX_COLS_PER_BANK; ++col) {
      selectColumn(bank, col);
      delayMicroseconds(SETTLE_US);
      uint32_t now = micros();

      // Single SIO register read for all 30 GPIOs, instead of 12 separate
      // digitalRead() calls (each carrying Arduino-wrapper overhead — pin
      // table lookup, mode branch). This is what actually sets the scan
      // pass's speed, which is what sets Velocity mode's timing resolution.
      uint32_t allGpio = gpio_get_all();

      for (uint8_t row = 0; row < HW::MATRIX_ROWS; ++row) {
        bool raw = ((allGpio >> HW::MATRIX_ROW_PINS[row]) & 1u) != 0; // closed = HIGH
        updateCell(row, col, bank, raw, now);
      }
    }
  }
}

bool KeyMatrix::popEvent(KeyEvent& out) {
  if (_qTail == _qHead) return false;
  out = _queue[_qTail];
  _qTail = (uint8_t)((_qTail + 1) % QUEUE_SIZE);
  return true;
}

void KeyMatrix::readRawColumns(uint16_t* out16) {
  for (uint8_t bank = 0; bank < HW::MATRIX_BANKS; ++bank) {
    for (uint8_t col = 0; col < HW::MATRIX_COLS_PER_BANK; ++col) {
      selectColumn(bank, col);
      delayMicroseconds(SETTLE_US);
      uint32_t allGpio = gpio_get_all();

      uint16_t rowBits = 0;
      for (uint8_t row = 0; row < HW::MATRIX_ROWS; ++row) {
        if ((allGpio >> HW::MATRIX_ROW_PINS[row]) & 1u) rowBits |= (uint16_t)(1u << row);
      }
      out16[bank * HW::MATRIX_COLS_PER_BANK + col] = rowBits;
    }
  }
}