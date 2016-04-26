#pragma once

#include <sstream>
#include <string>
#include <stddef.h>

#include "params.hpp"

namespace stats {
  class UDPEndpoint {
   public:
    UDPEndpoint(const std::string& host, size_t port)
      : host(host), port(port) { }
    virtual ~UDPEndpoint() { }

    std::string string() const {
      std::ostringstream oss;
      oss << host << ":" << port;
      return oss.str();
    }

    bool operator==(const UDPEndpoint& other) const {
      return port == other.port && host == other.host;
    }

    const std::string host;
    const size_t port;
  };
}
