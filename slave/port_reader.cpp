#include "port_reader.hpp"

#include <boost/asio.hpp>
#include <glog/logging.h>

#define RECV_BUFFER_MAX_SIZE 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

typedef boost::asio::ip::udp::resolver resolver_t;
typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

stats::PortReader::PortReader(
    boost::asio::io_service& io_service,
    std::shared_ptr<PortWriter> port_writer,
    const UDPEndpoint& requested_endpoint,
    bool annotations_enabled)
  : port_writer(port_writer),
    requested_endpoint(requested_endpoint),
    annotations_enabled(annotations_enabled),
    io_service(io_service),
    socket(io_service) { }

stats::PortReader::~PortReader() {
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

Try<stats::UDPEndpoint> stats::PortReader::open() {
  if (actual_endpoint) {
    return *actual_endpoint;
  }

  resolver_t resolver(io_service);
  resolver_t::query query(requested_endpoint.host, "");
  boost::system::error_code ec;
  resolver_t::iterator iter = resolver.resolve(query, ec);
  boost::asio::ip::address resolved_address;
  if (!ec && iter != resolver_t::iterator()) {
    // resolved, bind to first entry in list
    resolved_address = iter->endpoint().address();
  } else {
    // failed or no results, fall back to using the host as-is
    resolved_address = boost::asio::ip::address::from_string(requested_endpoint.host);
  }

  socket.bind(udp_endpoint_t(resolved_address, requested_endpoint.port), ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to bind reader socket: " << ec;
    return Try<stats::UDPEndpoint>::error(oss.str());
  }

  std::string actual_address = resolved_address.to_string(ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to stringify resolved address[" << resolved_address << "]: " << ec;
    return Try<stats::UDPEndpoint>::error(oss.str());
  }

  // Set endpoint (indicates open socket) and start listening AFTER all error conditions are clear
  actual_endpoint.reset(new UDPEndpoint(actual_address, requested_endpoint.port));
  start_recv();

  return *actual_endpoint;
}

Try<stats::UDPEndpoint> stats::PortReader::endpoint() const {
  if (actual_endpoint) {
    return *actual_endpoint;
  } else {
    return Try<stats::UDPEndpoint>::error("Not listening on UDP");
  }
}

Try<stats::UDPEndpoint> stats::PortReader::register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& /*executor_info*/) {
  registered_container_ids.insert(container_id);
  return endpoint();
}
void stats::PortReader::unregister_container(const mesos::ContainerID& container_id) {
  registered_container_ids.erase(container_id);
}

void stats::PortReader::start_recv() {
  socket.async_receive(buffer.prepare(RECV_BUFFER_MAX_SIZE),
      std::bind(&PortReader::recv_cb, this, std::placeholders::_1, std::placeholders::_2));
}
void stats::PortReader::recv_cb(const boost::system::error_code& ec, std::size_t bytes_transferred) {
  if (ec) {
    //FIXME handle certain errors here, eg boost::asio::error::message_size.
    LOG(WARNING) << "Error when receiving data from socket at " << actual_endpoint->host << ":" << actual_endpoint->port << ": " << ec;
    start_recv();
    return;
  }

  buffer.commit(bytes_transferred);
  std::istream is(&buffer);
  std::string payload;
  is >> payload;
  switch (registered_container_ids.size()) {
    case 0:
      // TODO add special error case tags
      break;
    case 1:
      // TODO add tags
      break;
    default:
      // TODO add special error case tags
      break;
  }
  port_writer->send(payload);

  start_recv();
}
