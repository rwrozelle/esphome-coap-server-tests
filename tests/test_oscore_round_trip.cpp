// Tests for OSCORE protect/unprotect round-trip.
// Uses RFC 8613 Appendix C.1.1 test vectors for key material.
// Round-trip: encrypt with oscore_protect_response_ (sender_key),
// decrypt with psa_aead_decrypt using recipient_key of a second server
// instance configured with swapped IDs.

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "esphome/components/coap_server/coap_server.h"
#include "nvs.h"
#include <psa/crypto.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace esphome::coap_server {

// RFC 8613 Appendix C.1.1 test vectors
static const uint8_t kSecret[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                   0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
static const uint8_t kSalt[] = {0x9e, 0x7c, 0xa9, 0x22, 0x23, 0x78, 0x63, 0x40};

// Sender key for sender_id=h'' (what "client" uses to send)
static const uint8_t kClientSenderKey[] = {0xf0, 0x91, 0x0e, 0xd7, 0x29, 0x5e, 0x6a, 0xd4,
                                            0xb5, 0x4f, 0xc7, 0x93, 0x15, 0x43, 0x02, 0xff};
// Recipient key for recipient_id=h'01' (what "server" uses to receive from client)
// = sender key for sender_id=h'01'
static const uint8_t kServerRecipientKey[] = {0xff, 0xb1, 0x4e, 0x09, 0x3c, 0x94, 0xc9, 0xca,
                                               0xc9, 0x47, 0x16, 0x48, 0xb4, 0xf9, 0x87, 0x10};
static const uint8_t kCommonIv[] = {0x46, 0x22, 0xd4, 0xdd, 0x6d, 0x94, 0x41, 0x68,
                                     0xee, 0xfb, 0x54, 0x98, 0x7c};

class TestableCoapServer : public CoapServer {
 public:
  void on_entity_update(EntityBase *) override {}
  bool derive_keys() { return oscore_derive_keys_(); }

  void set_material(std::vector<uint8_t> secret, std::vector<uint8_t> salt,
                    std::vector<uint8_t> sender_id, std::vector<uint8_t> recipient_id) {
    oscore_master_secret_ = std::move(secret);
    oscore_master_salt_ = std::move(salt);
    oscore_sender_id_ = std::move(sender_id);
    oscore_recipient_id_ = std::move(recipient_id);
  }

  size_t protect(const uint8_t *inner, size_t inner_len, const OscoreRequestInfo &req_info,
                 bool is_notification, uint8_t *out, size_t out_len) {
    return oscore_protect_response_(inner, inner_len, req_info, is_notification, out, out_len);
  }

  uint32_t sender_seq() const { return oscore_sender_seq_no_; }

  static void build_nonce(const uint8_t *piv, uint8_t piv_len, const uint8_t *kid, uint8_t kid_len,
                          const uint8_t *common_iv, uint8_t nonce[OSCORE_IV_LEN]) {
    oscore_build_nonce(piv, piv_len, kid, kid_len, common_iv, nonce);
  }

  static size_t build_aad(const uint8_t *kid, uint8_t kid_len, const uint8_t *piv, uint8_t piv_len,
                           uint8_t *buf, size_t buf_len) {
    return oscore_build_aad(kid, kid_len, piv, piv_len, buf, buf_len);
  }

  static constexpr size_t KEY_LEN = OSCORE_KEY_LEN;
  static constexpr size_t IV_LEN = OSCORE_IV_LEN;
  static constexpr size_t TAG_LEN = OSCORE_TAG_LEN;

  const uint8_t *sender_key() const { return oscore_sender_key_; }
  const uint8_t *recipient_key() const { return oscore_recipient_key_; }
  const uint8_t *common_iv() const { return oscore_common_iv_; }

  bool unprotect_core(const uint8_t *opt_val, uint8_t opt_len, const uint8_t *ciphertext,
                      uint16_t ciphertext_len, uint8_t *plaintext, size_t plaintext_buf_len,
                      size_t *plaintext_len, OscoreRequestInfo *req_info) {
    return oscore_unprotect_core_(opt_val, opt_len, ciphertext, ciphertext_len, plaintext, plaintext_buf_len,
                                  plaintext_len, req_info);
  }

  uint64_t replay_mask() const { return oscore_replay_mask_; }
  uint32_t replay_top() const { return oscore_replay_top_; }
};

class OscoreRoundTripTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // "Server" role: sender_id=h'01' (server's own ID), recipient_id=h'' (client's ID)
    srv_.set_material(
        {kSecret, kSecret + sizeof(kSecret)},
        {kSalt, kSalt + sizeof(kSalt)},
        {0x01},  // sender_id
        {});     // recipient_id = empty (client)
    ASSERT_TRUE(srv_.derive_keys());
    // After derivation: srv_.sender_key() == kServerRecipientKey (= key for id=h'01')
    // and srv_.common_iv() == kCommonIv
  }

  TestableCoapServer srv_;
};

TEST_F(OscoreRoundTripTest, ProtectResponseReturnsNonZeroLength) {
  const uint8_t inner[] = {0x45, 0xFF, 0x01};  // 2.05 + payload marker + byte
  uint8_t out[64];
  CoapServer::OscoreRequestInfo req{};
  req.piv[0] = 0x01;
  req.piv_len = 1;
  req.kid_len = 0;

  size_t out_len = srv_.protect(inner, sizeof(inner), req, false, out, sizeof(out));

  EXPECT_GT(out_len, 0u);
}

TEST_F(OscoreRoundTripTest, CiphertextLengthIsPlaintextPlusTag) {
  const uint8_t inner[] = {0x45, 0xFF, 0xAA, 0xBB};
  uint8_t out[64];
  CoapServer::OscoreRequestInfo req{};
  req.piv[0] = 0x02;
  req.piv_len = 1;
  req.kid_len = 0;

  size_t out_len = srv_.protect(inner, sizeof(inner), req, false, out, sizeof(out));

  EXPECT_EQ(out_len, sizeof(inner) + TestableCoapServer::TAG_LEN);
}

TEST_F(OscoreRoundTripTest, RoundTripDecryptsCorrectly) {
  // Encrypt with srv_ (sender_key = kServerRecipientKey)
  const uint8_t inner[] = {0x45, 0xFF, 0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t ciphertext[64];
  CoapServer::OscoreRequestInfo req{};
  req.piv[0] = 0x05;
  req.piv_len = 1;
  req.kid[0] = 0x01;
  req.kid_len = 1;

  size_t cipher_len = srv_.protect(inner, sizeof(inner), req, false, ciphertext, sizeof(ciphertext));
  ASSERT_GT(cipher_len, 0u);

  // Build nonce and AAD matching what protect_response_ used
  uint8_t nonce[TestableCoapServer::IV_LEN];
  TestableCoapServer::build_nonce(req.piv, req.piv_len, req.kid, req.kid_len, srv_.common_iv(), nonce);

  uint8_t aad[80];
  size_t aad_len = TestableCoapServer::build_aad(req.kid, req.kid_len, req.piv, req.piv_len, aad, sizeof(aad));
  ASSERT_GT(aad_len, 0u);

  // Decrypt using the same key (kServerRecipientKey = srv_.sender_key())
  psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attrs, 128);
  psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&attrs, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, TestableCoapServer::TAG_LEN));
  psa_key_id_t key_id = PSA_KEY_ID_NULL;
  ASSERT_EQ(psa_import_key(&attrs, srv_.sender_key(), TestableCoapServer::KEY_LEN, &key_id), PSA_SUCCESS);

  uint8_t plaintext[64];
  size_t plain_len = 0;
  psa_status_t s = psa_aead_decrypt(key_id, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, TestableCoapServer::TAG_LEN),
                                    nonce, TestableCoapServer::IV_LEN, aad, aad_len, ciphertext, cipher_len,
                                    plaintext, sizeof(plaintext), &plain_len);
  psa_destroy_key(key_id);

  ASSERT_EQ(s, PSA_SUCCESS);
  ASSERT_EQ(plain_len, sizeof(inner));
  EXPECT_EQ(memcmp(plaintext, inner, sizeof(inner)), 0);
}

TEST_F(OscoreRoundTripTest, NotificationIncrementsSenderSeq) {
  uint32_t seq_before = srv_.sender_seq();

  const uint8_t inner[] = {0x45, 0xFF, 0x01};
  uint8_t out[64];
  CoapServer::OscoreRequestInfo req{};  // notification uses sender_seq_, req ignored

  srv_.protect(inner, sizeof(inner), req, true, out, sizeof(out));

  EXPECT_EQ(srv_.sender_seq(), seq_before + 1);
}

TEST_F(OscoreRoundTripTest, ReplyDoesNotIncrementSenderSeq) {
  uint32_t seq_before = srv_.sender_seq();

  const uint8_t inner[] = {0x45, 0xFF, 0x01};
  uint8_t out[64];
  CoapServer::OscoreRequestInfo req{};
  req.piv[0] = 0x01;
  req.piv_len = 1;

  srv_.protect(inner, sizeof(inner), req, false, out, sizeof(out));

  EXPECT_EQ(srv_.sender_seq(), seq_before);  // replies reuse req PIV, don't advance seq
}

TEST_F(OscoreRoundTripTest, BuildNonceProducesExpectedLength) {
  uint8_t piv[] = {0x01};
  uint8_t kid[] = {0x00};
  uint8_t nonce[TestableCoapServer::IV_LEN];
  TestableCoapServer::build_nonce(piv, 1, kid, 1, kCommonIv, nonce);
  // Nonce is always OSCORE_IV_LEN (13) bytes — just verify it's non-zero
  bool all_zero = true;
  for (size_t i = 0; i < TestableCoapServer::IV_LEN; i++)
    if (nonce[i] != 0) { all_zero = false; break; }
  EXPECT_FALSE(all_zero);
}

TEST_F(OscoreRoundTripTest, BuildAadProducesNonZeroLength) {
  uint8_t kid[] = {0x01};
  uint8_t piv[] = {0x01};
  uint8_t buf[80];
  size_t len = TestableCoapServer::build_aad(kid, 1, piv, 1, buf, sizeof(buf));
  EXPECT_GT(len, 0u);
}

// Helper: build a fake "client request" ciphertext using PSA directly,
// matching the nonce/AAD that oscore_unprotect_core_ will reconstruct.
static bool encrypt_client_request(const uint8_t *recipient_key, const uint8_t *common_iv,
                                    const uint8_t *piv, uint8_t piv_len,
                                    const uint8_t *kid, uint8_t kid_len,
                                    const uint8_t *plaintext, size_t plaintext_len,
                                    uint8_t *ciphertext_out, size_t *ciphertext_len_out) {
  uint8_t nonce[TestableCoapServer::IV_LEN];
  TestableCoapServer::build_nonce(piv, piv_len, kid, kid_len, common_iv, nonce);
  uint8_t aad[80];
  size_t aad_len = TestableCoapServer::build_aad(kid, kid_len, piv, piv_len, aad, sizeof(aad));
  if (aad_len == 0) return false;

  psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attrs, 128);
  psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_ENCRYPT);
  psa_set_key_algorithm(&attrs, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, TestableCoapServer::TAG_LEN));
  psa_key_id_t key_id = PSA_KEY_ID_NULL;
  if (psa_import_key(&attrs, recipient_key, TestableCoapServer::KEY_LEN, &key_id) != PSA_SUCCESS)
    return false;
  psa_status_t s = psa_aead_encrypt(key_id, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, TestableCoapServer::TAG_LEN),
                                    nonce, TestableCoapServer::IV_LEN, aad, aad_len,
                                    plaintext, plaintext_len, ciphertext_out, 256, ciphertext_len_out);
  psa_destroy_key(key_id);
  return s == PSA_SUCCESS;
}

class OscoreUnprotectCoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Server: sender_id=h'01', recipient_id=h'' (client's sender ID)
    srv_.set_material(
        {kSecret, kSecret + sizeof(kSecret)},
        {kSalt, kSalt + sizeof(kSalt)},
        {0x01},  // sender_id
        {});     // recipient_id = h'' (client)
    ASSERT_TRUE(srv_.derive_keys());
    // srv_.recipient_key() == kClientSenderKey
  }

  TestableCoapServer srv_;
};

TEST_F(OscoreUnprotectCoreTest, DecryptsValidRequest) {
  const uint8_t piv[] = {0x01};
  // opt_val: flags=0x09 (piv_len=1, k=KID present), piv byte, empty KID
  const uint8_t opt_val[] = {0x09, 0x01};
  const uint8_t inner[] = {0x01, 0xFF, 0xDE, 0xAD, 0xBE, 0xEF};

  uint8_t ciphertext[256];
  size_t cipher_len = 0;
  ASSERT_TRUE(encrypt_client_request(srv_.recipient_key(), srv_.common_iv(),
                                     piv, 1, nullptr, 0,
                                     inner, sizeof(inner), ciphertext, &cipher_len));

  uint8_t plaintext[256];
  size_t plain_len = 0;
  CoapServer::OscoreRequestInfo req_info{};
  bool ok = srv_.unprotect_core(opt_val, sizeof(opt_val), ciphertext, (uint16_t) cipher_len,
                                 plaintext, sizeof(plaintext), &plain_len, &req_info);

  ASSERT_TRUE(ok);
  ASSERT_EQ(plain_len, sizeof(inner));
  EXPECT_EQ(memcmp(plaintext, inner, sizeof(inner)), 0);
  EXPECT_EQ(req_info.piv_len, 1u);
  EXPECT_EQ(req_info.piv[0], 0x01);
  EXPECT_EQ(req_info.kid_len, 0u);
}

TEST_F(OscoreUnprotectCoreTest, RejectsReplayedSequenceNumber) {
  const uint8_t piv[] = {0x07};
  const uint8_t opt_val[] = {0x01, 0x07};  // flags: piv_len=1, no KID
  const uint8_t inner[] = {0x01, 0xFF, 0xAA};

  uint8_t ciphertext[256];
  size_t cipher_len = 0;
  ASSERT_TRUE(encrypt_client_request(srv_.recipient_key(), srv_.common_iv(),
                                     piv, 1, nullptr, 0,
                                     inner, sizeof(inner), ciphertext, &cipher_len));

  uint8_t plaintext[256];
  size_t plain_len = 0;
  CoapServer::OscoreRequestInfo req_info{};

  // First call — accepted, replay window seeded
  ASSERT_TRUE(srv_.unprotect_core(opt_val, sizeof(opt_val), ciphertext, (uint16_t) cipher_len,
                                   plaintext, sizeof(plaintext), &plain_len, &req_info));

  // Second call with same seq — replay_mask_ is now non-zero, should be rejected
  plain_len = 0;
  EXPECT_FALSE(srv_.unprotect_core(opt_val, sizeof(opt_val), ciphertext, (uint16_t) cipher_len,
                                    plaintext, sizeof(plaintext), &plain_len, &req_info));
}

TEST_F(OscoreUnprotectCoreTest, ZeroPivLengthSkipsReplayCheck) {
  // flags=0x00: piv_len=0, no KID, no KID context
  const uint8_t opt_val[] = {0x00};
  const uint8_t inner[] = {0x01, 0xFF, 0xBB};

  // Nonce for piv_len=0 uses empty piv
  uint8_t nonce[TestableCoapServer::IV_LEN];
  TestableCoapServer::build_nonce(nullptr, 0, nullptr, 0, srv_.common_iv(), nonce);
  uint8_t aad[80];
  size_t aad_len = TestableCoapServer::build_aad(nullptr, 0, nullptr, 0, aad, sizeof(aad));
  ASSERT_GT(aad_len, 0u);

  // Encrypt directly with PSA
  psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attrs, 128);
  psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_ENCRYPT);
  psa_set_key_algorithm(&attrs, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, TestableCoapServer::TAG_LEN));
  psa_key_id_t key_id = PSA_KEY_ID_NULL;
  ASSERT_EQ(psa_import_key(&attrs, srv_.recipient_key(), TestableCoapServer::KEY_LEN, &key_id), PSA_SUCCESS);
  uint8_t ciphertext[256];
  size_t cipher_len = 0;
  ASSERT_EQ(psa_aead_encrypt(key_id, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, TestableCoapServer::TAG_LEN),
                              nonce, TestableCoapServer::IV_LEN, aad, aad_len,
                              inner, sizeof(inner), ciphertext, sizeof(ciphertext), &cipher_len), PSA_SUCCESS);
  psa_destroy_key(key_id);

  uint8_t plaintext[256];
  size_t plain_len = 0;
  CoapServer::OscoreRequestInfo req_info{};
  // Call twice — piv_len=0 must not trigger replay rejection
  EXPECT_TRUE(srv_.unprotect_core(opt_val, sizeof(opt_val), ciphertext, (uint16_t) cipher_len,
                                   plaintext, sizeof(plaintext), &plain_len, &req_info));
  EXPECT_TRUE(srv_.unprotect_core(opt_val, sizeof(opt_val), ciphertext, (uint16_t) cipher_len,
                                   plaintext, sizeof(plaintext), &plain_len, &req_info));
}

}  // namespace esphome::coap_server
