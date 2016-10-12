#pragma once

#include <process/future.hpp>
#include <mesos/mesos.pb.h>

#include "container_reader.hpp"

namespace metrics {
  /**
   * The IORunner runs the async scheduler which powers the PortWriter and all PortReaders, while
   * also acting as a factory for PortReaders.
   *
   * This interface class is implemented in port_runner_impl.*. The interface is kept separate from
   * the implementation to allow for easier mockery.
   */
  class IORunner {
   public:
    virtual ~IORunner() { }

    /**
     * Utility function to dispatch the provided method against the enclosed async scheduler.
     */
    virtual void dispatch(std::function<void()> func) = 0;

    /**
     * Creates a new PortReader against the provided port which is powered by an internal async
     * scheduler, and which hasn't been open()ed yet.
     */
    virtual std::shared_ptr<ContainerReader> create_container_reader(size_t port) = 0;
  };
}
