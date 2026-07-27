#include "shared_config.h"

SharedConfig sharedConfig;

// ---------------------------------------------------------------------------
// Default CC assignments: General MIDI's undefined/free controller numbers
// (3, 9, 14, 15, 20-31) — 16 of them, one per pot. Placeholders — reassign
// by editing this array (a runtime CC-assignment menu is future work, see
// context.md "Known gaps / next steps").
//
// Moved here from app.cpp's applyDefaultConfig() as part of the dual-core
// split — config defaults belong with config management, not the App/UI
// layer. See context.md "Dual-core split".
// ---------------------------------------------------------------------------
void applyDefaultConfig(PersistentConfig& cfg) {
  cfg = PersistentConfig{}; // struct defaults (see keyboard_model.h)

  static const uint8_t kDefaultCc[HW::CC_CHANNELS] = {
    3, 9, 14, 15, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
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
