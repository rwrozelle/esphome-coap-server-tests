#pragma once
// Controller base stub — mirrors esphome/core/controller.h.
// Includes entity headers so coap_server_cbor.cpp can use the full types.

#include "esphome/core/entity_includes.h"

namespace esphome {

class Controller {
 public:
  virtual ~Controller() = default;

#ifdef USE_SENSOR
  virtual void on_sensor_update(sensor::Sensor * /*entity*/) {}
#endif
#ifdef USE_SWITCH
  virtual void on_switch_update(switch_::Switch * /*entity*/) {}
#endif
#ifdef USE_BINARY_SENSOR
  virtual void on_binary_sensor_update(binary_sensor::BinarySensor * /*entity*/) {}
#endif
#ifdef USE_TEXT_SENSOR
  virtual void on_text_sensor_update(text_sensor::TextSensor * /*entity*/) {}
#endif
#ifdef USE_NUMBER
  virtual void on_number_update(number::Number * /*entity*/) {}
#endif
#ifdef USE_LOCK
  virtual void on_lock_update(lock::Lock * /*entity*/) {}
#endif
#ifdef USE_VALVE
  virtual void on_valve_update(valve::Valve * /*entity*/) {}
#endif
};

}  // namespace esphome
