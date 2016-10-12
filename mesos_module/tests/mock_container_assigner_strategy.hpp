#pragma once

#include <gmock/gmock.h>

#include "container_assigner_strategy.hpp"

class MockContainerAssignerStrategy : public metrics::ContainerAssignerStrategy {
 public:
  MOCK_METHOD2(register_container, Try<metrics::UDPEndpoint>(
          const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info));
  MOCK_METHOD3(insert_container, void(const mesos::ContainerID& container_id,
          const mesos::ExecutorInfo& executor_info, const metrics::UDPEndpoint& endpoint));
  MOCK_METHOD1(unregister_container, void(const mesos::ContainerID& container_id));
};
