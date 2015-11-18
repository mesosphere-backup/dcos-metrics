#pragma once

#include <memory>

#include <boost/asio.hpp>
#include <mesos/mesos.pb.h>
#include <stout/try.hpp>

#include "mesos_hash.hpp"
#include "port_writer.hpp"
#include "udp_endpoint.hpp"

namespace stats {
  class PortReader {
   public:
    PortReader(boost::asio::io_service& io_service,
        std::shared_ptr<PortWriter> port_writer,
        const UDPEndpoint& requested_endpoint,
        bool annotations_enabled);
    virtual ~PortReader();

    Try<UDPEndpoint> open();
    Try<UDPEndpoint> endpoint() const;

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info);
    void unregister_container(const mesos::ContainerID& container_id);

   private:
    void start_recv();
    void recv_cb(const boost::system::error_code& ec, std::size_t bytes_transferred);

    const std::shared_ptr<PortWriter> port_writer;
    const UDPEndpoint requested_endpoint;
    const bool annotations_enabled;

    boost::asio::io_service& io_service;
    boost::asio::ip::udp::socket socket;
    char* buffer;

    std::unique_ptr<UDPEndpoint> actual_endpoint;
    container_id_set registered_container_ids;
  };
}
