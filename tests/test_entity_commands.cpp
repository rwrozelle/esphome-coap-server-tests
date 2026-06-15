// Tests for incoming CoAP POST command parsing in handle_entity_request.
// Uses oscore_exempt=true to bypass OSCORE so we can send plain CBOR payloads.

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

#include "esphome/components/coap_server/coap_server.h"
#include "cbor.h"

namespace esphome::coap_server {

// Concrete Switch — write_state is pure virtual
struct TrackingSwitch : switch_::Switch {
  void write_state(bool s) override { state = s; }
};

// Concrete Valve — get_traits is pure virtual; records last ValveCall cmd
struct TrackingValve : valve::Valve {
  int last_cmd{0};
  valve::ValveTraits get_traits() override { return {}; }
  void control(const valve::ValveCall &call) override { last_cmd = call.get_cmd(); }
};

// Number that records the value passed to control()
struct TrackingNumber : number::Number {
  float last_control{NAN};
  void control(float value) override { last_control = value; }
};

class TestableCoapServer : public CoapServerOT {
 public:
  void init() {
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(4);
  }

  void invoke(ehCoapResource *resource, otMessage *message, const otMessageInfo *msg_info, EntityType type) {
    handle_entity_request(resource, message, msg_info, type);
  }
};

// Build a fake POST message carrying a CBOR payload
static otMessage *make_post(const uint8_t *payload, size_t len) {
  otMessage *msg = otCoapNewMessage(nullptr, nullptr);
  otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
  otMessageAppend(msg, payload, static_cast<uint16_t>(len));
  return msg;
}

// Helpers that build common CBOR payloads using the CBOR encoder mock
static size_t cbor_map1_bool(uint8_t *buf, size_t buf_len, int key, bool val) {
  CborEncoder enc, map;
  cbor_encoder_init(&enc, buf, buf_len, 0);
  cbor_encoder_create_map(&enc, &map, 1);
  cbor_encode_int(&map, key);
  cbor_encode_boolean(&map, val);
  cbor_encoder_close_container(&enc, &map);
  return cbor_encoder_get_buffer_size(&enc, buf);
}

static size_t cbor_map1_float(uint8_t *buf, size_t buf_len, int key, float val) {
  CborEncoder enc, map;
  cbor_encoder_init(&enc, buf, buf_len, 0);
  cbor_encoder_create_map(&enc, &map, 1);
  cbor_encode_int(&map, key);
  cbor_encode_float(&map, val);
  cbor_encoder_close_container(&enc, &map);
  return cbor_encoder_get_buffer_size(&enc, buf);
}

static size_t cbor_map1_int(uint8_t *buf, size_t buf_len, int key, int val) {
  CborEncoder enc, map;
  cbor_encoder_init(&enc, buf, buf_len, 0);
  cbor_encoder_create_map(&enc, &map, 1);
  cbor_encode_int(&map, key);
  cbor_encode_int(&map, val);
  cbor_encoder_close_container(&enc, &map);
  return cbor_encoder_get_buffer_size(&enc, buf);
}

static ehCoapResource make_resource(TestableCoapServer *srv, EntityType type, EntityBase *entity,
                                    ActionType action = ACTIONTYPE_NO_ACTION) {
  ehCoapResource r{};
  r.server = srv;
  r.entity = entity;
  r.type = type;
  r.action = action;
  r.oscore_exempt = true;
  r.observable = true;
  strncpy(r.path, "fp/1/g/1", sizeof(r.path));
  r.mUriPath = r.path;
  r.mContext = &r;
  return r;
}

// ── Switch tests ────────────────────────────────────────────────────────────

TEST(EntityCommands, SwitchTurnOn) {
  TestableCoapServer srv;
  srv.init();
  TrackingSwitch sw;
  sw.set_name("sw");
  sw.state = false;
  auto res = make_resource(&srv, ENTITYTYPE_SWITCH, &sw);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 4, true);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_SWITCH);

  EXPECT_TRUE(sw.state);
}

TEST(EntityCommands, SwitchTurnOff) {
  TestableCoapServer srv;
  srv.init();
  TrackingSwitch sw;
  sw.set_name("sw");
  sw.state = true;
  auto res = make_resource(&srv, ENTITYTYPE_SWITCH, &sw);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 4, false);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_SWITCH);

  EXPECT_FALSE(sw.state);
}

TEST(EntityCommands, SwitchToggle) {
  TestableCoapServer srv;
  srv.init();
  TrackingSwitch sw;
  sw.set_name("sw");
  sw.state = false;
  auto res = make_resource(&srv, ENTITYTYPE_SWITCH, &sw, ACTIONTYPE_TOGGLE);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 4, true);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_SWITCH);

  EXPECT_TRUE(sw.state);  // toggled from false → true
}

// ── Lock tests ──────────────────────────────────────────────────────────────

TEST(EntityCommands, LockLock) {
  TestableCoapServer srv;
  srv.init();
  lock::Lock lk;
  lk.set_name("lk");
  lk.state = lock::LOCK_STATE_UNLOCKED;
  auto res = make_resource(&srv, ENTITYTYPE_LOCK, &lk);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 4, true);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_LOCK);

  EXPECT_EQ(lk.state, lock::LOCK_STATE_LOCKED);
}

TEST(EntityCommands, LockUnlock) {
  TestableCoapServer srv;
  srv.init();
  lock::Lock lk;
  lk.set_name("lk");
  lk.state = lock::LOCK_STATE_LOCKED;
  auto res = make_resource(&srv, ENTITYTYPE_LOCK, &lk);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 4, false);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_LOCK);

  EXPECT_EQ(lk.state, lock::LOCK_STATE_UNLOCKED);
}

// ── Valve tests ─────────────────────────────────────────────────────────────

TEST(EntityCommands, ValveOpen) {
  TestableCoapServer srv;
  srv.init();
  TrackingValve v;
  v.set_name("v");
  auto res = make_resource(&srv, ENTITYTYPE_VALVE, &v);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 4, true);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_VALVE);

  EXPECT_EQ(v.last_cmd, 1);  // set_command_open → cmd_ = 1
}

TEST(EntityCommands, ValveClose) {
  TestableCoapServer srv;
  srv.init();
  TrackingValve v;
  v.set_name("v");
  auto res = make_resource(&srv, ENTITYTYPE_VALVE, &v);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 4, false);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_VALVE);

  EXPECT_EQ(v.last_cmd, 2);  // set_command_close → cmd_ = 2
}

TEST(EntityCommands, ValveStop) {
  TestableCoapServer srv;
  srv.init();
  TrackingValve v;
  v.set_name("v");
  auto res = make_resource(&srv, ENTITYTYPE_VALVE, &v, ACTIONTYPE_STOP);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 4, true);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_VALVE);

  EXPECT_EQ(v.last_cmd, 3);  // set_command_stop → cmd_ = 3
}

// ── Number tests ─────────────────────────────────────────────────────────────

TEST(EntityCommands, NumberSetFloat) {
  TestableCoapServer srv;
  srv.init();
  TrackingNumber n;
  n.set_name("n");
  auto res = make_resource(&srv, ENTITYTYPE_NUMBER, &n);

  uint8_t buf[16];
  size_t len = cbor_map1_float(buf, sizeof(buf), 2, 42.5f);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_NUMBER);

  EXPECT_FLOAT_EQ(n.last_control, 42.5f);
}

TEST(EntityCommands, NumberSetInteger) {
  TestableCoapServer srv;
  srv.init();
  TrackingNumber n;
  n.set_name("n");
  auto res = make_resource(&srv, ENTITYTYPE_NUMBER, &n);

  uint8_t buf[16];
  size_t len = cbor_map1_int(buf, sizeof(buf), 2, 10);
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_NUMBER);

  EXPECT_FLOAT_EQ(n.last_control, 10.0f);
}

// ── Edge cases ───────────────────────────────────────────────────────────────

TEST(EntityCommands, UnknownKeyIgnored) {
  TestableCoapServer srv;
  srv.init();
  TrackingSwitch sw;
  sw.set_name("sw");
  sw.state = false;
  auto res = make_resource(&srv, ENTITYTYPE_SWITCH, &sw);

  uint8_t buf[16];
  size_t len = cbor_map1_bool(buf, sizeof(buf), 99, true);  // unknown key
  otMessageInfo mi{};
  srv.invoke(&res, make_post(buf, len), &mi, ENTITYTYPE_SWITCH);

  EXPECT_FALSE(sw.state);  // untouched
}

TEST(EntityCommands, EmptyPayloadNoCrash) {
  TestableCoapServer srv;
  srv.init();
  TrackingSwitch sw;
  sw.set_name("sw");
  auto res = make_resource(&srv, ENTITYTYPE_SWITCH, &sw);

  otMessage *msg = otCoapNewMessage(nullptr, nullptr);
  otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
  // no payload appended
  otMessageInfo mi{};
  srv.invoke(&res, msg, &mi, ENTITYTYPE_SWITCH);
  // just verify no crash / no assertion failure
}

// ---------------------------------------------------------------------------
// republish_all
// ---------------------------------------------------------------------------

class TestableCoapServerRepublish : public CoapServerOT {
 public:
  std::vector<EntityBase *> updated;

  void init(size_t capacity = 4) {
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(capacity);
  }

  void push_resource(EntityBase *entity, bool observable) {
    ehCoapResource r{};
    r.server = this;
    r.entity = entity;
    r.observable = observable;
    r.oscore_exempt = true;
    resources_.push_back(r);
  }

 protected:
  void on_entity_update(EntityBase *entity) override { updated.push_back(entity); }
};

TEST(CoapServerOTRepublish, ObservableEntityIsUpdated) {
  TestableCoapServerRepublish srv;
  srv.init();
  TrackingSwitch sw;
  srv.push_resource(&sw, true);

  srv.republish_all();
  srv.fire_timeouts();

  ASSERT_EQ(srv.updated.size(), 1u);
  EXPECT_EQ(srv.updated[0], &sw);
}

TEST(CoapServerOTRepublish, NonObservableSkipped) {
  TestableCoapServerRepublish srv;
  srv.init();
  TrackingSwitch sw;
  srv.push_resource(&sw, false);

  srv.republish_all();

  EXPECT_TRUE(srv.updated.empty());
}

TEST(CoapServerOTRepublish, NullEntitySkipped) {
  TestableCoapServerRepublish srv;
  srv.init();
  srv.push_resource(nullptr, true);

  srv.republish_all();

  EXPECT_TRUE(srv.updated.empty());
}

TEST(CoapServerOTRepublish, MultipleEntitiesAllUpdated) {
  TestableCoapServerRepublish srv;
  srv.init();
  TrackingSwitch sw1, sw2;
  srv.push_resource(&sw1, true);
  srv.push_resource(&sw2, true);

  srv.republish_all();
  srv.fire_timeouts();

  ASSERT_EQ(srv.updated.size(), 2u);
  EXPECT_EQ(srv.updated[0], &sw1);
  EXPECT_EQ(srv.updated[1], &sw2);
}

// ---------------------------------------------------------------------------
// handle_button_request
// ---------------------------------------------------------------------------

struct TrackingButton : button::Button {
  int press_count{0};
  void press() override { press_count++; }
};

class TestableCoapServerButton : public CoapServerOT {
 public:
  void init() {
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(4);
  }

  void invoke_button(ehCoapResource *resource, otMessage *message, const otMessageInfo *msg_info) {
    handle_button_request(resource, message, msg_info);
  }
};

TEST(CoapServerOTButton, PostPressesButton) {
  TestableCoapServerButton srv;
  srv.init();
  TrackingButton btn;
  btn.set_name("btn");

  ehCoapResource res{};
  res.server = &srv;
  res.entity = &btn;
  res.type = ENTITYTYPE_BUTTON;
  res.oscore_exempt = true;
  strncpy(res.path, "fp/1/g/1", sizeof(res.path));
  res.mUriPath = res.path;
  res.mContext = &res;

  otMessage *msg = otCoapNewMessage(nullptr, nullptr);
  otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
  otMessageInfo mi{};
  srv.invoke_button(&res, msg, &mi);

  EXPECT_EQ(btn.press_count, 1);
}

TEST(CoapServerOTButton, GetDoesNotPress) {
  TestableCoapServerButton srv;
  srv.init();
  TrackingButton btn;
  btn.set_name("btn");

  ehCoapResource res{};
  res.server = &srv;
  res.entity = &btn;
  res.type = ENTITYTYPE_BUTTON;
  res.oscore_exempt = true;
  strncpy(res.path, "fp/1/g/1", sizeof(res.path));
  res.mUriPath = res.path;
  res.mContext = &res;

  otMessage *msg = otCoapNewMessage(nullptr, nullptr);
  otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_GET);
  otMessageInfo mi{};
  srv.invoke_button(&res, msg, &mi);

  EXPECT_EQ(btn.press_count, 0);
}

// ---------------------------------------------------------------------------
// shrink_observers
// ---------------------------------------------------------------------------

class TestableCoapServerShrink : public CoapServerOT {
 public:
  void init() {
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(4);
  }

  void inject_free_observer() {
    auto *obs = new ehCoapObserver{};
    obs->next = free_observers_;
    free_observers_ = obs;
  }

  void set_high_water_mark(uint8_t val) { high_water_mark_ = val; }
  void set_active_count(uint8_t val) { active_count_ = val; }

  bool has_free_observers() const { return free_observers_ != nullptr; }
  uint8_t get_high_water_mark() const { return high_water_mark_; }
};

TEST(CoapServerOTShrink, FreeObserversListCleared) {
  TestableCoapServerShrink srv;
  srv.init();
  srv.inject_free_observer();
  srv.inject_free_observer();

  srv.shrink_observers();

  EXPECT_FALSE(srv.has_free_observers());
}

TEST(CoapServerOTShrink, HighWaterMarkResetToActiveCount) {
  TestableCoapServerShrink srv;
  srv.init();
  srv.set_active_count(3);
  srv.set_high_water_mark(7);

  srv.shrink_observers();

  EXPECT_EQ(srv.get_high_water_mark(), 3u);
}

// ---------------------------------------------------------------------------
// add_on_client_connected_callback / add_on_client_disconnected_callback
// (tested via OT subclass that exposes new_client_ / free_client_)
// ---------------------------------------------------------------------------

class TestableCoapServerCallbacks : public CoapServerOT {
 public:
  void init() {
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(4);
  }

  void connect_client(const otMessageInfo &mi) { new_client_(mi); }
  void disconnect_client(ehCoapClient *c) { free_client_(c); }

  ehCoapClient *first_active_client() {
    for (auto &c : active_clients_)
      if (c.active) return &c;
    return nullptr;
  }
};

TEST(CoapServerOTCallbacks, ConnectedCallbackFires) {
  TestableCoapServerCallbacks srv;
  srv.init();
  std::string got_addr;
  srv.add_on_client_connected_callback([&](const std::string &addr) { got_addr = addr; });

  otMessageInfo mi{};
  srv.connect_client(mi);

  EXPECT_FALSE(got_addr.empty());
}

TEST(CoapServerOTCallbacks, DisconnectedCallbackFires) {
  TestableCoapServerCallbacks srv;
  srv.init();
  std::string disconnected_addr;
  srv.add_on_client_disconnected_callback([&](const std::string &addr) { disconnected_addr = addr; });

  otMessageInfo mi{};
  srv.connect_client(mi);
  ehCoapClient *c = srv.first_active_client();
  ASSERT_NE(c, nullptr);
  srv.disconnect_client(c);

  EXPECT_FALSE(disconnected_addr.empty());
}

}  // namespace esphome::coap_server
