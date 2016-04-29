#pragma once

#include <gmock/gmock.h>

#include "port_reader.hpp"

class MockPortReader : public stats::PortReader {
 public:
  MOCK_METHOD0(open, Try<stats::UDPEndpoint>());
  MOCK_METHOD0(close, void());
  MOCK_CONST_METHOD0(endpoint, Try<stats::UDPEndpoint>());
  MOCK_METHOD2(register_container, void(
          const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info));
  MOCK_METHOD1(unregister_container, void(const mesos::ContainerID& container_id));
};
