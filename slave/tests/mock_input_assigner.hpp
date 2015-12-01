#pragma once

#include <list>

#include <gmock/gmock.h>
#include <stout/try.hpp>
#include <mesos/mesos.pb.h>
#include <mesos/slave/isolator.pb.h>

#include "udp_endpoint.hpp"

class MockInputAssigner {
 public:
  MOCK_METHOD2(register_container,
      void(const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info));

  MOCK_METHOD1(register_containers,
      void(const std::list<mesos::slave::ContainerState>& containers));

  MOCK_METHOD1(unregister_container,
      void(const mesos::ContainerID& container_id));

  MOCK_METHOD1(get_statsd_endpoint,
      Try<stats::UDPEndpoint>(const mesos::ExecutorInfo& executor_info));
};
