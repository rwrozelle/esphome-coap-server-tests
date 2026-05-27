#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include "esphome/core/helpers.h"

namespace esphome {

static constexpr size_t MAX_DEVICE_CLASS_LENGTH = 48;

class EntityBase {
 public:
  EntityBase() = default;
  virtual ~EntityBase() = default;

  void set_name(const char *name) { name_ = name; }
  StringRef get_name() const { return StringRef(name_); }

  void set_object_id(const char *id) { object_id_ = id; }
  const char *get_object_id() const { return object_id_; }

  uint32_t get_object_id_hash() const { return object_id_hash_; }
  void set_object_id_hash(uint32_t h) { object_id_hash_ = h; }

  bool is_internal() const { return internal_; }
  void set_internal(bool i) { internal_ = i; }

  // Returns the device class as a C string written to buffer; empty string if none.
  virtual const char *get_device_class_to(std::span<char, MAX_DEVICE_CLASS_LENGTH> buffer) const {
    buffer[0] = '\0';
    return buffer.data();
  }

 protected:
  const char *name_{""};
  const char *object_id_{""};
  uint32_t object_id_hash_{0};
  bool internal_{false};
};

}  // namespace esphome
