// Tests for .well-known/core Block2 handling in the Net transport (RFC 7959).
// Uses the TestableCoapServerNet inject/capture pattern from test_net_transport.cpp.

#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "esphome/components/coap_server/coap_server.h"
#include "esphome/core/application.h"

namespace esphome::coap_server {

using CoapPacket = CoapServerNet::CoapPacket;

// ---------------------------------------------------------------------------
// Minimal testable subclass — only what we need for block-wise tests
// ---------------------------------------------------------------------------

class TestableNetBW : public CoapServerNet {
 public:
  void set_link_format(const std::string &content) {
    link_format_buf_ = std::make_unique<uint8_t[]>(content.size());
    memcpy(link_format_buf_.get(), content.data(), content.size());
    link_format_size_ = content.size();
    resources_.init(4);
  }

  void inject(const uint8_t *buf, size_t len) {
    sockaddr_storage peer{};
    auto &p = reinterpret_cast<sockaddr_in6 &>(peer);
    p.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &p.sin6_addr);
    process_datagram_(buf, len, &peer);
  }

  // Expose parse_coap for unit testing option parsing
  static bool parse(const uint8_t *buf, size_t len, CoapPacket *pkt) {
    return parse_coap(buf, len, pkt);
  }

  size_t get_link_format_size() const { return link_format_size_; }

  std::vector<uint8_t> last_sent;

 protected:
  void send_response(const uint8_t *buf, size_t len, const sockaddr_storage *) override {
    last_sent.assign(buf, buf + len);
  }
};

// ---------------------------------------------------------------------------
// Packet builder helpers
// ---------------------------------------------------------------------------

// Encode a CoAP option value as minimum bytes (big-endian)
static std::vector<uint8_t> encode_uint(uint32_t val) {
  std::vector<uint8_t> v;
  if (val > 0xFFFF) v.push_back((uint8_t)(val >> 16));
  if (val > 0xFF)   v.push_back((uint8_t)(val >> 8));
  v.push_back((uint8_t)(val & 0xFF));
  return v;
}

// Append a CoAP option header (delta/length byte + ext bytes)
static void append_option_header(std::vector<uint8_t> &pkt, uint16_t delta, uint16_t vlen) {
  uint8_t d4, l4;
  uint8_t ext[4];
  uint8_t ext_len = 0;
  if (delta < 13) {
    d4 = (uint8_t)delta;
  } else if (delta < 269) {
    d4 = 13;
    ext[ext_len++] = (uint8_t)(delta - 13);
  } else {
    d4 = 14;
    uint16_t v = (uint16_t)(delta - 269);
    ext[ext_len++] = (uint8_t)(v >> 8);
    ext[ext_len++] = (uint8_t)v;
  }
  if (vlen < 13) {
    l4 = (uint8_t)vlen;
  } else if (vlen < 269) {
    l4 = 13;
    ext[ext_len++] = (uint8_t)(vlen - 13);
  } else {
    l4 = 14;
    uint16_t v = (uint16_t)(vlen - 269);
    ext[ext_len++] = (uint8_t)(v >> 8);
    ext[ext_len++] = (uint8_t)v;
  }
  pkt.push_back((uint8_t)((d4 << 4) | l4));
  for (uint8_t i = 0; i < ext_len; i++) pkt.push_back(ext[i]);
}

// Build a NON GET for .well-known/core, optionally with a Block2 option.
// block2_szx=-1 means no Block2 option.
static std::vector<uint8_t> make_wk_get(int block2_num = -1, int block2_szx = -1) {
  std::vector<uint8_t> pkt;
  pkt.push_back(0x51);  // VER=1, T=NON(1), TKL=1
  pkt.push_back(0x01);  // GET
  pkt.push_back(0x00);
  pkt.push_back(0x01);  // msg_id=1
  pkt.push_back(0xAB);  // token

  // Uri-Path: ".well-known" (opt 11, delta=11, len=11)
  uint16_t last_opt = 0;
  const char *seg1 = ".well-known";
  uint16_t len1 = (uint16_t)strlen(seg1);
  append_option_header(pkt, 11 - last_opt, len1);
  last_opt = 11;
  for (size_t i = 0; i < len1; i++) pkt.push_back((uint8_t)seg1[i]);

  // Uri-Path: "core" (delta=0 from same option number)
  const char *seg2 = "core";
  uint16_t len2 = (uint16_t)strlen(seg2);
  append_option_header(pkt, 0, len2);
  for (size_t i = 0; i < len2; i++) pkt.push_back((uint8_t)seg2[i]);

  // Block2 option (23), delta from 11 = 12
  if (block2_num >= 0 && block2_szx >= 0) {
    uint32_t val = ((uint32_t)block2_num << 4) | ((uint32_t)block2_szx & 0x07);
    auto vbytes = encode_uint(val);
    // Handle val=0 case (block 0, szx 0): must encode as 1 byte
    if (vbytes.empty()) vbytes.push_back(0x00);
    append_option_header(pkt, 23 - last_opt, (uint16_t)vbytes.size());
    for (auto b : vbytes) pkt.push_back(b);
  }

  return pkt;
}

// Parse the Block2 option out of a response packet.
// Returns false if no Block2 option found.
static bool parse_response_block2(const std::vector<uint8_t> &resp,
                                  uint32_t *num, bool *more, uint8_t *szx) {
  if (resp.size() < 4) return false;
  uint8_t tkl = resp[0] & 0x0F;
  size_t pos = 4 + tkl;
  uint16_t opt_num = 0;
  while (pos < resp.size()) {
    uint8_t b = resp[pos++];
    if (b == 0xFF) break;
    uint16_t delta = (b >> 4) & 0x0F;
    uint16_t opt_len = b & 0x0F;
    if (delta == 13) delta = resp[pos++] + 13;
    else if (delta == 14) { delta = (uint16_t)(((resp[pos] << 8) | resp[pos+1]) + 269); pos += 2; }
    if (opt_len == 13) opt_len = resp[pos++] + 13;
    else if (opt_len == 14) { opt_len = (uint16_t)(((resp[pos] << 8) | resp[pos+1]) + 269); pos += 2; }
    opt_num += delta;
    if (opt_num == 23 && opt_len <= 3) {
      uint32_t val = 0;
      for (uint16_t i = 0; i < opt_len; i++) val = (val << 8) | resp[pos + i];
      *szx = (uint8_t)(val & 0x07);
      *more = (val >> 3) & 1;
      *num = val >> 4;
      return true;
    }
    pos += opt_len;
  }
  return false;
}

// Return the payload bytes from a CoAP response (after 0xFF marker)
static std::string response_payload(const std::vector<uint8_t> &resp) {
  uint8_t tkl = resp.size() >= 1 ? (resp[0] & 0x0F) : 0;
  size_t pos = 4 + tkl;
  uint16_t opt_num = 0;
  while (pos < resp.size()) {
    uint8_t b = resp[pos++];
    if (b == 0xFF) return std::string(resp.begin() + pos, resp.end());
    uint16_t delta = (b >> 4) & 0x0F;
    uint16_t opt_len = b & 0x0F;
    if (delta == 13) delta = resp[pos++] + 13;
    else if (delta == 14) { delta = (uint16_t)(((resp[pos] << 8) | resp[pos+1]) + 269); pos += 2; }
    if (opt_len == 13) opt_len = resp[pos++] + 13;
    else if (opt_len == 14) { opt_len = (uint16_t)(((resp[pos] << 8) | resp[pos+1]) + 269); pos += 2; }
    opt_num += delta;
    pos += opt_len;
  }
  return {};
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class BlockwiseNetTest : public ::testing::Test {
 protected:
  void SetUp() override { App.reset_entities(); }
  void TearDown() override { App.reset_entities(); }
  TestableNetBW server;
};

// ---------------------------------------------------------------------------
// parse_coap: Block2 option parsing unit tests
// ---------------------------------------------------------------------------

TEST(BlockwiseNetParse, NoBlock2_FlagFalse) {
  auto pkt = make_wk_get();
  CoapPacket parsed{};
  ASSERT_TRUE(TestableNetBW::parse(pkt.data(), pkt.size(), &parsed));
  EXPECT_FALSE(parsed.block2_present);
}

TEST(BlockwiseNetParse, Block2_NUM0_SZX6) {
  // Block2 value = (0 << 4) | 6 = 0x06
  auto pkt = make_wk_get(0, 6);
  CoapPacket parsed{};
  ASSERT_TRUE(TestableNetBW::parse(pkt.data(), pkt.size(), &parsed));
  EXPECT_TRUE(parsed.block2_present);
  EXPECT_EQ(parsed.block2_num, 0u);
  EXPECT_EQ(parsed.block2_szx, 6u);
}

TEST(BlockwiseNetParse, Block2_NUM1_SZX6) {
  // Block2 value = (1 << 4) | 6 = 0x16
  auto pkt = make_wk_get(1, 6);
  CoapPacket parsed{};
  ASSERT_TRUE(TestableNetBW::parse(pkt.data(), pkt.size(), &parsed));
  EXPECT_TRUE(parsed.block2_present);
  EXPECT_EQ(parsed.block2_num, 1u);
  EXPECT_EQ(parsed.block2_szx, 6u);
}

TEST(BlockwiseNetParse, Block2_NUM16_TwoBytes) {
  // Block2 value = (16 << 4) | 6 = 0x106, needs 2 bytes
  auto pkt = make_wk_get(16, 6);
  CoapPacket parsed{};
  ASSERT_TRUE(TestableNetBW::parse(pkt.data(), pkt.size(), &parsed));
  EXPECT_TRUE(parsed.block2_present);
  EXPECT_EQ(parsed.block2_num, 16u);
  EXPECT_EQ(parsed.block2_szx, 6u);
}

TEST(BlockwiseNetParse, Block2_SZX4_SmallBlock) {
  // SZX=4 → 16<<4 = 256 bytes per block
  auto pkt = make_wk_get(0, 4);
  CoapPacket parsed{};
  ASSERT_TRUE(TestableNetBW::parse(pkt.data(), pkt.size(), &parsed));
  EXPECT_TRUE(parsed.block2_present);
  EXPECT_EQ(parsed.block2_szx, 4u);
}

// ---------------------------------------------------------------------------
// handle_well_known_core_ dispatch tests
// ---------------------------------------------------------------------------

TEST_F(BlockwiseNetTest, PlainGet_SmallPayload_Block2InResponse) {
  const std::string content = "</fp/1/g/1>;rt=\"sensor\"";
  server.set_link_format(content);

  auto pkt = make_wk_get();  // no Block2 in request
  server.inject(pkt.data(), pkt.size());

  ASSERT_FALSE(server.last_sent.empty());
  EXPECT_EQ(server.last_sent[1], 0x45);  // 2.05 Content

  uint32_t num; bool more; uint8_t szx;
  ASSERT_TRUE(parse_response_block2(server.last_sent, &num, &more, &szx));
  EXPECT_EQ(num, 0u);
  EXPECT_FALSE(more);  // fits in one block
  EXPECT_EQ(szx, 6u);  // default 1024-byte blocks

  EXPECT_EQ(response_payload(server.last_sent), content);
}

TEST_F(BlockwiseNetTest, PlainGet_LargePayload_OnlyFirstBlock) {
  // Build a payload > 1024 bytes (50 entries → ~1240 bytes)
  std::string content;
  content.reserve(1300);
  for (int i = 0; i < 50; i++) {
    if (i > 0) content += ',';
    content += "</fp/" + std::to_string(i + 1) + "/g/1>;rt=\"sensor\"";
  }
  server.set_link_format(content);

  auto pkt = make_wk_get();
  server.inject(pkt.data(), pkt.size());

  ASSERT_FALSE(server.last_sent.empty());
  EXPECT_EQ(server.last_sent[1], 0x45);

  uint32_t num; bool more; uint8_t szx;
  ASSERT_TRUE(parse_response_block2(server.last_sent, &num, &more, &szx));
  EXPECT_EQ(num, 0u);
  EXPECT_TRUE(more);   // more blocks follow

  std::string payload = response_payload(server.last_sent);
  EXPECT_EQ(payload.size(), 1024u);
  EXPECT_EQ(payload, content.substr(0, 1024));
}

TEST_F(BlockwiseNetTest, Block2Req_NUM1_ReturnsSecondBlock) {
  std::string content;
  content.reserve(1300);
  for (int i = 0; i < 50; i++) {
    if (i > 0) content += ',';
    content += "</fp/" + std::to_string(i + 1) + "/g/1>;rt=\"sensor\"";
  }
  server.set_link_format(content);

  auto pkt = make_wk_get(1, 6);  // Block2 NUM=1, SZX=6 (1024 bytes)
  server.inject(pkt.data(), pkt.size());

  ASSERT_FALSE(server.last_sent.empty());
  EXPECT_EQ(server.last_sent[1], 0x45);

  uint32_t num; bool more; uint8_t szx;
  ASSERT_TRUE(parse_response_block2(server.last_sent, &num, &more, &szx));
  EXPECT_EQ(num, 1u);
  EXPECT_FALSE(more);  // last block

  std::string payload = response_payload(server.last_sent);
  size_t remaining = content.size() - 1024;
  EXPECT_EQ(payload.size(), remaining);
  EXPECT_EQ(payload, content.substr(1024));
}

TEST_F(BlockwiseNetTest, Block2Req_OutOfRange_Returns400) {
  server.set_link_format("</fp/1/g/1>;rt=\"sensor\"");

  auto pkt = make_wk_get(99, 6);  // NUM=99, way beyond end
  server.inject(pkt.data(), pkt.size());

  ASSERT_FALSE(server.last_sent.empty());
  EXPECT_EQ(server.last_sent[1], 0x80);  // 4.00 Bad Request
}

TEST_F(BlockwiseNetTest, Block2Req_SZX4_HonorsSmallBlockSize) {
  // SZX=4 → 256-byte blocks; verify first 256 bytes returned
  const std::string content(600, 'x');
  server.set_link_format(content);

  auto pkt = make_wk_get(0, 4);  // SZX=4
  server.inject(pkt.data(), pkt.size());

  ASSERT_FALSE(server.last_sent.empty());
  uint32_t num; bool more; uint8_t szx;
  ASSERT_TRUE(parse_response_block2(server.last_sent, &num, &more, &szx));
  EXPECT_EQ(szx, 4u);
  EXPECT_TRUE(more);
  EXPECT_EQ(response_payload(server.last_sent).size(), 256u);
}

TEST_F(BlockwiseNetTest, NonGet_NoResponse) {
  server.set_link_format("</fp/1/g/1>;rt=\"sensor\"");

  // POST to .well-known/core
  uint8_t post[] = {0x51, 0x02, 0x00, 0x01, 0xAB,
                    0xBB, '.', 'w', 'e', 'l', 'l', '-', 'k', 'n', 'o', 'w', 'n',
                    0x04, 'c', 'o', 'r', 'e'};
  server.inject(post, sizeof(post));
  EXPECT_TRUE(server.last_sent.empty());
}

}  // namespace esphome::coap_server
