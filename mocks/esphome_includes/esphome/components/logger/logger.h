#pragma once
#include <cstdint>

namespace esphome::logger {

class Logger {
 public:
  using LogCallback = void (*)(void *self, uint8_t level, const char *tag, const char *message, size_t message_len);

  void add_log_callback(void *self, LogCallback cb) { self_ = self; cb_ = cb; }

  void emit(uint8_t level, const char *tag, const char *msg, size_t len) {
    if (cb_) cb_(self_, level, tag, msg, len);
  }

 private:
  void *self_{nullptr};
  LogCallback cb_{nullptr};
};

extern Logger *global_logger;  // defined in mock_app.cpp

}  // namespace esphome::logger
