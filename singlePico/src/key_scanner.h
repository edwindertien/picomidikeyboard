// =============================================================================
// key_scanner.h — Tonewheel organ key scanner
// =============================================================================
//
// Hardware (one manual, 44 keys):
//   Strobe outputs  GP16–GP21  (6 lines, only GP16–GP19 used for 44 keys)
//   Read inputs     GP2–GP13   (12 lines, pulled LOW; close to strobe = HIGH)
//   UART MIDI out   GP0        (TX1 at 31250 baud → tone-generator Pico GP5)
//
// Matrix layout (strobe × read-bit):
//   Strobe 0 (GP16): read bits 0–11  → keys  0–11  (F3..E4,  MIDI 53..64)
//   Strobe 1 (GP17): read bits 0–11  → keys 12–23  (F4..E5,  MIDI 65..76)
//   Strobe 2 (GP18): read bits 0–11  → keys 24–35  (F5..E6,  MIDI 77..88)
//   Strobe 3 (GP19): read bits 0– 7  → keys 36–43  (F6..C7,  MIDI 89..96)
//   Strobe 4 (GP20): unused (reserved for second manual)
//   Strobe 5 (GP21): unused (reserved for second manual)
//
// Key order within each strobe: F G A B C D E F G A B C (12 per bank)
//   This gives a chromatic run; the MIDI_NOTE_TABLE maps physical position
//   to the correct MIDI note number.
//
// Adjust MIDI_BASE_NOTE to transpose the whole keyboard.
//
// Debug output:
//   All serial output is on USB CDC (Serial).
//   Scan timing, raw matrix state, note-on/off events, and MIDI bytes are
//   all printed; adjust DEBUG_LEVEL to reduce verbosity.
//
// Build environment: Arduino-Pico (earlephilhower), PlatformIO
// =============================================================================

#pragma once
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// Raw USB MIDI device — must NOT be static; TinyUSB registers this object
// internally and needs a single shared instance across all translation units.
Adafruit_USBD_MIDI usb_midi_iface;

// ---------------------------------------------------------------------------
// Configuration — adjust these without touching anything else
// ---------------------------------------------------------------------------

// MIDI note number for the lowest key (F below middle C = F3 = 53).
// Change to 65 for F4 if you want the keyboard one octave higher.
#define MIDI_BASE_NOTE       41

// MIDI channel (1-based; wire protocol sends channel-1 = 0)
#define MIDI_CHANNEL         1

// Debounce: a key must read the same state for this many consecutive scan
// cycles before a note-on or note-off is generated.  At ~1 ms per full scan
// this is a ~4 ms debounce window.
#define DEBOUNCE_CYCLES      50   // ~5 ms at ~10 000 scans/sec

// Debug verbosity:
//   0 = silent
//   1 = note-on / note-off events only
//   2 = + raw matrix state changes
//   3 = + full scan dump every DEBUG_DUMP_INTERVAL_MS ms
#define DEBUG_LEVEL          2
#define DEBUG_DUMP_INTERVAL_MS  2000

// ---------------------------------------------------------------------------
// Pin assignments — match your PCB
// ---------------------------------------------------------------------------

// Strobe output pins (driven HIGH one at a time)
static const uint8_t STROBE_PINS[] = { 16, 17, 18, 19, 20, 21 };
static const uint8_t NUM_STROBES   = 6;

// Read input pins (pulled LOW; contact to active strobe = reads HIGH)
static const uint8_t READ_PINS[]   = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
static const uint8_t NUM_READS     = 12;

// MIDI UART — earlephilhower maps Serial1 to GP0 (TX) / GP1 (RX)
#define MIDI_SERIAL     Serial1
#define MIDI_BAUD       31250

// Total keys and matrix cells
static const uint8_t TOTAL_KEYS    = 44;
static const uint8_t MATRIX_CELLS  = NUM_STROBES * NUM_READS;  // 72

// ---------------------------------------------------------------------------
// MIDI note lookup table [strobe_index][read_bit_index]
// Value of 0xFF means "unused / no key at this intersection".
//
// Keyboard: F4–C8, 44 keys chromatic. 5 of 6 strobe lines connected.
// Read lines fire high→low within each strobe (highest note = bit 0).
//
//   GP16 (strobe 0): not connected — all 0xFF
//   GP17 (strobe 1): 7 keys,  B4..F4  = MIDI 71..65  (bits 0–6, bits 7–11 unused)
//   GP18 (strobe 2): 12 keys, E5..F5  wait — next octave above F4 is F5..E6?
//
//   Full chromatic range F4–C8 = MIDI 65–108, split across strobes:
//     GP17: MIDI 71..65   F4..B4   (7 notes,  partial octave)
//     GP18: MIDI 83..72   F5..E5   no — let's be precise:
//
//   F4=65, then 12 semitones per octave:
//     GP17: 71 70 69 68 67 66 65           = B4  Bb4 A4 Ab4 G4 F#4 F4
//     GP18: 83 82 81 80 79 78 77 76 75 74 73 72  = B5..C5  (12 notes, high→low)
//     GP19: 95 94 93 92 91 90 89 88 87 86 85 84  = B6..C6  (12 notes, high→low)
//     GP20: 107 106 105 104 103 102 101 100 99 98 97 96 = B7..C7 (12 notes)
//     GP21: 108 = C8, single note on bit 0, rest unused
//
//   Total: 7 + 12 + 12 + 12 + 1 = 44 ✓
// ---------------------------------------------------------------------------

static const uint8_t MIDI_NOTE_TABLE[6][12] = {
    // Strobe 0 (GP16): not connected
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },

    // Strobe 1 (GP17): F4..B4 — 7 keys, high→low, bits 7–11 unused
    { 71, 70, 69, 68, 67, 66, 65, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },

    // Strobe 2 (GP18): C5..B5 — 12 keys, high→low
    { 83, 82, 81, 80, 79, 78, 77, 76, 75, 74, 73, 72 },

    // Strobe 3 (GP19): C6..B6 — 12 keys, high→low
    { 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84 },

    // Strobe 4 (GP20): C7..B7 — 12 keys, high→low
    { 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96 },

    // Strobe 5 (GP21): C8 on bit 11 (GP13)
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 108 },
};

// ---------------------------------------------------------------------------
// Scanner state (file-scoped, not exposed outside this header)
// ---------------------------------------------------------------------------

// Raw debounce counters: positive = key held, counting down to stable-pressed
//                        negative = key released, counting toward stable-open
// We use int8_t; DEBOUNCE_CYCLES must be ≤ 127.
static int8_t  s_debounce[NUM_STROBES][NUM_READS];

// Stable key state: true = key is pressed and confirmed
static bool    s_key_pressed[NUM_STROBES][NUM_READS];

// Track what we've sent to MIDI so we can send note-off on release
static bool    s_midi_on[NUM_STROBES][NUM_READS];

// Timing
static uint32_t s_last_scan_us   = 0;
static uint32_t s_last_dump_ms   = 0;
static uint32_t s_scan_count     = 0;
static uint32_t s_scan_max_us    = 0;
static uint32_t s_note_on_count  = 0;
static uint32_t s_note_off_count = 0;

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

static void _midi_send_byte(uint8_t b) {
    MIDI_SERIAL.write(b);
}

// Lowest note number present in MIDI_NOTE_TABLE (F4 = 65).
// Transposition shifts this to MIDI_BASE_NOTE at send time.
#define MIDI_TABLE_BASE      65

static void _midi_note_on(uint8_t note, uint8_t velocity = 127) {
    uint8_t transposed = note - MIDI_TABLE_BASE + MIDI_BASE_NOTE;
    uint8_t ch = (MIDI_CHANNEL - 1) & 0x0F;
    // UART
    _midi_send_byte(0x90 | ch);
    _midi_send_byte(transposed & 0x7F);
    _midi_send_byte(velocity & 0x7F);
    // USB MIDI — 4-byte packet: [cable<<4|CIN, status, d1, d2]
    if (TinyUSBDevice.mounted()) {
        uint8_t pkt[4] = { 0x09, (uint8_t)(0x90 | ch), transposed & 0x7F, velocity & 0x7F };
        usb_midi_iface.writePacket(pkt);
    }
}

static void _midi_note_off(uint8_t note) {
    uint8_t transposed = note - MIDI_TABLE_BASE + MIDI_BASE_NOTE;
    uint8_t ch = (MIDI_CHANNEL - 1) & 0x0F;
    // UART
    _midi_send_byte(0x80 | ch);
    _midi_send_byte(transposed & 0x7F);
    _midi_send_byte(0x00);
    // USB MIDI
    if (TinyUSBDevice.mounted()) {
        uint8_t pkt[4] = { 0x08, (uint8_t)(0x80 | ch), transposed & 0x7F, 0x00 };
        usb_midi_iface.writePacket(pkt);
    }
}

// Panic: send note-off for every possible MIDI note on our channel.
// Call on startup and on any detected fault.
static void _midi_all_notes_off() {
    Serial.println("[MIDI] Sending All Notes Off (CC 123)");
    uint8_t ch = (MIDI_CHANNEL - 1) & 0x0F;
    _midi_send_byte(0xB0 | ch);
    _midi_send_byte(123);
    _midi_send_byte(0);
    if (TinyUSBDevice.mounted()) {
        uint8_t pkt[4] = { 0x0B, (uint8_t)(0xB0 | ch), 123, 0 };
        usb_midi_iface.writePacket(pkt);
    }
}

// Human-readable note name, e.g. note 60 → "C4"
static const char* _note_name(uint8_t midi_note) {
    static char buf[5];
    static const char* names[] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    if (midi_note > 127) { return "---"; }
    int octave = (midi_note / 12) - 1;  // MIDI middle C (60) = C4
    snprintf(buf, sizeof(buf), "%s%d", names[midi_note % 12], octave);
    return buf;
}

// ---------------------------------------------------------------------------
// Dump the full matrix state to Serial (DEBUG_LEVEL >= 3)
// ---------------------------------------------------------------------------
static void _debug_matrix_dump() {
    Serial.println("--- Matrix state dump ---");
    Serial.print("       ");
    for (uint8_t r = 0; r < NUM_READS; r++) {
        Serial.printf("GP%-2d ", READ_PINS[r]);
    }
    Serial.println();

    for (uint8_t s = 0; s < NUM_STROBES; s++) {
        Serial.printf("GP%2d:  ", STROBE_PINS[s]);
        for (uint8_t r = 0; r < NUM_READS; r++) {
            uint8_t note = MIDI_NOTE_TABLE[s][r];
            if (note == 0xFF) {
                Serial.print(" --  ");
            } else if (s_key_pressed[s][r]) {
                Serial.printf("[%-3s] ", _note_name(note));
            } else {
                Serial.printf(" %-3s  ", _note_name(note));
            }
        }
        Serial.println();
    }

    Serial.printf("Scans: %lu  max_us: %lu  note_on: %lu  note_off: %lu\n",
                  s_scan_count, s_scan_max_us, s_note_on_count, s_note_off_count);
    Serial.println("-------------------------");
}

// ---------------------------------------------------------------------------
// keyscan_init() — call once in setup()
// ---------------------------------------------------------------------------
void keyscan_init() {
    Serial.println("[KeyScan] Initialising...");

    // Strobe pins: OUTPUT, start LOW
    for (uint8_t i = 0; i < NUM_STROBES; i++) {
        pinMode(STROBE_PINS[i], OUTPUT);
        digitalWrite(STROBE_PINS[i], LOW);
        Serial.printf("[KeyScan]   Strobe GP%d → OUTPUT LOW\n", STROBE_PINS[i]);
    }

    // Read pins: INPUT with internal pull-down
    // (key contact connects read pin to the active strobe = HIGH)
    for (uint8_t i = 0; i < NUM_READS; i++) {
        pinMode(READ_PINS[i], INPUT_PULLDOWN);
        Serial.printf("[KeyScan]   Read   GP%d → INPUT_PULLDOWN\n", READ_PINS[i]);
    }

    // UART1 for MIDI out on GP0
    MIDI_SERIAL.begin(MIDI_BAUD);
    Serial.printf("[KeyScan]   MIDI Serial1 GP0 at %d baud\n", MIDI_BAUD);

    // USB MIDI device
    usb_midi_iface.begin();
    TinyUSBDevice.setManufacturerDescriptor("Tonewheel");
    TinyUSBDevice.setProductDescriptor("Tonewheel Keyboard");
    Serial.println("[KeyScan]   USB MIDI started");

    // Clear state
    memset(s_debounce,    0, sizeof(s_debounce));
    memset(s_key_pressed, 0, sizeof(s_key_pressed));
    memset(s_midi_on,     0, sizeof(s_midi_on));

    // Safety: all notes off before we start
    delay(10);  // let UART settle
    _midi_all_notes_off();

    // Print lookup table for verification
    Serial.println("[KeyScan] Note table (strobe × read → MIDI note):");
    for (uint8_t s = 0; s < NUM_STROBES; s++) {
        Serial.printf("  GP%d: ", STROBE_PINS[s]);
        for (uint8_t r = 0; r < NUM_READS; r++) {
            uint8_t n = MIDI_NOTE_TABLE[s][r];
            if (n == 0xFF) Serial.print("  -- ");
            else           Serial.printf("%4s ", _note_name(n));
        }
        Serial.println();
    }

    Serial.printf("[KeyScan] Ready. MIDI ch%d, base note %d (%s), debounce %d cycles\n",
                  MIDI_CHANNEL, MIDI_BASE_NOTE, _note_name(MIDI_BASE_NOTE), DEBOUNCE_CYCLES);
}

// ---------------------------------------------------------------------------
// keyscan_poll() — call as fast as possible from loop()
//
// One call = one full scan of all strobes.
// Timing is printed when DEBUG_LEVEL >= 2.
// ---------------------------------------------------------------------------
void keyscan_poll() {
    uint32_t t_start = micros();

    for (uint8_t s = 0; s < NUM_STROBES; s++) {

        // Assert this strobe line HIGH
        digitalWrite(STROBE_PINS[s], HIGH);

        // Short settle time — adjust if reading glitches on fast MCU
        // At 125/133 MHz a few nops is enough; delayMicroseconds(1) is safe.
        delayMicroseconds(1);

        // Read all 12 input pins into a 16-bit mask for efficient checking
        uint16_t raw_mask = 0;
        for (uint8_t r = 0; r < NUM_READS; r++) {
            if (digitalRead(READ_PINS[r])) {
                raw_mask |= (1u << r);
            }
        }

        // De-assert strobe immediately
        digitalWrite(STROBE_PINS[s], LOW);

        // Process each read bit through the debounce filter
        for (uint8_t r = 0; r < NUM_READS; r++) {
            uint8_t note = MIDI_NOTE_TABLE[s][r];
            if (note == 0xFF) continue;  // skip unmapped cells

            bool raw_pressed = (raw_mask >> r) & 1;
            bool was_pressed = s_key_pressed[s][r];

            if (raw_pressed) {
                // Key is physically down: count up toward DEBOUNCE_CYCLES
                if (s_debounce[s][r] < DEBOUNCE_CYCLES) {
                    s_debounce[s][r]++;
                }
                if (s_debounce[s][r] >= DEBOUNCE_CYCLES && !was_pressed) {
                    // Transition: released → pressed (stable)
                    s_key_pressed[s][r] = true;

                    if (!s_midi_on[s][r]) {
                        _midi_note_on(note);
                        s_midi_on[s][r] = true;
                        s_note_on_count++;

                        if (DEBUG_LEVEL >= 1) {
                            Serial.printf("[NOTE ON ] strobe=GP%d read=GP%d note=%3d (%s) "
                                          "| MIDI: 0x%02X 0x%02X 0x7F\n",
                                          STROBE_PINS[s], READ_PINS[r], note, _note_name(note),
                                          (uint8_t)(0x90 | (MIDI_CHANNEL-1)), note);
                        }
                    }
                }
            } else {
                // Key is physically up: count down toward 0
                if (s_debounce[s][r] > 0) {
                    s_debounce[s][r]--;
                }
                if (s_debounce[s][r] == 0 && was_pressed) {
                    // Transition: pressed → released (stable)
                    s_key_pressed[s][r] = false;

                    if (s_midi_on[s][r]) {
                        _midi_note_off(note);
                        s_midi_on[s][r] = false;
                        s_note_off_count++;

                        if (DEBUG_LEVEL >= 1) {
                            Serial.printf("[NOTE OFF] strobe=GP%d read=GP%d note=%3d (%s) "
                                          "| MIDI: 0x%02X 0x%02X 0x00\n",
                                          STROBE_PINS[s], READ_PINS[r], note, _note_name(note),
                                          (uint8_t)(0x80 | (MIDI_CHANNEL-1)), note);
                        }
                    }
                }
            }

            // Level-2 debug: print raw state changes even before debounce settles
            if (DEBUG_LEVEL >= 2) {
                static bool s_last_raw[NUM_STROBES][NUM_READS];
                if (raw_pressed != s_last_raw[s][r]) {
                    s_last_raw[s][r] = raw_pressed;
                    Serial.printf("[RAW     ] strobe=GP%d read=GP%d note=%3d (%s) %s "
                                  "(debounce counter=%d)\n",
                                  STROBE_PINS[s], READ_PINS[r], note, _note_name(note),
                                  raw_pressed ? "DOWN" : "UP  ",
                                  (int)s_debounce[s][r]);
                }
            }
        }
    }

    // Scan timing
    uint32_t t_elapsed = micros() - t_start;
    s_scan_count++;
    if (t_elapsed > s_scan_max_us) {
        s_scan_max_us = t_elapsed;
    }

    // Drain any incoming USB MIDI packets (keeps TinyUSB happy)
    if (TinyUSBDevice.mounted()) {
        uint8_t pkt[4];
        while (usb_midi_iface.readPacket(pkt)) {}
    }

    // Level-3: full matrix dump at interval
    if (DEBUG_LEVEL >= 3) {
        uint32_t now_ms = millis();
        if (now_ms - s_last_dump_ms >= DEBUG_DUMP_INTERVAL_MS) {
            s_last_dump_ms = now_ms;
            _debug_matrix_dump();
            Serial.printf("[Timing ] scan_us=%lu  max_us=%lu  avg=%.1f us\n",
                          t_elapsed, s_scan_max_us,
                          (float)t_elapsed);
        }
    }
}

// ---------------------------------------------------------------------------
// keyscan_all_notes_off() — call if connection is lost or on demand
// ---------------------------------------------------------------------------
void keyscan_all_notes_off() {
    _midi_all_notes_off();

    // Also clear internal state so we don't re-send note-offs later
    for (uint8_t s = 0; s < NUM_STROBES; s++) {
        for (uint8_t r = 0; r < NUM_READS; r++) {
            s_midi_on[s][r]     = false;
            s_key_pressed[s][r] = false;
            s_debounce[s][r]    = 0;
        }
    }

    if (DEBUG_LEVEL >= 1) {
        Serial.println("[KeyScan] All notes off + state cleared.");
    }
}