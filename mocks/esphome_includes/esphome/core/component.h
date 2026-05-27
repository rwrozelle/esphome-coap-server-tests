#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "esphome/core/log.h"

namespace esphome {

class Component {
 public:
  virtual ~Component() = default;

  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual bool teardown() { return true; }
  virtual float get_setup_priority() const { return 0.0f; }

  void mark_failed() { failed_ = true; }
  void mark_failed(const char *) { failed_ = true; }
  void mark_failed(const LogString *) { failed_ = true; }
  bool is_failed() const { return failed_; }

  // No-op scheduler stubs — tests don't need real timing
  void set_timeout(const std::string & /*name*/, uint32_t /*timeout*/, std::function<void()> &&) {}
  void set_timeout(const char * /*name*/, uint32_t /*timeout*/, std::function<void()> &&) {}
  void set_timeout(uint32_t /*timeout*/, std::function<void()> &&) {}
  void cancel_timeout(const std::string &) {}
  void cancel_timeout(const char *) {}

  void set_interval(const std::string & /*name*/, uint32_t /*interval*/, std::function<void()> &&) {}
  void set_interval(uint32_t /*interval*/, std::function<void()> &&) {}
  void cancel_interval(const std::string &) {}

 protected:
  bool failed_{false};
};

}  // namespace esphome
