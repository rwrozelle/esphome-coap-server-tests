// Unit tests for CBOR entity encoding in coap_server_cbor.cpp.
// Ported from tests/components/coap_server with additions for lock and valve.

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Pull in the encode functions declared static in coap_server_cbor.cpp.
// We reach them through the non-static cbor_output_ dispatcher exposed via CoapServer.
// For direct function access, include the encode header if it exists, otherwise
// instantiate CoapServer and route through cbor_output_.
//
// coap_server_cbor.cpp declares encode_entity() as static — we cannot call it
// directly from outside. Instead we drive the test through cbor_output_() which
// is a protected member.  We expose it via a thin test subclass.

#include "esphome/components/coap_server/coap_server.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch_/switch.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/lock/lock.h"
#include "esphome/components/valve/valve.h"

namespace esphome::coap_server {

// ---------------------------------------------------------------------------
// Concrete subclasses for abstract entity types
// ---------------------------------------------------------------------------

class TestSwitch : public switch_::Switch {
 public:
  void write_state(bool) override {}
};

class TestNumber : public number::Number {
 public:
  void control(float) override {}
};

class TestLock : public lock::Lock {
 public:
  void control(const lock::LockCall &) override {}
};

class TestValve : public valve::Valve {
 public:
  valve::ValveTraits get_traits() override { return valve::ValveTraits{}; }
  void control(const valve::ValveCall &) override {}
};

// Expose protected cbor_output_
class TestableCoapServer : public CoapServer {
 public:
  void on_entity_update(EntityBase *) override {}
  size_t encode(uint8_t *buf, size_t buf_len, ehCoapResource *res) {
    return cbor_output_(buf, buf_len, res->entity, res->type);
  }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float decoded_float(const uint8_t *buf, size_t offset) {
  uint32_t bits = ((uint32_t) buf[offset] << 24) | ((uint32_t) buf[offset + 1] << 16) |
                  ((uint32_t) buf[offset + 2] << 8) | buf[offset + 3];
  float val;
  std::memcpy(&val, &bits, 4);
  return val;
}

// Build a minimal resource pointing at the given entity/type
static ehCoapResource make_resource(EntityType type, EntityBase *entity) {
  ehCoapResource r{};
  r.type = type;
  r.entity = entity;
  return r;
}

// ---------------------------------------------------------------------------
// sensor::Sensor
// ---------------------------------------------------------------------------

#ifdef USE_SENSOR
TEST(CborOutput, SensorFloat) {
  TestableCoapServer srv;
  uint8_t buf[32]{};
  sensor::Sensor s;
  s.state = 1.5f;
  auto r = make_resource(ENTITYTYPE_SENSOR, &s);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  // {2: 1.5f} => 0xa1 0x02 0xfa <float32>
  ASSERT_EQ(len, 7u);
  EXPECT_EQ(buf[0], 0xa1u);
  EXPECT_EQ(buf[1], 0x02u);
  EXPECT_EQ(buf[2], 0xfau);
  EXPECT_FLOAT_EQ(decoded_float(buf, 3), 1.5f);
}

TEST(CborOutput, SensorNaN) {
  TestableCoapServer srv;
  uint8_t buf[32]{};
  sensor::Sensor s;
  s.state = std::numeric_limits<float>::quiet_NaN();
  auto r = make_resource(ENTITYTYPE_SENSOR, &s);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  // {3: "NA"} => 0xa1 0x03 0x62 'N' 'A'
  ASSERT_EQ(len, 5u);
  EXPECT_EQ(buf[1], 0x03u);
  EXPECT_EQ(buf[3], (uint8_t) 'N');
  EXPECT_EQ(buf[4], (uint8_t) 'A');
}
#endif  // USE_SENSOR

// ---------------------------------------------------------------------------
// switch_::Switch
// ---------------------------------------------------------------------------

#ifdef USE_SWITCH
TEST(CborOutput, SwitchTrue) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestSwitch sw;
  sw.state = true;
  auto r = make_resource(ENTITYTYPE_SWITCH, &sw);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  // {4: true} => 0xa1 0x04 0xf5
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[0], 0xa1u);
  EXPECT_EQ(buf[1], 0x04u);
  EXPECT_EQ(buf[2], 0xf5u);
}

TEST(CborOutput, SwitchFalse) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestSwitch sw;
  sw.state = false;
  auto r = make_resource(ENTITYTYPE_SWITCH, &sw);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[2], 0xf4u);
}
#endif  // USE_SWITCH

// ---------------------------------------------------------------------------
// binary_sensor::BinarySensor
// ---------------------------------------------------------------------------

#ifdef USE_BINARY_SENSOR
TEST(CborOutput, BinarySensorTrue) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  binary_sensor::BinarySensor bs;
  bs.state = true;
  auto r = make_resource(ENTITYTYPE_BINARY_SENSOR, &bs);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[1], 0x04u);
  EXPECT_EQ(buf[2], 0xf5u);
}
#endif

// ---------------------------------------------------------------------------
// text_sensor::TextSensor
// ---------------------------------------------------------------------------

#ifdef USE_TEXT_SENSOR
TEST(CborOutput, TextSensorValue) {
  TestableCoapServer srv;
  uint8_t buf[32]{};
  text_sensor::TextSensor ts;
  ts.state = "hi";
  auto r = make_resource(ENTITYTYPE_TEXT_SENSOR, &ts);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 5u);
  EXPECT_EQ(buf[1], 0x03u);
  EXPECT_EQ(buf[3], (uint8_t) 'h');
  EXPECT_EQ(buf[4], (uint8_t) 'i');
}

TEST(CborOutput, TextSensorEmpty) {
  TestableCoapServer srv;
  uint8_t buf[16]{};
  text_sensor::TextSensor ts;
  ts.state = "";
  auto r = make_resource(ENTITYTYPE_TEXT_SENSOR, &ts);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 5u);
  EXPECT_EQ(buf[3], (uint8_t) 'N');
  EXPECT_EQ(buf[4], (uint8_t) 'A');
}
#endif

// ---------------------------------------------------------------------------
// number::Number
// ---------------------------------------------------------------------------

#ifdef USE_NUMBER
TEST(CborOutput, NumberFloat) {
  TestableCoapServer srv;
  uint8_t buf[32]{};
  TestNumber n;
  n.state = 0.5f;
  auto r = make_resource(ENTITYTYPE_NUMBER, &n);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 7u);
  EXPECT_EQ(buf[1], 0x02u);
  EXPECT_FLOAT_EQ(decoded_float(buf, 3), 0.5f);
}
#endif

// ---------------------------------------------------------------------------
// lock::Lock
// ---------------------------------------------------------------------------

#ifdef USE_LOCK
TEST(CborOutput, LockNone) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestLock lk;
  lk.state = lock::LOCK_STATE_NONE;
  auto r = make_resource(ENTITYTYPE_LOCK, &lk);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  // {2: 0u} => 0xa1 0x02 0x00
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[0], 0xa1u);
  EXPECT_EQ(buf[1], 0x02u);
  EXPECT_EQ(buf[2], 0x00u);
}

TEST(CborOutput, LockLocked) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestLock lk;
  lk.state = lock::LOCK_STATE_LOCKED;
  auto r = make_resource(ENTITYTYPE_LOCK, &lk);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[2], 0x01u);
}

TEST(CborOutput, LockUnlocked) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestLock lk;
  lk.state = lock::LOCK_STATE_UNLOCKED;
  auto r = make_resource(ENTITYTYPE_LOCK, &lk);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[2], 0x02u);
}

TEST(CborOutput, LockJammed) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestLock lk;
  lk.state = lock::LOCK_STATE_JAMMED;
  auto r = make_resource(ENTITYTYPE_LOCK, &lk);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[2], 0x03u);
}

TEST(CborOutput, LockLocking) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestLock lk;
  lk.state = lock::LOCK_STATE_LOCKING;
  auto r = make_resource(ENTITYTYPE_LOCK, &lk);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[2], 0x04u);
}

TEST(CborOutput, LockUnlocking) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestLock lk;
  lk.state = lock::LOCK_STATE_UNLOCKING;
  auto r = make_resource(ENTITYTYPE_LOCK, &lk);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[2], 0x05u);
}

TEST(CborOutput, LockOpening) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestLock lk;
  lk.state = lock::LOCK_STATE_OPENING;
  auto r = make_resource(ENTITYTYPE_LOCK, &lk);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[2], 0x06u);
}

TEST(CborOutput, LockOpen) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  TestLock lk;
  lk.state = lock::LOCK_STATE_OPEN;
  auto r = make_resource(ENTITYTYPE_LOCK, &lk);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 3u);
  EXPECT_EQ(buf[2], 0x07u);
}
#endif

// ---------------------------------------------------------------------------
// valve::Valve
// ---------------------------------------------------------------------------

#ifdef USE_VALVE
TEST(CborOutput, ValveHalfOpen) {
  TestableCoapServer srv;
  uint8_t buf[32]{};
  TestValve v;
  v.position = 0.5f;
  auto r = make_resource(ENTITYTYPE_VALVE, &v);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  // {2: 0.5f}
  ASSERT_EQ(len, 7u);
  EXPECT_EQ(buf[1], 0x02u);
  EXPECT_FLOAT_EQ(decoded_float(buf, 3), 0.5f);
}

TEST(CborOutput, ValveClosed) {
  TestableCoapServer srv;
  uint8_t buf[32]{};
  TestValve v;
  v.position = 0.0f;
  auto r = make_resource(ENTITYTYPE_VALVE, &v);
  size_t len = srv.encode(buf, sizeof(buf), &r);
  ASSERT_EQ(len, 7u);
  EXPECT_FLOAT_EQ(decoded_float(buf, 3), 0.0f);
}
#endif

// ---------------------------------------------------------------------------
// Non-entity types return 0
// ---------------------------------------------------------------------------

TEST(CborOutput, ButtonReturnsZero) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  auto r = make_resource(ENTITYTYPE_BUTTON, nullptr);
  EXPECT_EQ(srv.encode(buf, sizeof(buf), &r), 0u);
}

TEST(CborOutput, DeviceReturnsZero) {
  TestableCoapServer srv;
  uint8_t buf[8]{};
  auto r = make_resource(ENTITYTYPE_DEVICE, nullptr);
  EXPECT_EQ(srv.encode(buf, sizeof(buf), &r), 0u);
}

}  // namespace esphome::coap_server
