#include "nvs.h"

// Singleton
MockNvs &MockNvs::instance() {
  static MockNvs inst;
  return inst;
}

// C-linkage implementations delegate to the mock
extern "C" {

int nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out_handle) {
  return MockNvs::instance().nvs_open(name, mode, out_handle);
}

int nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value) {
  return MockNvs::instance().nvs_get_u32(handle, key, out_value);
}

int nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value) {
  return MockNvs::instance().nvs_set_u32(handle, key, value);
}

int nvs_commit(nvs_handle_t handle) {
  return MockNvs::instance().nvs_commit(handle);
}

void nvs_close(nvs_handle_t handle) {
  MockNvs::instance().nvs_close(handle);
}

}  // extern "C"
