#pragma once
#include <cstdint>
#include <string>

// --- Non-blockwise send ---
void mock_ot_reset_last_response();
uint16_t mock_ot_last_response_len();
std::string mock_ot_last_response_str();
int mock_ot_last_response_code();
bool mock_ot_inline_send_called();  // otCoapSendResponse (not block-wise) was called

// --- Block-wise send ---
void mock_ot_reset_blockwise();
bool mock_ot_blockwise_called();
void *mock_ot_blockwise_ctx();

// --- Block2 option appended to response ---
bool mock_ot_response_has_block2();
uint32_t mock_ot_response_block2_num();
bool mock_ot_response_block2_more();
int mock_ot_response_block2_szx();

// --- Block2 option in request (simulate client hint) ---
// Call before invoke_wk to make the iterator return Block2 with given SZX.
// SZX=6 → 1024-byte blocks.  Call with szx=-1 to clear (no Block2 in request).
void mock_ot_set_request_block2(int szx);

// --- Block-wise resource registration ---
bool mock_ot_blockwise_resource_registered();
const char *mock_ot_blockwise_resource_uri();
