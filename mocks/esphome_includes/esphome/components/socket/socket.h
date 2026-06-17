#pragma once
// Test-only mock for esphome/components/socket/socket.h.
// Provides a minimal Socket interface.  Tests never call setup()/loop(), so
// CoapServerNet::sock_ stays nullptr and none of the virtual methods are called.
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>

namespace esphome::socket {

class Socket {
 public:
  virtual ~Socket() = default;
  virtual int bind(const struct sockaddr *addr, socklen_t addrlen) = 0;
  virtual int close() = 0;
  virtual ssize_t recvfrom(void *buf, size_t len, struct sockaddr *addr, socklen_t *addr_len) = 0;
  virtual ssize_t sendto(const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) = 0;
  virtual bool ready() const = 0;
  virtual int setblocking(bool blocking) = 0;
  virtual int setsockopt(int level, int optname, const void *optval, socklen_t optlen) = 0;
};

using ListenSocket = Socket;

inline std::unique_ptr<Socket> socket_loop_monitored(int /*domain*/, int /*type*/, int /*protocol*/) {
  return nullptr;
}

}  // namespace esphome::socket
