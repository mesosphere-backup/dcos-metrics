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
      LOG(WARNING) << oss.str();
      return Try<size_t>(Error(oss.str()));
    }

    /**
     * Reserves and returns a specific integer value from the pool.
     * Returns an error if the requested value is unavailable.
     */
    Try<size_t> get(size_t value) {
      if (value < start) {
        std::ostringstream oss;
        oss << "Requested value " << value << " is smaller than min value " << start;
        LOG(WARNING) << oss.str();
        return Try<size_t>(Error(oss.str()));
      }
      size_t idx = value - start;
      if (idx >= used.size()) {
        std::ostringstream oss;
        oss << "Requested value " << value << " is larger than max value " << range_end();
        LOG(WARNING) << oss.str();
        return Try<size_t>(Error(oss.str()));
      }
      if (used[idx]) {
        std::ostringstream oss;
        oss << "Requested value " << value << " is already marked as being used.";
        LOG(WARNING) << oss.str();
        return Try<size_t>(Error(oss.str()));
      }
      used[idx] = true;
      return value;
    }

    /**
     * Returns a previously-taken value to the pool.
     */
    void put(size_t value) {
      // Use fatal errors: We REALLY shouldn't be getting back numbers that we didn't hand out!
      if (value < start) {
        LOG(FATAL) << "Returned value " << value << " is smaller than min value " << start;
        return;
      }
      size_t idx = value - start;
      if (idx >= used.size()) {
        LOG(FATAL) << "Returned value " << value << " is larger than max value " << range_end();
        return;
      }
      if (!used[idx]) {
        LOG(FATAL) << "Returned value " << value << " isn't marked as being used.";
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
