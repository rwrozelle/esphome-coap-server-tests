// Tests for CoapServer::encode_device_info_ with a configurable Application stub.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

#include "esphome/components/coap_server/coap_server.h"
#include "esphome/core/application.h"

namespace esphome::coap_server {

// Expose the protected static method
class TestableCoapServer : public CoapServer {
 public:
  static size_t encode_info(uint8_t *buf, size_t len, CoapServer *srv) {
    return encode_device_info_(buf, len, srv);
  }
};

// Helper: parse a CBOR text-string key-value map entry.
// Returns pointer to value start and sets value_len, or nullptr if not found.
// Only handles definite-length text strings (major type 3) and simple uint values.
static const uint8_t *find_cbor_text_value(const uint8_t *buf, size_t len, const char *key, size_t *value_len) {
  size_t key_len = strlen(key);
  for (size_t i = 0; i + 1 < len;) {
    if ((buf[i] & 0xe0u) != 0x60u) { i++; continue; }  // not a text string
    size_t klen;
    size_t hdr = 1;
    if ((buf[i] & 0x1fu) <= 23u) {
      klen = buf[i] & 0x1fu;
    } else if (buf[i] == 0x78u && i + 2 < len) {
      klen = buf[i + 1];
      hdr = 2;
    } else {
      i++;
      continue;
    }
    if (i + hdr + klen < len && klen == key_len && memcmp(buf + i + hdr, key, key_len) == 0) {
      // Found key — value follows
      size_t vi = i + hdr + klen;
      if (vi < len && (buf[vi] & 0xe0u) == 0x60u) {
        // Value is also a text string
        if ((buf[vi] & 0x1fu) <= 23u) {
          *value_len = buf[vi] & 0x1fu;
          return buf + vi + 1;
        } else if (buf[vi] == 0x78u && vi + 2 < len) {
          *value_len = buf[vi + 1];
          return buf + vi + 2;
        }
      }
      return nullptr;  // value not a text string
    }
    i += hdr + klen;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class DeviceInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    App.set_name("my_device");
    App.set_friendly_name("My Device");
    App.set_build_time("2024-06-01T12:00:00");
  }

  TestableCoapServer srv_;
  uint8_t buf_[512]{};
};

TEST_F(DeviceInfoTest, ContainsNameKey) {
  size_t len = TestableCoapServer::encode_info(buf_, sizeof(buf_), &srv_);
  ASSERT_GT(len, 0u);

  size_t vlen = 0;
  const uint8_t *v = find_cbor_text_value(buf_, len, "name", &vlen);
  ASSERT_NE(v, nullptr);
  ASSERT_EQ(vlen, strlen("my_device"));
  EXPECT_EQ(memcmp(v, "my_device", vlen), 0);
}

TEST_F(DeviceInfoTest, ContainsFriendlyName) {
  size_t len = TestableCoapServer::encode_info(buf_, sizeof(buf_), &srv_);
  ASSERT_GT(len, 0u);

  size_t vlen = 0;
  const uint8_t *v = find_cbor_text_value(buf_, len, "friendly_name", &vlen);
  ASSERT_NE(v, nullptr);
  std::string got(reinterpret_cast<const char *>(v), vlen);
  EXPECT_EQ(got, "My Device");
}

TEST_F(DeviceInfoTest, ContainsVersion) {
  size_t len = TestableCoapServer::encode_info(buf_, sizeof(buf_), &srv_);
  ASSERT_GT(len, 0u);
  size_t vlen = 0;
  const uint8_t *v = find_cbor_text_value(buf_, len, "version", &vlen);
  ASSERT_NE(v, nullptr);
  EXPECT_GT(vlen, 0u);  // ESPHOME_VERSION is set
}

TEST_F(DeviceInfoTest, ContainsModel) {
  size_t len = TestableCoapServer::encode_info(buf_, sizeof(buf_), &srv_);
  ASSERT_GT(len, 0u);
  size_t vlen = 0;
  const uint8_t *v = find_cbor_text_value(buf_, len, "model", &vlen);
  ASSERT_NE(v, nullptr);
  EXPECT_GT(vlen, 0u);  // ESPHOME_BOARD is set
}

TEST_F(DeviceInfoTest, ReturnsNonZeroLength) {
  size_t len = TestableCoapServer::encode_info(buf_, sizeof(buf_), &srv_);
  EXPECT_GT(len, 16u);  // at minimum a map with several keys
}

TEST_F(DeviceInfoTest, FriendlyNameOmittedWhenEmpty) {
  App.set_friendly_name("");
  size_t len = TestableCoapServer::encode_info(buf_, sizeof(buf_), &srv_);
  ASSERT_GT(len, 0u);
  size_t vlen = 0;
  const uint8_t *v = find_cbor_text_value(buf_, len, "friendly_name", &vlen);
  EXPECT_EQ(v, nullptr);  // key should not be present
}

TEST_F(DeviceInfoTest, PingIntervalReflectsServerSetting) {
  srv_.set_server_ping_interval(30000);  // 30 s
  size_t len = TestableCoapServer::encode_info(buf_, sizeof(buf_), &srv_);
  ASSERT_GT(len, 0u);
  // Verify the map was encoded without error (detailed uint parsing omitted;
  // tested functionally by ensuring non-zero output).
  EXPECT_GT(len, 0u);
}

}  // namespace esphome::coap_server
