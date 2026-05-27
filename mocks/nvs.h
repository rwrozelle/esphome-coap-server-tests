#pragma once
// NVS (Non-Volatile Storage) stub for coap_server host unit tests.
// Real implementations in mock_nvs.cpp — override via gmock in tests.

#include <cstdint>
#include <gmock/gmock.h>

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;

static constexpr int ESP_OK = 0;
static constexpr int ESP_ERR_NVS_NOT_FOUND = 0x1102;

// Mock interface — tests configure expectations via MockNvs::instance()
class MockNvs {
 public:
  static MockNvs &instance();
  MOCK_METHOD(int, nvs_open, (const char *name, nvs_open_mode_t mode, nvs_handle_t *out));
  MOCK_METHOD(int, nvs_get_u32, (nvs_handle_t handle, const char *key, uint32_t *out));
  MOCK_METHOD(int, nvs_set_u32, (nvs_handle_t handle, const char *key, uint32_t value));
  MOCK_METHOD(int, nvs_commit, (nvs_handle_t handle));
  MOCK_METHOD(void, nvs_close, (nvs_handle_t handle));
};

// C-linkage declarations — implemented in mock_nvs.cpp
#ifdef __cplusplus
extern "C" {
#endif
int nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out_handle);
int nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);
int nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
int nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
#ifdef __cplusplus
}
#endif
