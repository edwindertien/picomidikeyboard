#pragma once
#include <Arduino.h>
#include "../config.h"
#include "../model/keyboard_model.h"

// Same flat-struct-on-LittleFS pattern as picoST3215's PersistentConfig:
// magic + version checked on load; any mismatch means "not valid, use
// defaults" rather than trying to migrate in place.
static constexpr uint32_t KBD_PERSIST_MAGIC   = 0x4B424431; // "KBD1"
static constexpr uint8_t  KBD_PERSIST_VERSION = 1;
static constexpr char     KBD_PERSIST_PATH[]  = "/kbdconfig.bin";

struct PersistentConfig {
  uint32_t magic   = KBD_PERSIST_MAGIC;
  uint8_t  version = KBD_PERSIST_VERSION;

  KeyboardMode   mode = KeyboardMode::DoubleManual;
  ManualConfig   manualA;
  ManualConfig   manualB;
  VelocityConfig velocity;
  CcSlot         ccMap[HW::CC_CHANNELS];
};

struct PersistDiag {
  bool     mounted     = false;
  uint32_t totalBytes  = 0;
  uint32_t usedBytes   = 0;
  bool     fileExists  = false;
  uint32_t fileSize    = 0;
  uint32_t magic       = 0;
  uint8_t  version     = 0;
};

class Persist {
public:
  bool begin();
  bool load(PersistentConfig& dst);
  bool save(const PersistentConfig& cfg);
  PersistDiag diagnose();
  bool isMounted() const { return _mounted; }

private:
  bool _mounted = false;
};

extern Persist persist;
