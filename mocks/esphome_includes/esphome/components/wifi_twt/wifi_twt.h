#pragma once
#include "esphome/core/helpers.h"

namespace esphome::wifi_twt {

class WiFiTWT {
 public:
  template<typename F> void add_on_start_callback(F &&cb) { this->start_.add(std::forward<F>(cb)); }
  template<typename F> void add_on_stop_callback(F &&cb) { this->stop_.add(std::forward<F>(cb)); }
  template<typename F> void add_on_wakeup_callback(F &&cb) { this->wakeup_.add(std::forward<F>(cb)); }

  void fire_start() { this->start_.call(); }
  void fire_stop() { this->stop_.call(); }
  void fire_wakeup() { this->wakeup_.call(); }

 private:
  LazyCallbackManager<void()> start_;
  LazyCallbackManager<void()> stop_;
  LazyCallbackManager<void()> wakeup_;
};

extern WiFiTWT *global_wifi_twt_component;  // NOLINT

}  // namespace esphome::wifi_twt
