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
#include "drivers/shared_config.h"
#include "drivers/oled_ui.h"
#include "drivers/encoder_unit.h"
#include "app/app.h"

// Single shared I2C bus for OLED + M5Stack encoder (GPIO20/21, per spec).
// Both peripherals, and everything that touches them (Settings menu input,
// rendering), live entirely on core1 as of the dual-core split — see
// context.md "Dual-core split" for the full reasoning. Serial/CDC stays
// exclusively core0's, including at boot (see the g_core1Ready handshake
// below) — never called from setup1()/loop1().
static UnitEncoder encoder(Wire, HW::ENC_ADDR);

AppState appState;
OledUi   ui(HW::OLED_W, HW::OLED_H, &Wire, HW::OLED_ADDR);
App      app(appState, encoder, ui);

// Set once, immediately after sharedConfig.begin() in setup() (core0).
// setup1() (core1) waits on this before touching SharedConfig at all — see
// setup1()'s comment for why this is needed, not just nice-to-have.
static volatile bool g_sharedConfigReady = false;

// Set once, at the end of setup1(), after I2C/OLED/encoder init completes.
// core0's setup() waits on this (with a timeout) before printing the boot
// summary, so the two cores' one-time init messages can't interleave —
// after this point, all Serial usage is core0's steady-state loop() only.
static volatile bool g_core1Ready = false;

static void onCCReceived(uint8_t channel, uint8_t cc, uint8_t value) {
  app.onMidiCC(channel, cc, value);
}

// =============================================================================
// core0 — USB MIDI/CDC, key matrix, CC mux, persistence, serial commands
// =============================================================================

void setup() {
  // Unblock core1 as early as possible: SharedConfig's mutex must exist
  // before core1 (already running setup1() concurrently — see its comment)
  // can safely touch it via tickCore1(). This has to come before the USB
  // MIDI wait below, which can take up to 1.5s — core1 shouldn't be stuck
  // waiting that long just to lock a mutex.
  persist.begin();
  sharedConfig.begin();
  g_sharedConfigReady = true;

  // USB MIDI device must come up first — this also brings up the CDC debug
  // port (CFG_TUD_CDC=1 in platformio.ini), identical init order to
  // picoST3215's setup(). It also starts Serial1 (the hardware MIDI DIN
  // out) internally — see midi_engine.cpp.
  midiEngine.setOnCC(onCCReceived);
  midiEngine.begin();
  { unsigned long t = millis();
    while (!midiEngine.isMounted() && millis() - t < 1500) delay(10); }

  Serial.begin(115200);

  app.beginCore0();

  // core1 is already running setup1() concurrently (see its comment) — wait
  // for it to finish before printing the combined boot summary, so no
  // Serial output can interleave between the two cores' one-time setup
  // messages.
  Serial.println(F("[BOOT] core0 ready, waiting for core1 (I2C/OLED/encoder)..."));
  { unsigned long t = millis();
    while (!g_core1Ready && millis() - t < 2000) delay(1);
    if (!g_core1Ready) Serial.println(F("[BOOT] WARNING: core1 init did not finish within 2s")); }

  Serial.println(F("[BOOT] Keyboard controller ready."));
  Serial.print(F("[BOOT] oledOk="));    Serial.print(appState.ui.oledOk ? "YES" : "NO");
  Serial.print(F("  encoderOk="));      Serial.println(appState.ui.encoderOk ? "YES" : "NO");
  Serial.println(F("[BOOT] Debug commands: 'm'=toggle mode  's'=status dump  'z'=MIDI panic  'p'=toggle raw CC-pot CSV stream  'k'=toggle raw key-matrix hex stream  'r'=reset config to compiled-in defaults"));
}

void loop() {
  app.tickCore0();

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
    } else if (cmd == 'k' || cmd == 'K') {
      app.toggleKeyStream();
    } else if (cmd == 'r' || cmd == 'R') {
      app.factoryReset();
    }
  }
}

// =============================================================================
// core1 — I2C: OLED + M5Stack encoder, Settings menu input, rendering.
// setup1()/loop1() are picked up automatically by the earlephilhower core;
// no manual multicore_launch_core1() needed.
// =============================================================================

void setup1() {
  // setup() (core0) and setup1() (core1) start CONCURRENTLY — core1 is
  // launched just BEFORE core0's setup() is even called, not after it
  // returns (confirmed against the earlephilhower core's own main.cpp and
  // docs — see context.md "Dual-core split"). Without this wait, core1
  // could reach loop1() -> tickCore1() and lock SharedConfig's mutex before
  // core0's sharedConfig.begin() has run mutex_init() on it — undefined
  // behavior. Nothing else on this core is safe to do before this either,
  // so it comes first, before even the I2C recovery below.
  while (!g_sharedConfigReady) { /* busy-wait; core0 sets this almost
                                     immediately, near the top of setup() */ }

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

  appState.ui.oledOk = ui.begin();
  if (appState.ui.oledOk) ui.splash("Tonewheel", "Keyboard");

  // encoder.begin() does NOT touch Wire bus config (only Wire.beginTransmission
  // etc.) — safe to call after Wire.begin() above on a shared bus. See
  // encoder_unit.cpp, reused unmodified from picoST3215.
  appState.ui.encoderOk = encoder.begin(HW::I2C_SDA_PIN, HW::I2C_SCL_PIN, HW::I2C_HZ);
  if (appState.ui.encoderOk) encoder.setLedColor(0, 0x002000);

  g_core1Ready = true;  // core0's setup() is waiting on this — see above
}

void loop1() {
  app.tickCore1();
}
