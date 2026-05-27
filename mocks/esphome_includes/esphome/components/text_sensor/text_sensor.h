#pragma once
#include <string>
#include "esphome/core/entity_base.h"

namespace esphome::text_sensor {

class TextSensor : public EntityBase {
 public:
  std::string state;

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

}  // namespace esphome::text_sensor
