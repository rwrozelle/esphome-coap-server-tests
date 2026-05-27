#include "openthread/coap.h"
#include "openthread/ip6.h"
#include <cstdio>
#include <cstring>

// Minimal stub implementations of OpenThread CoAP API for host unit tests.
// Tests that need to verify CoAP interactions can replace these with gmock objects
// using the link-seam pattern (extern pointer + override in test body).

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

otError otCoapOptionIteratorInit(otCoapOptionIterator *it, const otMessage * /*msg*/) {
  memset(it, 0, sizeof(*it));
  return OT_ERROR_NONE;
}

const otCoapOption *otCoapOptionIteratorGetFirstOptionMatching(otCoapOptionIterator * /*it*/, uint16_t /*num*/) {
  return nullptr;
}

const otCoapOption *otCoapOptionIteratorGetNextOptionMatching(otCoapOptionIterator * /*it*/, uint16_t /*num*/) {
  return nullptr;
}

otError otCoapOptionIteratorGetOptionUintValue(otCoapOptionIterator * /*it*/, uint64_t *val) {
  *val = 0;
  return OT_ERROR_NONE;
}

otError otCoapOptionIteratorGetOptionValue(otCoapOptionIterator * /*it*/, void * /*val*/) { return OT_ERROR_NONE; }

otError otCoapSendResponse(otInstance * /*instance*/, otMessage * /*msg*/, const otMessageInfo * /*info*/) {
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
