// Tests for the log CBOR encoding format produced by on_log / flush_logs_.
// Parses the log buffer using raw byte helpers rather than the CBOR decoder
// so the tests remain independent of CBOR decoder mock completeness.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "esphome/components/coap_server/coap_server.h"

namespace esphome::coap_server {

// ---------------------------------------------------------------------------
// Minimal raw CBOR read helpers — no dependency on cbor.h decoder
// ---------------------------------------------------------------------------

// Read a CBOR unsigned integer at buf[pos], advance pos.  Returns false on overflow.
static bool read_cbor_uint(const uint8_t *buf, size_t len, size_t &pos, uint64_t &out) {
  if (pos >= len) return false;
  uint8_t b = buf[pos++];
  uint8_t info = b & 0x1fu;
  if (info <= 23u) { out = info; return true; }
  if (info == 0x18u && pos < len) { out = buf[pos++]; return true; }
  if (info == 0x19u && pos + 1 < len) {
    out = (static_cast<uint64_t>(buf[pos]) << 8) | buf[pos + 1];
    pos += 2;
    return true;
  }
  if (info == 0x1au && pos + 3 < len) {
    out = (static_cast<uint64_t>(buf[pos]) << 24) | (static_cast<uint64_t>(buf[pos + 1]) << 16) |
          (static_cast<uint64_t>(buf[pos + 2]) << 8) | buf[pos + 3];
    pos += 4;
    return true;
  }
  return false;
}

// Read a CBOR text string at buf[pos] into out, advance pos.
static bool read_cbor_text(const uint8_t *buf, size_t len, size_t &pos, std::string &out) {
  if (pos >= len) return false;
  uint8_t major = buf[pos] >> 5u;
  if (major != 3u) return false;
  uint64_t str_len = 0;
  if (!read_cbor_uint(buf, len, pos, str_len)) return false;
  if (pos + str_len > len) return false;
  out.assign(reinterpret_cast<const char *>(buf + pos), static_cast<size_t>(str_len));
  pos += static_cast<size_t>(str_len);
  return true;
}

struct LogEntry {
  uint64_t millis;
  uint64_t level;
  std::string tag;
  std::string message;
};

// Parse the log buffer produced by on_log.
// Format: 0x9F [0x84 millis level tag message]* 0xFF
static std::vector<LogEntry> parse_log_buffer(const uint8_t *buf, size_t len) {
  std::vector<LogEntry> entries;
  size_t pos = 0;
  if (pos >= len || buf[pos++] != 0x9Fu) return entries;  // must start with indefinite array
  while (pos < len && buf[pos] != 0xFFu) {
    if (buf[pos++] != 0x84u) break;  // each entry is a 4-element array
    LogEntry e{};
    if (!read_cbor_uint(buf, len, pos, e.millis)) break;
    if (!read_cbor_uint(buf, len, pos, e.level)) break;
    if (!read_cbor_text(buf, len, pos, e.tag)) break;
    if (!read_cbor_text(buf, len, pos, e.message)) break;
    entries.push_back(e);
  }
  return entries;
}

// ---------------------------------------------------------------------------
// TestableCoapServer — expose on_log and the log buffer internals
// ---------------------------------------------------------------------------

class TestableCoapServer : public CoapServerOT {
 public:
  void init_for_log_test() {
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(4);

    // Minimal log resource (on_log checks active_observers_ for this resource)
    resources_.push_back(ehCoapResource());
    logs_resource_ = &resources_[0];
    logs_resource_->server = this;

    // Register a fake observer pointing at the log resource so on_log proceeds
    fake_obs_.resource = logs_resource_;
    active_observers_ = &fake_obs_;

    log_buf_[0] = 0x9Fu;
    log_buf_pos_ = 1;
    log_buf_has_data_ = false;
  }

  void call_on_log(uint8_t level, const char *tag, const char *msg) {
    on_log(level, tag, msg, strlen(msg));
  }

  const uint8_t *log_buffer() const { return log_buf_; }
  size_t log_buffer_pos() const { return log_buf_pos_; }
  bool has_log_data() const { return log_buf_has_data_; }
  void clear_observers() { active_observers_ = nullptr; }

 private:
  ehCoapObserver fake_obs_{};
};

// ── tests ──────────────────────────────────────────────────────────────────

TEST(LogCbor, BufferStartsWith9F) {
  TestableCoapServer srv;
  srv.init_for_log_test();

  EXPECT_EQ(srv.log_buffer()[0], 0x9Fu);
}

TEST(LogCbor, HasDataAfterOnLog) {
  TestableCoapServer srv;
  srv.init_for_log_test();

  EXPECT_FALSE(srv.has_log_data());
  srv.call_on_log(3, "test", "hello");
  EXPECT_TRUE(srv.has_log_data());
}

TEST(LogCbor, EntryIs4ElementArray) {
  TestableCoapServer srv;
  srv.init_for_log_test();
  srv.call_on_log(3, "mytag", "mymsg");

  // The first byte after the 0x9F start is the array header for the first entry
  EXPECT_EQ(srv.log_buffer()[1], 0x84u);  // CBOR array(4)
}

TEST(LogCbor, LevelIsEncodedAtIndex1) {
  TestableCoapServer srv;
  srv.init_for_log_test();
  srv.call_on_log(5, "tag", "msg");

  auto entries = parse_log_buffer(srv.log_buffer(), srv.log_buffer_pos());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].level, 5u);
}

TEST(LogCbor, TagIsEncodedAtIndex2) {
  TestableCoapServer srv;
  srv.init_for_log_test();
  srv.call_on_log(2, "mycomponent", "msg");

  auto entries = parse_log_buffer(srv.log_buffer(), srv.log_buffer_pos());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].tag, "mycomponent");
}

TEST(LogCbor, MessageIsEncodedAtIndex3) {
  TestableCoapServer srv;
  srv.init_for_log_test();
  srv.call_on_log(1, "tag", "hello world");

  auto entries = parse_log_buffer(srv.log_buffer(), srv.log_buffer_pos());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].message, "hello world");
}

TEST(LogCbor, MultipleEntriesAppended) {
  TestableCoapServer srv;
  srv.init_for_log_test();
  srv.call_on_log(1, "t1", "first");
  srv.call_on_log(2, "t2", "second");
  srv.call_on_log(3, "t3", "third");

  auto entries = parse_log_buffer(srv.log_buffer(), srv.log_buffer_pos());
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].message, "first");
  EXPECT_EQ(entries[1].message, "second");
  EXPECT_EQ(entries[2].message, "third");
}

TEST(LogCbor, NoObserverSkipsEntry) {
  TestableCoapServer srv;
  srv.init_for_log_test();

  // Remove the fake observer so on_log bails early
  srv.clear_observers();

  srv.call_on_log(1, "tag", "ignored");

  EXPECT_FALSE(srv.has_log_data());
  EXPECT_EQ(srv.log_buffer_pos(), 1u);  // only the 0x9F start byte
}

TEST(LogCbor, NullTagEncodesAsEmptyString) {
  TestableCoapServer srv;
  srv.init_for_log_test();
  srv.call_on_log(1, nullptr, "msg");

  auto entries = parse_log_buffer(srv.log_buffer(), srv.log_buffer_pos());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].tag, "");
}

}  // namespace esphome::coap_server
