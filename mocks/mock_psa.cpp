#include "psa/crypto.h"
#include <cstring>
#include <new>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/err.h>

// PSA Crypto mock backed by OpenSSL 3.0.
//
// Only the subset used by coap_server is implemented:
//   - HKDF key derivation (psa_key_derivation_*)
//   - AES-CCM-16-64-128 AEAD encrypt/decrypt (psa_aead_*)
//   - Transient key import/destroy (psa_import_key / psa_destroy_key)
//
// Key handles are simple indices into a small key table.

// ---------------------------------------------------------------------------
// Key store
// ---------------------------------------------------------------------------

namespace {

static constexpr size_t MAX_KEYS = 16;
static constexpr size_t MAX_KEY_LEN = 32;

struct KeySlot {
  bool active{false};
  uint8_t data[MAX_KEY_LEN]{};
  size_t len{0};
  psa_algorithm_t alg{0};
};

static KeySlot g_keys[MAX_KEYS];

static psa_key_handle_t alloc_key() {
  for (psa_key_handle_t i = 1; i < MAX_KEYS; i++) {
    if (!g_keys[i].active) return i;
  }
  return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Key derivation (HKDF via OpenSSL 3.0 EVP_KDF)
// ---------------------------------------------------------------------------

namespace {

// Internal HKDF state stored inside the opaque operation struct
struct HkdfState {
  uint32_t magic{0xABCD1234};
  uint8_t salt[64]{};
  size_t salt_len{0};
  uint8_t secret[64]{};
  size_t secret_len{0};
  uint8_t info[256]{};
  size_t info_len{0};
  psa_algorithm_t alg{0};
  bool salt_set{false};
  bool secret_set{false};
  bool info_set{false};
};

static HkdfState *get_state(psa_key_derivation_operation_t *op) {
  return reinterpret_cast<HkdfState *>(op->_opaque);
}

static bool do_hkdf(const uint8_t *secret, size_t secret_len, const uint8_t *salt, size_t salt_len,
                    const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len) {
  EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
  if (!kdf) return false;

  EVP_KDF_CTX *ctx = EVP_KDF_CTX_new(kdf);
  EVP_KDF_free(kdf);
  if (!ctx) return false;

  OSSL_PARAM params[6];
  int n = 0;
  params[n++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char *>("SHA256"), 0);
  params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                                  const_cast<uint8_t *>(secret), secret_len);
  if (salt_len > 0)
    params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                                    const_cast<uint8_t *>(salt), salt_len);
  if (info_len > 0)
    params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                                                    const_cast<uint8_t *>(info), info_len);
  params[n] = OSSL_PARAM_construct_end();

  bool ok = (EVP_KDF_derive(ctx, out, out_len, params) > 0);
  EVP_KDF_CTX_free(ctx);
  return ok;
}

}  // namespace

extern "C" {

psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t *op, psa_algorithm_t alg) {
  static_assert(sizeof(HkdfState) <= sizeof(op->_opaque), "HkdfState too large for opaque buffer");
  HkdfState *s = new (op->_opaque) HkdfState{};
  s->alg = alg;
  return PSA_SUCCESS;
}

psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t *op, psa_key_derivation_step_t step,
                                            const uint8_t *data, size_t len) {
  HkdfState *s = get_state(op);
  if (s->magic != 0xABCD1234) return PSA_ERROR_BAD_STATE;
  switch (step) {
    case PSA_KEY_DERIVATION_INPUT_SALT:
      if (len <= sizeof(s->salt)) { memcpy(s->salt, data, len); s->salt_len = len; s->salt_set = true; }
      break;
    case PSA_KEY_DERIVATION_INPUT_SECRET:
      if (len <= sizeof(s->secret)) { memcpy(s->secret, data, len); s->secret_len = len; s->secret_set = true; }
      break;
    case PSA_KEY_DERIVATION_INPUT_INFO:
      if (len <= sizeof(s->info)) { memcpy(s->info, data, len); s->info_len = len; s->info_set = true; }
      break;
    default:
      return PSA_ERROR_INVALID_ARGUMENT;
  }
  return PSA_SUCCESS;
}

psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t *op, uint8_t *output, size_t out_len) {
  HkdfState *s = get_state(op);
  if (s->magic != 0xABCD1234 || !s->secret_set) return PSA_ERROR_BAD_STATE;
  bool ok = do_hkdf(s->secret, s->secret_len,
                    s->salt_set ? s->salt : nullptr, s->salt_set ? s->salt_len : 0,
                    s->info_set ? s->info : nullptr, s->info_set ? s->info_len : 0,
                    output, out_len);
  return ok ? PSA_SUCCESS : PSA_ERROR_GENERIC_ERROR;
}

psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t *op) {
  HkdfState *s = get_state(op);
  if (s->magic == 0xABCD1234) {
    memset(s, 0, sizeof(HkdfState));
  }
  return PSA_SUCCESS;
}

// ---------------------------------------------------------------------------
// Key import / destroy
// ---------------------------------------------------------------------------

psa_status_t psa_import_key(const psa_key_attributes_t *attrs, const uint8_t *data, size_t data_len,
                            psa_key_handle_t *handle) {
  psa_key_handle_t h = alloc_key();
  if (h == 0) return PSA_ERROR_INSUFFICIENT_MEMORY;
  if (data_len > MAX_KEY_LEN) return PSA_ERROR_INVALID_ARGUMENT;
  g_keys[h].active = true;
  g_keys[h].len = data_len;
  g_keys[h].alg = attrs->alg;
  memcpy(g_keys[h].data, data, data_len);
  *handle = h;
  return PSA_SUCCESS;
}

psa_status_t psa_destroy_key(psa_key_handle_t handle) {
  if (handle == 0 || handle >= MAX_KEYS) return PSA_ERROR_INVALID_ARGUMENT;
  memset(&g_keys[handle], 0, sizeof(KeySlot));
  return PSA_SUCCESS;
}

// ---------------------------------------------------------------------------
// AES-CCM AEAD (PSA algorithm id 10 = AES-CCM-16-64-128)
// OpenSSL does not expose CCM via EVP_AEAD, so we use EVP_CIPHER with CCM mode.
// ---------------------------------------------------------------------------

static psa_status_t aes_ccm(bool encrypt, psa_key_handle_t handle,
                             const uint8_t *nonce, size_t nonce_len,
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t *in, size_t in_len,
                             uint8_t *out, size_t out_size, size_t *out_len) {
  if (handle == 0 || handle >= MAX_KEYS || !g_keys[handle].active) return PSA_ERROR_INVALID_ARGUMENT;

  const KeySlot &key = g_keys[handle];
  static constexpr size_t TAG_LEN = 8;

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return PSA_ERROR_INSUFFICIENT_MEMORY;

  const EVP_CIPHER *cipher = (key.len == 16) ? EVP_aes_128_ccm() :
                             (key.len == 24) ? EVP_aes_192_ccm() :
                                               EVP_aes_256_ccm();
  int ret = EVP_CipherInit_ex(ctx, cipher, nullptr, nullptr, nullptr, encrypt ? 1 : 0);

  // CCM requires setting tag length before key/IV
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, (int) nonce_len, nullptr);
  if (encrypt) {
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, TAG_LEN, nullptr);
  } else {
    // For decryption: strip tag from end of ciphertext
    if (in_len < TAG_LEN) { EVP_CIPHER_CTX_free(ctx); return PSA_ERROR_INVALID_ARGUMENT; }
    uint8_t tag[TAG_LEN];
    memcpy(tag, in + in_len - TAG_LEN, TAG_LEN);
    in_len -= TAG_LEN;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, TAG_LEN, tag);
  }

  EVP_CipherInit_ex(ctx, nullptr, nullptr, key.data, nonce, -1);

  int out_partial = 0;
  // Tell OpenSSL the plaintext length (required for CCM)
  EVP_CipherUpdate(ctx, nullptr, &out_partial, nullptr, (int) in_len);
  // Set AAD
  if (aad_len > 0) EVP_CipherUpdate(ctx, nullptr, &out_partial, aad, (int) aad_len);

  size_t needed = in_len + (encrypt ? TAG_LEN : 0);
  if (out_size < needed) { EVP_CIPHER_CTX_free(ctx); return PSA_ERROR_INSUFFICIENT_MEMORY; }

  ret = EVP_CipherUpdate(ctx, out, &out_partial, in, (int) in_len);
  if (ret <= 0 && !encrypt) {
    EVP_CIPHER_CTX_free(ctx);
    return PSA_ERROR_GENERIC_ERROR;  // tag mismatch
  }
  *out_len = (size_t) out_partial;

  if (encrypt) {
    uint8_t tag[TAG_LEN];
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, TAG_LEN, tag);
    memcpy(out + *out_len, tag, TAG_LEN);
    *out_len += TAG_LEN;
  }

  EVP_CIPHER_CTX_free(ctx);
  return PSA_SUCCESS;
}

psa_status_t psa_aead_encrypt(psa_key_handle_t key, psa_algorithm_t /*alg*/, const uint8_t *nonce,
                              size_t nonce_len, const uint8_t *aad, size_t aad_len, const uint8_t *plaintext,
                              size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                              size_t *ciphertext_length) {
  return aes_ccm(true, key, nonce, nonce_len, aad, aad_len, plaintext, plaintext_len,
                 ciphertext, ciphertext_size, ciphertext_length);
}

psa_status_t psa_aead_decrypt(psa_key_handle_t key, psa_algorithm_t /*alg*/, const uint8_t *nonce,
                              size_t nonce_len, const uint8_t *aad, size_t aad_len, const uint8_t *ciphertext,
                              size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                              size_t *plaintext_length) {
  return aes_ccm(false, key, nonce, nonce_len, aad, aad_len, ciphertext, ciphertext_len,
                 plaintext, plaintext_size, plaintext_length);
}

}  // extern "C"
