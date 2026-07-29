#include "shared_config.h"

SharedConfig sharedConfig;

// ---------------------------------------------------------------------------
// Default CC assignments. The first HW::DRAWBAR_COUNT (9) channels are
// wired as drawbars, but NOT in mux-channel order — see config.h's
// DRAWBAR_MUX_CHANNEL for the physical wiring. This array is indexed by mux
// channel (matching CcMux::tick()'s scan order), and its values are derived
// FROM DRAWBAR_MUX_CHANNEL so a mux channel always sends the CC number for
// whichever logical drawbar position it actually carries (12 = drawbar 1,
// ..., 20 = drawbar 9) — not a GM-defined meaning (CC12/13 are technically
// GM "Effect Control 1/2", but that's irrelevant here — this is a custom
// controller, not talking to a GM-compliant synth). The remaining 7
// channels (volume/drive/chorus/etc., not yet wired) use General MIDI's
// undefined/free block (21-27), shifted past the drawbar range so nothing
// collides. Reassign by editing this array (a runtime CC-assignment menu is
// future work, see context.md "Known gaps / next steps").
// ---------------------------------------------------------------------------
void applyDefaultConfig(PersistentConfig& cfg) {
  cfg = PersistentConfig{}; // struct defaults (see keyboard_model.h)

  static const uint8_t kDefaultCc[HW::CC_CHANNELS] = {
    13, 16, 19, 12, 14, 18, 20, 17, 15,   // drawbars, indexed by MUX CHANNEL
                                           // (not logical position) — see above
    21, 22, 23, 24, 25, 26, 27            // volume/drive/chorus/etc. (unwired)
  };
  for (uint8_t i = 0; i < HW::CC_CHANNELS; ++i) {
    cfg.ccMap[i].cc      = kDefaultCc[i];
    cfg.ccMap[i].channel = 1;
  }

  // Two organ manuals stacked by default: lower manual starting at C2,
  // upper manual starting an octave+ above it. Adjust to taste in Settings.
  cfg.manualA.startNote = 36;  // C2
  cfg.manualA.channel   = 1;
  cfg.manualB.startNote = 12;  // C0 -- lowest key (F) reads F1
  cfg.manualB.channel   = 2;

  cfg.velocity.startNote = 36;
  cfg.velocity.channel   = 1;
}

void SharedConfig::begin() {
  mutex_init(&_mutex);
  if (!persist.load(_cfg)) {
    applyDefaultConfig(_cfg);
  }
}

bool SharedConfig::takeDirty() {
  if (!_dirty) return false;
  _dirty = false;
  return true;
}