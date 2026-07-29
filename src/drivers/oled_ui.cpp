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
                        bool midiActive,
                        const volatile uint8_t* ccLive,
                        uint8_t ccCount) {
  // Custom top row instead of the shared header() — same divider styling,
  // but packs in the last note sent and a MIDI activity indicator alongside
  // the mode, which header()/drawMenu()'s Settings screens don't need.
  // Abbreviated mode text (matching the Settings menu's "DblMan"/"Veloc")
  // on BOTH panel sizes now, not just the compact one — the row is 128px
  // wide either way, only the panel *height* differs, so the same space
  // budget applies to fit note+indicator in.
  _display.clearDisplay();
  _display.setTextSize(1);

  _display.setCursor(0, 0);
  _display.print(mode == KeyboardMode::DoubleManual ? "DblMan" : "Veloc");

  if (lastNote >= 0) {
    char nameBuf[6];
    midiNoteName((uint8_t)lastNote, nameBuf, sizeof(nameBuf));
    _display.setCursor(48, 0);
    _display.print(nameBuf);
  }

  // MIDI activity blink, top-right corner: filled when a message went out
  // recently (see MidiEngine::recentlyActive()), outlined (but still
  // visible) when idle, so there's always something to glance at even with
  // nothing playing.
  if (midiActive) {
    _display.fillRect(121, 1, 5, 5, _OLED_COLOR_ON);
  } else {
    _display.drawRect(121, 1, 5, 5, _OLED_COLOR_ON);
  }

  _display.drawLine(0, 9, 127, 9, _OLED_COLOR_ON);

  if (kCompact) {
    // 32px panel: no room for USB/dropped-event text as well — see
    // context.md "Drawbar visualization" for the tradeoff (still available
    // via the 's' serial command).
    drawDrawbarTracks(0, 11, 128, 20, ccLive, ccCount);
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

  if (lastNote >= 0 && lastVelocity >= 0) {
    _display.setCursor(0, 22);
    _display.print("Vel: ");
    _display.print(lastVelocity);
  }

  drawDrawbarTracks(0, 40, 128, 22, ccLive, ccCount);

  _display.display();
}

void OledUi::drawDrawbarTracks(int x0, int y0, int w, int h,
                               const volatile uint8_t* values, uint8_t count) {
  if (!values || count == 0 || w <= 0 || h <= 1) return;

  int pitch = w / count;
  int barW  = (pitch > 3) ? (pitch - 3) : pitch;  // small gap between tracks
  if (barW < 1) barW = 1;

  for (uint8_t i = 0; i < count; ++i) {
    // Logical drawbar position i (0-indexed, i=0 = "drawbar 1") is
    // physically wired to mux channel DRAWBAR_MUX_CHANNEL[i], not channel i
    // directly -- see config.h and context.md "Drawbar remap".
    uint8_t muxCh = (i < HW::DRAWBAR_COUNT) ? HW::DRAWBAR_MUX_CHANNEL[i] : i;
    uint8_t value = values[muxCh];

    int tx = x0 + i * pitch;
    // Full-height outline so the whole slot is visible regardless of
    // current position (matches a real drawbar's physical travel range).
    _display.drawRect(tx, y0, barW, h, _OLED_COLOR_ON);

    // Filled from the TOP down — 0 = no fill (bar pushed all the way in,
    // silent), 127 = filled the full track height (bar fully pulled out).
    int filled = (int)((uint32_t)value * h / 127);
    if (filled > 0) {
      _display.fillRect(tx, y0, barW, filled, _OLED_COLOR_ON);
    }
  }
}