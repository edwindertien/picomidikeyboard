#pragma once
#include <Arduino.h>

// =============================================================================
// HARDWARE CONFIGURATION — Tonewheel Organ Keyboard Controller
//
// One fixed custom PCB, one RP2040 Pico. No board variants (unlike
// picoST3215) so all pin constants live directly in this namespace instead
// of being selected via platformio.ini build flags.
//
// GPIO map, as specified:
//   GPIO0-11    : key matrix RECEIVE lines (12 rows), always inputs
//   GPIO12      : hardware MIDI DIN out (UART0 TX) — deliberately NOT GPIO0/1
//   GPIO13-15   : key matrix SEND mux address bits 0-2 (shared 74HC238 A/B/C)
//   GPIO16      : key matrix SEND mux bank select (which of the two 74HC238s
//                 is enabled) == address bit 3. 4 bits total -> 16 send lines.
//   GPIO17-19   : CC mux (74HC4067) address bits S0-S2
//   GPIO20-21   : I2C — OLED + M5Stack encoder, SHARED bus (setSDA/setSCL)
//   GPIO22      : CC mux (74HC4067) address bit S3
//   GPIO26(ADC0): CC mux common analog input
//   GPIO29      : unused — parked as MIDI DIN UART0's RX pin, see below
// =============================================================================
namespace HW {

  // ---- Key matrix: 12 receive (row) lines, always digital inputs ----
  static constexpr uint8_t MATRIX_ROW_PINS[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
  static constexpr uint8_t MATRIX_ROWS         = 12;

  // ---- Key matrix: 16 send (column) lines via two 74HC238 3-to-8 decoders.
  // A/B/C address inputs are wired in parallel to BOTH decoders (3 bits ->
  // 8 columns). GPIO16 goes to the decoders' enable pins (complementary
  // between the two chips), so it selects WHICH decoder is actually driving
  // its 8 outputs — effectively the 4th (MSB) address bit. No dedicated
  // "output disable" state exists: every valid 4-bit code drives exactly one
  // of the 16 send lines, matching "no enable, always a line selected".
  static constexpr uint8_t MUX_SEL_A            = 13;  // column address bit 0
  static constexpr uint8_t MUX_SEL_B            = 14;  // column address bit 1
  static constexpr uint8_t MUX_SEL_C            = 15;  // column address bit 2
  static constexpr uint8_t MUX_SEL_BANK         = 16;  // bank/decoder select, address bit 3
  static constexpr uint8_t MATRIX_COLS_PER_BANK = 8;
  static constexpr uint8_t MATRIX_BANKS         = 2;
  // Logical key count per bank (12 rows x 8 cols) — used for note-range maths.
  static constexpr uint8_t MATRIX_KEYS_PER_BANK = MATRIX_ROWS * MATRIX_COLS_PER_BANK; // 96

  // ---- Hardware MIDI DIN out — UART0 (Serial1), TX only ----
  // GPIO12 is a valid UART0 TX alt-function pin. UART0's RX alt-function
  // candidates are GPIO1/13/17/29 — 1, 13 and 17 are all already claimed by
  // the key/CC matrices, so RX is parked on GPIO29 (physically unconnected)
  // purely so Serial1.begin() doesn't reassign a pin this project actually
  // needs. Nothing is ever read from Serial1 — see midi_engine.cpp.
  static constexpr uint8_t  MIDI_DIN_TX_PIN       = 12;
  static constexpr uint8_t  MIDI_DIN_RX_DUMMY_PIN = 29;
  static constexpr uint32_t MIDI_DIN_BAUD         = 31250UL;

  // ---- I2C: OLED + M5Stack encoder knob, SHARED bus (per spec: GPIO20/21) ----
  static constexpr uint8_t  I2C_SDA_PIN = 20;
  static constexpr uint8_t  I2C_SCL_PIN = 21;
  static constexpr uint32_t I2C_HZ      = 400000UL;

  static constexpr uint8_t  OLED_ADDR   = 0x3C;
  static constexpr uint8_t  ENC_ADDR    = 0x40;

#if defined(DISPLAY_SSD1306)
  // Actual hardware: 128x32 SSD1306 — same panel as picoControl.
  static constexpr uint16_t OLED_W        = 128;
  static constexpr uint16_t OLED_H        = 32;
  static constexpr int      OLED_ROTATION = 0;   // SSD1306: 0=normal
  static constexpr uint16_t OLED_CANVAS_W = 128;  // logical (post-rotation) canvas
  static constexpr uint16_t OLED_CANVAS_H = 32;   // — OledUi uses this to go compact
#else
  // SH1107 (M5Stack 1.3" OLED), same as picoST3215's Pico Grove build.
  static constexpr uint16_t OLED_W        = 64;
  static constexpr uint16_t OLED_H        = 128;
  static constexpr int      OLED_ROTATION = 1;   // SH1107: 1=portrait
  static constexpr uint16_t OLED_CANVAS_W = 128;  // 64x128 physical -> 128x64 after rotation
  static constexpr uint16_t OLED_CANVAS_H = 64;
#endif

  // ---- Analog CC bank: 74HC4067 16-channel mux -> single ADC pin ----
  static constexpr uint8_t CC_MUX_SEL_S0  = 17;
  static constexpr uint8_t CC_MUX_SEL_S1  = 18;
  static constexpr uint8_t CC_MUX_SEL_S2  = 19;
  static constexpr uint8_t CC_MUX_SEL_S3  = 22;
  static constexpr uint8_t CC_MUX_ADC_PIN = 26;   // ADC0
  static constexpr uint8_t CC_CHANNELS    = 16;

  // Of the 16 CC pot channels, the first N are wired as drawbars (mux
  // inputs 0..N-1) — used by the Home screen's drawbar visualization
  // (OledUi::drawDrawbarTracks()). Update this if the physical wiring
  // changes; see context.md "Drawbar visualization".
  static constexpr uint8_t DRAWBAR_COUNT = 9;

  // Logical drawbar position i (0-indexed, i=0 is "drawbar 1"/16') is
  // physically wired to mux channel DRAWBAR_MUX_CHANNEL[i] -- NOT channel i
  // directly. Confirmed against the real wiring (2026-07): drawbar 1->mux3,
  // 2->mux0, 3->mux4, 4->mux8, 5->mux1, 6->mux7, 7->mux5, 8->mux2, 9->mux6.
  // Used both for display order (OledUi::drawDrawbarTracks()) and for the
  // default CC assignment (shared_config.cpp's kDefaultCc, which is indexed
  // by mux channel and must agree with this table) -- see context.md
  // "Drawbar remap" for the derivation.
  static constexpr uint8_t DRAWBAR_MUX_CHANNEL[DRAWBAR_COUNT] = {
    3, 0, 4, 8, 1, 7, 5, 2, 6
  };
}

// =============================================================================
// UI / timing constants
// =============================================================================
namespace UI {
  static constexpr uint16_t UI_REFRESH_MS = 50;

  // How long the Home screen's MIDI activity indicator stays "on" after a
  // message is sent. Deliberately longer than UI_REFRESH_MS so a single,
  // isolated MIDI message is very likely to show as "on" for at least one
  // render frame, even in the worst-case timing alignment between when the
  // message was sent and when render() next samples it.
  static constexpr uint16_t MIDI_BLINK_WINDOW_MS = 80;
}