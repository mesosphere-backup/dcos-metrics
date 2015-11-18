#pragma once

#include <list>
#include <memory>

#include <mesos/mesos.pb.h>
#include <mesos/slave/isolator.pb.h>
#include <stout/try.hpp>

#include "udp_endpoint.hpp"

namespace stats {
  class InputAssignerImpl;

  /**
   * Assigns containers to monitoring ports, according to the strategy in the provided Parameters.
   */
  class InputAssigner {
   public:
    static std::shared_ptr<InputAssigner> get(const mesos::Parameters& parameters);

    virtual ~InputAssigner();

    /**
     * Registers a container which is about to be brought up.
     */
    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info);
    /**
     * See register_container().
     */
    std::list<Try<UDPEndpoint>> register_containers(
        const std::list<mesos::slave::ContainerState>& containers);
    /**
     * Removes a registered container which is now being brought down.
     */
    void unregister_container(const mesos::ContainerID& container_id);

    /**
     * Returns the assigned statsd endpoint for a registered container.
     */
    Try<UDPEndpoint> get_statsd_endpoint(
        const mesos::ExecutorInfo& executor_info);

   private:
    InputAssigner(const mesos::Parameters& parameters);

    std::unique_ptr<InputAssignerImpl> impl;
  };
}
