#pragma once
// PSA Crypto API stub for coap_server host unit tests.
// Real implementations backed by OpenSSL 3.0 in mock_psa.cpp.
// API follows PSA Crypto 1.1 (subset used by coap_server).

#include <cstddef>
#include <cstdint>

typedef int32_t psa_status_t;

static constexpr psa_status_t PSA_SUCCESS = 0;
static constexpr psa_status_t PSA_ERROR_GENERIC_ERROR = -132;
static constexpr psa_status_t PSA_ERROR_NOT_SUPPORTED = -134;
static constexpr psa_status_t PSA_ERROR_INVALID_ARGUMENT = -135;
static constexpr psa_status_t PSA_ERROR_INSUFFICIENT_MEMORY = -141;
static constexpr psa_status_t PSA_ERROR_BAD_STATE = -137;

// ---------------------------------------------------------------------------
// Algorithm identifiers (subset)
// ---------------------------------------------------------------------------

typedef uint32_t psa_algorithm_t;

static constexpr psa_algorithm_t PSA_ALG_SHA_256 = 0x02000009u;
static constexpr psa_algorithm_t PSA_ALG_HKDF_BASE = 0x08000100u;
static constexpr psa_algorithm_t PSA_ALG_CCM = 0x05500100u;
static constexpr psa_algorithm_t PSA_ALG_AEAD_WITH_SHORTENED_TAG_BASE = 0x05400000u;

static inline psa_algorithm_t PSA_ALG_HKDF(psa_algorithm_t hash) {
  return PSA_ALG_HKDF_BASE | (hash & 0x000000ffu);
}

static inline psa_algorithm_t PSA_ALG_AEAD_WITH_SHORTENED_TAG(psa_algorithm_t aead, unsigned tag_len) {
  return PSA_ALG_AEAD_WITH_SHORTENED_TAG_BASE | (aead & 0x003fffffu) | (tag_len << 24);
}

// AES-CCM-16-64-128 — the algorithm used by OSCORE (alg id 10 in COSE)
static inline psa_algorithm_t PSA_ALG_CCM_STAR_NO_TAG() { return 0x04c01300u; }

// ---------------------------------------------------------------------------
// Key derivation step selectors
// ---------------------------------------------------------------------------

typedef uint16_t psa_key_derivation_step_t;

static constexpr psa_key_derivation_step_t PSA_KEY_DERIVATION_INPUT_SALT = 0x0101;
static constexpr psa_key_derivation_step_t PSA_KEY_DERIVATION_INPUT_SECRET = 0x0201;
static constexpr psa_key_derivation_step_t PSA_KEY_DERIVATION_INPUT_INFO = 0x0301;

// ---------------------------------------------------------------------------
// Key attributes
// ---------------------------------------------------------------------------

typedef uint16_t psa_key_type_t;
typedef uint16_t psa_key_bits_t;
typedef uint32_t psa_key_usage_t;
typedef uint32_t psa_key_id_t;
typedef uint32_t psa_key_lifetime_t;

static constexpr psa_key_type_t PSA_KEY_TYPE_AES = 0x2400;
static constexpr psa_key_usage_t PSA_KEY_USAGE_ENCRYPT = 0x0100;
static constexpr psa_key_usage_t PSA_KEY_USAGE_DECRYPT = 0x0200;
static constexpr psa_key_lifetime_t PSA_KEY_LIFETIME_VOLATILE = 0x00000000u;

typedef struct {
  psa_key_type_t type;
  psa_key_bits_t bits;
  psa_key_lifetime_t lifetime;
  psa_key_id_t id;
  psa_key_usage_t usage;
  psa_algorithm_t alg;
} psa_key_attributes_t;

typedef uint32_t psa_key_handle_t;

// psa_key_attributes_t inline setters (mirror the PSA API pattern)
static inline void psa_set_key_type(psa_key_attributes_t *a, psa_key_type_t t) { a->type = t; }
static inline void psa_set_key_bits(psa_key_attributes_t *a, psa_key_bits_t b) { a->bits = b; }
static inline void psa_set_key_lifetime(psa_key_attributes_t *a, psa_key_lifetime_t l) { a->lifetime = l; }
static inline void psa_set_key_usage_flags(psa_key_attributes_t *a, psa_key_usage_t u) { a->usage = u; }
static inline void psa_set_key_algorithm(psa_key_attributes_t *a, psa_algorithm_t alg) { a->alg = alg; }
static inline psa_key_attributes_t psa_key_attributes_init() { return psa_key_attributes_t{}; }

#define PSA_KEY_ATTRIBUTES_INIT (psa_key_attributes_t{})
#define PSA_KEY_ID_NULL ((psa_key_id_t) 0)

// ---------------------------------------------------------------------------
// Key handle / derivation operation (opaque structs — size must be >= real)
// ---------------------------------------------------------------------------

typedef struct {
  uint8_t _opaque[512];
} psa_key_derivation_operation_t;

#define PSA_KEY_DERIVATION_OPERATION_INIT \
  { {0} }

// ---------------------------------------------------------------------------
// C function declarations — implemented in mock_psa.cpp
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t *operation, psa_algorithm_t alg);
psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t *operation, psa_key_derivation_step_t step,
                                            const uint8_t *data, size_t data_length);
psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t *operation, uint8_t *output,
                                             size_t output_length);
psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t *operation);

psa_status_t psa_import_key(const psa_key_attributes_t *attributes, const uint8_t *data, size_t data_length,
                             psa_key_handle_t *handle);
psa_status_t psa_destroy_key(psa_key_handle_t handle);

psa_status_t psa_aead_encrypt(psa_key_handle_t key, psa_algorithm_t alg, const uint8_t *nonce, size_t nonce_length,
                              const uint8_t *additional_data, size_t additional_data_length, const uint8_t *plaintext,
                              size_t plaintext_length, uint8_t *ciphertext, size_t ciphertext_size,
                              size_t *ciphertext_length);

psa_status_t psa_aead_decrypt(psa_key_handle_t key, psa_algorithm_t alg, const uint8_t *nonce, size_t nonce_length,
                              const uint8_t *additional_data, size_t additional_data_length,
                              const uint8_t *ciphertext, size_t ciphertext_length, uint8_t *plaintext,
                              size_t plaintext_size, size_t *plaintext_length);

#ifdef __cplusplus
}
#endif
