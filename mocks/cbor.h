#pragma once
// Minimal CBOR encoder mock for coap_server host unit tests.
// Replaces tinycbor (IDF-only) — writes real CBOR bytes so tests can verify output.

#include <cstddef>
#include <cstdint>
#include <cstring>

typedef enum { CborNoError = 0, CborErrorOutOfMemory = 1, CborErrorUnknownType = 2 } CborError;

struct CborEncoder {
  uint8_t *ptr;
  uint8_t *end;
  uint8_t *start;
};

static inline CborError cbor_write_byte_(CborEncoder *enc, uint8_t b) {
  if (enc->ptr >= enc->end) return CborErrorOutOfMemory;
  *enc->ptr++ = b;
  return CborNoError;
}

static inline CborError cbor_write_bytes_(CborEncoder *enc, const void *data, size_t len) {
  if (enc->ptr + len > enc->end) return CborErrorOutOfMemory;
  std::memcpy(enc->ptr, data, len);
  enc->ptr += len;
  return CborNoError;
}

static inline void cbor_encoder_init(CborEncoder *enc, uint8_t *buf, size_t len, int /*flags*/) {
  enc->ptr = buf;
  enc->end = buf + len;
  enc->start = buf;
}

static inline CborError cbor_encoder_create_map(CborEncoder *enc, CborEncoder *child, size_t count) {
  CborError err = cbor_write_byte_(enc, (uint8_t)(0xa0u | (uint8_t) count));
  *child = *enc;
  return err;
}

static inline CborError cbor_encoder_create_array(CborEncoder *enc, CborEncoder *child, size_t count) {
  CborError err = cbor_write_byte_(enc, (uint8_t)(0x80u | (uint8_t) count));
  *child = *enc;
  return err;
}

static inline CborError cbor_encoder_close_container(CborEncoder *enc, CborEncoder *child) {
  enc->ptr = child->ptr;
  return CborNoError;
}

static inline size_t cbor_encoder_get_buffer_size(CborEncoder *enc, const uint8_t * /*buf*/) {
  return (size_t)(enc->ptr - enc->start);
}

static inline CborError cbor_encode_uint(CborEncoder *enc, uint64_t val) {
  if (val <= 23u) return cbor_write_byte_(enc, (uint8_t) val);
  if (val <= 0xffu) {
    CborError e = cbor_write_byte_(enc, 0x18u);
    if (e != CborNoError) return e;
    return cbor_write_byte_(enc, (uint8_t) val);
  }
  return CborErrorOutOfMemory;
}

static inline CborError cbor_encode_int(CborEncoder *enc, int64_t val) {
  if (val >= 0) return cbor_encode_uint(enc, (uint64_t) val);
  uint64_t n = (uint64_t)(-(val + 1));
  if (n <= 23u) return cbor_write_byte_(enc, (uint8_t)(0x20u | (uint8_t) n));
  return CborErrorOutOfMemory;
}

static inline CborError cbor_encode_float(CborEncoder *enc, float val) {
  uint32_t bits;
  std::memcpy(&bits, &val, 4);
  uint8_t buf[5] = {0xfau, (uint8_t)(bits >> 24), (uint8_t)(bits >> 16), (uint8_t)(bits >> 8), (uint8_t) bits};
  return cbor_write_bytes_(enc, buf, 5);
}

static inline CborError cbor_encode_boolean(CborEncoder *enc, bool val) {
  return cbor_write_byte_(enc, val ? (uint8_t) 0xf5u : (uint8_t) 0xf4u);
}

static inline CborError cbor_encode_byte_string(CborEncoder *enc, const uint8_t *data, size_t len) {
  CborError e;
  if (len <= 23u) {
    e = cbor_write_byte_(enc, (uint8_t)(0x40u | (uint8_t) len));
  } else if (len <= 0xffu) {
    e = cbor_write_byte_(enc, 0x58u);
    if (e != CborNoError) return e;
    e = cbor_write_byte_(enc, (uint8_t) len);
  } else {
    return CborErrorOutOfMemory;
  }
  if (e != CborNoError) return e;
  return cbor_write_bytes_(enc, data, len);
}

static inline CborError cbor_encode_text_string(CborEncoder *enc, const char *str, size_t len) {
  CborError e;
  if (len <= 23u) {
    e = cbor_write_byte_(enc, (uint8_t)(0x60u | (uint8_t) len));
  } else if (len <= 0xffu) {
    e = cbor_write_byte_(enc, 0x78u);
    if (e != CborNoError) return e;
    e = cbor_write_byte_(enc, (uint8_t) len);
  } else {
    return CborErrorOutOfMemory;
  }
  if (e != CborNoError) return e;
  return cbor_write_bytes_(enc, str, len);
}

static inline CborError cbor_encode_text_stringz(CborEncoder *enc, const char *str) {
  return cbor_encode_text_string(enc, str, std::strlen(str));
}

static inline CborError cbor_encode_null(CborEncoder *enc) {
  return cbor_write_byte_(enc, 0xf6u);  // CBOR null
}

// ---------------------------------------------------------------------------
// CBOR decoder — minimal stub for host unit tests
// Decodes real CBOR bytes. Only the functions used by coap_server_ot.cpp.
// ---------------------------------------------------------------------------

struct CborParser {
  const uint8_t *buf;
  size_t buf_len;
};

struct CborValue {
  const uint8_t *buf;
  size_t buf_len;
  size_t offset;       // current read position
  bool at_end;
};

// Major type helpers
static inline uint8_t cbor_major_(const CborValue *v) {
  if (v->at_end || v->offset >= v->buf_len) return 0xff;
  return (v->buf[v->offset] >> 5u) & 0x07u;
}

static inline uint64_t cbor_read_uint_(CborValue *v) {
  if (v->offset >= v->buf_len) { v->at_end = true; return 0; }
  uint8_t info = v->buf[v->offset] & 0x1fu;
  v->offset++;
  if (info <= 23u) return info;
  if (info == 0x18u && v->offset < v->buf_len) return v->buf[v->offset++];
  if (info == 0x19u && v->offset + 1 < v->buf_len) {
    uint64_t n = ((uint64_t) v->buf[v->offset] << 8) | v->buf[v->offset + 1];
    v->offset += 2;
    return n;
  }
  if (info == 0x1au && v->offset + 3 < v->buf_len) {
    uint64_t n = ((uint64_t) v->buf[v->offset] << 24) | ((uint64_t) v->buf[v->offset + 1] << 16) |
                 ((uint64_t) v->buf[v->offset + 2] << 8) | v->buf[v->offset + 3];
    v->offset += 4;
    return n;
  }
  return 0;
}

static inline CborError cbor_parser_init(const uint8_t *buf, size_t len, int /*flags*/, CborParser *parser,
                                          CborValue *value) {
  parser->buf = buf;
  parser->buf_len = len;
  value->buf = buf;
  value->buf_len = len;
  value->offset = 0;
  value->at_end = (len == 0);
  return CborNoError;
}

static inline bool cbor_value_is_map(const CborValue *v) {
  return !v->at_end && v->offset < v->buf_len && ((v->buf[v->offset] >> 5u) == 5u);
}
static inline bool cbor_value_is_integer(const CborValue *v) {
  if (v->at_end || v->offset >= v->buf_len) return false;
  uint8_t major = v->buf[v->offset] >> 5u;
  return major == 0u || major == 1u;
}
static inline bool cbor_value_is_boolean(const CborValue *v) {
  if (v->at_end || v->offset >= v->buf_len) return false;
  uint8_t b = v->buf[v->offset];
  return b == 0xf4u || b == 0xf5u;
}
static inline bool cbor_value_is_float(const CborValue *v) {
  return !v->at_end && v->offset < v->buf_len && v->buf[v->offset] == 0xfau;
}
static inline bool cbor_value_is_double(const CborValue *v) {
  return !v->at_end && v->offset < v->buf_len && v->buf[v->offset] == 0xfbu;
}
static inline bool cbor_value_is_half_float(const CborValue *v) {
  return !v->at_end && v->offset < v->buf_len && v->buf[v->offset] == 0xf9u;
}
static inline bool cbor_value_at_end(const CborValue *v) { return v->at_end || v->offset >= v->buf_len; }

static inline CborError cbor_value_enter_container(CborValue *container, CborValue *child) {
  if (!cbor_value_is_map(container)) return CborErrorUnknownType;
  // Skip the map header byte (count is embedded but we iterate until end)
  size_t off = container->offset;
  off++;  // skip major/count byte (we ignore count, iterate by position)
  *child = *container;
  child->offset = off;
  return CborNoError;
}

static inline CborError cbor_value_get_int(CborValue *v, int *out) {
  if (!cbor_value_is_integer(v)) return CborErrorUnknownType;
  uint8_t major = v->buf[v->offset] >> 5u;
  CborValue tmp = *v;
  uint64_t n = cbor_read_uint_(&tmp);
  *v = tmp;  // advance past the int
  if (major == 1u)
    *out = -(int) (n + 1);
  else
    *out = (int) n;
  return CborNoError;
}

static inline CborError cbor_value_get_int64(CborValue *v, int64_t *out) {
  if (!cbor_value_is_integer(v)) return CborErrorUnknownType;
  uint8_t major = v->buf[v->offset] >> 5u;
  CborValue tmp = *v;
  uint64_t n = cbor_read_uint_(&tmp);
  *v = tmp;
  if (major == 1u)
    *out = -(int64_t)(n + 1);
  else
    *out = (int64_t) n;
  return CborNoError;
}

static inline CborError cbor_value_get_boolean(CborValue *v, bool *out) {
  if (!cbor_value_is_boolean(v)) return CborErrorUnknownType;
  *out = (v->buf[v->offset] == 0xf5u);
  v->offset++;
  return CborNoError;
}

static inline CborError cbor_value_get_double(CborValue *v, double *out) {
  if (cbor_value_is_float(v) && v->offset + 4 < v->buf_len) {
    uint32_t bits = ((uint32_t) v->buf[v->offset + 1] << 24) | ((uint32_t) v->buf[v->offset + 2] << 16) |
                    ((uint32_t) v->buf[v->offset + 3] << 8) | v->buf[v->offset + 4];
    float f;
    std::memcpy(&f, &bits, 4);
    *out = (double) f;
    v->offset += 5;
    return CborNoError;
  }
  return CborErrorUnknownType;
}

// cbor_value_advance: skip current value (move to next)
static inline CborError cbor_value_advance(CborValue *v) {
  if (cbor_value_at_end(v)) return CborNoError;
  uint8_t b = v->buf[v->offset];
  uint8_t major = b >> 5u;
  uint8_t info = b & 0x1fu;

  if (major == 7u) {
    // Simple values / floats
    if (b == 0xf4u || b == 0xf5u) { v->offset++; return CborNoError; }  // false/true
    if (b == 0xfau) { v->offset += 5; return CborNoError; }              // float32
    if (b == 0xfbu) { v->offset += 9; return CborNoError; }              // float64
    if (b == 0xf9u) { v->offset += 3; return CborNoError; }              // float16
    v->offset++;
    return CborNoError;
  }

  // For integer, byte string, text string: consume header then data
  size_t header_len = 1;
  size_t data_len = 0;
  if (info <= 23u) {
    data_len = (major == 0u || major == 1u) ? 0u : info;
  } else if (info == 0x18u) {
    if (v->offset + 1 >= v->buf_len) { v->at_end = true; return CborNoError; }
    data_len = (major == 0u || major == 1u) ? 0u : v->buf[v->offset + 1];
    header_len = 2;
  } else if (info == 0x19u) {
    if (v->offset + 2 >= v->buf_len) { v->at_end = true; return CborNoError; }
    data_len = (major == 0u || major == 1u) ? 0u : ((size_t) v->buf[v->offset + 1] << 8 | v->buf[v->offset + 2]);
    header_len = 3;
  }
  v->offset += header_len + data_len;
  if (v->offset >= v->buf_len) v->at_end = true;
  return CborNoError;
}
