#pragma once
#include <cmath>
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"

namespace esphome::sensor {

class Sensor : public EntityBase {
 public:
  float state{NAN};

  void set_unit_of_measurement(const char *uom) { uom_ = uom; }

  int8_t get_accuracy_decimals() const { return accuracy_decimals_; }
  void set_accuracy_decimals(int8_t ad) { accuracy_decimals_ = ad; }

  StringRef get_unit_of_measurement_ref() const override { return StringRef(uom_); }

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
  const char *uom_{""};
  const char *device_class_{nullptr};
  int8_t accuracy_decimals_{1};
};

}  // namespace esphome::sensor
