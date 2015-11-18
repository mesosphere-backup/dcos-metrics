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
    void write(const char* bytes, size_t size);

   private:
    void start_flush_timer();
    void flush_cb(const boost::system::error_code& ec);
    void send_raw_bytes(const char* bytes, size_t size);

    const std::string send_host;
    const size_t send_port;
    const size_t buffer_size;

    boost::asio::io_service& io_service;
    boost::asio::deadline_timer flush_timer;
    boost::asio::ip::udp::endpoint send_endpoint;
    boost::asio::ip::udp::socket socket;
    char* buffer;
    size_t buffer_used;
  };
}
