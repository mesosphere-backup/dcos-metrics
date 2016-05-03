#pragma once

#include "mesos_hash.hpp"
#include "udp_endpoint.hpp"

namespace stats {
  /**
   * Writes container state to disk, so that it can be recovered if the agent is restarted.
   * This interface definition is mainly here for easy mockery.
   */
  class ContainerStateCache {
   public:
    /**
     * Returns the configured location of the cache for logging purposes.
     */
    virtual const std::string& path() const = 0;

    /**
     * Returns a listing of all currently known containers, mapped to their endpoints.
     */
    virtual container_id_map<UDPEndpoint> get_containers() = 0;

    /**
     * Adds a container (and its endpoint) to the listing.
     * If the container_id already exists, this overrides it with a new endpoint.
     */
    virtual void add_container(
        const mesos::ContainerID& container_id, const UDPEndpoint& endpoint) = 0;

    /**
     * Removes a container (and its endpoint) from the listing.
     * If the container_id doesn't exist, this does nothing.
     */
    virtual void remove_container(const mesos::ContainerID& container_id) = 0;
  };
}
