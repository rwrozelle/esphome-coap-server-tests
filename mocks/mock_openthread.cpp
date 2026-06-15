#include "openthread/coap.h"
#include "openthread/ip6.h"
#include <cstdio>
#include <cstring>
#include <string>

// Minimal stub implementations of OpenThread CoAP API for host unit tests.
// Tests that need to verify CoAP interactions can replace these with gmock objects
// using the link-seam pattern (extern pointer + override in test body).

// Response capture — written by otCoapSendResponse / otCoapSendResponseBlockWise
static uint8_t g_last_response_buf[4096];
static uint16_t g_last_response_len = 0;
static int g_last_response_code = 0;

// Block-wise send capture
static otCoapBlockwiseTransmitHook g_last_blockwise_hook = nullptr;
static void *g_last_blockwise_ctx = nullptr;

// Inline (non-block-wise) send capture
static bool g_inline_send_called = false;

// Block2 option appended to response capture
static bool g_response_has_block2 = false;
static uint32_t g_response_block2_num = 0;
static bool g_response_block2_more = false;
static int g_response_block2_szx = 0;

// Block2 in request simulation (set by test before invoking handler)
static int g_request_block2_szx = -1;  // -1 means no Block2 in request

// Block-wise resource registration capture
static bool g_blockwise_resource_registered = false;
static const char *g_blockwise_resource_uri = nullptr;

void mock_ot_reset_last_response() { g_last_response_len = 0; g_inline_send_called = false; }
uint16_t mock_ot_last_response_len() { return g_last_response_len; }
std::string mock_ot_last_response_str() {
  return std::string(reinterpret_cast<char *>(g_last_response_buf), g_last_response_len);
}
int mock_ot_last_response_code() { return g_last_response_code; }
bool mock_ot_inline_send_called() { return g_inline_send_called; }

void mock_ot_reset_blockwise() {
  g_last_blockwise_hook = nullptr;
  g_last_blockwise_ctx = nullptr;
  g_response_has_block2 = false;
  g_response_block2_num = 0;
  g_response_block2_more = false;
  g_response_block2_szx = 0;
  g_request_block2_szx = -1;
  g_blockwise_resource_registered = false;
  g_blockwise_resource_uri = nullptr;
}
bool mock_ot_blockwise_called() { return g_last_blockwise_hook != nullptr; }
void *mock_ot_blockwise_ctx() { return g_last_blockwise_ctx; }

bool mock_ot_response_has_block2() { return g_response_has_block2; }
uint32_t mock_ot_response_block2_num() { return g_response_block2_num; }
bool mock_ot_response_block2_more() { return g_response_block2_more; }
int mock_ot_response_block2_szx() { return g_response_block2_szx; }

void mock_ot_set_request_block2(int szx) { g_request_block2_szx = szx; }

bool mock_ot_blockwise_resource_registered() { return g_blockwise_resource_registered; }
const char *mock_ot_blockwise_resource_uri() { return g_blockwise_resource_uri; }

namespace {

// Fake message storage so otMessageAppend etc. have somewhere to write
struct FakeMessage {
  uint8_t buf[2048];
  uint16_t len{0};
  otCoapCode code{OT_COAP_CODE_GET};
  otCoapType type{OT_COAP_TYPE_NON_CONFIRMABLE};
  uint8_t token[OT_COAP_MAX_TOKEN_LENGTH]{};
  uint8_t token_len{0};
  bool has_payload_marker{false};
  // Option storage (simplified: single observe option)
  bool has_observe{false};
  uint32_t observe_value{0};
};

static FakeMessage g_messages[8];
static int g_message_next = 0;

static FakeMessage *alloc_message() {
  FakeMessage *m = &g_messages[g_message_next % 8];
  g_message_next++;
  *m = FakeMessage{};
  return m;
}

}  // namespace

extern "C" {

otError otCoapStart(otInstance * /*instance*/, uint16_t /*port*/) { return OT_ERROR_NONE; }
otError otCoapStop(otInstance * /*instance*/) { return OT_ERROR_NONE; }
void otCoapAddResource(otInstance * /*instance*/, otCoapResource * /*resource*/) {}
void otCoapRemoveResource(otInstance * /*instance*/, otCoapResource * /*resource*/) {}

otMessage *otCoapNewMessage(otInstance * /*instance*/, const otMessageInfo * /*info*/) {
  return reinterpret_cast<otMessage *>(alloc_message());
}

void otMessageFree(otMessage * /*msg*/) {}

otError otMessageAppend(otMessage *msg, const void *buf, uint16_t len) {
  FakeMessage *m = reinterpret_cast<FakeMessage *>(msg);
  if (m->len + len > sizeof(m->buf)) return OT_ERROR_NO_BUFS;
  memcpy(m->buf + m->len, buf, len);
  m->len += len;
  return OT_ERROR_NONE;
}

uint16_t otMessageGetLength(const otMessage *msg) { return reinterpret_cast<const FakeMessage *>(msg)->len; }
uint16_t otMessageGetOffset(const otMessage * /*msg*/) { return 0; }

uint16_t otMessageRead(const otMessage *msg, uint16_t offset, void *buf, uint16_t len) {
  const FakeMessage *m = reinterpret_cast<const FakeMessage *>(msg);
  if (offset >= m->len) return 0;
  uint16_t avail = m->len - offset;
  if (len > avail) len = avail;
  memcpy(buf, m->buf + offset, len);
  return len;
}

otCoapCode otCoapMessageGetCode(const otMessage *msg) { return reinterpret_cast<const FakeMessage *>(msg)->code; }
otCoapType otCoapMessageGetType(const otMessage *msg) { return reinterpret_cast<const FakeMessage *>(msg)->type; }

otError otCoapMessageInit(otMessage *msg, otCoapType type, otCoapCode code) {
  FakeMessage *m = reinterpret_cast<FakeMessage *>(msg);
  m->type = type;
  m->code = code;
  return OT_ERROR_NONE;
}

otError otCoapMessageInitResponse(otMessage *response, const otMessage *request, otCoapType type, otCoapCode code) {
  FakeMessage *r = reinterpret_cast<FakeMessage *>(response);
  const FakeMessage *q = reinterpret_cast<const FakeMessage *>(request);
  r->type = type;
  r->code = code;
  memcpy(r->token, q->token, q->token_len);
  r->token_len = q->token_len;
  return OT_ERROR_NONE;
}

otError otCoapMessageAppendContentFormatOption(otMessage * /*msg*/, otCoapOptionContentFormat /*fmt*/) {
  return OT_ERROR_NONE;
}

otError otCoapMessageAppendObserveOption(otMessage *msg, uint32_t observe) {
  FakeMessage *m = reinterpret_cast<FakeMessage *>(msg);
  m->has_observe = true;
  m->observe_value = observe;
  return OT_ERROR_NONE;
}

otError otCoapMessageAppendOption(otMessage * /*msg*/, uint16_t /*number*/, uint16_t /*len*/,
                                  const void * /*value*/) {
  return OT_ERROR_NONE;
}

otError otCoapMessageAppendBlock2Option(otMessage * /*msg*/, uint32_t num, bool more, otCoapBlockSzx szx) {
  g_response_has_block2 = true;
  g_response_block2_num = num;
  g_response_block2_more = more;
  g_response_block2_szx = static_cast<int>(szx);
  return OT_ERROR_NONE;
}

void otCoapAddBlockWiseResource(otInstance * /*instance*/, otCoapBlockwiseResource *resource) {
  g_blockwise_resource_registered = true;
  g_blockwise_resource_uri = resource ? resource->mUriPath : nullptr;
}

void otCoapRemoveBlockWiseResource(otInstance * /*instance*/, otCoapBlockwiseResource * /*resource*/) {}

otError otCoapMessageAppendUriPathOptions(otMessage * /*msg*/, const char * /*uri*/) { return OT_ERROR_NONE; }

otError otCoapMessageSetPayloadMarker(otMessage *msg) {
  reinterpret_cast<FakeMessage *>(msg)->has_payload_marker = true;
  return OT_ERROR_NONE;
}

bool otCoapAreTokensEqual(const otMessage *a, const otMessage *b) {
  const FakeMessage *ma = reinterpret_cast<const FakeMessage *>(a);
  const FakeMessage *mb = reinterpret_cast<const FakeMessage *>(b);
  return ma->token_len == mb->token_len && memcmp(ma->token, mb->token, ma->token_len) == 0;
}

uint8_t otCoapMessageGetTokenLength(const otMessage *msg) {
  return reinterpret_cast<const FakeMessage *>(msg)->token_len;
}

const uint8_t *otCoapMessageGetToken(const otMessage *msg) {
  return reinterpret_cast<const FakeMessage *>(msg)->token;
}

static otCoapOption g_block2_option{OT_COAP_OPTION_BLOCK2, 1};

otError otCoapOptionIteratorInit(otCoapOptionIterator *it, const otMessage * /*msg*/) {
  memset(it, 0, sizeof(*it));
  return OT_ERROR_NONE;
}

const otCoapOption *otCoapOptionIteratorGetFirstOptionMatching(otCoapOptionIterator * /*it*/, uint16_t num) {
  if (num == OT_COAP_OPTION_BLOCK2 && g_request_block2_szx >= 0)
    return &g_block2_option;
  return nullptr;
}

const otCoapOption *otCoapOptionIteratorGetNextOptionMatching(otCoapOptionIterator * /*it*/, uint16_t /*num*/) {
  return nullptr;
}

otError otCoapOptionIteratorGetOptionUintValue(otCoapOptionIterator * /*it*/, uint64_t *val) {
  // Encode Block2(NUM=0, M=false, SZX) as the raw uint value: (0 << 4) | (0 << 3) | szx
  if (g_request_block2_szx >= 0)
    *val = static_cast<uint64_t>(g_request_block2_szx & 0x7);
  else
    *val = 0;
  return OT_ERROR_NONE;
}

otError otCoapOptionIteratorGetOptionValue(otCoapOptionIterator * /*it*/, void * /*val*/) { return OT_ERROR_NONE; }

otError otCoapSendResponse(otInstance * /*instance*/, otMessage *msg, const otMessageInfo * /*info*/) {
  FakeMessage *m = reinterpret_cast<FakeMessage *>(msg);
  g_last_response_len = m->len;
  g_last_response_code = static_cast<int>(m->code);
  if (m->len > 0)
    memcpy(g_last_response_buf, m->buf, m->len);
  g_inline_send_called = true;
  return OT_ERROR_NONE;
}

otError otCoapSendResponseBlockWise(otInstance * /*instance*/, otMessage *msg, const otMessageInfo * /*info*/,
                                     void *ctx, otCoapBlockwiseTransmitHook hook) {
  g_last_blockwise_hook = hook;
  g_last_blockwise_ctx = ctx;

  // Drive the hook at 512-byte blocks to assemble the full payload.
  uint8_t assembled[4096];
  uint16_t assembled_len = 0;
  uint32_t pos = 0;
  while (assembled_len < sizeof(assembled)) {
    uint16_t block_len = 512;
    if (assembled_len + block_len > (uint16_t) sizeof(assembled))
      block_len = (uint16_t) (sizeof(assembled) - assembled_len);
    bool more = false;
    otError err = hook(ctx, assembled + assembled_len, pos, &block_len, &more);
    if (err != OT_ERROR_NONE || block_len == 0)
      break;
    assembled_len = (uint16_t) (assembled_len + block_len);
    pos += block_len;
    if (!more)
      break;
  }

  FakeMessage *m = reinterpret_cast<FakeMessage *>(msg);
  g_last_response_code = static_cast<int>(m->code);
  g_last_response_len = assembled_len;
  if (assembled_len > 0)
    memcpy(g_last_response_buf, assembled, assembled_len);
  return OT_ERROR_NONE;
}

otError otCoapSendRequest(otInstance * /*instance*/, otMessage * /*msg*/, const otMessageInfo * /*info*/,
                          otCoapResponseHandler /*handler*/, void * /*ctx*/) {
  return OT_ERROR_NONE;
}

const char *otThreadErrorToString(otError error) {
  switch (error) {
    case OT_ERROR_NONE: return "None";
    case OT_ERROR_FAILED: return "Failed";
    case OT_ERROR_DROP: return "Drop";
    case OT_ERROR_NO_BUFS: return "NoBufs";
    default: return "Unknown";
  }
}

uint32_t otLinkGetPollPeriod(otInstance * /*instance*/) { return 5000; }

bool otIp6IsAddressEqual(const otIp6Address *a, const otIp6Address *b) {
  return memcmp(a->mFields.m8, b->mFields.m8, 16) == 0;
}

void otIp6AddressToString(const otIp6Address *addr, char *buf, uint16_t size) {
  snprintf(buf, size, "%02x%02x::%02x%02x", addr->mFields.m8[0], addr->mFields.m8[1],
           addr->mFields.m8[14], addr->mFields.m8[15]);
}

}  // extern "C"
