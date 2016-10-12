#include "params.hpp"

#include <glog/logging.h>

namespace {
  size_t to_uint(const std::string& key, const std::string& v) {
    char* invalid = NULL;
    long val = strtol(v.c_str(), &invalid, 10);
    if (invalid != NULL && invalid[0] != '\0') {
      LOG(FATAL) << "Invalid config value (must be int): " << key << "=" << v;
    }
    if (val < 0) {
      LOG(FATAL) << "Invalid config value (must be non-negative): " << key << "=" << v;
    }
    return (size_t) val;
  }

  bool to_bool(const std::string& key, const std::string& v) {
    if (v.empty()) {
      LOG(FATAL) << "Invalid config value (must be non-empty): " << key << " = " << v;
      return false;
    }
    switch (v[0]) {
      case 't':
      case 'y':
      case '1':
        return true;
      case 'f':
      case 'n':
      case '0':
        return false;
      default: {
        LOG(FATAL) << "Invalid config value (must start with 't','y','1' (true) or 'f','n','0' (false)): " << key << " = " << v;
        return false;
      }
    }
  }
}


metrics::params::port_mode::Value metrics::params::to_port_mode(const std::string& param) {
  if (param == LISTEN_PORT_MODE_SINGLE) {
    return port_mode::SINGLE;
  } else if (param == LISTEN_PORT_MODE_EPHEMERAL) {
    return port_mode::EPHEMERAL;
  } else if (param == LISTEN_PORT_MODE_RANGE) {
    return port_mode::RANGE;
  }
  return port_mode::UNKNOWN;
}

metrics::params::annotation_mode::Value metrics::params::to_annotation_mode(const std::string& param) {
  if (param == OUTPUT_STATSD_ANNOTATION_MODE_NONE) {
    return annotation_mode::NONE;
  } else if (param == OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG) {
    return annotation_mode::TAG_DATADOG;
  } else if (param == OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX) {
    return annotation_mode::KEY_PREFIX;
  }
  return annotation_mode::UNKNOWN;
}

std::string metrics::params::get_str(
    const mesos::Parameters& parameters, const std::string& key, const std::string& default_value) {
  for (const mesos::Parameter& parameter : parameters.parameter()) {
    if (parameter.key() == key) {
      const std::string& v = parameter.value();
      if (v.empty()) {
        LOG(FATAL) << "Invalid config value (must be non-empty): " << key << " = " << v;
      }
      return v;
    }
  }
  return default_value;
}

size_t metrics::params::get_uint(
    const mesos::Parameters& parameters, const std::string& key, size_t default_value) {
  for (const mesos::Parameter& parameter : parameters.parameter()) {
    if (parameter.key() == key) {
      return to_uint(key, parameter.value());
    }
  }
  return default_value;
}

bool metrics::params::get_bool(
    const mesos::Parameters& parameters, const std::string& key, bool default_value) {
  for (const mesos::Parameter& parameter : parameters.parameter()) {
    if (parameter.key() == key) {
      return to_bool(key, parameter.value());
    }
  }
  return default_value;
}
