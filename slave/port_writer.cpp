#include "port_writer.hpp"

#include <glog/logging.h>

typedef boost::asio::ip::udp::resolver resolver_t;
typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

stats::PortWriter::PortWriter(boost::asio::io_service& io_service, const mesos::Parameters& parameters)
  : send_host(params::get_str(parameters, params::DEST_HOST, params::DEST_HOST_DEFAULT)),
    send_port(params::get_uint(parameters, params::DEST_PORT, params::DEST_PORT_DEFAULT)),
    udp_max_bytes(params::get_uint(parameters, params::DEST_UDP_MAX_BYTES, params::DEST_UDP_MAX_BYTES_DEFAULT)),
    io_service(io_service),
    socket(io_service) {
}

stats::PortWriter::~PortWriter() {
  boost::system::error_code ec;
  socket.shutdown(boost::asio::socket_base::shutdown_type::shutdown_both, ec);
  if (ec) {
    LOG(ERROR) << "Error on reader socket shutdown: " << ec;
  }
  socket.close(ec);
  if (ec) {
    LOG(ERROR) << "Error on reader socket close: " << ec;
  }
}

bool stats::PortWriter::open() {
  //TODO open socket
}

void stats::PortWriter::send(const std::string& metric) {
  //TODO implement sending data to socket (with aggregation)
  //TODO look into scheduling periodic flushes of aggregated data
}
