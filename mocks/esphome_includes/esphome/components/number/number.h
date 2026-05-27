#pragma once
#include <cmath>
#include "esphome/core/entity_base.h"

namespace esphome::number {

class Number;

class NumberCall {
 public:
  explicit NumberCall(Number *parent) : parent_(parent) {}
  NumberCall &set_value(float value) { value_ = value; return *this; }
  void perform();
 private:
  Number *parent_;
  float value_{NAN};
};

class Number : public EntityBase {
 public:
  float state{NAN};

  NumberCall make_call() { return NumberCall(this); }
  virtual void control(float /*value*/) {}

  const char *get_device_class_to(std::span<char, MAX_DEVICE_CLASS_LENGTH> buffer) const override {
    if (device_class_) {
      strncpy(buffer.data(), device_class_, MAX_DEVICE_CLASS_LENGTH - 1);
      buffer[MAX_DEVICE_CLASS_LENGTH - 1] = '\0';
      return buffer.data();
    }
    buffer[0] = '\0';
    return buffer.data();
  }
  void set_device_class(const char *dc) { device_class_ = dc; }

 protected:
  const char *device_class_{nullptr};
};

inline void NumberCall::perform() {
  if (!std::isnan(value_)) parent_->control(value_);
}

}  // namespace esphome::number
