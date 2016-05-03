#pragma once

#include <boost/asio.hpp>

#include "mesos_hash.hpp"
#include "container_reader.hpp"
#include "output_writer.hpp"

namespace stats {
  /**
   * The default/prod implementation of ContainerReader.
   * PortWriter is templated out to allow for easy mockery of PortWriter in tests.
   */
  class ContainerReaderImpl : public ContainerReader {
   public:
    ContainerReaderImpl(
        const std::shared_ptr<boost::asio::io_service>& io_service,
        const std::shared_ptr<OutputWriter>& output_writer,
        const UDPEndpoint& requested_endpoint);
    virtual ~ContainerReaderImpl();

    Try<UDPEndpoint> open();

    Try<UDPEndpoint> endpoint() const;

    void register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info);

    void unregister_container(const mesos::ContainerID& container_id);

   private:
    typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

    void start_recv();
    void recv_cb(boost::system::error_code ec, size_t bytes_transferred);
    void write_message(const char* data, size_t size);
    void shutdown_cb();

    const std::shared_ptr<OutputWriter> output_writer;
    const UDPEndpoint requested_endpoint;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::ip::udp::socket socket;
    std::vector<char> socket_buffer;
    udp_endpoint_t sender_endpoint;

    std::unique_ptr<UDPEndpoint> actual_endpoint;
    container_id_map<mesos::ExecutorInfo> registered_containers;
  };
}
