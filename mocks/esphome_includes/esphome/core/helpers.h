#pragma once
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

// millis() stub — returns monotonic ms since process start
static inline uint32_t millis() {
  using namespace std::chrono;
  static auto start = steady_clock::now();
  return (uint32_t) duration_cast<milliseconds>(steady_clock::now() - start).count();
}

namespace esphome {

inline uint32_t random_uint32() { return (uint32_t) rand(); }

// ---------------------------------------------------------------------------
// StringRef — immutable view of a string (const char * + length)
// ---------------------------------------------------------------------------
class StringRef {
 public:
  StringRef() : str_(""), len_(0) {}
  StringRef(const char *s) : str_(s ? s : ""), len_(s ? strlen(s) : 0) {}  // NOLINT
  StringRef(const char *s, size_t n) : str_(s), len_(n) {}
  StringRef(const std::string &s) : str_(s.c_str()), len_(s.size()) {}  // NOLINT

  const char *c_str() const { return str_; }
  size_t size() const { return len_; }
  bool empty() const { return len_ == 0; }

 private:
  const char *str_;
  size_t len_;
};

// ---------------------------------------------------------------------------
// FixedVector — single allocation, no reallocation, vector-like API
// ---------------------------------------------------------------------------
template<typename T>
class FixedVector {
 public:
  FixedVector() = default;
  ~FixedVector() = default;

  void init(size_t n) {
    data_ = std::make_unique<T[]>(n);
    capacity_ = n;
    size_ = 0;
  }

  void push_back(T &&val) {
    if (size_ < capacity_) data_[size_++] = std::move(val);
  }
  void push_back(const T &val) {
    if (size_ < capacity_) data_[size_++] = val;
  }

  T &operator[](size_t i) { return data_[i]; }
  const T &operator[](size_t i) const { return data_[i]; }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  T *begin() { return data_.get(); }
  T *end() { return data_.get() + size_; }
  const T *begin() const { return data_.get(); }
  const T *end() const { return data_.get() + size_; }

 private:
  std::unique_ptr<T[]> data_;
  size_t capacity_{0};
  size_t size_{0};
};

// ---------------------------------------------------------------------------
// LazyCallbackManager — deferred std::vector of callbacks
// ---------------------------------------------------------------------------
template<typename... Ts>
class LazyCallbackManager;

template<typename... Args>
class LazyCallbackManager<void(Args...)> {
 public:
  template<typename F>
  void add(F &&f) {
    callbacks_.emplace_back(std::forward<F>(f));
  }

  void call(Args... args) {
    for (auto &cb : callbacks_) cb(args...);
  }

  void operator()(Args... args) { call(args...); }

 private:
  std::vector<std::function<void(Args...)>> callbacks_;
};

// ---------------------------------------------------------------------------
// CallbackManager — same interface, always allocated
// ---------------------------------------------------------------------------
template<typename... Ts>
class CallbackManager;

template<typename... Args>
class CallbackManager<void(Args...)> : public LazyCallbackManager<void(Args...)> {};

}  // namespace esphome
