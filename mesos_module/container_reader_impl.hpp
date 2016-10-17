#pragma once

#include <boost/asio.hpp>

#include "mesos_hash.hpp"
#include "container_reader.hpp"
#include "output_writer.hpp"

namespace metrics {
  /**
   * The default/prod implementation of ContainerReader.
   * PortWriter is templated out to allow for easy mockery of PortWriter in tests.
   */
  class ContainerReaderImpl : public ContainerReader {
   public:
    ContainerReaderImpl(
        const std::shared_ptr<boost::asio::io_service>& io_service,
        const std::vector<output_writer_ptr_t>& writers,
        const UDPEndpoint& requested_endpoint,
        size_t limit_period_ms,
        size_t limit_amount_bytes);
    virtual ~ContainerReaderImpl();

    Try<UDPEndpoint> open();

    Try<UDPEndpoint> endpoint() const;

    void register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info);

    void unregister_container(const mesos::ContainerID& container_id);

   private:
    typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

    void start_limit_reset_timer();
    void limit_reset_cb(boost::system::error_code ec);
    void start_recv();
    void recv_cb(boost::system::error_code ec, size_t bytes_transferred);
    void write_message(const char* data, size_t size);
    void shutdown_cb();

    const std::vector<output_writer_ptr_t> writers;
    const UDPEndpoint requested_endpoint;
    const size_t limit_period_ms;
    const size_t limit_amount_bytes;

    std::shared_ptr<boost::asio::io_service> io_service;
    bool shutdown;
    boost::asio::deadline_timer limit_reset_timer;
    boost::asio::ip::udp::socket socket;
    std::vector<char> socket_buffer;
    udp_endpoint_t sender_endpoint;

    std::unique_ptr<UDPEndpoint> actual_endpoint;
    container_id_map<mesos::ExecutorInfo> registered_containers;

    size_t received_bytes;
    size_t dropped_bytes;
  };
}
