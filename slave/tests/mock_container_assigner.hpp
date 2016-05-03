#pragma once

#include <list>

#include <gmock/gmock.h>
#include <stout/try.hpp>
#include <mesos/mesos.pb.h>
#include <mesos/slave/isolator.pb.h>

#include "udp_endpoint.hpp"

class MockContainerAssigner {
 public:
  MOCK_METHOD2(register_container,
      Try<stats::UDPEndpoint>(
          const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info));
  MOCK_METHOD1(recover_containers,
      void(const std::list<mesos::slave::ContainerState>& containers));
  MOCK_METHOD1(unregister_container,
      void(const mesos::ContainerID& container_id));
};
