#include "midi_engine.h"

MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, midiEngine._usbMidi, _midiLib);

MidiEngine midiEngine;

// ---------------------------------------------------------------------------
// Trampoline — MIDI library uses a static C callback
// ---------------------------------------------------------------------------
static void onCCTrampoline(uint8_t channel, uint8_t cc, uint8_t value) {
  if (midiEngine._onCC) midiEngine._onCC(channel, cc, value);
}

// ---------------------------------------------------------------------------

void MidiEngine::begin() {
  // USB CDC + MIDI device init, identical order/pattern to picoST3215:
  // registering/starting the MIDI library brings the whole TinyUSB device
  // stack up, and CFG_TUD_CDC=1 (platformio.ini) adds the CDC debug port
  // alongside it automatically.
  _midiLib.begin(MIDI_CHANNEL_OMNI);
  _midiLib.setHandleControlChange(onCCTrampoline);
  _midiLib.turnThruOff();

  // Hardware MIDI DIN out — UART0 (Serial1), TX only, GPIO12.
  // RX is parked on HW::MIDI_DIN_RX_DUMMY_PIN (unconnected) purely so
  // Serial1.begin() doesn't reclaim GPIO1/13/17, which the key/CC matrices
  // already own. Nothing is ever read from Serial1.
  Serial1.setTX(HW::MIDI_DIN_TX_PIN);
  Serial1.setRX(HW::MIDI_DIN_RX_DUMMY_PIN);
  Serial1.begin(HW::MIDI_DIN_BAUD);
}

void MidiEngine::tick() {
  _midiLib.read();
}

bool MidiEngine::isMounted() const {
  return TinyUSBDevice.mounted();
}

void MidiEngine::dinWrite3(uint8_t status, uint8_t d1, uint8_t d2) {
  Serial1.write(status);
  Serial1.write(d1);
  Serial1.write(d2);
}

void MidiEngine::sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) {
  noteActivity();
  if (isMounted()) _midiLib.sendNoteOn(note, velocity, channel);
  dinWrite3((uint8_t)(0x90 | ((channel - 1) & 0x0F)), note & 0x7F, velocity & 0x7F);
}

void MidiEngine::sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) {
  noteActivity();
  if (isMounted()) _midiLib.sendNoteOff(note, velocity, channel);
  dinWrite3((uint8_t)(0x80 | ((channel - 1) & 0x0F)), note & 0x7F, velocity & 0x7F);
}

void MidiEngine::sendCC(uint8_t cc, uint8_t value, uint8_t channel) {
  noteActivity();
  if (isMounted()) _midiLib.sendControlChange(cc, value, channel);
  dinWrite3((uint8_t)(0xB0 | ((channel - 1) & 0x0F)), cc & 0x7F, value & 0x7F);
}

void MidiEngine::panic() {
  noteActivity();
  for (uint8_t ch = 1; ch <= 16; ++ch) {
    if (isMounted()) {
      _midiLib.sendControlChange(120, 0, ch); // all sound off
      _midiLib.sendControlChange(123, 0, ch); // all notes off
    }
    dinWrite3((uint8_t)(0xB0 | ((ch - 1) & 0x0F)), 120, 0);
    dinWrite3((uint8_t)(0xB0 | ((ch - 1) & 0x0F)), 123, 0);
  }
}