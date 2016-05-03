#include "container_reader_impl.hpp"

#include <boost/asio.hpp>
#include <glog/logging.h>

#include "sync_util.hpp"

#define UDP_MAX_PACKET_BYTES 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

typedef boost::asio::ip::udp::endpoint udp_endpoint_t;
typedef boost::asio::ip::udp::resolver resolver_t;

stats::ContainerReaderImpl::ContainerReaderImpl(
    const std::shared_ptr<boost::asio::io_service>& io_service,
    const std::shared_ptr<OutputWriter>& output_writer,
    const UDPEndpoint& requested_endpoint)
  : output_writer(output_writer),
    requested_endpoint(requested_endpoint),
    io_service(io_service),
    socket(*io_service),
    socket_buffer(UDP_MAX_PACKET_BYTES, '\0') {
  LOG(INFO) << "Reader constructed for " << requested_endpoint.string();
}

stats::ContainerReaderImpl::~ContainerReaderImpl() {
  LOG(INFO) << "Triggering ContainerReader shutdown";
  if (sync_util::dispatch_run("~ContainerReaderImpl:shutdown",
          *io_service, std::bind(&ContainerReaderImpl::shutdown_cb, this))) {
    LOG(INFO) << "ContainerReader shutdown succeeded";
  } else {
    LOG(ERROR) << "ContainerReader shutdown failed";
  }
}

Try<stats::UDPEndpoint> stats::ContainerReaderImpl::open() {
  if (actual_endpoint) {
    return *actual_endpoint;
  }

  resolver_t resolver(*io_service);
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

  udp_endpoint_t bind_endpoint(resolved_address, requested_endpoint.port);
  socket.open(bind_endpoint.protocol(), ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to open reader socket at endpoint[" << bind_endpoint << "]: " << ec;
    return Try<stats::UDPEndpoint>(Error(oss.str()));
  }

  // Enable SO_REUSEADDR: When mesos-slave is restarted, child processes such as
  // mesos-logrotate-logger and mesos-executor wind up taking ownership of the socket, preventing us
  // from recovering the socket after a mesos-slave restart (result: bind() => EADDRINUSE).
  // Due to this behavior, SO_REUSEADDR is required for the agent to recover its own sockets.
  // To verify that data wasn't being lost after a recovery, the author ran several 'test-sender'
  // tasks and observed that 'loop_gauge' was incrementing without any skipped values.
  socket.set_option(boost::asio::socket_base::reuse_address(true), ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to set bind reader socket at endpoint[" << bind_endpoint << "]: " << ec;
    return Try<stats::UDPEndpoint>(Error(oss.str()));
  }

  socket.bind(bind_endpoint, ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to bind reader socket at endpoint[" << bind_endpoint << "]: " << ec;
    return Try<stats::UDPEndpoint>(Error(oss.str()));
  }

  udp_endpoint_t bound_endpoint = socket.local_endpoint(ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to retrieve reader socket's resulting endpoint for bind at "
        << "endpoint[" << bind_endpoint << "]: " << ec;
    return Try<stats::UDPEndpoint>(Error(oss.str()));
  }

  std::string bound_endpoint_address_str = bound_endpoint.address().to_string(ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to stringify reader socket's "
        << "address[" << bound_endpoint.address() << "]: " << ec;
    return Try<stats::UDPEndpoint>(Error(oss.str()));
  }

  // Set endpoint (indicates open socket) and start listening AFTER all error conditions are clear
  actual_endpoint.reset(new UDPEndpoint(bound_endpoint_address_str, bound_endpoint.port()));
  start_recv();

  LOG(INFO) << "Reader listening on " << actual_endpoint->string();
  return *actual_endpoint;
}

Try<stats::UDPEndpoint> stats::ContainerReaderImpl::endpoint() const {
  if (actual_endpoint) {
    return *actual_endpoint;
  } else {
    return Try<stats::UDPEndpoint>(Error("Not listening on UDP"));
  }
}

void stats::ContainerReaderImpl::register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  registered_containers[container_id] = executor_info;
}

void stats::ContainerReaderImpl::unregister_container(
    const mesos::ContainerID& container_id) {
  registered_containers.erase(container_id);
}

void stats::ContainerReaderImpl::start_recv() {
  socket.async_receive_from(boost::asio::buffer(socket_buffer.data(), socket_buffer.size()),
      sender_endpoint,
      std::bind(&ContainerReaderImpl::recv_cb, this,
          std::placeholders::_1, std::placeholders::_2));
}

void stats::ContainerReaderImpl::recv_cb(
    boost::system::error_code ec, size_t bytes_transferred) {
  if (ec) {
    // FIXME handle certain errors here, eg boost::asio::error::message_size.
    if (boost::asio::error::operation_aborted) {
      // We're being destroyed. Don't look at local state, it may be destroyed already.
      LOG(WARNING) << "Aborted: Exiting read loop immediately";
    } else {
      if (actual_endpoint) {
        LOG(WARNING) << "Error when receiving data from reader socket at "
                     << "dest[" << actual_endpoint->host << ":" << actual_endpoint->port << "] "
                     << "from source[" << sender_endpoint << "]: " << ec;
      } else {
        LOG(WARNING) << "Error when receiving data from reader socket at "
                     << "dest[???] from source[" << sender_endpoint << "]: " << ec;
      }
      start_recv();
    }
    return;
  }

  // Search for newline chars, which indicate multiple statsd entries in a single packet
  char* next_newline = (char*) memchr(socket_buffer.data(), '\n', bytes_transferred);
  if (next_newline == NULL) {
    // Single entry. Pass buffer directly.
    write_message(socket_buffer.data(), bytes_transferred);
  } else {
    // Multiple newline-separated entries.
    // Copy each entry into the scratch buffer, tagging and passing each line separately.
    size_t start_index = 0;
    for (;;) {
      size_t newline_offset = (next_newline != NULL)
        ? next_newline - socket_buffer.data()
        : bytes_transferred; // no more newlines, use end of buffer
      DLOG(INFO) << "newline_offset=" << newline_offset << ", start_index=" << start_index;
      size_t entry_size = newline_offset - start_index;
      DLOG(INFO) << "entry_size " << entry_size << " => copy "
                 << "[" << start_index << "," << start_index+entry_size << ") to front of scratch";
      if (entry_size > 0) { // check/skip empty rows ("\n\n", or "\n" at start/end of pkt)
        write_message(socket_buffer.data() + start_index, entry_size);
      }
      start_index = start_index + entry_size + 1; // pass over newline itself
      if (start_index >= bytes_transferred) {
        break;
      }
      next_newline =
        (char*) memchr(socket_buffer.data() + start_index, '\n', bytes_transferred - start_index);
    }
  }

  start_recv();
}

void stats::ContainerReaderImpl::write_message(const char* data, size_t size) {
  DLOG(INFO) << "Received/forwarded " << size << " byte entry from "
             << "endpoint[" << sender_endpoint << "] => "
             << registered_containers.size() << " containers";
  switch (registered_containers.size()) {
    case 0:
      // No containers assigned to this reader, nothing to pair the data with.
      output_writer->write_container_statsd(NULL, NULL, data, size);
      break;
    case 1:
      // Typical/expected case: One container per UDP port.
      {
        auto container_entry = *registered_containers.cbegin();
        output_writer->write_container_statsd(
            &container_entry.first, &container_entry.second, data, size);
      }
      break;
    default:
      // Multiple containers assigned to this port. Unable to determine which container this
      // data came from.
      // FIXME: This is where ip-per-container support would be added, using the ip provided
      // in the 'endpoint' param.
      output_writer->write_container_statsd(NULL, NULL, data, size);
      break;
  }
}

void stats::ContainerReaderImpl::shutdown_cb() {
  boost::system::error_code ec;
  udp_endpoint_t bound_endpoint = socket.local_endpoint(ec);
  if (ec) {
    LOG(INFO) << "Destroying reader for requested[" << requested_endpoint.string() << "] -> "
              << "actual[???], " << socket.available() << " bytes dropped";
  } else {
    LOG(INFO) << "Destroying reader for requested[" << requested_endpoint.string() << "] -> "
              << "actual[" << bound_endpoint << "], " << socket.available() << " bytes dropped";
  }

  // Flush any remaining data queued in the socket
  while (socket.available()) {
    size_t bytes_transferred =
      socket.receive_from(boost::asio::buffer(socket_buffer.data(), socket_buffer.size()),
          sender_endpoint, 0 /* flags */, ec);
    if (ec) {
      LOG(WARNING) << "Sync receive failed, dropping " << socket.available() << " bytes: " << ec;
      break;
    } else if (bytes_transferred == 0) {
      LOG(WARNING)
        << "Sync receive had no data, dropping " << socket.available() << " bytes: " << ec;
      break;
    } else {
      recv_cb(ec, bytes_transferred);
    }
  }

  // Close the socket
  if (socket.is_open()) {
    socket.close(ec);
    if (ec) {
      LOG(ERROR) << "Error on reader socket close: " << ec;
    }
  }
}
