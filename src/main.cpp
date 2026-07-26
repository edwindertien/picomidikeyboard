#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_TinyUSB.h>

#include "config.h"
#include "app_state.h"
#include "drivers/key_matrix.h"
#include "drivers/cc_mux.h"
#include "drivers/keyboard_engine.h"
#include "drivers/midi_engine.h"
#include "drivers/persist.h"
#include "drivers/oled_ui.h"
#include "drivers/encoder_unit.h"
#include "app/app.h"

// Single shared I2C bus for OLED + M5Stack encoder (GPIO20/21, per spec) —
// same pattern as picoST3215's BOARD_CUSTOM_RS485 variant, where both
// peripherals live on one Wire instance instead of Wire/Wire1.
static UnitEncoder encoder(Wire, HW::ENC_ADDR);

AppState appState;
OledUi   ui(HW::OLED_W, HW::OLED_H, &Wire, HW::OLED_ADDR);
App      app(appState, encoder, ui);

static void onCCReceived(uint8_t channel, uint8_t cc, uint8_t value) {
  app.onMidiCC(channel, cc, value);
}

void setup() {
  // USB MIDI device must come up first — this also brings up the CDC debug
  // port (CFG_TUD_CDC=1 in platformio.ini), identical init order to
  // picoST3215's setup(). It also starts Serial1 (the hardware MIDI DIN
  // out) internally — see midi_engine.cpp.
  midiEngine.setOnCC(onCCReceived);
  midiEngine.begin();
  { unsigned long t = millis();
    while (!midiEngine.isMounted() && millis() - t < 1500) delay(10); }

  // CDC serial — open early so boot messages appear even if I2C hangs later.
  Serial.begin(115200);

  // I2C bus recovery — clock SCL 9 times before Wire.begin() to free any
  // slave device left holding SDA low from a previous reset. Same sequence
  // as picoST3215's setup(); see its context.md "I2C bus sharing and
  // recovery" for why this is unconditional and runs before any I2C use.
  pinMode(HW::I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(HW::I2C_SCL_PIN, OUTPUT);
  for (int i = 0; i < 9; ++i) {
    digitalWrite(HW::I2C_SCL_PIN, HIGH); delayMicroseconds(5);
    digitalWrite(HW::I2C_SCL_PIN, LOW);  delayMicroseconds(5);
  }
  // Manufactured STOP condition: SDA low -> high while SCL is high.
  pinMode(HW::I2C_SDA_PIN, OUTPUT);
  digitalWrite(HW::I2C_SDA_PIN, LOW);
  digitalWrite(HW::I2C_SCL_PIN, HIGH); delayMicroseconds(5);
  digitalWrite(HW::I2C_SDA_PIN, HIGH); delayMicroseconds(5);

  Wire.setSDA(HW::I2C_SDA_PIN);
  Wire.setSCL(HW::I2C_SCL_PIN);
  Wire.begin();
  Wire.setClock(HW::I2C_HZ);
  Serial.println(F("[BOOT] Wire init OK"));

  appState.oledOk = ui.begin();
  if (appState.oledOk) ui.splash("Tonewheel", "Keyboard");

  // encoder.begin() does NOT touch Wire bus config (only Wire.beginTransmission
  // etc.) — safe to call after Wire.begin() above on a shared bus. See
  // encoder_unit.cpp, reused unmodified from picoST3215.
  appState.encoderOk = encoder.begin(HW::I2C_SDA_PIN, HW::I2C_SCL_PIN, HW::I2C_HZ);
  if (appState.encoderOk) encoder.setLedColor(0, 0x002000);

  persist.begin();
  app.begin();

  Serial.println(F("[BOOT] Keyboard controller ready."));
  Serial.print(F("[BOOT] oledOk="));    Serial.print(appState.oledOk ? "YES" : "NO");
  Serial.print(F("  encoderOk="));      Serial.println(appState.encoderOk ? "YES" : "NO");
  Serial.println(F("[BOOT] Debug commands: 'm'=toggle mode  's'=status dump  'z'=MIDI panic  'p'=toggle raw CC-pot CSV stream"));
}

void loop() {
  app.tick();

  if (Serial.available()) {
    char cmd = (char)Serial.read();
    if (cmd == 'm' || cmd == 'M') {
      app.toggleMode();
    } else if (cmd == 's' || cmd == 'S') {
      app.dumpStatus(Serial);
    } else if (cmd == 'z' || cmd == 'Z') {
      midiEngine.panic();
      Serial.println(F("[MIDI] panic sent"));
    } else if (cmd == 'p' || cmd == 'P') {
      app.toggleCcStream();
    }
  }
}