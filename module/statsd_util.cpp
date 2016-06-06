#include "statsd_util.hpp"

#define MODULE_STATSD_PREFIX "dcos.metrics.module."

std::string metrics::statsd_counter_per_sec(const std::string& label, size_t value, size_t period_ms) {
  std::ostringstream oss;
  if (period_ms == 0) {
    // give up and return the value directly
    oss << MODULE_STATSD_PREFIX << label << ':' << value << "|g";
  } else {
    // convert counter value to a per-second value, based on the provided interval period
    oss << MODULE_STATSD_PREFIX << label << ':' << value / (period_ms / 1000.) << "|g";
  }
  return oss.str();
}
