// Tests for OSCORE nonce construction and AAD building.
// These are pure-logic functions with no I/O, backed only by the CBOR mock.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

#include "esphome/components/coap_server/coap_server.h"

namespace esphome::coap_server {

// Expose protected static methods via subclass
class TestableCoapServer : public CoapServer {
 public:
  static void build_nonce(const uint8_t *piv, uint8_t piv_len, const uint8_t *kid, uint8_t kid_len,
                          const uint8_t *common_iv, uint8_t out[13]) {
    oscore_build_nonce_(piv, piv_len, kid, kid_len, common_iv, out);
  }

  static size_t build_aad(const uint8_t *kid, uint8_t kid_len, const uint8_t *piv, uint8_t piv_len, uint8_t *buf,
                          size_t buf_len) {
    return oscore_build_aad_(kid, kid_len, piv, piv_len, buf, buf_len);
  }
};

// ---------------------------------------------------------------------------
// oscore_build_nonce_
// ---------------------------------------------------------------------------

TEST(OscoreNonce, AllZerosGivesCommonIv) {
  // piv=[], kid=[] → input is all zeros → nonce = common_iv XOR 0 = common_iv
  const uint8_t common_iv[13] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                  0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd};
  uint8_t nonce[13]{};
  TestableCoapServer::build_nonce(nullptr, 0, nullptr, 0, common_iv, nonce);
  EXPECT_EQ(memcmp(nonce, common_iv, 13), 0);
}

TEST(OscoreNonce, PivRightAlignedInLastFiveBytes) {
  // PIV = {0x01} → placed in byte 12 (last byte, right-aligned in 5-byte field)
  const uint8_t common_iv[13] = {};
  const uint8_t piv[1] = {0x01};
  uint8_t nonce[13]{};
  TestableCoapServer::build_nonce(piv, 1, nullptr, 0, common_iv, nonce);
  // input[12] = piv[0] = 0x01; XOR 0 = 0x01
  EXPECT_EQ(nonce[12], 0x01u);
  // bytes 0-11 should be 0 (input) XOR 0 (iv) = 0
  for (int i = 0; i < 12; i++) EXPECT_EQ(nonce[i], 0u) << "byte " << i;
}

TEST(OscoreNonce, KidLenInFirstByte) {
  // kid = {0x02} (1-byte kid) → input[0] = kid_len = 1
  const uint8_t common_iv[13] = {};
  const uint8_t kid[1] = {0x02};
  uint8_t nonce[13]{};
  TestableCoapServer::build_nonce(nullptr, 0, kid, 1, common_iv, nonce);
  EXPECT_EQ(nonce[0], 1u);  // kid_len
  // kid right-aligned in bytes 1..7 (kid_field_len = 13-6 = 7)
  // kid_field_len=7, kid_len=1, so placed at index 1 + (7-1) = 7
  EXPECT_EQ(nonce[7], 0x02u);
}

TEST(OscoreNonce, XorWithCommonIv) {
  const uint8_t common_iv[13] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  const uint8_t piv[1] = {0x00};
  uint8_t nonce[13]{};
  TestableCoapServer::build_nonce(piv, 1, nullptr, 0, common_iv, nonce);
  // input[12] = 0x00; XOR 0xff = 0xff
  EXPECT_EQ(nonce[12], 0xffu);
  // All other bytes: input=0 XOR 0xff = 0xff
  for (int i = 0; i < 12; i++) EXPECT_EQ(nonce[i], 0xffu) << "byte " << i;
}

// ---------------------------------------------------------------------------
// oscore_build_aad_
// ---------------------------------------------------------------------------

TEST(OscoreAad, NonZeroOutput) {
  // Any valid parameters should produce a non-empty AAD
  const uint8_t kid[1] = {0x01};
  const uint8_t piv[1] = {0x00};
  uint8_t buf[64]{};
  size_t len = TestableCoapServer::build_aad(kid, 1, piv, 1, buf, sizeof(buf));
  EXPECT_GT(len, 0u);
}

TEST(OscoreAad, StartsWithCborArray3) {
  // Enc_Structure is a 3-element CBOR array: 0x83
  const uint8_t kid[1] = {0x01};
  const uint8_t piv[1] = {0x00};
  uint8_t buf[64]{};
  size_t len = TestableCoapServer::build_aad(kid, 1, piv, 1, buf, sizeof(buf));
  ASSERT_GT(len, 0u);
  EXPECT_EQ(buf[0], 0x83u) << "AAD must start with CBOR 3-element array";
}

TEST(OscoreAad, ContainsEncrypt0String) {
  // "Encrypt0" as a CBOR text string (8 chars → 0x68 + bytes)
  const uint8_t kid[1] = {0x01};
  const uint8_t piv[1] = {0x00};
  uint8_t buf[64]{};
  size_t len = TestableCoapServer::build_aad(kid, 1, piv, 1, buf, sizeof(buf));
  ASSERT_GE(len, 10u);
  // buf[1] = 0x68 (8-char text string), buf[2..9] = "Encrypt0"
  EXPECT_EQ(buf[1], 0x68u);
  EXPECT_EQ(memcmp(buf + 2, "Encrypt0", 8), 0);
}

TEST(OscoreAad, EmptyPivAllowed) {
  const uint8_t kid[1] = {0x01};
  uint8_t buf[64]{};
  size_t len = TestableCoapServer::build_aad(kid, 1, nullptr, 0, buf, sizeof(buf));
  EXPECT_GT(len, 0u);
}

TEST(OscoreAad, BufferTooSmallReturnsZero) {
  const uint8_t kid[1] = {0x01};
  const uint8_t piv[1] = {0x00};
  uint8_t buf[4]{};
  size_t len = TestableCoapServer::build_aad(kid, 1, piv, 1, buf, sizeof(buf));
  EXPECT_EQ(len, 0u);
}

}  // namespace esphome::coap_server
