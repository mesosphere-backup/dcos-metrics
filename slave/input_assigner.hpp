#pragma once

#include <list>
#include <memory>
#include <mutex>

#include <mesos/slave/isolator.pb.h>
#include <stout/try.hpp>

#include "udp_endpoint.hpp"

namespace stats {
  class InputAssignerStrategy;
  class InputStateCache;
  class IORunner;
  class RangePool;

  /**
   * Base class for a port assignment. Holds common logic for the different strategy
   * implementations.
   */
  class InputAssigner {
   public:
    /**
     * Creates an uninitialized instance. init() must be called before anything else.
     */
    InputAssigner();

    virtual ~InputAssigner();

    /**
     * Must be called before any other access to this instance.
     */
    void init(
        std::shared_ptr<IORunner> io_runner,
        std::shared_ptr<InputStateCache> state_cache,
        std::shared_ptr<InputAssignerStrategy> strategy);

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
    std::shared_ptr<InputStateCache> state_cache;
    std::shared_ptr<InputAssignerStrategy> strategy;
    std::mutex mutex;
  };
}
