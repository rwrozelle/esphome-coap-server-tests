#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "esphome/core/helpers.h"
#include "esphome/core/device.h"

// Forward declarations so that Application can hold pointers without circular includes
namespace esphome::sensor { class Sensor; }
namespace esphome::switch_ { class Switch; }
namespace esphome::binary_sensor { class BinarySensor; }
namespace esphome::text_sensor { class TextSensor; }
namespace esphome::number { class Number; }
namespace esphome::lock { class Lock; }
namespace esphome::valve { class Valve; }
namespace esphome::button { class Button; }

namespace esphome {

class Application {
 public:
  static constexpr size_t BUILD_TIME_STR_SIZE = 32;

  StringRef get_name() const { return StringRef(name_); }
  StringRef get_friendly_name() const { return StringRef(friendly_name_); }

  void set_name(const char *n) { name_ = n; }
  void set_friendly_name(const char *n) { friendly_name_ = n; }

  void get_build_time_string(char *buf) const { strncpy(buf, build_time_, BUILD_TIME_STR_SIZE - 1); }
  void set_build_time(const char *t) { strncpy(build_time_, t, BUILD_TIME_STR_SIZE - 1); }

  // Entity list accessors — tests populate these vectors
  std::vector<sensor::Sensor *> &get_sensors() { return sensors_; }
  std::vector<switch_::Switch *> &get_switches() { return switches_; }
  std::vector<binary_sensor::BinarySensor *> &get_binary_sensors() { return binary_sensors_; }
  std::vector<text_sensor::TextSensor *> &get_text_sensors() { return text_sensors_; }
  std::vector<number::Number *> &get_numbers() { return numbers_; }
  std::vector<lock::Lock *> &get_locks() { return locks_; }
  std::vector<valve::Valve *> &get_valves() { return valves_; }
  std::vector<button::Button *> &get_buttons() { return buttons_; }

#ifdef USE_DEVICES
  std::vector<Device *> &get_devices() { return devices_; }
#endif

  void reset_entities() {
    sensors_.clear(); switches_.clear(); binary_sensors_.clear();
    text_sensors_.clear(); numbers_.clear(); locks_.clear();
    valves_.clear(); buttons_.clear();
#ifdef USE_DEVICES
    devices_.clear();
#endif
  }

 private:
  const char *name_{"test_device"};
  const char *friendly_name_{"Test Device"};
  char build_time_[BUILD_TIME_STR_SIZE]{"2024-01-01T00:00:00"};

  std::vector<sensor::Sensor *> sensors_;
  std::vector<switch_::Switch *> switches_;
  std::vector<binary_sensor::BinarySensor *> binary_sensors_;
  std::vector<text_sensor::TextSensor *> text_sensors_;
  std::vector<number::Number *> numbers_;
  std::vector<lock::Lock *> locks_;
  std::vector<valve::Valve *> valves_;
  std::vector<button::Button *> buttons_;
#ifdef USE_DEVICES
  std::vector<Device *> devices_;
#endif
};

extern Application App;  // defined in mock_app.cpp

}  // namespace esphome
