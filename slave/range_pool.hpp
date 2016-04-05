#pragma once

#include <stddef.h>
#include <sstream>
#include <vector>

#include <stout/try.hpp>

namespace stats {

  /**
   * A pool of integer values within an inclusive range [start, end].
   */
  class RangePool {
   public:
    RangePool(size_t start, size_t end)
      : start(start),
        used((end + 1) - start, false) { }

    /**
     * Reserves and returns an integer value from the pool.
     * Returns an error if no available values remain.
     */
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
      oss << "Port range " << start << "-" << range_end() << " has been depleted.";
      return Try<size_t>(Error(oss.str()));
    }

    /**
     * Returns a previously-taken value to the pool.
     */
    void put(size_t port) {
      if (port < start) {
        LOG(FATAL) << "Returned port " << port << " is smaller than min port " << start;
        return;
      }
      size_t idx = port - start;
      if (idx >= used.size()) {
        LOG(FATAL) << "Returned port " << port << " is larger than max port " << range_end();
        return;
      }
      if (!used[idx]) {
        LOG(FATAL) << "Returned port " << port << " isn't marked as being used.";
        return;
      }
      used[idx] = false;
    }

   private:
    size_t range_end() const {
      return start + used.size() - 1;
    }

    const size_t start;
    std::vector<bool> used;
  };

}
