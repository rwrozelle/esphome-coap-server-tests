#pragma once
#include <cstdint>
#include <string>

void mock_ot_reset_last_response();
uint16_t mock_ot_last_response_len();
std::string mock_ot_last_response_str();
int mock_ot_last_response_code();
