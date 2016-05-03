#pragma once

#include <gmock/gmock.h>

#include "container_state_cache.hpp"

class MockContainerStateCache : public stats::ContainerStateCache {
 public:
  MOCK_CONST_METHOD0(path, const std::string&());
  MOCK_METHOD0(get_containers, stats::container_id_map<stats::UDPEndpoint>());
  MOCK_METHOD2(add_container, void(
          const mesos::ContainerID& container_id, const stats::UDPEndpoint& endpoint));
  MOCK_METHOD1(remove_container, void(const mesos::ContainerID& container_id));
};
