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
    switch (v[0]) {
      case 't':
      case 'y':
        return true;
      case 'f':
      case 'n':
        return false;
      default: {
        LOG(FATAL) << "Invalid config value (must be bool): " << key << "=" << v;
        return false;
      }
    }
  }
}


stats::params::PortMode stats::params::to_port_mode(const std::string& param) {
  if (param == LISTEN_PORT_MODE_SINGLE) {
    return SINGLE_PORT;
  } else if (param == LISTEN_PORT_MODE_EPHEMERAL) {
    return EPHEMERAL_PORTS;
  } else if (param == LISTEN_PORT_MODE_RANGE) {
    return PORT_RANGE;
  }
  return UNKNOWN_PORT_MODE;
}

std::string stats::params::get_str(
    const mesos::Parameters& parameters, const std::string& key, const std::string& default_value) {
  for (const mesos::Parameter& parameter : parameters.parameter()) {
    if (parameter.key() == key) {
      const std::string& v = parameter.value();
      if (v.empty()) {
        LOG(FATAL) << "Invalid config value (must be non-empty): " << key << "=" << v;
      }
      return v;
    }
  }
  return default_value;
}

size_t stats::params::get_uint(
    const mesos::Parameters& parameters, const std::string& key, size_t default_value) {
  for (const mesos::Parameter& parameter : parameters.parameter()) {
    if (parameter.key() == key) {
      return to_uint(key, parameter.value());
    }
  }
  return default_value;
}

bool stats::params::get_bool(
    const mesos::Parameters& parameters, const std::string& key, bool default_value) {
  for (const mesos::Parameter& parameter : parameters.parameter()) {
    if (parameter.key() == key) {
      return to_bool(key, parameter.value());
    }
  }
  return default_value;
}
