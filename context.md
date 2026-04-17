# Context — Tonewheel Keyboard Scanner

Handoff document for the key scanner Pico. Read alongside README.md.
Contains hard-won lessons and known pitfalls.

---

## Project state

Single manual (44 keys, F4–C8) working:
- Key matrix scan with debounce ✓
- UART MIDI out on GP0 ✓
- USB MIDI device ✓ (appears as "Tonewheel Keyboard")
- Dual UART + USB simultaneous output ✓

Not yet built:
- Second manual via two 74HC238 decoders
- Analogue mux (74HC4067) for drawbars/volume/leslie

---

## Critical lessons learned

### 1. `Adafruit_USBD_MIDI` must NOT be declared `static` in a header

**Symptom:** compiles cleanly, UART MIDI works, but the device never
appears as a USB MIDI device on the host.

**Cause:** `static` in a header gives each translation unit its own
private copy of the object. TinyUSB registers the interface in the
constructor of the instance it sees at link time, but `writePacket()`
calls go to a different copy that was never registered. The USB
descriptor never advertises a MIDI interface.

**Fix:** declare without `static`:
```cpp
Adafruit_USBD_MIDI usb_midi_iface;  // correct
static Adafruit_USBD_MIDI usb_midi_iface;  // WRONG — silent failure
```

### 2. `CFG_TUD_MIDI=1` is required in build_flags

Without it TinyUSB compiles but the MIDI device class is never enabled.
The device enumerates as USB but presents no MIDI interface.

```ini
build_flags =
    -O2
    -DUSE_TINYUSB
    -DCFG_TUD_MIDI=1
```

### 3. Do not use `MIDI_CREATE_INSTANCE` for USB MIDI on this stack

The fortyseveneffects MIDI library wraps `Adafruit_USBD_MIDI` in a
`SerialMIDI<>` adapter, producing type
`midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>>`.
This causes complex extern declaration problems across header/cpp
boundaries and is unnecessary. Use `Adafruit_USBD_MIDI` directly with
`writePacket()` / `readPacket()` instead.

### 4. `keyscan_init()` must be called before `Serial.begin()`

TinyUSB needs to configure the USB device descriptor before the USB
stack enumerates. Calling `Serial.begin()` first races the stack.

```cpp
void setup() {
    keyscan_init();      // USB MIDI descriptor set here
    Serial.begin(115200); // CDC serial after
    delay(100);
}
```

### 5. No `while (!Serial)` — kills standalone operation

Waiting for CDC connection blocks the scan loop. The keyboard won't
work without a USB host attached. Just use `delay(100)` after
`Serial.begin()`.

### 6. USB device name

Set via `TinyUSBDevice` descriptors, not via `platformio.ini`:
```cpp
TinyUSBDevice.setManufacturerDescriptor("Tonewheel");
TinyUSBDevice.setProductDescriptor("Tonewheel Keyboard");
```
Setting `USB_MANUFACTURER` / `USB_PRODUCT` in `build_flags` causes
redefinition warnings because earlephilhower already defines them.

### 7. GP29 is ADC VREF — do not use as ADC input

Use GP28 (ADC2) for the analogue mux signal input.

### 8. 74HC238 not 74HC138 for strobe decoders

Read lines use INPUT_PULLDOWN; a key press connects the read pin to
the active strobe = HIGH. The decoder outputs must therefore be
active-HIGH → use 74HC238. The 74HC138 is active-LOW and would
require inverting all the logic.

### 9. Raw USB MIDI packet format

4-byte packets: `[CIN, status, data1, data2]`
- Note on:  `{ 0x09, 0x90|ch, note, velocity }`
- Note off: `{ 0x08, 0x80|ch, note, 0x00 }`
- CC:       `{ 0x0B, 0xB0|ch, cc,   value   }`

Guard all writes with `if (TinyUSBDevice.mounted())`.

---

## Pin allocation (current + planned)

| GPIO | Function | State |
|------|----------|-------|
| GP0 | UART TX (MIDI out, Serial1) | active |
| GP1 | free | — |
| GP2–GP13 | 12 read inputs (INPUT_PULLDOWN) | active |
| GP14 | 238-A enable (bottom manual) | planned |
| GP15 | 238-B enable (top manual) | planned |
| GP16 | 238 address A0 (shared) | planned |
| GP17 | 238 address A1 / current strobe 1 | active/planned |
| GP18 | 238 address A2 / current strobe 2 | active/planned |
| GP19–GP20 | current strobes 3–4 → free when 238s fitted | active/planned |
| GP21 | current strobe 5 / mux S0 when 238s fitted | active/planned |
| GP22 | mux S1 | planned |
| GP23 | SMPS control — do not use | — |
| GP24 | VBUS sense — do not use | — |
| GP25 | LED | — |
| GP26 | mux S2 | planned |
| GP27 | mux S3 | planned |
| GP28 | ADC input from 74HC4067 (ADC2) | planned |
| GP29 | ADC VREF — do not use as ADC input | — |

---

## Next steps

1. Build the decoder + mux board
2. Update `STROBE_PINS[]` to use address+enable pattern for 74HC238s
3. Add second manual note table (strobes 0–4 of 238-B)
4. Set `HAS_ADC_MUX 1` and verify drawbar CC output
5. Wire note overlap between manuals (same read pins, different strobes)