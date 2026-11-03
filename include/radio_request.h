#pragma once
#include "radio_settings.h"

// Apply a request-local profile and restore the configured receiver on every
// exit path. This class never writes persistent configuration.
class RadioRequestProfile {
 public:
  using Init = bool (*)(float);
  RadioRequestProfile(RadioSettings& current, bool temporary, Init init)
    : current_(current), saved_(current), temporary_(temporary), init_(init) {}
  RadioRequestProfile(const RadioRequestProfile&) = delete;
  RadioRequestProfile& operator=(const RadioRequestProfile&) = delete;
  bool apply(const RadioSettings& effective) {
    if (!temporary_) return true;
    current_ = effective; applied_ = true;
    return init_(effective.frequency);
  }
  void restore() {
    if (!applied_) return;
    current_ = saved_; applied_ = false;
    restored = init_(saved_.frequency);
  }
  ~RadioRequestProfile() { restore(); }
  bool restored = true;
 private:
  RadioSettings& current_;
  RadioSettings saved_;
  bool temporary_, applied_ = false;
  Init init_;
};
