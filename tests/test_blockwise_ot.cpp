// Tests for handle_well_known_core using the OT block-wise API (RFC 7959).
// Verifies that .well-known/core GET responses go through otCoapSendResponseBlockWise
// and that the assembled payload matches link_format_buf_ for both small and large payloads.

#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>

#include "esphome/components/coap_server/coap_server.h"
#include "mock_ot_helpers.h"

namespace esphome::coap_server {

// ---------------------------------------------------------------------------
// Testable subclass — exposes protected state for direct setup
// ---------------------------------------------------------------------------

class TestableCoapServerBW : public CoapServerOT {
 public:
  // Populate link_format_buf_ and wk_source_ from a string, bypassing setup().
  void set_link_format(const std::string &content) {
    link_format_buf_ = std::make_unique<uint8_t[]>(content.size());
    memcpy(link_format_buf_.get(), content.data(), content.size());
    link_format_size_ = content.size();
    instance_ = reinterpret_cast<otInstance *>(1);
    resources_.init(4);
  }

  // Build and expose the well-known resource struct for invoking the handler directly.
  ehCoapResource &wk_resource() { return wk_resource_; }

  void invoke_wk(otMessage *msg, const otMessageInfo *info) {
    wk_resource_.server = this;
    wk_resource_.mUriPath = ".well-known/core";
    wk_resource_.mHandler = &CoapServerOT::handle_well_known_core;
    wk_resource_.mContext = &wk_resource_;
    wk_resource_.oscore_exempt = true;
    CoapServerOT::handle_well_known_core(&wk_resource_, msg, info);
  }

  // Expose wk_source_ for direct inspection in tests
  CoapServer::BlockwiseSource &get_wk_source() { return wk_source_; }

 private:
  ehCoapResource wk_resource_{};
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
// Tests
// ---------------------------------------------------------------------------

TEST(BlockwiseOT, SmallPayload_UsesBlockwiseSend) {
  TestableCoapServerBW srv;
  mock_ot_reset_last_response();
  mock_ot_reset_blockwise();

  const std::string content = "</fp/1/g/1>;rt=\"sensor\"";
  srv.set_link_format(content);

  auto *msg = make_get();
  auto info = make_info();
  srv.invoke_wk(msg, &info);

  EXPECT_TRUE(mock_ot_blockwise_called());
  EXPECT_EQ(mock_ot_last_response_code(), OT_COAP_CODE_CONTENT);
  EXPECT_EQ(mock_ot_last_response_str(), content);
}

TEST(BlockwiseOT, LargePayload_AssemblesCorrectly) {
  TestableCoapServerBW srv;
  mock_ot_reset_last_response();
  mock_ot_reset_blockwise();

  // Build a payload that spans multiple 512-byte blocks (> 1100 bytes)
  std::string content;
  content.reserve(1200);
  for (int i = 0; i < 40; i++) {
    if (i > 0)
      content += ',';
    content += "</fp/";
    content += std::to_string(i + 1);
    content += "/g/1>;rt=\"sensor\"";
  }
  srv.set_link_format(content);

  auto *msg = make_get();
  auto info = make_info();
  srv.invoke_wk(msg, &info);

  EXPECT_TRUE(mock_ot_blockwise_called());
  EXPECT_EQ(mock_ot_last_response_code(), OT_COAP_CODE_CONTENT);
  EXPECT_EQ(mock_ot_last_response_len(), (uint16_t) content.size());
  EXPECT_EQ(mock_ot_last_response_str(), content);
}

TEST(BlockwiseOT, NonGet_NoResponseSent) {
  TestableCoapServerBW srv;
  mock_ot_reset_last_response();
  mock_ot_reset_blockwise();

  srv.set_link_format("</fp/1/g/1>;rt=\"sensor\"");

  auto *msg = make_post();
  auto info = make_info();
  srv.invoke_wk(msg, &info);

  EXPECT_FALSE(mock_ot_blockwise_called());
  EXPECT_EQ(mock_ot_last_response_len(), 0u);
}

TEST(BlockwiseOT, HookCtx_IsWkSource) {
  // Verifies that the ctx pointer OT receives is wk_source_ inside CoapServerOT,
  // which in turn points to link_format_buf_.
  TestableCoapServerBW srv;
  mock_ot_reset_last_response();
  mock_ot_reset_blockwise();

  const std::string content = "</fp/1/g/1>;rt=\"switch\"";
  srv.set_link_format(content);

  auto *msg = make_get();
  auto info = make_info();
  srv.invoke_wk(msg, &info);

  ASSERT_TRUE(mock_ot_blockwise_called());
  // The ctx pointer must be &srv.get_wk_source()
  EXPECT_EQ(mock_ot_blockwise_ctx(), &srv.get_wk_source());
}

TEST(BlockwiseOT, ResponseCode_IsContent) {
  TestableCoapServerBW srv;
  mock_ot_reset_last_response();
  mock_ot_reset_blockwise();

  srv.set_link_format("</fp/1/g/1>;rt=\"binary_sensor\"");

  auto *msg = make_get();
  auto info = make_info();
  srv.invoke_wk(msg, &info);

  EXPECT_EQ(mock_ot_last_response_code(), OT_COAP_CODE_CONTENT);
}

}  // namespace esphome::coap_server
