// =============================================================================
// main.cpp — Tonewheel organ key scanner Pico
// =============================================================================

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "key_scanner.h"

void setup() {
    keyscan_init();
    Serial.begin(115200);
    delay(100);
    Serial.println("=== Tonewheel scanner ===");
    Serial.printf("Build: %s %s\n", __DATE__, __TIME__);
    Serial.printf("CPU: %lu MHz\n", rp2040.f_cpu() / 1000000);
}

void loop() {
    keyscan_poll();
}