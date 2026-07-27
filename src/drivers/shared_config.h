#pragma once
#include <Arduino.h>
#include "pico/mutex.h"
#include "../model/keyboard_model.h"
#include "persist.h"

// ---------------------------------------------------------------------------
// SharedConfig — the single PersistentConfig instance, shared between core0
// (KeyboardEngine + CcMux, read-only every tick) and core1 (the Settings
// menu, read/write). All access goes through SharedConfigGuard, a short-held
// RAII lock around a raw pico-sdk mutex_t — critical sections here are just
// a handful of struct-field reads/writes, never LittleFS I/O. The actual
// flash write stays on core0 exclusively (App::tickCore0()), per the same
// "LittleFS must never run on core 1" rule as picoST3215.
//
// _dirty is a plain volatile bool, deliberately NOT behind the mutex: it has
// exactly one writer (core1, via markDirty()) and one reader/clearer
// (core0, via takeDirty()), so a raw flag is enough — taking the mutex just
// to flip a bool would cost more than it protects.
// ---------------------------------------------------------------------------
class SharedConfig {
public:
  void begin();  // mutex_init(), then persist.load() or applyDefaultConfig()

  void markDirty() { _dirty = true; }
  bool takeDirty();  // core0 only — test-and-clear

  // Prefer SharedConfigGuard over calling these directly.
  void lock()   { mutex_enter_blocking(&_mutex); }
  void unlock() { mutex_exit(&_mutex); }
  PersistentConfig& raw() { return _cfg; }

private:
  mutex_t _mutex;
  PersistentConfig _cfg;
  volatile bool _dirty = false;
};

// Scoped lock: { SharedConfigGuard g(sharedConfig); g->mode = ...; }
// sharedConfig.markDirty() afterward if anything was actually changed.
class SharedConfigGuard {
public:
  explicit SharedConfigGuard(SharedConfig& sc) : _sc(sc) { _sc.lock(); }
  ~SharedConfigGuard() { _sc.unlock(); }
  SharedConfigGuard(const SharedConfigGuard&) = delete;
  SharedConfigGuard& operator=(const SharedConfigGuard&) = delete;

  PersistentConfig* operator->() { return &_sc.raw(); }
  PersistentConfig& operator*()  { return _sc.raw(); }

private:
  SharedConfig& _sc;
};

// Exposed (not static) so App::factoryReset() can re-apply it under its own
// guard, in addition to SharedConfig::begin() using it as the load fallback.
void applyDefaultConfig(PersistentConfig& cfg);

extern SharedConfig sharedConfig;
