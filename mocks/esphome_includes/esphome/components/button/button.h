#pragma once
#include "esphome/core/entity_base.h"

namespace esphome::button {

class Button : public EntityBase {
 public:
  virtual void press() = 0;
};

}  // namespace esphome::button
