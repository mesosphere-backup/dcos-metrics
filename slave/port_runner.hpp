#pragma once

#include <memory>

#include "port_reader.hpp"

namespace stats {
  /**
   * The PortRunner runs the async scheduler which powers the PortWriter and all PortReaders, while
   * also acting as a factory for PortReaders.
   *
   * This interface class is implemented in port_runner_impl.*. The interface is kept separate from
   * the implementation to allow for easier mocking.
   */
  class PortRunner {
   public:
    virtual ~PortRunner() { }

    /**
     * Utility function to dispatch the provided method against the enclosed async scheduler.
     */
    virtual void dispatch(std::function<void()> func) = 0;

    /**
     * Creates a new PortReader against the provided port which is powered by an internal async
     * scheduler, and which hasn't been open()ed yet.
     */
    virtual std::shared_ptr<PortReader> create_port_reader(size_t port) = 0;
  };
}
