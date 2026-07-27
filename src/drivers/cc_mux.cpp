#include "cc_mux.h"
#include "midi_engine.h"

CcMux ccMux;

void CcMux::begin() {
  pinMode(HW::CC_MUX_SEL_S0, OUTPUT);
  pinMode(HW::CC_MUX_SEL_S1, OUTPUT);
  pinMode(HW::CC_MUX_SEL_S2, OUTPUT);
  pinMode(HW::CC_MUX_SEL_S3, OUTPUT);

  // RP2040's ADC is a native 12-bit SAR — use the full range rather than the
  // AVR-compatible 10-bit default, so the EMA filter below has real
  // sub-CC-step precision to average over instead of already-quantized
  // 10-bit noise.
  analogReadResolution(12);

  for (uint8_t i = 0; i < HW::CC_CHANNELS; ++i) _filtered[i] = -1;
}

void CcMux::selectChannel(uint8_t ch) {
  digitalWrite(HW::CC_MUX_SEL_S0, (ch >> 0) & 1);
  digitalWrite(HW::CC_MUX_SEL_S1, (ch >> 1) & 1);
  digitalWrite(HW::CC_MUX_SEL_S2, (ch >> 2) & 1);
  digitalWrite(HW::CC_MUX_SEL_S3, (ch >> 3) & 1);
}

int CcMux::readSettled() {
  delayMicroseconds(SWITCH_SETTLE_US);
  // Throwaway conversion: the shared analog bus (all 16 pot wipers land on
  // one trace through the 4067) still carries residual charge from whichever
  // channel was selected before this one. A single read right after
  // switching can read a blend of the two; this first conversion clears
  // that, the one right after it is the real sample.
  (void)analogRead(HW::CC_MUX_ADC_PIN);
  return analogRead(HW::CC_MUX_ADC_PIN); // 0-4095
}

void CcMux::tick(const CcSlot* map, volatile uint8_t* liveOut) {
  selectChannel(_channel);
  int raw = readSettled();

  int16_t& filt = _filtered[_channel];
  if (filt < 0) {
    filt = (int16_t)raw;  // first sample for this channel — jump straight there, no ramp-up
  } else {
    filt = (int16_t)(filt + ((raw - filt) >> FILTER_SHIFT));
  }
  uint8_t value = (uint8_t)(filt >> 5); // 0-4095 -> 0-127

  liveOut[_channel] = value;

  bool changedEnough = !_everSent[_channel] ||
                        (abs((int)value - (int)_lastSent[_channel]) > HYSTERESIS);
  if (changedEnough) {
    const CcSlot& slot = map[_channel];
    if (slot.cc != 0xFF) {
      midiEngine.sendCC(slot.cc, value, slot.channel);
    }
    _lastSent[_channel] = value;
    _everSent[_channel] = true;
  }

  _channel = (uint8_t)((_channel + 1) % HW::CC_CHANNELS);
}

void CcMux::readAllRaw(uint16_t* out16) {
  for (uint8_t ch = 0; ch < HW::CC_CHANNELS; ++ch) {
    selectChannel(ch);
    out16[ch] = (uint16_t)readSettled();
  }
}
