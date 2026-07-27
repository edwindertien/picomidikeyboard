#pragma once
#include "config.h"
#include "model/keyboard_model.h"

enum class ScreenId : uint8_t { Home, Settings };

// Owned exclusively by core1 (the I2C/UI side) — core0 never reads or
// writes any of this. No synchronization needed since only one core ever
// touches it.
struct UiState {
  bool oledOk    = false;
  bool encoderOk = false;

  ScreenId screen  = ScreenId::Home;
  int  menuIndex   = 0;
  bool editing     = false;
};

// Written by core0 (App::tickCore0()/drainKeyEvents()), read by core1 for
// the Home-screen status display. Plain `volatile` fields, deliberately NOT
// mutex-protected: the worst case from a torn/stale read here is a single
// display frame briefly showing a mismatched note+velocity pair or a
// half-updated CC bar — cosmetic, not a hazard — and taking a lock on every
// single scan-loop update would cost far more than that's worth protecting
// against. See context.md "Dual-core split".
struct MonitorState {
  volatile int      lastNote         = -1;
  volatile int      lastVelocity     = -1;
  volatile uint32_t droppedKeyEvents = 0;
  volatile uint8_t  ccLive[HW::CC_CHANNELS] = {0};
};

struct AppState {
  UiState      ui;
  MonitorState monitor;
};
