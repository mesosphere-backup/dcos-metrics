#pragma once

#include <string>
#include <stddef.h>

namespace stats {
  class UDPEndpoint {
   public:
    UDPEndpoint(const std::string& host, size_t port)
      : host(host), port(port) { }
    virtual ~UDPEndpoint() { }

    const std::string host;
    const size_t port;
  };
}
