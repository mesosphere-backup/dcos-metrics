#pragma once

#include <gmock/gmock.h>

#include "container_state_cache.hpp"

class MockContainerStateCache : public metrics::ContainerStateCache {
 public:
  MOCK_CONST_METHOD0(path, const std::string&());
  MOCK_METHOD0(get_containers, metrics::container_id_map<metrics::UDPEndpoint>());
  MOCK_METHOD2(add_container, void(
          const mesos::ContainerID& container_id, const metrics::UDPEndpoint& endpoint));
  MOCK_METHOD1(remove_container, void(const mesos::ContainerID& container_id));
};
