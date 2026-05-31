#pragma once
#include "esphome/core/entity_base.h"

namespace esphome::valve {

struct ValveTraits {
  bool supports_position{false};
  bool supports_stop{false};
};

class Valve;

class ValveCall {
 public:
  explicit ValveCall(Valve *parent) : parent_(parent) {}
  ValveCall &set_command_open() { cmd_ = 1; return *this; }
  ValveCall &set_command_close() { cmd_ = 2; return *this; }
  ValveCall &set_command_stop() { cmd_ = 3; return *this; }
  ValveCall &set_command_toggle() { cmd_ = 4; return *this; }
  ValveCall &set_command(const char * /*cmd*/) { return *this; }
  ValveCall &set_position(float pos) { position_ = pos; return *this; }
  ValveCall &set_stop(bool s) { stop_ = s; return *this; }
  int get_cmd() const { return cmd_; }
  void perform();
 private:
  Valve *parent_;
  int cmd_{0};
  float position_{-1.0f};
  bool stop_{false};
};

class Valve : public EntityBase {
 public:
  float position{0.0f};

  ValveCall make_call() { return ValveCall(this); }

  virtual ValveTraits get_traits() = 0;
  virtual void control(const ValveCall & /*call*/) {}
};

inline void ValveCall::perform() { parent_->control(*this); }

}  // namespace esphome::valve
