#pragma once

#include <sstream>

namespace metrics {
  /**
   * Returns a statsd-formatted metric which has converted the provided 'value' counted over 'period_ms' into
   * a per-second value.
   */
  std::string statsd_counter_per_sec(const std::string& label, size_t value, size_t period_ms);
}
