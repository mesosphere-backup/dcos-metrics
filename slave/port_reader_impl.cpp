#include "port_reader_impl.hpp"

#include <boost/asio.hpp>
#include <glog/logging.h>

#include "port_writer.hpp"
#include "sync_util.hpp"
#include "tag_datadog.hpp"
#include "tag_key_prefix.hpp"

#define UDP_MAX_PACKET_BYTES 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

typedef boost::asio::ip::udp::endpoint udp_endpoint_t;
typedef boost::asio::ip::udp::resolver resolver_t;

namespace {
  /**
   * Tags to use when there's a container issue.
   */
  const std::string MISSING_CONTAINER_TAG("missing_container"); // zero matches
  const std::string UNKNOWN_CONTAINER_TAG("unknown_container"); // too many matches

  /**
   * Tag names to use for datadog tags
   */
  const std::string CONTAINER_ID_DATADOG_KEY("container_id");
  const std::string EXECUTOR_ID_DATADOG_KEY("executor_id");
  const std::string FRAMEWORK_ID_DATADOG_KEY("framework_id");
}

template <typename PortWriter>
stats::PortReaderImpl<PortWriter>::PortReaderImpl(
    const std::shared_ptr<boost::asio::io_service>& io_service,
    const std::shared_ptr<PortWriter>& port_writer,
    const UDPEndpoint& requested_endpoint,
    params::annotation_mode::Value annotation_mode)
  : port_writer(port_writer),
    requested_endpoint(requested_endpoint),
    annotation_mode(annotation_mode),
    io_service(io_service),
    socket(*io_service),
    buffer(UDP_MAX_PACKET_BYTES, '\0'),
    tag_reorder_scratch_buffer(/* start empty, grow when needed */),
    multiline_scratch_buffer(/* start empty, grow when needed */) {
  LOG(INFO) << "Reader constructed for " << requested_endpoint.string();
}

template <typename PortWriter>
stats::PortReaderImpl<PortWriter>::~PortReaderImpl() {
  LOG(INFO) << "Triggering PortReader shutdown";
  if (sync_util::dispatch_run("~PortReaderImpl:shutdown",
          *io_service, std::bind(&PortReaderImpl<PortWriter>::shutdown_cb, this))) {
    LOG(INFO) << "PortReader shutdown succeeded";
  } else {
    LOG(ERROR) << "PortReader shutdown failed";
  }
}

template <typename PortWriter>
Try<stats::UDPEndpoint> stats::PortReaderImpl<PortWriter>::open() {
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

template <typename PortWriter>
Try<stats::UDPEndpoint> stats::PortReaderImpl<PortWriter>::endpoint() const {
  if (actual_endpoint) {
    return *actual_endpoint;
  } else {
    return Try<stats::UDPEndpoint>(Error("Not listening on UDP"));
  }
}

template <typename PortWriter>
Try<stats::UDPEndpoint> stats::PortReaderImpl<PortWriter>::register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  registered_containers[container_id] = executor_info;
  return endpoint();
}

template <typename PortWriter>
void stats::PortReaderImpl<PortWriter>::unregister_container(
    const mesos::ContainerID& container_id) {
  registered_containers.erase(container_id);
}

template <typename PortWriter>
void stats::PortReaderImpl<PortWriter>::start_recv() {
  socket.async_receive_from(boost::asio::buffer(buffer.data(), buffer.size()),
      sender_endpoint,
      std::bind(&PortReaderImpl<PortWriter>::recv_cb, this,
          std::placeholders::_1, std::placeholders::_2));
}

template <typename PortWriter>
void stats::PortReaderImpl<PortWriter>::recv_cb(
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
  char* next_newline = (char*) memchr(buffer.data(), '\n', bytes_transferred);
  if (next_newline == NULL) {
    // Single entry. Tag and pass buffer directly.
    tag_and_send(buffer, bytes_transferred);
  } else {
    // Multiple newline-separated entries.
    // Copy each entry into the scratch buffer, tagging and passing each line separately.
    size_t start_index = 0;
    for (;;) {
      size_t newline_offset = (next_newline != NULL)
        ? next_newline - buffer.data()
        : bytes_transferred; // no more newlines, use end of buffer
      DLOG(INFO) << "newline_offset=" << newline_offset << ", start_index=" << start_index;
      size_t entry_size = newline_offset - start_index;
      DLOG(INFO) << "entry_size " << entry_size << " => copy "
                 << "[" << start_index << "," << start_index+entry_size << ") to front of scratch";
      if (multiline_scratch_buffer.empty()) {
        // In practice, most services aren't expected to use multiline packet support. Therefore we
        // leave the multiline buffer empty until a given service has sent a first multiline packet.
        // NOTE: We intentionally just grow right up to full size, to ensure that tag_and_send() has
        // plenty of room to append its tags.
        DLOG(INFO) << "Initialize scratch buffer for multiline to "
                   << UDP_MAX_PACKET_BYTES << " bytes";
        multiline_scratch_buffer.resize(UDP_MAX_PACKET_BYTES, '\0');
      }
      memcpy(multiline_scratch_buffer.data(), buffer.data() + start_index, entry_size);
      if (entry_size > 0) { // skip empty rows ("\n\n", or "\n" at start/end of pkt)
        tag_and_send(multiline_scratch_buffer, entry_size);
      }
      start_index = start_index + entry_size + 1; // pass over newline itself
      if (start_index >= bytes_transferred) {
        break;
      }
      next_newline =
        (char*) memchr(buffer.data() + start_index, '\n', bytes_transferred - start_index);
    }
  }

  start_recv();
}

template <typename PortWriter>
void stats::PortReaderImpl<PortWriter>::tag_and_send(
    std::vector<char>& buffer, size_t size) {
  switch (annotation_mode) {
    case params::annotation_mode::Value::UNKNOWN:
      LOG(FATAL) << "Unknown annotation mode, assuming 'none'.";
      // pass through
    case params::annotation_mode::Value::NONE:
      DLOG(INFO) << "Received/forwarded " << size << " byte entry from "
                 << "endpoint[" << sender_endpoint << "]";
      break;
    case params::annotation_mode::Value::TAG_DATADOG:
      {
        // Prepare the data for our tags, moving any existing tags to the end as needed.
        // Note that we must avoid using a separate buffer from multiline_scratch_buffer, since
        // multiline_scratch_buffer is meant for use by our caller!
        tag_datadog::TagMode tag_mode =
          tag_datadog::prepare_for_tags(buffer.data(), size, tag_reorder_scratch_buffer);

        size_t original_size = size;
        switch (registered_containers.size()) {
          case 0:
            // No containers assigned, nothing to tag this data with.
            tag_datadog::append_tag(buffer, size, MISSING_CONTAINER_TAG, tag_mode);
            break;
          case 1: {
            auto entry = *registered_containers.cbegin();
            tag_datadog::append_tag(buffer, size,
                CONTAINER_ID_DATADOG_KEY, entry.first.value(), tag_mode);
            tag_datadog::append_tag(buffer, size,
                EXECUTOR_ID_DATADOG_KEY, entry.second.executor_id().value(), tag_mode);
            tag_datadog::append_tag(buffer, size,
                FRAMEWORK_ID_DATADOG_KEY, entry.second.framework_id().value(), tag_mode);
            break;
          }
          default:
            // Multiple containers assigned to this port. Unable to determine which container this
            // data came from.
            // FIXME: This is where ip-per-container support would be added, using the ip provided
            // in the 'endpoint' param.
            tag_datadog::append_tag(buffer, size, UNKNOWN_CONTAINER_TAG, tag_mode);
            break;
        }
        DLOG(INFO) << "Received " << original_size << " byte entry from "
                   << "endpoint[" << sender_endpoint << "], "
                   << "forwarding " << size << " bytes with datadog tags";
      }
      break;
    case params::annotation_mode::Value::KEY_PREFIX:
      {
        size_t original_size = size;
        switch (registered_containers.size()) {
          case 0:
            // No containers assigned, nothing to tag this data with.
            tag_key_prefix::prepend_key(buffer, size, MISSING_CONTAINER_TAG);
            break;
          case 1: {
            auto entry = *registered_containers.cbegin();
            // framework id, executor id, container id
            tag_key_prefix::prepend_three_keys(buffer, size,
                entry.second.framework_id().value(),
                entry.second.executor_id().value(),
                entry.first.value());
            break;
          }
          default:
            // Multiple containers assigned to this port. Unable to determine which container this
            // data came from.
            // FIXME: This is where ip-per-container support would be added, using the ip provided
            // in the 'endpoint' param.
            tag_key_prefix::prepend_key(buffer, size, UNKNOWN_CONTAINER_TAG);
            break;
        }
        DLOG(INFO) << "Received " << original_size << " byte entry from "
                   << "endpoint[" << sender_endpoint << "], "
                   << "forwarding " << size << " bytes with key prefix tags";
      }
      break;
  }

  port_writer->write(buffer.data(), size);
}

template <typename PortWriter>
void stats::PortReaderImpl<PortWriter>::shutdown_cb() {
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
      socket.receive_from(boost::asio::buffer(buffer.data(), buffer.size()),
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

// Manually instantiate default prod type
template class stats::PortReaderImpl<stats::PortWriter>;
