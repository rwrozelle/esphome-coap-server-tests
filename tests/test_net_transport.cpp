// Tests for CoapServerNet: CoAP packet parsing and request dispatch.
// Uses process_datagram_() injection and virtual send_response() capture
// to avoid real socket I/O.

#include <gtest/gtest.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <memory>
#include <vector>
#include "esphome/components/coap_server/coap_server.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch_/switch.h"
#include "esphome/core/application.h"
#include "cbor.h"

namespace esphome::coap_server {

// ---------------------------------------------------------------------------
// Test subclass — captures outbound packets, exposes inject()
// ---------------------------------------------------------------------------
class TestableCoapServerNet : public CoapServerNet {
 public:
  void init_resources(size_t n) {
    resources_.init(n);
    // Ensure link_format_buf_ is always non-null so handle_well_known_core_
    // never calls append(nullptr, ...). Cache is rebuilt in populate_link_format_cache().
    populate_link_format_cache();
  }

  void inject(const uint8_t *buf, size_t len, const struct sockaddr_in6 &peer) {
    process_datagram_(buf, len, &peer);
  }

  void add_resource_for_test(EntityType type, EntityBase *entity, bool obs, uint16_t &idx) {
    size_t before = resources_.size();
    add_net_resource_(type, entity, obs, idx);
    // Mark all newly-added resources as OSCORE-exempt so plain requests work in tests
    for (size_t i = before; i < resources_.size(); i++)
      resources_[i].oscore_exempt = true;
  }

  // Rebuilds link_format_buf_ from the current App entity lists (as setup() would do).
  // Call this after adding all entities to App and before making .well-known/core requests.
  void populate_link_format_cache() {
    size_t size = build_link_format(nullptr, 0);
    link_format_buf_ = std::make_unique<uint8_t[]>(size);
    build_link_format(link_format_buf_.get(), size);
    link_format_size_ = size;
  }

  uint16_t get_next_msg_id() const { return next_msg_id_; }

  std::vector<uint8_t> last_sent;

 protected:
  void send_response(const uint8_t *buf, size_t len, const struct sockaddr_in6 *) override {
    last_sent.assign(buf, buf + len);
  }
};

// ---------------------------------------------------------------------------
// Packet builder helpers
// ---------------------------------------------------------------------------

static struct sockaddr_in6 make_peer(uint16_t port = 12345) {
  struct sockaddr_in6 p{};
  p.sin6_family = AF_INET6;
  p.sin6_port = htons(port);
  inet_pton(AF_INET6, "::1", &p.sin6_addr);
  return p;
}

// Build a CoAP NON GET for a path like "ping" or "fp/1/g/1"
static std::vector<uint8_t> make_coap_get(const char *path, uint16_t msg_id = 1,
                                           bool add_observe = false, uint8_t obs_val = 0) {
  std::vector<uint8_t> pkt;
  pkt.push_back(0x51);                    // VER=1, T=NON, TKL=1
  pkt.push_back(0x01);                    // GET
  pkt.push_back((uint8_t)(msg_id >> 8));
  pkt.push_back((uint8_t)(msg_id & 0xFF));
  pkt.push_back(0xAB);                    // token

  // Observe option (6) before Uri-Path (11) — delta=6
  if (add_observe) {
    pkt.push_back((uint8_t)(0x61));       // delta=6, len=1
    pkt.push_back(obs_val);
  }

  // Uri-Path options (11), split on '/'
  uint16_t last_opt = add_observe ? 6 : 0;
  std::string p(path);
  size_t start = 0;
  while (start <= p.size()) {
    size_t slash = p.find('/', start);
    if (slash == std::string::npos) slash = p.size();
    std::string seg = p.substr(start, slash - start);
    if (!seg.empty()) {
      uint16_t delta = 11 - last_opt;
      last_opt = 11;
      pkt.push_back((uint8_t)((delta << 4) | (seg.size() & 0x0F)));
      for (char c : seg) pkt.push_back((uint8_t)c);
    }
    start = slash + 1;
  }
  return pkt;
}

// Build a CoAP NON POST with a CBOR payload
static std::vector<uint8_t> make_coap_post(const char *path, const uint8_t *payload,
                                            size_t payload_len, uint16_t msg_id = 2) {
  std::vector<uint8_t> pkt;
  pkt.push_back(0x51);                    // VER=1, T=NON, TKL=1
  pkt.push_back(0x02);                    // POST
  pkt.push_back((uint8_t)(msg_id >> 8));
  pkt.push_back((uint8_t)(msg_id & 0xFF));
  pkt.push_back(0xCD);                    // token

  uint16_t last_opt = 0;
  std::string ps(path);
  size_t start = 0;
  while (start <= ps.size()) {
    size_t slash = ps.find('/', start);
    if (slash == std::string::npos) slash = ps.size();
    std::string seg = ps.substr(start, slash - start);
    if (!seg.empty()) {
      uint16_t delta = 11 - last_opt;
      last_opt = 11;
      pkt.push_back((uint8_t)((delta << 4) | (seg.size() & 0x0F)));
      for (char c : seg) pkt.push_back((uint8_t)c);
    }
    start = slash + 1;
  }
  pkt.push_back(0xFF);  // payload marker
  for (size_t i = 0; i < payload_len; i++) pkt.push_back(payload[i]);
  return pkt;
}

// ---------------------------------------------------------------------------
// CoAP parser tests
// ---------------------------------------------------------------------------

TEST(CoapServerNetParse, TooShortReturnsFalse) {
  uint8_t buf[] = {0x51, 0x01};
  CoapServerNet::CoapPacket pkt{};
  EXPECT_FALSE(CoapServerNet::parse_coap(buf, sizeof(buf), &pkt));
}

TEST(CoapServerNetParse, ParseGetPing) {
  auto raw = make_coap_get("ping");
  CoapServerNet::CoapPacket pkt{};
  ASSERT_TRUE(CoapServerNet::parse_coap(raw.data(), raw.size(), &pkt));
  EXPECT_EQ(pkt.type, 1u);    // NON
  EXPECT_EQ(pkt.code, 0x01u); // GET
  EXPECT_STREQ(pkt.uri_path, "ping");
  EXPECT_EQ(pkt.token_len, 1u);
  EXPECT_EQ(pkt.token[0], 0xAB);
}

TEST(CoapServerNetParse, ParseGetInfo) {
  auto raw = make_coap_get("info");
  CoapServerNet::CoapPacket pkt{};
  ASSERT_TRUE(CoapServerNet::parse_coap(raw.data(), raw.size(), &pkt));
  EXPECT_STREQ(pkt.uri_path, "info");
}

TEST(CoapServerNetParse, ParseWellKnownCore) {
  auto raw = make_coap_get(".well-known/core");
  CoapServerNet::CoapPacket pkt{};
  ASSERT_TRUE(CoapServerNet::parse_coap(raw.data(), raw.size(), &pkt));
  EXPECT_STREQ(pkt.uri_path, ".well-known/core");
}

TEST(CoapServerNetParse, ParseFpPath) {
  auto raw = make_coap_get("fp/1/g/1");
  CoapServerNet::CoapPacket pkt{};
  ASSERT_TRUE(CoapServerNet::parse_coap(raw.data(), raw.size(), &pkt));
  EXPECT_STREQ(pkt.uri_path, "fp/1/g/1");
}

TEST(CoapServerNetParse, ParseObserveRegister) {
  auto raw = make_coap_get("fp/1/g/1", 1, true, 0);
  CoapServerNet::CoapPacket pkt{};
  ASSERT_TRUE(CoapServerNet::parse_coap(raw.data(), raw.size(), &pkt));
  EXPECT_TRUE(pkt.observe_present);
  EXPECT_EQ(pkt.observe, 0u);
}

TEST(CoapServerNetParse, ParseObserveDeregister) {
  auto raw = make_coap_get("fp/1/g/1", 1, true, 1);
  CoapServerNet::CoapPacket pkt{};
  ASSERT_TRUE(CoapServerNet::parse_coap(raw.data(), raw.size(), &pkt));
  EXPECT_TRUE(pkt.observe_present);
  EXPECT_EQ(pkt.observe, 1u);
}

TEST(CoapServerNetParse, ParsePostWithPayload) {
  uint8_t payload[] = {0xA1, 0x04, 0xF5};  // {4: true} in CBOR
  auto raw = make_coap_post("fp/1/g/1", payload, sizeof(payload));
  CoapServerNet::CoapPacket pkt{};
  ASSERT_TRUE(CoapServerNet::parse_coap(raw.data(), raw.size(), &pkt));
  EXPECT_EQ(pkt.code, 0x02u);  // POST
  EXPECT_STREQ(pkt.uri_path, "fp/1/g/1");
  ASSERT_EQ(pkt.payload_len, sizeof(payload));
  EXPECT_EQ(memcmp(pkt.payload, payload, sizeof(payload)), 0);
}

// ---------------------------------------------------------------------------
// Dispatch tests — inject datagram, verify response code byte
// ---------------------------------------------------------------------------

TEST(CoapServerNetDispatch, PingResponds205) {
  TestableCoapServerNet srv;
  srv.init_resources(2);
  auto raw = make_coap_get("ping");
  srv.inject(raw.data(), raw.size(), make_peer());
  ASSERT_GE(srv.last_sent.size(), 2u);
  EXPECT_EQ(srv.last_sent[1], 0x45u);  // 2.05 Content
}

TEST(CoapServerNetDispatch, InfoResponds205) {
  TestableCoapServerNet srv;
  srv.init_resources(2);
  auto raw = make_coap_get("info");
  srv.inject(raw.data(), raw.size(), make_peer());
  ASSERT_GE(srv.last_sent.size(), 2u);
  EXPECT_EQ(srv.last_sent[1], 0x45u);
}

TEST(CoapServerNetDispatch, WellKnownCoreResponds205) {
  TestableCoapServerNet srv;
  srv.init_resources(2);
  auto raw = make_coap_get(".well-known/core");
  srv.inject(raw.data(), raw.size(), make_peer());
  ASSERT_GE(srv.last_sent.size(), 2u);
  EXPECT_EQ(srv.last_sent[1], 0x45u);
}

TEST(CoapServerNetDispatch, UnknownPathResponds404) {
  TestableCoapServerNet srv;
  srv.init_resources(2);
  auto raw = make_coap_get("nonexistent");
  srv.inject(raw.data(), raw.size(), make_peer());
  ASSERT_GE(srv.last_sent.size(), 2u);
  EXPECT_EQ(srv.last_sent[1], 0x84u);  // 4.04 Not Found
}

TEST(CoapServerNetDispatch, MirrorsMsgIdAndToken) {
  TestableCoapServerNet srv;
  srv.init_resources(2);
  auto raw = make_coap_get("ping", 0x1234);
  srv.inject(raw.data(), raw.size(), make_peer());
  ASSERT_GE(srv.last_sent.size(), 5u);
  uint16_t resp_id = ((uint16_t)srv.last_sent[2] << 8) | srv.last_sent[3];
  EXPECT_EQ(resp_id, 0x1234u);
  EXPECT_EQ(srv.last_sent[4], 0xABu);  // token
}

TEST(CoapServerNetDispatch, SensorGetResponds205) {
  TestableCoapServerNet srv;
  srv.init_resources(4);

  sensor::Sensor s;
  s.set_name("temp");
  s.state = 22.5f;
  uint16_t idx = 1;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  auto raw = make_coap_get("fp/1/g/1");
  srv.inject(raw.data(), raw.size(), make_peer());
  ASSERT_GE(srv.last_sent.size(), 2u);
  EXPECT_EQ(srv.last_sent[1], 0x45u);
}

TEST(CoapServerNetDispatch, SwitchPostResponds244) {
  TestableCoapServerNet srv;
  srv.init_resources(4);

  struct TrackSwitch : switch_::Switch {
    bool last{false};
    void write_state(bool s) override { last = s; }
  } sw;
  sw.set_name("relay");

  uint16_t idx = 1;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SWITCH, &sw, true, idx);

  uint8_t cbor[] = {0xA1, 0x04, 0xF5};  // {4: true}
  auto raw = make_coap_post("fp/1/g/1", cbor, sizeof(cbor));
  srv.inject(raw.data(), raw.size(), make_peer());
  ASSERT_GE(srv.last_sent.size(), 2u);
  EXPECT_EQ(srv.last_sent[1], 0x44u);  // 2.04 Changed
  EXPECT_TRUE(sw.last);
}

// ---------------------------------------------------------------------------
// Link-format content helpers
// ---------------------------------------------------------------------------

// Extract the link-format payload from a captured CoAP response (after 0xFF marker).
static std::string extract_payload(const std::vector<uint8_t> &pkt) {
  for (size_t i = 4; i < pkt.size(); i++) {
    if (pkt[i] == 0xFF)
      return std::string(pkt.begin() + i + 1, pkt.end());
  }
  return {};
}

struct LinkFormatNet : TestableCoapServerNet {
  void init_resources(size_t n) {
    resources_.init(n + 1);
    resources_.push_back(NetCoapResource());  // index-0 placeholder (.well-known/core)
  }

  // Adds entity to both resources_ (request handlers) and App lists (link format).
  void add_entity(EntityType type, EntityBase *entity, bool obs, uint16_t &idx) {
    add_resource_for_test(type, entity, obs, idx);
    switch (type) {
      case ENTITYTYPE_SENSOR: App.get_sensors().push_back(static_cast<sensor::Sensor *>(entity)); break;
      default: break;
    }
  }
};

// Fixture for link-format content tests — resets App entity lists between tests.
class NetLinkFormatTest : public ::testing::Test {
 protected:
  void SetUp() override { App.reset_entities(); }
  void TearDown() override { App.reset_entities(); }
};

static std::string get_wkc(LinkFormatNet &srv) {
  srv.populate_link_format_cache();
  auto raw = make_coap_get(".well-known/core");
  srv.inject(raw.data(), raw.size(), make_peer());
  return extract_payload(srv.last_sent);
}

TEST_F(NetLinkFormatTest, SensorHasDcAndUom) {
  LinkFormatNet srv;
  srv.init_resources(4);
  sensor::Sensor s;
  s.set_name("temperature");
  s.set_object_id_hash(305419896u);
  s.set_unit_of_measurement("C");
  s.set_device_class("temperature");
  uint16_t idx = 1;
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  auto lf = get_wkc(srv);

  EXPECT_NE(lf.find("dc=\"temperature\""), std::string::npos);
  EXPECT_NE(lf.find("uom=\"C\""), std::string::npos);
}

TEST_F(NetLinkFormatTest, SensorWithoutDcOmitsDc) {
  LinkFormatNet srv;
  srv.init_resources(4);
  sensor::Sensor s;
  s.set_name("raw");
  uint16_t idx = 1;
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  auto lf = get_wkc(srv);

  EXPECT_EQ(lf.find(";dc="), std::string::npos);
}

TEST_F(NetLinkFormatTest, EntityHasDvWhenDeviceIndexSet) {
  // Sensor belongs to device ID 42, which is the second device in App (index 2).
  LinkFormatNet srv;
  srv.init_resources(4);
  sensor::Sensor s;
  s.set_name("remote");
  s.set_device_id(42);

  esphome::Device dev1, dev2;
  dev1.set_device_id(99);  // index 1
  dev2.set_device_id(42);  // index 2 — matches s.device_id
  App.get_devices().push_back(&dev1);
  App.get_devices().push_back(&dev2);

  uint16_t idx = 1;
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  auto lf = get_wkc(srv);

  EXPECT_NE(lf.find(";dv=2"), std::string::npos);
}

TEST_F(NetLinkFormatTest, EntityWithDefaultDeviceIndexOmitsDv) {
  LinkFormatNet srv;
  srv.init_resources(4);
  sensor::Sensor s;
  s.set_name("local");
  uint16_t idx = 1;
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  auto lf = get_wkc(srv);

  EXPECT_EQ(lf.find(";dv="), std::string::npos);
}

// ---------------------------------------------------------------------------
// republish_all
// ---------------------------------------------------------------------------

struct TrackingNet : TestableCoapServerNet {
  std::vector<EntityBase *> updated;

  void push_observable_null_entity() {
    NetCoapResource r{};
    r.observable = true;
    r.entity = nullptr;
    resources_.push_back(r);
  }

 protected:
  void on_entity_update(EntityBase *entity) override { updated.push_back(entity); }
};

TEST(CoapServerNetRepublish, ObservableEntityIsUpdated) {
  TrackingNet srv;
  srv.init_resources(4);
  sensor::Sensor s;
  uint16_t idx = 1;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  srv.republish_all();

  ASSERT_EQ(srv.updated.size(), 1u);
  EXPECT_EQ(srv.updated[0], &s);
}

TEST(CoapServerNetRepublish, NonObservableSkipped) {
  TrackingNet srv;
  srv.init_resources(4);
  sensor::Sensor s;
  uint16_t idx = 1;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s, false, idx);

  srv.republish_all();

  EXPECT_TRUE(srv.updated.empty());
}

TEST(CoapServerNetRepublish, NullEntitySkipped) {
  TrackingNet srv;
  srv.init_resources(2);
  srv.push_observable_null_entity();

  srv.republish_all();

  EXPECT_TRUE(srv.updated.empty());
}

TEST(CoapServerNetRepublish, MultipleEntitiesAllUpdated) {
  TrackingNet srv;
  srv.init_resources(8);
  sensor::Sensor s1, s2;
  uint16_t idx = 1;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s1, true, idx);
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s2, true, idx);

  srv.republish_all();

  ASSERT_EQ(srv.updated.size(), 2u);
  EXPECT_EQ(srv.updated[0], &s1);
  EXPECT_EQ(srv.updated[1], &s2);
}

// ---------------------------------------------------------------------------
// add_on_client_connected_callback / add_on_client_disconnected_callback
// ---------------------------------------------------------------------------

struct CallbackNet : TestableCoapServerNet {
  NetCoapClient *expose_new_client(const struct sockaddr_in6 &peer) { return new_client_(peer); }
  void expose_free_client(NetCoapClient *c) { free_client_(c); }

  NetCoapClient *first_active_client() {
    for (auto &c : active_clients_)
      if (c.active) return &c;
    return nullptr;
  }
};

TEST(CoapServerNetCallbacks, ConnectedCallbackFires) {
  CallbackNet srv;
  srv.init_resources(2);
  std::string got_addr;
  srv.add_on_client_connected_callback([&](const std::string &addr) { got_addr = addr; });

  srv.expose_new_client(make_peer());

  EXPECT_FALSE(got_addr.empty());
}

TEST(CoapServerNetCallbacks, DisconnectedCallbackFires) {
  CallbackNet srv;
  srv.init_resources(2);
  std::string disconnected_addr;
  srv.add_on_client_disconnected_callback([&](const std::string &addr) { disconnected_addr = addr; });

  srv.expose_new_client(make_peer());
  NetCoapClient *c = srv.first_active_client();
  ASSERT_NE(c, nullptr);
  srv.expose_free_client(c);

  EXPECT_FALSE(disconnected_addr.empty());
}

TEST(CoapServerNetCallbacks, ConnectedCallbackReceivesAddress) {
  CallbackNet srv;
  srv.init_resources(2);
  std::string got_addr;
  srv.add_on_client_connected_callback([&](const std::string &addr) { got_addr = addr; });

  struct sockaddr_in6 peer{};
  peer.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &peer.sin6_addr);
  srv.expose_new_client(peer);

  EXPECT_NE(got_addr.find("::1"), std::string::npos);
}

TEST(CoapServerNetCallbacks, ActiveClientCountIncrementedOnConnect) {
  CallbackNet srv;
  srv.init_resources(2);

  EXPECT_EQ(srv.get_active_client_count(), 0u);
  srv.expose_new_client(make_peer(10001));
  EXPECT_EQ(srv.get_active_client_count(), 1u);
}

TEST(CoapServerNetCallbacks, ActiveClientCountDecrementedOnDisconnect) {
  CallbackNet srv;
  srv.init_resources(2);

  srv.expose_new_client(make_peer());
  EXPECT_EQ(srv.get_active_client_count(), 1u);
  NetCoapClient *c = srv.first_active_client();
  ASSERT_NE(c, nullptr);
  srv.expose_free_client(c);
  EXPECT_EQ(srv.get_active_client_count(), 0u);
}

// ---------------------------------------------------------------------------
// CON notification ACK/RST handling (Finding #2)
// ---------------------------------------------------------------------------

// Build a CON GET with observe=0 (register)
static std::vector<uint8_t> make_con_observe(const char *path, uint16_t msg_id = 10) {
  std::vector<uint8_t> pkt;
  pkt.push_back(0x41);                    // VER=1, T=CON(0), TKL=1
  pkt.push_back(0x01);                    // GET
  pkt.push_back((uint8_t)(msg_id >> 8));
  pkt.push_back((uint8_t)(msg_id & 0xFF));
  pkt.push_back(0xBB);                    // token

  // Observe option (6) = 0 (register)
  pkt.push_back(0x61);  // delta=6, len=1
  pkt.push_back(0x00);

  // Uri-Path options (11), split on '/'
  uint16_t last_opt = 6;
  std::string p(path);
  size_t start = 0;
  while (start <= p.size()) {
    size_t slash = p.find('/', start);
    if (slash == std::string::npos) slash = p.size();
    std::string seg = p.substr(start, slash - start);
    if (!seg.empty()) {
      uint16_t delta = 11 - last_opt;
      last_opt = 11;
      pkt.push_back((uint8_t)((delta << 4) | (seg.size() & 0x0F)));
      for (char c : seg) pkt.push_back((uint8_t)c);
    }
    start = slash + 1;
  }
  return pkt;
}

// Build an empty ACK for a given message ID (client acknowledges a CON from server)
static std::vector<uint8_t> make_coap_ack(uint16_t msg_id) {
  return {0x60, 0x00, (uint8_t)(msg_id >> 8), (uint8_t)(msg_id & 0xFF)};
}

// Build an RST for a given message ID (client rejects a CON from server)
static std::vector<uint8_t> make_coap_rst(uint16_t msg_id) {
  return {0x70, 0x00, (uint8_t)(msg_id >> 8), (uint8_t)(msg_id & 0xFF)};
}

struct ConObserverNet : TestableCoapServerNet {
  NetCoapClient *first_active_client() {
    for (auto &c : active_clients_)
      if (c.active) return &c;
    return nullptr;
  }
  NetCoapObserver *first_active_observer() { return active_observers_; }
  size_t active_observer_count() {
    size_t n = 0;
    for (auto *o = active_observers_; o != nullptr; o = o->next)
      n++;
    return n;
  }
  void trigger_notify(NetCoapResource *res, const uint8_t *payload, size_t len) {
    notify_observers_(res, payload, len);
  }
  NetCoapResource *first_observable_resource() {
    for (size_t i = 0; i < resources_.size(); i++)
      if (resources_[i].observable && resources_[i].entity != nullptr)
        return &resources_[i];
    return nullptr;
  }
  // Peek at the next msg_id the server will assign (without consuming it)
  uint16_t peek_next_msg_id() const { return next_msg_id_; }
};

// Helper to set up a sensor resource and register a CON observer.
// Returns the observable resource pointer.
static NetCoapResource *setup_con_observer(ConObserverNet &srv, sensor::Sensor &s, uint16_t &idx) {
  srv.init_resources(4);
  srv.set_subscription_confirm(true);
  s.set_name("temp");
  s.state = 22.5f;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  // CON GET with observe=0 from peer port 12345
  auto sub = make_con_observe("fp/1/g/1");
  srv.inject(sub.data(), sub.size(), make_peer());

  return srv.first_observable_resource();
}

// Build a NON 2.05 Content with no URI path (client's response to a server-initiated GET /ping)
static std::vector<uint8_t> make_non_205(uint16_t msg_id = 0x0099) {
  return {
    0x50,                         // VER=1, T=NON, TKL=0
    0x45,                         // 2.05 Content
    (uint8_t)(msg_id >> 8),
    (uint8_t)(msg_id & 0xFF),
  };
}

// ---------------------------------------------------------------------------
// Server-initiated ping response handling (NON 2.xx touch_client_)
// ---------------------------------------------------------------------------

TEST(CoapServerNetPingResponse, Non205TouchesClientAndSendsNo404) {
  // Reproduces the server-initiated GET /ping → client 2.05 response flow.
  // Before fix: the 2.05 fell through to find_resource_("") → 4.04 sent and
  // last_response_ms was never updated.
  // After fix: touch_client_() is called and no 4.04 is sent.
  ConObserverNet srv;
  srv.init_resources(4);
  sensor::Sensor s;
  s.set_name("temp");
  s.state = 1.0f;
  uint16_t idx = 1;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  // NON subscribe → creates client record with has_non_observer=true
  auto sub = make_coap_get("fp/1/g/1", 1, true, 0);
  auto peer = make_peer();
  srv.inject(sub.data(), sub.size(), peer);

  NetCoapClient *c = srv.first_active_client();
  ASSERT_NE(c, nullptr);
  EXPECT_TRUE(c->has_non_observer);

  // Set last_response_ms to a sentinel that millis() cannot plausibly return, and
  // set ping_miss_count to a non-zero value so we can verify touch_client_ ran.
  c->last_response_ms = UINT32_MAX;
  c->ping_miss_count = 1;

  srv.last_sent.clear();

  // Inject a NON 2.05 (client's response to a server-initiated GET /ping)
  auto resp = make_non_205();
  srv.inject(resp.data(), resp.size(), peer);

  EXPECT_NE(c->last_response_ms, UINT32_MAX);  // last_response_ms refreshed by touch_client_
  EXPECT_EQ(c->ping_miss_count, 0u);           // ping_miss_count cleared by touch_client_
  EXPECT_TRUE(srv.last_sent.empty());          // no 4.04 sent back
}

TEST(CoapServerNetConObserve, AckClearsPending) {
  ConObserverNet srv;
  sensor::Sensor s;
  uint16_t idx = 1;
  NetCoapResource *res = setup_con_observer(srv, s, idx);
  ASSERT_NE(res, nullptr);

  NetCoapObserver *obs = srv.first_active_observer();
  ASSERT_NE(obs, nullptr);
  EXPECT_TRUE(obs->is_con);

  // Trigger a notification — server sends a CON
  uint8_t payload[] = {0x81};
  srv.trigger_notify(res, payload, sizeof(payload));
  ASSERT_FALSE(srv.last_sent.empty());
  EXPECT_EQ(srv.last_sent[0] & 0x30, 0x00u);  // type=CON(0)
  uint16_t sent_msg_id = ((uint16_t)srv.last_sent[2] << 8) | srv.last_sent[3];
  EXPECT_TRUE(obs->con_pending);

  // Client ACKs the notification
  auto ack = make_coap_ack(sent_msg_id);
  srv.inject(ack.data(), ack.size(), make_peer());

  EXPECT_FALSE(obs->con_pending);
  EXPECT_EQ(srv.active_observer_count(), 1u);  // observer still alive
}

TEST(CoapServerNetConObserve, RstFreesObserver) {
  ConObserverNet srv;
  sensor::Sensor s;
  uint16_t idx = 1;
  NetCoapResource *res = setup_con_observer(srv, s, idx);
  ASSERT_NE(res, nullptr);
  ASSERT_EQ(srv.active_observer_count(), 1u);

  // Trigger a notification — server sends a CON
  uint8_t payload[] = {0x81};
  srv.trigger_notify(res, payload, sizeof(payload));
  ASSERT_FALSE(srv.last_sent.empty());
  uint16_t sent_msg_id = ((uint16_t)srv.last_sent[2] << 8) | srv.last_sent[3];

  // Client RSTs the notification — server should remove the observer
  auto rst = make_coap_rst(sent_msg_id);
  srv.inject(rst.data(), rst.size(), make_peer());

  EXPECT_EQ(srv.active_observer_count(), 0u);
}

TEST(CoapServerNetConObserve, PendingBlocksNextCon) {
  ConObserverNet srv;
  sensor::Sensor s;
  uint16_t idx = 1;
  NetCoapResource *res = setup_con_observer(srv, s, idx);
  ASSERT_NE(res, nullptr);

  uint8_t payload[] = {0x81};

  // First notify (notify_count=0) — sends CON, sets con_pending
  srv.trigger_notify(res, payload, sizeof(payload));
  ASSERT_FALSE(srv.last_sent.empty());
  NetCoapObserver *obs = srv.first_active_observer();
  ASSERT_NE(obs, nullptr);
  EXPECT_TRUE(obs->con_pending);
  EXPECT_EQ(obs->notify_count, 1u);

  // Notifications 2-5 (notify_count 1-4) — sent as NON, not blocked by con_pending
  for (int i = 0; i < 4; i++) {
    srv.last_sent.clear();
    srv.trigger_notify(res, payload, sizeof(payload));
    EXPECT_FALSE(srv.last_sent.empty()) << "NON notification " << (i + 2) << " should not be blocked";
    EXPECT_EQ(srv.last_sent[0] & 0x30, 0x10) << "should be NON (type=1)";
  }
  EXPECT_TRUE(obs->con_pending);  // still pending — no ACK received
  EXPECT_EQ(obs->notify_count, 5u);

  // 6th notification (notify_count=5) — would be CON, but blocked by pending
  srv.last_sent.clear();
  srv.trigger_notify(res, payload, sizeof(payload));
  EXPECT_TRUE(srv.last_sent.empty()) << "6th CON should be blocked while previous CON is pending";
  EXPECT_EQ(obs->notify_count, 5u);  // not incremented when skipped
}

TEST(CoapServerNetConObserve, AckFromWrongPeerIgnored) {
  ConObserverNet srv;
  sensor::Sensor s;
  uint16_t idx = 1;
  NetCoapResource *res = setup_con_observer(srv, s, idx);
  ASSERT_NE(res, nullptr);

  uint8_t payload[] = {0x81};
  srv.trigger_notify(res, payload, sizeof(payload));
  ASSERT_FALSE(srv.last_sent.empty());
  uint16_t sent_msg_id = ((uint16_t)srv.last_sent[2] << 8) | srv.last_sent[3];

  NetCoapObserver *obs = srv.first_active_observer();
  ASSERT_NE(obs, nullptr);
  EXPECT_TRUE(obs->con_pending);

  // ACK from a *different* peer — must not clear pending
  auto ack = make_coap_ack(sent_msg_id);
  srv.inject(ack.data(), ack.size(), make_peer(99999));  // different port → different peer

  EXPECT_TRUE(obs->con_pending);  // still pending
}

TEST(CoapServerNetConObserve, AckAfterClearDoesNothing) {
  ConObserverNet srv;
  sensor::Sensor s;
  uint16_t idx = 1;
  NetCoapResource *res = setup_con_observer(srv, s, idx);
  ASSERT_NE(res, nullptr);

  uint8_t payload[] = {0x81};
  srv.trigger_notify(res, payload, sizeof(payload));
  uint16_t sent_msg_id = ((uint16_t)srv.last_sent[2] << 8) | srv.last_sent[3];

  // ACK once — clears pending
  auto ack = make_coap_ack(sent_msg_id);
  srv.inject(ack.data(), ack.size(), make_peer());
  EXPECT_EQ(srv.active_observer_count(), 1u);

  // Duplicate ACK — must not crash or remove the observer
  srv.inject(ack.data(), ack.size(), make_peer());
  EXPECT_EQ(srv.active_observer_count(), 1u);
}

TEST(CoapServerNetConObserve, NonObserverUnaffectedByAck) {
  ConObserverNet srv;
  srv.init_resources(4);
  sensor::Sensor s;
  s.set_name("temp");
  s.state = 22.5f;
  uint16_t idx = 1;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  // NON GET with observe=0 → NON observer
  auto sub = make_coap_get("fp/1/g/1", 1, true, 0);
  srv.inject(sub.data(), sub.size(), make_peer());

  NetCoapObserver *obs = srv.first_active_observer();
  ASSERT_NE(obs, nullptr);
  EXPECT_FALSE(obs->is_con);

  // Inject an ACK with any msg_id — must not affect the NON observer
  auto ack = make_coap_ack(0x0001);
  srv.inject(ack.data(), ack.size(), make_peer());

  EXPECT_EQ(srv.active_observer_count(), 1u);
  EXPECT_FALSE(obs->con_pending);
}

// ---------------------------------------------------------------------------
// Ping boot signal: client identified by IP only, not IP+port
//
// aiocoap may send the /ping NON GET from a different source port than the
// source port used for observe-GET subscriptions.  The client slot is created
// when the observe GET arrives; the ping handler must find it by IP alone so
// that boot_notified is set correctly and -1 is only returned once.
// ---------------------------------------------------------------------------

TEST(CoapServerNetPing, BootSignalSentOnceWhenPingPortDiffersFromObservePort) {
  TestableCoapServerNet srv;
  srv.init_resources(4);

  sensor::Sensor s;
  s.set_name("temp");
  s.state = 22.5f;
  uint16_t idx = 1;
  srv.add_resource_for_test(EntityType::ENTITYTYPE_SENSOR, &s, true, idx);

  // Subscribe (observe GET) from port 12345 — creates client slot
  auto sub = make_coap_get("fp/1/g/1", 1, true, 0);
  srv.inject(sub.data(), sub.size(), make_peer(12345));

  // First ping from a DIFFERENT port (9999) — must return -1 (first boot contact)
  auto ping1 = make_coap_get("ping", 2);
  srv.inject(ping1.data(), ping1.size(), make_peer(9999));
  ASSERT_GE(srv.last_sent.size(), 5u);
  // Payload is {2: -1} = 0xa1 0x02 0x20 (3 bytes after payload marker 0xFF)
  auto it = std::find(srv.last_sent.begin(), srv.last_sent.end(), 0xFF);
  ASSERT_NE(it, srv.last_sent.end());
  std::vector<uint8_t> payload1(it + 1, srv.last_sent.end());
  ASSERT_EQ(payload1.size(), 3u);
  EXPECT_EQ(payload1[2], 0x20u);  // CBOR integer -1

  // Second ping from the same different port — must NOT return -1 (boot_notified=true)
  auto ping2 = make_coap_get("ping", 3);
  srv.inject(ping2.data(), ping2.size(), make_peer(9999));
  ASSERT_GE(srv.last_sent.size(), 5u);
  it = std::find(srv.last_sent.begin(), srv.last_sent.end(), 0xFF);
  ASSERT_NE(it, srv.last_sent.end());
  std::vector<uint8_t> payload2(it + 1, srv.last_sent.end());
  ASSERT_GE(payload2.size(), 3u);
  EXPECT_NE(payload2[2], 0x20u);  // Not -1 — uptime value
}

// ---------------------------------------------------------------------------
// RFC 7252 §4.4: initial message ID must be random
// ---------------------------------------------------------------------------

TEST(CoapServerNetSetup, InitialMsgIdIsRandomized) {
  TestableCoapServerNet a, b;
  a.init_resources(0);
  b.init_resources(0);
  // Two independently initialized instances must not both start at 1.
  // The probability of a false failure is 1/65536 per run.
  EXPECT_NE(a.get_next_msg_id(), uint16_t{1});
  EXPECT_NE(b.get_next_msg_id(), uint16_t{1});
  EXPECT_NE(a.get_next_msg_id(), b.get_next_msg_id());
}

// ---------------------------------------------------------------------------
// TWT queue tests
// ---------------------------------------------------------------------------

#ifdef USE_WIFI_TWT

struct TwtTestNet : CoapServerNet {
  void init_resources(size_t n) {
    resources_.init(n);
    size_t size = build_link_format(nullptr, 0);
    link_format_buf_ = std::make_unique<uint8_t[]>(size);
    build_link_format(link_format_buf_.get(), size);
    link_format_size_ = size;
  }

  void inject(const uint8_t *buf, size_t len, const struct sockaddr_in6 &peer) {
    process_datagram_(buf, len, &peer);
  }

  void set_queuing(bool v) { this->twt_queuing_enabled_ = v; }
  bool is_queuing() const { return this->twt_queuing_enabled_; }
  uint8_t queue_size() const { return this->twt_queue_.size(); }
  bool queue_empty() const { return this->twt_queue_.empty(); }
  uint16_t queue_front_peer_port() const { return this->twt_queue_.front().peer.sin6_port; }
  void do_flush() { this->flush_twt_queue_(); }
};

TEST(CoapServerNetTwt, NotQueuedByDefault) {
  TwtTestNet srv;
  srv.init_resources(4);

  auto raw = make_coap_get("ping");
  srv.inject(raw.data(), raw.size(), make_peer());

  EXPECT_TRUE(srv.queue_empty());
}

TEST(CoapServerNetTwt, QueuedWhenEnabled) {
  TwtTestNet srv;
  srv.init_resources(4);
  srv.set_queuing(true);

  auto raw = make_coap_get("ping");
  srv.inject(raw.data(), raw.size(), make_peer());

  EXPECT_GT(srv.queue_size(), 0u);
}

TEST(CoapServerNetTwt, FlushClearsQueue) {
  TwtTestNet srv;
  srv.init_resources(4);
  srv.set_queuing(true);

  auto raw = make_coap_get("ping");
  srv.inject(raw.data(), raw.size(), make_peer());
  EXPECT_GT(srv.queue_size(), 0u);

  srv.do_flush();

  EXPECT_TRUE(srv.queue_empty());
}

TEST(CoapServerNetTwt, FlushDisablesQueuing) {
  // flush_twt_queue_() sets twt_queuing_enabled_=false before draining.
  // Only fires when queue is non-empty; empty queue is a no-op.
  TwtTestNet srv;
  srv.init_resources(4);
  srv.set_queuing(true);

  auto raw = make_coap_get("ping");
  srv.inject(raw.data(), raw.size(), make_peer());
  EXPECT_GT(srv.queue_size(), 0u);

  srv.do_flush();

  EXPECT_FALSE(srv.is_queuing());
}

TEST(CoapServerNetTwt, DropOldestWhenFull) {
  // Inject 9 packets into a depth-8 queue — the oldest should be dropped.
  TwtTestNet srv;
  srv.init_resources(4);
  srv.set_queuing(true);

  for (int i = 0; i < 9; i++) {
    auto raw = make_coap_get("ping", static_cast<uint16_t>(i + 1));
    srv.inject(raw.data(), raw.size(), make_peer());
  }

  EXPECT_EQ(srv.queue_size(), 8u);
}

TEST(CoapServerNetTwt, QueuePreservesDestinationPeer) {
  TwtTestNet srv;
  srv.init_resources(4);
  srv.set_queuing(true);

  auto peer_a = make_peer(10001);
  auto peer_b = make_peer(10002);

  auto raw_a = make_coap_get("ping", 1);
  srv.inject(raw_a.data(), raw_a.size(), peer_a);

  auto raw_b = make_coap_get("ping", 2);
  srv.inject(raw_b.data(), raw_b.size(), peer_b);

  EXPECT_EQ(srv.queue_size(), 2u);
  EXPECT_EQ(srv.queue_front_peer_port(), peer_a.sin6_port);
}

#endif  // USE_WIFI_TWT

}  // namespace esphome::coap_server
