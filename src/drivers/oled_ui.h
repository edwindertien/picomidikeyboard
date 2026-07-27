#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "../config.h"          // must come before display includes for DISPLAY_xxx
#include "../model/keyboard_model.h"

// Compile-time display backend selection, same switch as picoST3215.
#if defined(DISPLAY_SSD1306)
  #include <Adafruit_SSD1306.h>
  #define _OLED_CLASS      Adafruit_SSD1306
  #define _OLED_COLOR_ON   SSD1306_WHITE
  #define _OLED_CTOR(w,h,wire) Adafruit_SSD1306((w), (h), (wire), -1)
#else
  // Default: SH1107 (M5Stack 1.3" OLED)
  #include <Adafruit_SH110X.h>
  #define _OLED_CLASS      Adafruit_SH1107
  #define _OLED_COLOR_ON   SH110X_WHITE
  #define _OLED_CTOR(w,h,wire) Adafruit_SH1107((w), (h), (wire))
#endif

struct PersistDiag;

class OledUi {
public:
  OledUi(uint16_t w, uint16_t h, TwoWire* wire, uint8_t addr);

  bool begin();
  void clear();
  void splash(const String& l1, const String& l2 = "");

  // Generic scrolling menu — reused verbatim in spirit from picoST3215's
  // OledUi::drawMenu(): a plain list of pre-formatted strings, one selection
  // cursor, optional "editing" (*) vs "browsing" (>) marker.
  void drawMenu(const char* title,
                const char* const* items,
                int itemCount,
                int selected,
                bool editing = false,
                const char* footer = nullptr);

  // Home screen: live status while playing.
  void drawStatus(KeyboardMode mode,
                   bool usbMounted,
                   int lastNote,
                   int lastVelocity,
                   uint32_t droppedKeyEvents,
                   const volatile uint8_t* ccLive,
                   uint8_t ccCount);

private:
  _OLED_CLASS _display;
  uint8_t _addr;
  int _rotation = HW::OLED_ROTATION;

  void header(const char* title);
  void footer(const char* text);
  void drawSelectionMarker(int rowY, bool editing);
};

// Formats a MIDI note number as e.g. "C#4" (60 = C4 / middle C).
void midiNoteName(uint8_t note, char* buf, size_t bufLen);
