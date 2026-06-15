// Tests for .well-known/core link-format generation.
// Since the refactor to build_link_format() + link_format_buf_ cache, the
// link-format payload is built once in setup() from App entity lists and served
// directly from the cache.  Tests that need to inspect payload content call
// build_link_format() via the TestableCoapServer helper rather than routing
// through handle_well_known_core.

#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>

#include "esphome/components/coap_server/coap_server.h"
#include "esphome/core/application.h"
#include "esphome/components/number/number.h"
#include "mock_ot_helpers.h"

namespace esphome::coap_server {

// Concrete Switch — write_state is pure virtual in mock
struct TestSwitch : switch_::Switch {
  void write_state(bool s) override { state = s; }
};

// Concrete Number — control() is pure virtual in mock
struct TestNumber : number::Number {
  void control(float v) override { state = v; }
};

// Concrete Valve — get_traits() is pure virtual in mock
struct TestValve : valve::Valve {
  valve::ValveTraits get_traits() override { return {}; }
  void control(const valve::ValveCall &) override {}
};

class TestableCoapServer : public CoapServerOT {
 public:
  void init_for_link_test() {
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(64);
    resources_.push_back(ehCoapResource());
    ehCoapResource *wk = &resources_[0];
    wk->server = this;
    wk->mUriPath = ".well-known/core";
    wk->mHandler = &CoapServerOT::handle_well_known_core;
    wk->mContext = wk;
    wk->oscore_exempt = true;
  }

  // Adds entity to both App entity lists (read by build_link_format()) and
  // resources_ (used by entity request handlers).
  void add_entity(EntityType type, EntityBase *entity, bool observable = true) {
    add_coap_resource_(type, entity, observable, senml_idx_);
    switch (type) {
      case ENTITYTYPE_SENSOR:
        App.get_sensors().push_back(static_cast<sensor::Sensor *>(entity));
        break;
      case ENTITYTYPE_SWITCH:
        App.get_switches().push_back(static_cast<switch_::Switch *>(entity));
        break;
      case ENTITYTYPE_BINARY_SENSOR:
        App.get_binary_sensors().push_back(static_cast<binary_sensor::BinarySensor *>(entity));
        break;
      case ENTITYTYPE_TEXT_SENSOR:
        App.get_text_sensors().push_back(static_cast<text_sensor::TextSensor *>(entity));
        break;
      case ENTITYTYPE_NUMBER:
        App.get_numbers().push_back(static_cast<number::Number *>(entity));
        break;
      case ENTITYTYPE_LOCK:
        App.get_locks().push_back(static_cast<lock::Lock *>(entity));
        break;
      case ENTITYTYPE_VALVE:
        App.get_valves().push_back(static_cast<valve::Valve *>(entity));
        break;
      case ENTITYTYPE_BUTTON:
        App.get_buttons().push_back(static_cast<button::Button *>(entity));
        break;
      default:
        break;
    }
  }

  // Builds link_format_buf_ from App entity lists (as setup() would do) and
  // returns the payload as a string for assertion.
  std::string get_link_format() {
    size_t size = build_link_format(nullptr, 0);
    link_format_buf_ = std::make_unique<uint8_t[]>(size);
    build_link_format(link_format_buf_.get(), size);
    link_format_size_ = size;
    return std::string(reinterpret_cast<const char *>(link_format_buf_.get()), link_format_size_);
  }

  // Returns the true link-format size (passes nullptr so no bytes are written).
  size_t compute_link_format_size() { return build_link_format(nullptr, 0); }
  size_t compute_link_format_fill(uint8_t *buf, size_t len) { return build_link_format(buf, len); }

  // Call format_link_entry with the given observable flag for unit testing.
  // Constructs a LinkFormatResource internally to stay within the protected access context.
  static uint16_t format_entry_for_test(char *buf, size_t len, const char *path, const char *domain,
                                        EntityBase *entity, EntityType type, bool observable) {
    LinkFormatResource res{path, domain, entity, type, ACTIONTYPE_NO_ACTION, observable, 0};
    return format_link_entry(buf, len, res, false);
  }

 private:
  uint16_t senml_idx_{1};
};

// ── fixture ────────────────────────────────────────────────────────────────

class LinkFormatTest : public ::testing::Test {
 protected:
  void SetUp() override { App.reset_entities(); }
  void TearDown() override { App.reset_entities(); }
};

// ── tests ──────────────────────────────────────────────────────────────────

TEST_F(LinkFormatTest, PingAlwaysPresent) {
  TestableCoapServer srv;
  srv.init_for_link_test();

  auto lf = srv.get_link_format();

  EXPECT_NE(lf.find("</ping>"), std::string::npos);
  EXPECT_NE(lf.find("esphome.ping"), std::string::npos);
}

TEST_F(LinkFormatTest, SensorEntryHasRtObsOidTitleUomDc) {
  TestableCoapServer srv;
  srv.init_for_link_test();
  sensor::Sensor s;
  s.set_name("temperature");
  s.set_object_id_hash(305419896u);  // 0x12345678
  s.set_unit_of_measurement("C");
  s.set_device_class("temperature");
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s, true);

  auto lf = srv.get_link_format();

  EXPECT_NE(lf.find("rt=\"esphome.sensor\""), std::string::npos);
  EXPECT_NE(lf.find("obs"), std::string::npos);
  EXPECT_NE(lf.find("title=\"temperature\""), std::string::npos);
  EXPECT_NE(lf.find("oid=305419896"), std::string::npos);
  EXPECT_NE(lf.find("uom=\"C\""), std::string::npos);
  EXPECT_NE(lf.find("dc=\"temperature\""), std::string::npos);
}

TEST_F(LinkFormatTest, SensorWithoutUomOmitsUomDc) {
  TestableCoapServer srv;
  srv.init_for_link_test();
  sensor::Sensor s;
  s.set_name("raw");
  s.set_object_id_hash(1u);
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s, true);

  auto lf = srv.get_link_format();

  EXPECT_EQ(lf.find("uom="), std::string::npos);
  EXPECT_EQ(lf.find("dc="), std::string::npos);
}

TEST_F(LinkFormatTest, SwitchHasStateAndToggleEntries) {
  TestableCoapServer srv;
  srv.init_for_link_test();
  TestSwitch sw;
  sw.set_name("light");
  sw.set_object_id_hash(99u);
  srv.add_entity(EntityType::ENTITYTYPE_SWITCH, &sw, true);

  auto lf = srv.get_link_format();

  EXPECT_NE(lf.find("rt=\"esphome.switch\""), std::string::npos);
  EXPECT_NE(lf.find("rt=\"esphome.action\""), std::string::npos);
  EXPECT_NE(lf.find("title=\"toggle\""), std::string::npos);
}

TEST_F(LinkFormatTest, ValveHasStateAndStopEntries) {
  TestableCoapServer srv;
  srv.init_for_link_test();
  TestValve v;
  v.set_name("water");
  v.set_object_id_hash(77u);
  srv.add_entity(EntityType::ENTITYTYPE_VALVE, &v, true);

  auto lf = srv.get_link_format();

  EXPECT_NE(lf.find("rt=\"esphome.valve\""), std::string::npos);
  EXPECT_NE(lf.find("title=\"stop\""), std::string::npos);
}

TEST_F(LinkFormatTest, InfoResourceAlwaysPresentWithCtFormat60) {
  TestableCoapServer srv;
  srv.init_for_link_test();

  // /info is always the first entry in build_link_format(); no explicit
  // add_info_resource() call needed.
  auto lf = srv.get_link_format();

  EXPECT_NE(lf.find("ct=60"), std::string::npos);
  EXPECT_NE(lf.find("esphome.device"), std::string::npos);
}

TEST_F(LinkFormatTest, OidMatchesEntityHash) {
  TestableCoapServer srv;
  srv.init_for_link_test();
  sensor::Sensor s;
  s.set_name("x");
  s.set_object_id_hash(42u);
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s, true);

  auto lf = srv.get_link_format();

  EXPECT_NE(lf.find("oid=42"), std::string::npos);
}

TEST_F(LinkFormatTest, MultipleEntriesSeparatedByComma) {
  TestableCoapServer srv;
  srv.init_for_link_test();
  sensor::Sensor s1, s2;
  s1.set_name("a");
  s1.set_object_id_hash(1u);
  s2.set_name("b");
  s2.set_object_id_hash(2u);
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s1, true);
  srv.add_entity(EntityType::ENTITYTYPE_SENSOR, &s2, true);

  auto lf = srv.get_link_format();

  size_t first = lf.find("</fp/");
  ASSERT_NE(first, std::string::npos);
  size_t second = lf.find("</fp/", first + 1);
  ASSERT_NE(second, std::string::npos);
  EXPECT_EQ(lf[second - 1], ',');
}

TEST_F(LinkFormatTest, NonObservableEntryUsesIfA) {
  // format_link_entry() should write if="if.a" and omit ;obs when observable=false.
  // Test this directly so the assertion is decoupled from whether build_link_format
  // hardcodes observable for a specific entity type.
  sensor::Sensor s;
  s.set_name("noobs");
  s.set_object_id_hash(5u);
  char buf[256];
  uint16_t len =
      TestableCoapServer::format_entry_for_test(buf, sizeof(buf), "fp/1/g/1", "sensor", &s, ENTITYTYPE_SENSOR, false);
  std::string entry(buf, len);

  EXPECT_NE(entry.find("if=\"if.a\""), std::string::npos);
  EXPECT_EQ(entry.find(";obs"), std::string::npos);
}

// Verify that build_link_format() returns the true payload size (no truncation)
// even for large entity counts — block-wise transfer (RFC 7959) handles delivery.
TEST_F(LinkFormatTest, BuildLinkFormat_LargeEntityCount_ReturnsTrueSize) {
  // 20 sensors × ~77 B/entry ≈ 1540 B of entity entries.
  static constexpr int kSensorCount = 20;
  auto sensors = std::make_unique<sensor::Sensor[]>(kSensorCount);
  char names[kSensorCount][16];
  for (int i = 0; i < kSensorCount; i++) {
    snprintf(names[i], sizeof(names[i]), "sensor_%02d", i);
    sensors[i].set_name(names[i]);
    sensors[i].set_object_id_hash(static_cast<uint32_t>(i + 1));
    App.get_sensors().push_back(&sensors[i]);
  }

  TestableCoapServer srv;
  size_t size = srv.compute_link_format_size();

  // nullptr-pass (size measurement) and full-fill pass must agree.
  auto buf = std::make_unique<uint8_t[]>(size);
  size_t filled = srv.compute_link_format_fill(buf.get(), size);
  EXPECT_EQ(size, filled);
  EXPECT_GT(size, 1024u) << "Expected " << kSensorCount << " sensors to exceed one block (" << size << " B)";
}

// ---------------------------------------------------------------------------
// Number entity min/max/step in link-format
// ---------------------------------------------------------------------------

TEST_F(LinkFormatTest, NumberEntry_HasMinMaxStep) {
  TestNumber n;
  n.set_name("brightness");
  n.set_object_id_hash(42);
  n.traits.set_min_value(-10.0f);
  n.traits.set_max_value(255.0f);
  n.traits.set_step(0.5f);
  App.get_numbers().push_back(&n);

  TestableCoapServer srv;
  srv.init_for_link_test();
  std::string lf = srv.get_link_format();

  EXPECT_NE(lf.find(";min=-10"), std::string::npos)  << lf;
  EXPECT_NE(lf.find(";max=255"), std::string::npos)  << lf;
  EXPECT_NE(lf.find(";step=0.5"), std::string::npos) << lf;
}

TEST_F(LinkFormatTest, NumberEntry_NaN_Omitted) {
  TestNumber n;
  n.set_name("setpoint");
  n.set_object_id_hash(43);
  // traits default to NaN — not set
  App.get_numbers().push_back(&n);

  TestableCoapServer srv;
  srv.init_for_link_test();
  std::string lf = srv.get_link_format();

  EXPECT_EQ(lf.find(";min="), std::string::npos)  << lf;
  EXPECT_EQ(lf.find(";max="), std::string::npos)  << lf;
  EXPECT_EQ(lf.find(";step="), std::string::npos) << lf;
}

TEST_F(LinkFormatTest, NumberEntry_PartialRange_OnlyStepSet) {
  TestNumber n;
  n.set_name("speed");
  n.set_object_id_hash(44);
  n.traits.set_step(1.0f);
  App.get_numbers().push_back(&n);

  TestableCoapServer srv;
  srv.init_for_link_test();
  std::string lf = srv.get_link_format();

  EXPECT_EQ(lf.find(";min="), std::string::npos)   << lf;
  EXPECT_EQ(lf.find(";max="), std::string::npos)   << lf;
  EXPECT_NE(lf.find(";step=1"), std::string::npos) << lf;
}

}  // namespace esphome::coap_server
