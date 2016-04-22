#pragma once

#include "mesos_hash.hpp"
#include "udp_endpoint.hpp"

namespace stats {
  /**
   * Writes container state to disk, so that it can be recovered if the agent is restarted.
   * This interface definition is mainly here for easy mockery.
   */
  class InputStateCache {
   public:
    virtual container_id_map<UDPEndpoint> get_containers() = 0;
    virtual void add_container(
        const mesos::ContainerID& container_id, const UDPEndpoint& endpoint) = 0;
    virtual void remove_container(const mesos::ContainerID& container_id) = 0;
  };
}
