#pragma once
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include "../config.h"

using MidiCCCallback = void (*)(uint8_t channel, uint8_t cc, uint8_t value);

// ---------------------------------------------------------------------------
// MidiEngine — combined USB (TinyUSB CDC debug port + USB-MIDI device),
// reused near-verbatim from picoST3215, PLUS a hardware MIDI DIN output on
// GPIO12 (UART0/Serial1, TX only, 31250 baud) that mirrors every outgoing
// note/CC message. See context.md for why DIN mirrors everything.
//
// USB MIDI is driven through the fortyseveneffects MIDI library exactly as
// in picoST3215 (MIDI_CREATE_INSTANCE trampoline pattern); DIN bytes are
// written directly to Serial1 without going through the library, since the
// library is bound to the USB transport only.
// ---------------------------------------------------------------------------
class MidiEngine {
public:
  void begin();     // brings up USB CDC+MIDI device (CFG_TUD_CDC=1 adds CDC)
                     // and the DIN UART; call once from setup()
  void tick();       // drains incoming USB MIDI (CC only is handled for now)
  bool isMounted() const;

  void sendNoteOn (uint8_t note, uint8_t velocity, uint8_t channel);
  void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel);
  void sendCC     (uint8_t cc, uint8_t value, uint8_t channel);
  void panic();      // all-notes-off + all-sound-off on every channel

  // True if any message was sent within the last windowMs -- read-only,
  // safe to call from core1 (Home screen's activity blink) for the same
  // reason isMounted() is: no multi-step state involved, just a plain
  // volatile timestamp compare, no risk of corrupting the actual USB/DIN
  // send path.
  bool recentlyActive(uint32_t windowMs) const {
    return (millis() - _lastActivityMs) < windowMs;
  }

  void setOnCC(MidiCCCallback cb) { _onCC = cb; }

  // Public: accessed by the MIDI_CREATE_INSTANCE macro and the CC trampoline
  Adafruit_USBD_MIDI _usbMidi;
  MidiCCCallback      _onCC = nullptr;

private:
  void dinWrite3(uint8_t status, uint8_t d1, uint8_t d2);
  void noteActivity() { _lastActivityMs = millis(); }
  volatile uint32_t _lastActivityMs = 0;
};

extern MidiEngine midiEngine;