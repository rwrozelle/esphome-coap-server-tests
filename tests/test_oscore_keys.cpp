// Tests for OSCORE key derivation using real HKDF (PSA mock backed by OpenSSL).
// Test vectors from RFC 8613 Appendix C.1.1 (Master Secret=8 bytes, no salt,
// Sender ID=h'', Recipient ID=h'01', no ID Context).

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "esphome/components/coap_server/coap_server.h"
#include "nvs.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace esphome::coap_server {

// Expose protected oscore_derive_keys_ and internal key buffers
class TestableCoapServer : public CoapServer {
 public:
  void on_entity_update(EntityBase *) override {}
  bool derive_keys() { return oscore_derive_keys_(); }
  const uint8_t *sender_key() const { return oscore_sender_key_; }
  const uint8_t *recipient_key() const { return oscore_recipient_key_; }
  const uint8_t *common_iv() const { return oscore_common_iv_; }
  static constexpr size_t KEY_LEN = OSCORE_KEY_LEN;
  static constexpr size_t IV_LEN = OSCORE_IV_LEN;

  void set_material(std::vector<uint8_t> secret, std::vector<uint8_t> salt,
                    std::vector<uint8_t> sender_id, std::vector<uint8_t> recipient_id,
                    std::vector<uint8_t> id_context = {}) {
    oscore_master_secret_ = std::move(secret);
    oscore_master_salt_ = std::move(salt);
    oscore_sender_id_ = std::move(sender_id);
    oscore_recipient_id_ = std::move(recipient_id);
    oscore_id_context_ = std::move(id_context);
  }

  // Accessors for key material verification (used after derivation)
  bool key_material_cleared() const {
    return oscore_master_secret_.empty() && oscore_master_salt_.empty() &&
           oscore_sender_id_.empty() && oscore_recipient_id_.empty();
  }
};

// RFC 8613 Appendix C.1.1 test vectors
// Master Secret: 0x0102030405060708090a0b0c0d0e0f10 (16 bytes)
// Master Salt: 0x9e7ca92223786340 (8 bytes)
// Sender ID: 0x (empty)
// Recipient ID: 0x01
// ID Context: 0x (empty)

static const uint8_t kSecret[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                   0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
static const uint8_t kSalt[] = {0x9e, 0x7c, 0xa9, 0x22, 0x23, 0x78, 0x63, 0x40};
// Sender Key (Client→Server): f0910ed7295e6ad4b54fc793154302ff
static const uint8_t kExpectedSenderKey[] = {0xf0, 0x91, 0x0e, 0xd7, 0x29, 0x5e, 0x6a, 0xd4,
                                              0xb5, 0x4f, 0xc7, 0x93, 0x15, 0x43, 0x02, 0xff};
// Recipient Key (Server→Client): ffb14e093c94c9cac9471648b4f98710
static const uint8_t kExpectedRecipientKey[] = {0xff, 0xb1, 0x4e, 0x09, 0x3c, 0x94, 0xc9, 0xca,
                                                  0xc9, 0x47, 0x16, 0x48, 0xb4, 0xf9, 0x87, 0x10};
// Common IV: 4622d4dd6d944168eefb54987c
static const uint8_t kExpectedCommonIv[] = {0x46, 0x22, 0xd4, 0xdd, 0x6d, 0x94, 0x41, 0x68,
                                             0xee, 0xfb, 0x54, 0x98, 0x7c};

class OscoreKeyDerivationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // NVS calls happen in setup() not derive_keys(), so no NVS expectations needed here.
  }

  TestableCoapServer srv_;
};

TEST_F(OscoreKeyDerivationTest, SenderKeyMatchesRfc8613C1_1) {
  srv_.set_material(
      {kSecret, kSecret + sizeof(kSecret)},
      {kSalt, kSalt + sizeof(kSalt)},
      {},        // sender_id = empty
      {0x01});   // recipient_id = {0x01}

  ASSERT_TRUE(srv_.derive_keys());
  EXPECT_EQ(memcmp(srv_.sender_key(), kExpectedSenderKey, TestableCoapServer::KEY_LEN), 0)
      << "Sender key does not match RFC 8613 C.1.1";
}

TEST_F(OscoreKeyDerivationTest, RecipientKeyMatchesRfc8613C1_1) {
  srv_.set_material(
      {kSecret, kSecret + sizeof(kSecret)},
      {kSalt, kSalt + sizeof(kSalt)},
      {},
      {0x01});

  ASSERT_TRUE(srv_.derive_keys());
  EXPECT_EQ(memcmp(srv_.recipient_key(), kExpectedRecipientKey, TestableCoapServer::KEY_LEN), 0)
      << "Recipient key does not match RFC 8613 C.1.1";
}

TEST_F(OscoreKeyDerivationTest, CommonIvMatchesRfc8613C1_1) {
  srv_.set_material(
      {kSecret, kSecret + sizeof(kSecret)},
      {kSalt, kSalt + sizeof(kSalt)},
      {},
      {0x01});

  ASSERT_TRUE(srv_.derive_keys());
  EXPECT_EQ(memcmp(srv_.common_iv(), kExpectedCommonIv, TestableCoapServer::IV_LEN), 0)
      << "Common IV does not match RFC 8613 C.1.1";
}

TEST_F(OscoreKeyDerivationTest, DifferentSenderRecipientIdsProduceDifferentKeys) {
  srv_.set_material(
      {kSecret, kSecret + sizeof(kSecret)},
      {kSalt, kSalt + sizeof(kSalt)},
      {0x01},  // swap: sender=01
      {});     // recipient=empty

  ASSERT_TRUE(srv_.derive_keys());
  // Sender key should now match what was previously the recipient key (they're symmetric)
  EXPECT_EQ(memcmp(srv_.sender_key(), kExpectedRecipientKey, TestableCoapServer::KEY_LEN), 0);
  EXPECT_EQ(memcmp(srv_.recipient_key(), kExpectedSenderKey, TestableCoapServer::KEY_LEN), 0);
}

TEST_F(OscoreKeyDerivationTest, KeyMaterialClearedAfterDerivation) {
  srv_.set_material(
      {kSecret, kSecret + sizeof(kSecret)},
      {kSalt, kSalt + sizeof(kSalt)},
      {},
      {0x01});

  ASSERT_TRUE(srv_.derive_keys());
  // Key material should be cleared after derivation for security
  EXPECT_TRUE(srv_.key_material_cleared());
}

}  // namespace esphome::coap_server
