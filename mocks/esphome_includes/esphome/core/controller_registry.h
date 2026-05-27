#pragma once
// ControllerRegistry stub — register_controller is a no-op in tests.

namespace esphome {

class Controller;

class ControllerRegistry {
 public:
  static void register_controller(Controller * /*controller*/) {}
};

}  // namespace esphome
