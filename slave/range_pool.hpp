#pragma once

#include <stddef.h>
#include <sstream>
#include <vector>

namespace stats {

  class RangePool {
   public:
    RangePool(size_t start, size_t end)
      : start(start),
        used((end + 1) - start, false) { }

    Try<size_t> take() {
      /* FIXME: instead of prioritizing low-numbered ports, consider prioritizing by age since port was last freed */
      for (size_t i = 0; i < used.size(); ++i) {
        if (!used[i]) {
          used[i] = true;
          return start + i;
        }
      }
      // No ports left!
      std::ostringstream oss;
      oss << "Port range " << port_start() << "-" << port_end() << " has been depleted.";
      return Try<size_t>::error(oss.str());
    }

    void put(size_t port) {
      size_t idx = port - start;
      if (idx >= used.size()) {
        LOG(FATAL) << "Requested port " << port << " exceeds max port " << port_end();
        return;
      }
      used[idx] = false;
    }

    size_t port_start() const {
      return start;
    }
    size_t port_end() const {
      return start + used.size() - 1;
    }

   private:
    const size_t start;
    std::vector<bool> used;
  };

}
