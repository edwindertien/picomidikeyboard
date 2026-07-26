#include "key_matrix.h"
#include "hardware/gpio.h"

KeyMatrix keyMatrix;

void KeyMatrix::begin() {
  pinMode(HW::MUX_SEL_A,    OUTPUT);
  pinMode(HW::MUX_SEL_B,    OUTPUT);
  pinMode(HW::MUX_SEL_C,    OUTPUT);
  pinMode(HW::MUX_SEL_BANK, OUTPUT);

  // ASSUMPTION: contacts pull the row line to GND when closed, so rows use
  // the internal pull-up and a closed contact reads LOW. If your PCB wires
  // it the other way (pull-down + contact to 3V3), flip the "== LOW" test
  // in poll() below and switch these to INPUT / INPUT_PULLDOWN.
  for (uint8_t r = 0; r < HW::MATRIX_ROWS; ++r) {
    pinMode(HW::MATRIX_ROW_PINS[r], INPUT_PULLUP);
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
  bool&     last       = _rawLast[row][col][bank];
  bool&     stable     = _stable[row][col][bank];
  uint32_t& lastChange = _lastChange[row][col][bank];

  if (raw != last) {
    // Raw level just changed (or bounced) — restart the debounce window.
    last = raw;
    lastChange = nowUs;
    return;
  }
  if (raw != stable && (nowUs - lastChange) >= DEBOUNCE_US) {
    stable = raw;
    pushEvent(row, col, bank, raw ? KeyEventType::Make : KeyEventType::Break, nowUs);
  }
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
        bool raw = ((allGpio >> HW::MATRIX_ROW_PINS[row]) & 1u) == 0; // closed = LOW
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