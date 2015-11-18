#include "port_writer.hpp"

#include <glog/logging.h>

#define SEND_BUFFER_MAX_SIZE 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

typedef boost::asio::ip::udp::resolver resolver_t;

namespace stats {
  size_t select_buffer_size(const mesos::Parameters& parameters) {
    size_t params_size = params::get_uint(parameters, params::DEST_UDP_MAX_BYTES, params::DEST_UDP_MAX_BYTES_DEFAULT);
    if (params_size == 0) {
      LOG(WARNING) << "Ignoring invalid requested UDP packet size " << params_size << ", "
                   << "using " << params::DEST_UDP_MAX_BYTES_DEFAULT;
      return params::DEST_UDP_MAX_BYTES_DEFAULT;
    }
    if (params_size > SEND_BUFFER_MAX_SIZE) {
      LOG(WARNING) << "Ignoring excessive requested UDP packet size " << params_size << ", "
                   << "using " << SEND_BUFFER_MAX_SIZE;
      return SEND_BUFFER_MAX_SIZE;
    }
    return params_size;
  }

  bool add_to_buffer(char* buffer, size_t buffer_size, size_t& buffer_used, const char* to_add, size_t to_add_size) {
    if (to_add_size == 0) {
      return true;
    }
    if (buffer_used == 0) {
      // Buffer is empty, no separator is needed yet.
      if (to_add_size > buffer_size) {
        return false;// Too big
      }
    } else {
      // Appending to existing buffer, with newline char as separator.
      if ((buffer_used + to_add_size + 1) > buffer_size) {// "1" includes newline char
        return false;// Too big
      }
      // Insert a newline separator before adding the new row
      buffer[buffer_used] = '\n';
      ++buffer_used;
    }
    // Add the new data following any existing data.
    memcpy(buffer + buffer_used, to_add, to_add_size);
    buffer_used += to_add_size;
    return true;
  }
}

stats::PortWriter::PortWriter(boost::asio::io_service& io_service, const mesos::Parameters& parameters)
  : send_host(params::get_str(parameters, params::DEST_HOST, params::DEST_HOST_DEFAULT)),
    send_port(params::get_uint(parameters, params::DEST_PORT, params::DEST_PORT_DEFAULT)),
    buffer_size(select_buffer_size(parameters)),
    io_service(io_service),
    flush_timer(io_service),
    socket(io_service),
    buffer((char*) malloc(buffer_size)),
    buffer_used(0) { }

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
  if (socket.is_open()) {
    return true;
  }

  resolver_t resolver(io_service);
  resolver_t::query query(send_host, "");
  boost::system::error_code ec;
  resolver_t::iterator iter = resolver.resolve(query, ec);
  boost::asio::ip::address resolved_address;
  if (!ec && iter != resolver_t::iterator()) {
    // resolved, bind to first entry in list
    resolved_address = iter->endpoint().address();
  } else {
    // failed or no results, fall back to using the host as-is
    resolved_address = boost::asio::ip::address::from_string(send_host);
  }

  send_endpoint = boost::asio::ip::udp::endpoint(resolved_address, send_port);
  socket.open(send_endpoint.protocol());
  if (ec) {
    LOG(ERROR) << "Failed to connect writer socket to "
               << "address[" << resolved_address << ":" << send_port << "]:" << ec;
    return false;
  }

  start_flush_timer();

  return true;
}

void stats::PortWriter::write(const char* bytes, size_t size) {
  if (add_to_buffer(buffer, buffer_size, buffer_used, bytes, size)) {
    // Data fit in buffer, will be flushed when the buffer gets full or when the flush timer goes off.
    return;
  }

  // New data doesn't fit in the buffer's available space. Send/empty the buffer and try again.
  send_raw_bytes(buffer, buffer_used);
  buffer_used = 0;
  if (add_to_buffer(buffer, buffer_size, buffer_used, bytes, size)) {
    // Data fit this time. Again it will get flushed later.
    return;
  }

  // Data is too big to fit even in an empty buffer. Skip the buffer and send the data as-is.
  // Note that the data doesn't jump the 'queue' since we've already sent anything in the current buffer.
  LOG(WARNING) << "Ignoring requested packet max[" << buffer_size << "]: Sending metric of size " << size;
  send_raw_bytes(bytes, size);
}

void stats::PortWriter::start_flush_timer() {
  flush_timer.expires_from_now(boost::posix_time::seconds(1));
  flush_timer.async_wait(std::bind(&PortWriter::flush_cb, this, std::placeholders::_1));
}

void stats::PortWriter::flush_cb(const boost::system::error_code& ec) {
  if (ec) {
    LOG(ERROR) << "Flush timer returned error:" << ec;
    //FIXME: should we abort at this point? this may signal that the process is shutting down
  }

  send_raw_bytes(buffer, buffer_used);
  buffer_used = 0;

  start_flush_timer();
}

void stats::PortWriter::send_raw_bytes(const char* bytes, size_t size) {
  if (size == 0) {
    return;
  }

  if (!socket.is_open()) {
    LOG(WARNING) << "Dropped " << size << " bytes of data due to lack of open writer socket to " << send_host << ":" << send_port;
    return;
  }

  boost::system::error_code ec;
  size_t sent = socket.send_to(boost::asio::buffer(bytes, size), send_endpoint, 0 /* flags */, ec);
  if (ec) {
    LOG(ERROR) << "Failed to send " << size << " bytes of data to " << send_host << ":" << send_port << ": " << ec;
  }
  if (sent != size) {
    LOG(WARNING) << "Sent size=" << sent << " doesn't match requested size=" << size;
  }
}
