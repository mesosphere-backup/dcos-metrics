#pragma once

#include <list>
#include <memory>
#include <mutex>

#include <mesos/slave/containerizer.hpp>
#include <stout/try.hpp>

#include "udp_endpoint.hpp"

namespace metrics {
  class ContainerAssignerStrategy;
  class ContainerStateCache;
  class IORunner;
  class RangePool;

  /**
   * Base class for a port assignment. Holds common logic for the different strategy
   * implementations.
   */
  class ContainerAssigner {
   public:
    /**
     * Creates an uninitialized instance. init() must be called before anything else.
     */
    ContainerAssigner();

    virtual ~ContainerAssigner();

    /**
     * Must be called before any other access to this instance.
     */
    void init(
        std::shared_ptr<IORunner> io_runner,
        std::shared_ptr<ContainerStateCache> state_cache,
        std::shared_ptr<ContainerAssignerStrategy> strategy);

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void recover_containers(const std::list<mesos::slave::ContainerState>& containers);
    void unregister_container(const mesos::ContainerID& container_id);

   private:
    void recover_containers_imp(const std::list<mesos::slave::ContainerState>& containers);
    Try<UDPEndpoint> register_and_update_cache(
        const mesos::ContainerID container_id,
        const mesos::ExecutorInfo executor_info);
    void unregister_and_update_cache(const mesos::ContainerID container_id);

    std::shared_ptr<IORunner> io_runner;
    std::shared_ptr<ContainerStateCache> state_cache;
    std::shared_ptr<ContainerAssignerStrategy> strategy;
    std::mutex mutex;
  };
}
