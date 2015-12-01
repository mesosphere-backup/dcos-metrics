#pragma once

#include <mesos/mesos.pb.h>
#include <stout/try.hpp>

#include "udp_endpoint.hpp"

namespace stats {
  /**
   * A PortReader opens a listen port and reads for incoming statsd data. The data may be
   * annotatated with container information before it is passed to the provided PortWriter.
   * In practice, there is one PortReader per listen port. Without ip-per-container, this means
   * there is one PortReader for each container, since each is given a different port. Once
   * ip-per-container is supported, this may change to a single PortReader for all containers on a
   * mesos-slave, since all may share the same endpoint with distinct source ips.
   *
   * This interface class is implemented in port_reader_impl.*. The interface is kept distinct from
   * the implementation to allow for easier mocking.
   */
  class PortReader {
   public:
    /**
     * Cleanly flushes and shuts down the socket before destruction.
     */
    virtual ~PortReader() { }

    /**
     * Opens a listen socket at the location specified in the constructor.
     * Returns the resulting endpoint (may vary if eg port=0) on success, or an error otherwise.
     */
    virtual Try<UDPEndpoint> open() = 0;

    /**
     * Returns the bound listen endpoint, or an error if the socket isn't open.
     */
    virtual Try<UDPEndpoint> endpoint() const = 0;

    /**
     * Registers the container with the provided information to this reader. The information will
     * be used for tagging data that comes through the listen socket. This interface allows
     * registering multiple containers to a single port/reader, but that behavior is only supported
     * in an ip-per-container scenario.
     * Returns the result of calling endpoint(), or an error if the registration failed.
     */
    virtual Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) = 0;

    /**
     * Unregisters the previously registered container of the specified id.
     */
    virtual void unregister_container(const mesos::ContainerID& container_id) = 0;
  };
}
