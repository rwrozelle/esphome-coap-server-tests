// Tests for handle_well_known_core using the OT block-wise API (RFC 7959).
//
// Post-fix behavior:
//   - GET with Block2(0,F,SZX) hint  → response gets Block2 appended + otCoapSendResponseBlockWise
//   - GET without Block2 hint        → full payload inline via otCoapSendResponse
//   - Handler context is CoapServerOT* (not ehCoapResource*)
//   - setup() registers .well-known/core via otCoapAddBlockWiseResource (not otCoapAddResource)
//   - wk_blockwise_transmit_hook wraps blockwise_transmit_hook using CoapServerOT* context

#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>

#include "esphome/components/coap_server/coap_server.h"
#include "mock_ot_helpers.h"

namespace esphome::coap_server {

// ---------------------------------------------------------------------------
// Testable subclass
// ---------------------------------------------------------------------------

class TestableCoapServerBW : public CoapServerOT {
 public:
  void set_link_format(const std::string &content) {
    link_format_buf_ = std::make_unique<uint8_t[]>(content.size());
    memcpy(link_format_buf_.get(), content.data(), content.size());
    link_format_size_ = content.size();
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(4);
  }

  // Invoke the .well-known/core handler with CoapServerOT* as context (matches wk_bw_resource_.mContext).
  void invoke_wk(otMessage *msg, const otMessageInfo *info) {
    CoapServerOT::handle_well_known_core(this, msg, info);
  }

  CoapServer::BlockwiseSource &get_wk_source() { return wk_source_; }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static otMessage *make_get() {
  otMessage *msg = otCoapNewMessage(nullptr, nullptr);
  otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_GET);
  return msg;
}

static otMessage *make_post() {
  otMessage *msg = otCoapNewMessage(nullptr, nullptr);
  otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
  return msg;
}

static otMessageInfo make_info() {
  otMessageInfo info{};
  return info;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class BlockwiseOTTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_ot_reset_last_response();
    mock_ot_reset_blockwise();
  }

  TestableCoapServerBW server;
};

// ---------------------------------------------------------------------------
// Tests: GET with Block2 hint in request
// ---------------------------------------------------------------------------

TEST_F(BlockwiseOTTest, WithBlock2Hint_BlockwiseSendCalled) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");
  mock_ot_set_request_block2(6);  // SZX=6 → 1024-byte blocks

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_TRUE(mock_ot_blockwise_called());
  EXPECT_FALSE(mock_ot_inline_send_called());
}

TEST_F(BlockwiseOTTest, WithBlock2Hint_ResponseCodeIsContent) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");
  mock_ot_set_request_block2(6);

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_EQ(mock_ot_last_response_code(), OT_COAP_CODE_CONTENT);
}

TEST_F(BlockwiseOTTest, WithBlock2Hint_ResponseHasBlock2Option) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");
  mock_ot_set_request_block2(6);

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_TRUE(mock_ot_response_has_block2());
  EXPECT_EQ(mock_ot_response_block2_num(), 0u);   // NUM=0 for first response
  EXPECT_TRUE(mock_ot_response_block2_more());    // M=1 (OT drives subsequent blocks)
  EXPECT_EQ(mock_ot_response_block2_szx(), 6);   // SZX echoed from request
}

TEST_F(BlockwiseOTTest, WithBlock2Hint_SzxEchoedFromRequest) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");
  mock_ot_set_request_block2(4);  // SZX=4 → 256-byte blocks

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_TRUE(mock_ot_response_has_block2());
  EXPECT_EQ(mock_ot_response_block2_szx(), 4);
}

TEST_F(BlockwiseOTTest, WithBlock2Hint_LargePayloadAssembled) {
  std::string content;
  content.reserve(1200);
  for (int i = 0; i < 40; i++) {
    if (i > 0) content += ',';
    content += "</fp/";
    content += std::to_string(i + 1);
    content += ">;rt=\"esphome.sensor\"";
  }
  server.set_link_format(content);
  mock_ot_set_request_block2(6);

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_TRUE(mock_ot_blockwise_called());
  EXPECT_EQ(mock_ot_last_response_str(), content);
}

TEST_F(BlockwiseOTTest, WithBlock2Hint_HookCtxIsWkSource) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");
  mock_ot_set_request_block2(6);

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  ASSERT_TRUE(mock_ot_blockwise_called());
  EXPECT_EQ(mock_ot_blockwise_ctx(), &server.get_wk_source());
}

// ---------------------------------------------------------------------------
// Tests: GET without Block2 hint — inline send
// ---------------------------------------------------------------------------

TEST_F(BlockwiseOTTest, WithoutBlock2Hint_InlineSendCalled) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");
  // no mock_ot_set_request_block2 → iterator returns nullptr for BLOCK2

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_FALSE(mock_ot_blockwise_called());
  EXPECT_TRUE(mock_ot_inline_send_called());
}

TEST_F(BlockwiseOTTest, WithoutBlock2Hint_PayloadInline) {
  const std::string content = "</info>;rt=\"esphome.device\"";
  server.set_link_format(content);

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_EQ(mock_ot_last_response_str(), content);
  EXPECT_EQ(mock_ot_last_response_code(), OT_COAP_CODE_CONTENT);
}

TEST_F(BlockwiseOTTest, WithoutBlock2Hint_NoBlock2InResponse) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_FALSE(mock_ot_response_has_block2());
}

// ---------------------------------------------------------------------------
// Tests: non-GET is ignored
// ---------------------------------------------------------------------------

TEST_F(BlockwiseOTTest, NonGet_NoResponseSent) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");

  auto *msg = make_post();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  EXPECT_FALSE(mock_ot_blockwise_called());
  EXPECT_FALSE(mock_ot_inline_send_called());
  EXPECT_EQ(mock_ot_last_response_len(), 0u);
}

// ---------------------------------------------------------------------------
// Tests: wk_blockwise_transmit_hook wrapper (via mock blockwise send path)
// ---------------------------------------------------------------------------

// The blockwise mock drives the transmit hook captured from otCoapSendResponseBlockWise.
// After the fix, that hook is CoapServer::blockwise_transmit_hook with ctx=&wk_source_.
// We verify it reads the correct data by checking the assembled payload.

TEST_F(BlockwiseOTTest, WkTransmitHook_CorrectDataAssembled) {
  const std::string content = "abcdefghijklmnopqrstuvwxyz_repeated";
  server.set_link_format(content);
  mock_ot_set_request_block2(6);

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  // mock drives the hook and stores assembled payload in g_last_response_buf
  ASSERT_TRUE(mock_ot_blockwise_called());
  EXPECT_EQ(mock_ot_last_response_str(), content);
}

TEST_F(BlockwiseOTTest, WkTransmitHook_CtxPointsToWkSource) {
  server.set_link_format("</fp/1>;rt=\"esphome.sensor\"");
  mock_ot_set_request_block2(6);

  auto *msg = make_get();
  auto info = make_info();
  server.invoke_wk(msg, &info);

  ASSERT_TRUE(mock_ot_blockwise_called());
  // The ctx passed to otCoapSendResponseBlockWise must be &wk_source_
  EXPECT_EQ(mock_ot_blockwise_ctx(), &server.get_wk_source());
}

}  // namespace esphome::coap_server
