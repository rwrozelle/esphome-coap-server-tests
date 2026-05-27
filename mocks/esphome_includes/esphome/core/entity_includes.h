#pragma once
// Include all entity headers that are enabled via USE_* defines.
// Mirrors esphome/core/entity_includes.h but points at our mock stubs.

#include "esphome/core/defines.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_LOCK
#include "esphome/components/lock/lock.h"
#endif
#ifdef USE_VALVE
#include "esphome/components/valve/valve.h"
#endif
