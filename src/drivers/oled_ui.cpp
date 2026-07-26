#include "oled_ui.h"
#include <stdio.h>

static const char* NOTE_NAMES[12] = {
  "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

// The 128x32 SSD1306 leaves only ~22px below the header for content — not
// enough for the original 4-row menu / footer / CC-bar layout, which was
// sized for a 128x64 canvas (SH1107 portrait). kCompact switches both
// drawMenu() and drawStatus() to a shorter layout that actually fits.
// HW::OLED_CANVAS_H is a compile-time constant, so the unused branch below
// is dropped by the optimizer either way.
static constexpr bool kCompact = (HW::OLED_CANVAS_H < 64);

void midiNoteName(uint8_t note, char* buf, size_t bufLen) {
  int octave = (int)(note / 12) - 1; // MIDI note 60 = C4 (middle C)
  snprintf(buf, bufLen, "%s%d", NOTE_NAMES[note % 12], octave);
}

OledUi::OledUi(uint16_t w, uint16_t h, TwoWire* wire, uint8_t addr)
    : _display(_OLED_CTOR(w, h, wire)), _addr(addr) {}

bool OledUi::begin() {
#if defined(DISPLAY_SSD1306)
  if (!_display.begin(SSD1306_SWITCHCAPVCC, _addr)) return false;
#else
  if (!_display.begin(_addr, true)) return false;
#endif
  _display.setRotation(_rotation);
  _display.clearDisplay();
  _display.setTextColor(_OLED_COLOR_ON);
  _display.setTextWrap(false);
  _display.cp437(true);
  _display.display();
  return true;
}

void OledUi::clear() {
  _display.clearDisplay();
  _display.display();
}

void OledUi::splash(const String& l1, const String& l2) {
  _display.clearDisplay();
  _display.setTextSize(1);
  _display.setCursor(0, 0);
  _display.println(l1);
  if (l2.length()) _display.println(l2);
  _display.display();
}

void OledUi::header(const char* title) {
  _display.clearDisplay();
  _display.setTextSize(1);
  _display.setCursor(0, 0);
  _display.println(title);
  _display.drawLine(0, 9, 127, 9, _OLED_COLOR_ON);
}

void OledUi::footer(const char* text) {
  if (!text) return;
  _display.drawLine(0, 55, 127, 55, _OLED_COLOR_ON);
  _display.setCursor(0, 57);
  _display.print(text);
}

void OledUi::drawSelectionMarker(int rowY, bool editing) {
  _display.setCursor(0, rowY);
  _display.print(editing ? "*" : ">");
}

void OledUi::drawMenu(const char* title,
                      const char* const* items,
                      int itemCount,
                      int selected,
                      bool editing,
                      const char* footerText) {
  header(title);

  const int visibleRows = kCompact ? 2 : 4;

  int start = 0;
  if (selected >= visibleRows) start = selected - (visibleRows - 1);

  for (int row = 0; row < visibleRows; ++row) {
    int idx = start + row;
    if (idx >= itemCount) break;
    int y = 11 + row * 10;
    if (idx == selected) drawSelectionMarker(y, editing);
    _display.setCursor(10, y);
    _display.print(items[idx]);
  }

  // No room for a footer hint line on the compact panel — the '>' / '*'
  // selection marker (drawSelectionMarker) is the only edit-state signal
  // there. Full-size panel keeps the footer as before.
  if (!kCompact) footer(footerText);
  _display.display();
}

void OledUi::drawStatus(KeyboardMode mode,
                        bool usbMounted,
                        int lastNote,
                        int lastVelocity,
                        uint32_t droppedKeyEvents,
                        const uint8_t* ccLive,
                        uint8_t ccCount) {
  header(mode == KeyboardMode::DoubleManual ? "Double Manual" : "Velocity");

  char nameBuf[6];
  if (kCompact) {
    // 32px panel: two content rows, no room for a CC bar graph.
    _display.setCursor(0, 11);
    _display.print("USB:");
    _display.print(usbMounted ? "up" : "---");
    if (droppedKeyEvents > 0) {
      _display.print(" Drop:");
      _display.print(droppedKeyEvents);
    }

    _display.setCursor(0, 21);
    if (lastNote >= 0) {
      midiNoteName((uint8_t)lastNote, nameBuf, sizeof(nameBuf));
      _display.print("Last:");
      _display.print(nameBuf);
      if (lastVelocity >= 0) {
        _display.print(" v");
        _display.print(lastVelocity);
      }
    } else {
      _display.print("Last: --");
    }

    _display.display();
    return;
  }

  _display.setCursor(0, 12);
  _display.print("USB: ");
  _display.print(usbMounted ? "up" : "---");

  _display.setCursor(64, 12);
  if (droppedKeyEvents > 0) {
    _display.print("Drop:");
    _display.print(droppedKeyEvents);
  }

  _display.setCursor(0, 22);
  if (lastNote >= 0) {
    midiNoteName((uint8_t)lastNote, nameBuf, sizeof(nameBuf));
    _display.print("Last: ");
    _display.print(nameBuf);
    if (lastVelocity >= 0) {
      _display.print(" v");
      _display.print(lastVelocity);
    }
  } else {
    _display.print("Last: --");
  }

  // Compact 16-channel CC bar graph, bottom-left, 8px per bar x 16 = 128px.
  // Only fits panels with a 64px-tall canvas or taller.
  if (ccLive && ccCount > 0) {
    const int barsY = 54;      // baseline
    const int barsH = 14;      // max bar height
    const int barW  = 7;
    for (uint8_t i = 0; i < ccCount && i < 16; ++i) {
      int h = (int)((uint32_t)ccLive[i] * barsH / 127);
      int x = i * (barW + 1);
      _display.drawRect(x, barsY - barsH, barW, barsH, _OLED_COLOR_ON);
      if (h > 0) {
        _display.fillRect(x, barsY - h, barW, h, _OLED_COLOR_ON);
      }
    }
  }

  _display.display();
}