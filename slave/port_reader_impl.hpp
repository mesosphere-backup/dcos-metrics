#pragma once

#include <boost/asio.hpp>

#include "mesos_hash.hpp"
#include "port_reader.hpp"

namespace stats {
  /**
   * The default/prod implementation of PortReader.
   * PortWriter is templated out to allow for easy mockery of PortWriter in tests.
   */
  template <typename PortWriter>
  class PortReaderImpl : public PortReader {
   public:
    PortReaderImpl(
        const std::shared_ptr<boost::asio::io_service>& io_service,
        const std::shared_ptr<PortWriter>& port_writer,
        const UDPEndpoint& requested_endpoint,
        bool annotations_enabled);
    virtual ~PortReaderImpl();

    Try<UDPEndpoint> open();

    Try<UDPEndpoint> endpoint() const;

    Try<stats::UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info);

    void unregister_container(const mesos::ContainerID& container_id);

   private:
    typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

    void start_recv();
    void recv_cb(boost::system::error_code ec, size_t bytes_transferred);
    void tag_and_send(std::vector<char>& entry_buffer, size_t size);
    void flush_cb();
    void shutdown_cb();

    const std::shared_ptr<PortWriter> port_writer;
    const UDPEndpoint requested_endpoint;
    const bool annotations_enabled;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::ip::udp::socket socket;
    std::vector<char> buffer, tag_reorder_scratch_buffer, multiline_scratch_buffer;
    udp_endpoint_t sender_endpoint;

    std::unique_ptr<UDPEndpoint> actual_endpoint;
    container_id_map<mesos::ExecutorInfo> registered_containers;
  };
}
