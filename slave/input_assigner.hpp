#pragma once

#include <list>
#include <mutex>
#include <unordered_set>

#include <mesos/slave/isolator.pb.h>
#include <stout/try.hpp>

#include "mesos_hash.hpp"
#include "port_reader.hpp"
#include "port_runner.hpp"
#include "udp_endpoint.hpp"

namespace stats {
  class RangePool;

  /**
   * Base class for a port assignment strategy implementation. Holds common boilerplate
   * code for the different strategy implementations, seen below.
   */
  class InputAssigner {
   public:
    InputAssigner(std::shared_ptr<PortRunner> port_runner);
    virtual ~InputAssigner();

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);

    void recover_containers(const std::list<mesos::slave::ContainerState>& containers);

    void unregister_container(const mesos::ContainerID& container_id);

   protected:
    virtual Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) = 0;
    virtual void _unregister_container(const mesos::ContainerID& container_id) = 0;

    std::shared_ptr<PortRunner> port_runner;

   private:
    std::mutex mutex;
  };

  /**
   * Listen on a single port across all containers in the slave.
   * IP-per-container should use this, then register individual container hosts inside the Reader.
   */
  class SinglePortAssigner : public InputAssigner {
   public:
    SinglePortAssigner(
        std::shared_ptr<PortRunner> port_runner, const mesos::Parameters& parameters);
    virtual ~SinglePortAssigner();

   protected:
    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void _unregister_container(const mesos::ContainerID& container_id);

   private:
    // The port to listen on, passed to all containers.
    const size_t single_port_value;
    // The sole reader shared by all containers. Any per-container mapping (eg per-IP) is done
    // internally.
    std::shared_ptr<PortReader> single_port_reader;
  };

  /**
   * Listen on ephemeral ports, dynamically-allocated and assigned by the kernel, one per container.
   * Use this unless you have some kind of localhost firewall to worry about.
   */
  class EphemeralPortAssigner : public InputAssigner {
   public:
    EphemeralPortAssigner(
        std::shared_ptr<PortRunner> port_runner);
    virtual ~EphemeralPortAssigner();

   protected:
    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void _unregister_container(const mesos::ContainerID& container_id);

   private:
    // Long-term mapping of container_id to the port reader assigned to that container. This mapping
    // exists for the lifespan of the container.
    container_id_map<std::shared_ptr<PortReader>> container_to_reader;
  };

  /**
   * Listen on a limited range of predefined ports, one per container.
   * Use this if you need to whitelist a specific range of ports.
   */
  class PortRangeAssigner : public InputAssigner {
   public:
    PortRangeAssigner(
        std::shared_ptr<PortRunner> port_runner, const mesos::Parameters& parameters);
    virtual ~PortRangeAssigner();

   protected:
    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void _unregister_container(const mesos::ContainerID& container_id);

   private:
    // Long-term mapping of container_id to the port reader assigned to that container. This mapping
    // exists for the lifespan of the container.
    container_id_map<std::shared_ptr<PortReader>> container_to_reader;
    // Allocator of ports within a range.
    std::shared_ptr<RangePool> range_pool;
  };
}
