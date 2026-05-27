#pragma once
#include "esphome/core/entity_base.h"

namespace esphome::binary_sensor {

class BinarySensor : public EntityBase {
 public:
  bool state{false};
};

}  // namespace esphome::binary_sensor
