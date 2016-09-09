#pragma once

#include <memory>

#include <mesos/slave/containerizer.pb.h>
#include <stout/try.hpp>

#include "mesos_hash.hpp"
#include "container_reader.hpp"
#include "udp_endpoint.hpp"

namespace metrics {
  class IORunner;
  class RangePool;

  /**
   * Base class for a port assignment strategy implementation. Holds common boilerplate
   * code for the different strategy implementations, seen below.
   */
  class ContainerAssignerStrategy {
   public:
    ContainerAssignerStrategy() { }
    virtual ~ContainerAssignerStrategy() { }

    virtual Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) = 0;
    virtual void insert_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info,
        const UDPEndpoint& endpoint) = 0;
    virtual void unregister_container(const mesos::ContainerID& container_id) = 0;
  };

  /**
   * Listens on a single port for all containers in the agent.
   * IP-per-container should use this, then register individual container hosts inside the Reader.
   */
  class SinglePortStrategy : public ContainerAssignerStrategy {
   public:
    /**
     * Creates an uninitialized instance. init() must be called before anything else.
     */
    SinglePortStrategy(
        std::shared_ptr<IORunner> io_runner, const mesos::Parameters& parameters);
    virtual ~SinglePortStrategy();

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void insert_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info,
        const UDPEndpoint& endpoint);
    void unregister_container(const mesos::ContainerID& container_id);

   private:
    Try<std::shared_ptr<ContainerReader>> init_reader();

    std::shared_ptr<IORunner> io_runner;

    // The port to listen on, passed to all containers.
    const size_t single_port_value;
    // The sole reader shared by all containers. Any per-container mapping (eg per-IP) is done
    // internally.
    std::shared_ptr<ContainerReader> single_container_reader;
  };

  /**
   * Listen on ephemeral ports, dynamically-allocated and assigned by the kernel, one per container.
   * Use this unless you have some kind of localhost firewall to worry about.
   */
  class EphemeralPortStrategy : public ContainerAssignerStrategy {
   public:
    EphemeralPortStrategy(std::shared_ptr<IORunner> io_runner);
    virtual ~EphemeralPortStrategy();

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void insert_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info,
        const UDPEndpoint& endpoint);
    void unregister_container(const mesos::ContainerID& container_id);

   private:
    std::shared_ptr<IORunner> io_runner;

    // Long-term mapping of container_id to the port reader assigned to that container. This mapping
    // exists for the lifespan of the container.
    container_id_map<std::shared_ptr<ContainerReader>> container_to_reader;
  };

  /**
   * Listen on a limited range of predefined ports, one per container.
   * Use this if you need to whitelist a specific range of ports.
   */
  class PortRangeStrategy : public ContainerAssignerStrategy {
   public:
    /**
     * Creates an uninitialized instance. init() must be called before anything else.
     */
    PortRangeStrategy(std::shared_ptr<IORunner> io_runner, const mesos::Parameters& parameters);
    virtual ~PortRangeStrategy();

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void insert_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info,
        const UDPEndpoint& endpoint);
    void unregister_container(const mesos::ContainerID& container_id);

   private:
    std::shared_ptr<IORunner> io_runner;

    // Long-term mapping of container_id to the port reader assigned to that container. This mapping
    // exists for the lifespan of the container.
    container_id_map<std::shared_ptr<ContainerReader>> container_to_reader;
    // Allocator of ports within a range.
    std::shared_ptr<RangePool> range_pool;
  };
}
