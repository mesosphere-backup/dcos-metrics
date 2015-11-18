#pragma once

#include <boost/asio.hpp>
#include <mesos/mesos.pb.h>

#include "params.hpp"

namespace stats {
  class PortWriter {
   public:
    PortWriter(boost::asio::io_service& io_service, const mesos::Parameters& parameters);
    virtual ~PortWriter();

    bool open();
    void send(const std::string& metric);

   private:
    const std::string send_host;
    const size_t send_port;
    const size_t udp_max_bytes;

    boost::asio::io_service& io_service;
    boost::asio::ip::udp::socket socket;
    boost::asio::streambuf buffer;
  };
}
