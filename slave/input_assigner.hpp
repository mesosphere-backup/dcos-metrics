#pragma once

#include <list>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>
#include <mesos/slave/isolator.pb.h>
#include <stout/try.hpp>

#include "mesos_hash.hpp"
#include "port_reader.hpp"
#include "udp_endpoint.hpp"

namespace stats {
  class RangePool;

  /**
   * Base class for a port assignment strategy. See below for implementations.
   */
  class InputAssigner {
   public:
    InputAssigner(const mesos::Parameters& parameters);
    virtual ~InputAssigner();

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info);

    std::list<Try<stats::UDPEndpoint>> register_containers(
        const std::list<mesos::slave::ContainerState>& containers);

    void unregister_container(const mesos::ContainerID& container_id);

    Try<UDPEndpoint> get_statsd_endpoint(const mesos::ExecutorInfo& executor_info);

   protected:
    virtual Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) = 0;
    virtual void _unregister_container(const mesos::ContainerID& container_id) = 0;
    virtual Try<UDPEndpoint> _get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) = 0;

    const std::string listen_host;
    const bool annotations_enabled;

    boost::asio::io_service io_service;
    std::shared_ptr<PortWriter> writer;

   private:
    void run_io_service();

    std::unique_ptr<std::thread> io_service_thread;
    std::mutex mutex;
  };

  /**
   * Listen on a single port across all containers in the slave.
   * IP-per-container should use this, then register individual container hosts inside the Reader.
   */
  class SinglePortAssigner : public InputAssigner {
   public:
    SinglePortAssigner(const mesos::Parameters& parameters);
    virtual ~SinglePortAssigner();

   protected:
    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void _unregister_container(const mesos::ContainerID& container_id);
    Try<UDPEndpoint> _get_statsd_endpoint(const mesos::ExecutorInfo& executor_info);

   private:
    // The port to listen on, passed to all containers.
    const size_t single_port_value;
    // The sole reader shared by all containers. Any per-container mapping (eg per-IP) is done internally.
    std::shared_ptr<port_reader_t> single_port_reader;
  };

  /**
   * Listen on ephemeral ports, dynamically-allocated and assigned by the kernel, one per container.
   * Use this unless you have some kind of localhost firewall to worry about.
   */
  class EphemeralPortAssigner : public InputAssigner {
   public:
    EphemeralPortAssigner(const mesos::Parameters& parameters);
    virtual ~EphemeralPortAssigner();

   protected:
    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void _unregister_container(const mesos::ContainerID& container_id);
    Try<UDPEndpoint> _get_statsd_endpoint(const mesos::ExecutorInfo& executor_info);

   private:
    // Temporary mapping of executor_id to container_id. Used to bridge a call to register_container(), followed by a call to get_statsd_endpoint().
    executor_id_map<mesos::ContainerID> executor_to_container;
    // Long-term mapping of container_id to the port reader assigned to that container. This mapping exists for the lifespan of the container.
    container_id_map<std::shared_ptr<port_reader_t>> container_to_reader;
  };

  /**
   * Listen on a limited range of predefined ports, one per container.
   * Use this if you need to whitelist a specific range of ports.
   */
  class PortRangeAssigner : public InputAssigner {
   public:
    PortRangeAssigner(const mesos::Parameters& parameters);
    virtual ~PortRangeAssigner();

   protected:
    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info);
    void _unregister_container(const mesos::ContainerID& container_id);
    Try<UDPEndpoint> _get_statsd_endpoint(const mesos::ExecutorInfo& executor_info);

   private:
    // Temporary mapping of executor_id to container_id. Used to bridge a call to register_container(), followed by a call to get_statsd_endpoint().
    executor_id_map<mesos::ContainerID> executor_to_container;
    // Long-term mapping of container_id to the port reader assigned to that container. This mapping exists for the lifespan of the container.
    container_id_map<std::shared_ptr<port_reader_t>> container_to_reader;
    // Allocator of ports within a range.
    std::shared_ptr<RangePool> range_pool;
  };
}
