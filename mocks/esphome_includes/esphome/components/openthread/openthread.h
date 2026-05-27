#pragma once
// InstanceLock stub — acquire() is a no-op; get_instance() returns a fixed test instance.

#include "openthread/coap.h"

namespace esphome::openthread {

class InstanceLock {
 public:
  static InstanceLock acquire() { return InstanceLock{}; }

  otInstance *get_instance() {
    static otInstance dummy{};
    return &dummy;
  }
};

}  // namespace esphome::openthread
