#pragma once

#include <memory>

#include <boost/asio.hpp>
#include <mesos/mesos.pb.h>
#include <stout/try.hpp>

#include "mesos_hash.hpp"
#include "udp_endpoint.hpp"

namespace stats {

  /**
   * A PortReader opens a listen port and reads for incoming statsd data. The data may be
   * annotatated with container information before it is passed to the provided PortWriter.
   * In practice, there is one PortReader per listen port. Without ip-per-container, this means
   * there is one PortReader for each container, since each is given a different port. Once
   * ip-per-container is supported, this may change to a single PortReader for all containers, since
   * all may share the same endpoint.
   * This class is templated to allow mockery of PortWriter.
   */
  template <typename PortWriter>
  class PortReader {
   public:
    PortReader(boost::asio::io_service& io_service,
        std::shared_ptr<PortWriter> port_writer,
        const UDPEndpoint& requested_endpoint,
        bool annotations_enabled);
    virtual ~PortReader();

    /**
     * Opens a listen socket at the location specified in the constructor.
     * Returns the resulting endpoint (may vary if eg port=0) on success, or an error otherwise.
     */
    Try<UDPEndpoint> open();

    /**
     * Returns the bound listen endpoint, or an error if the socket isn't open.
     */
    Try<UDPEndpoint> endpoint() const;

    /**
     * Registers the container with the provided information to this reader. The information will
     * be used for tagging data that comes through the listen socket. This interface allows
     * registering multiple containers to a single port/reader, but that behavior is only supported
     * in an ip-per-container scenario.
     * Returns the result of calling endpoint(), or an error if the registration failed.
     */
    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info);

    /**
     * Unregisters the previously registered container of the specified id.
     */
    void unregister_container(const mesos::ContainerID& container_id);

   private:
    typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

    void start_recv();
    void recv_cb(boost::system::error_code ec, std::size_t bytes_transferred);

    const std::shared_ptr<PortWriter> port_writer;
    const UDPEndpoint requested_endpoint;
    const bool annotations_enabled;

    boost::asio::io_service& io_service;
    boost::asio::ip::udp::socket socket;
    char* buffer;
    udp_endpoint_t sender_endpoint;

    std::unique_ptr<UDPEndpoint> actual_endpoint;
    container_id_map<mesos::ExecutorInfo> registered_containers;
  };

  class PortWriter;
  typedef PortReader<PortWriter> port_reader_t;
}
