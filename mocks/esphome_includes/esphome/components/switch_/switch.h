#pragma once
#include "esphome/core/entity_base.h"

namespace esphome::switch_ {

class Switch : public EntityBase {
 public:
  bool state{false};

  virtual void write_state(bool /*state*/) = 0;

  void turn_on() { write_state(true); }
  void turn_off() { write_state(false); }
  void toggle() { write_state(!state); }
};

}  // namespace esphome::switch_
