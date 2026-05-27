#pragma once
#include "esphome/core/entity_base.h"

namespace esphome::lock {

enum LockState : uint8_t {
  LOCK_STATE_NONE = 0,
  LOCK_STATE_LOCKED = 1,
  LOCK_STATE_UNLOCKED = 2,
  LOCK_STATE_JAMMED = 3,
  LOCK_STATE_LOCKING = 4,
  LOCK_STATE_UNLOCKING = 5,
  LOCK_STATE_OPENING = 6,
  LOCK_STATE_OPEN = 7,
};

class Lock;

class LockCall {
 public:
  explicit LockCall(Lock * /*parent*/) {}
};

class Lock : public EntityBase {
 public:
  LockState state{LOCK_STATE_NONE};

  virtual void control(const LockCall & /*call*/) {}

  void lock() { state = LOCK_STATE_LOCKED; }
  void unlock() { state = LOCK_STATE_UNLOCKED; }
  void open() { state = LOCK_STATE_OPENING; }
};

}  // namespace esphome::lock
