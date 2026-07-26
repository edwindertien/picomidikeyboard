#pragma once
#include "config.h"
#include "model/keyboard_model.h"
#include "drivers/persist.h"

enum class ScreenId : uint8_t { Home, Settings };

struct AppState {
  bool oledOk    = false;
  bool encoderOk = false;

  PersistentConfig cfg;      // live, editable copy of the persisted config
  bool cfgDirty = false;     // set on edit; App::tick() lazy-writes to flash

  ScreenId screen  = ScreenId::Home;
  int  menuIndex   = 0;
  bool editing     = false;

  // Live monitoring, shown on the Home screen — not persisted.
  int      lastNote      = -1;
  int      lastVelocity  = -1;
  uint32_t droppedKeyEvents = 0;
  uint8_t  ccLive[HW::CC_CHANNELS] = {0};

  unsigned long lastUiMs = 0;
};
