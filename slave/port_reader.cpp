#include "port_reader.hpp"

#include <boost/asio.hpp>
#include <glog/logging.h>

#include "port_writer.hpp"

#define RECV_BUFFER_CAPACITY 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

typedef boost::asio::ip::udp::endpoint udp_endpoint_t;
typedef boost::asio::ip::udp::resolver resolver_t;

namespace {
  const std::string TAG_PREFIX("|#");
  const std::string TAG_DIVIDER(",");
  const std::string TAG_KEY_VALUE_SEPARATOR(":");

  const std::string MISSING_CONTAINER_TAG("missing_container");
  const std::string UNKNOWN_CONTAINER_TAG("unknown_container");

  const std::string CONTAINER_ID_KEY("container_id");
  const std::string EXECUTOR_ID_KEY("executor_id");
  const std::string FRAMEWORK_ID_KEY("framework_id");

  bool append_tag(char* buffer, size_t& size,
      const std::string& tag, bool is_first_tag) {
    if (is_first_tag) {
      // <buffer>|#tag
      if (size + TAG_PREFIX.size() + tag.size() > RECV_BUFFER_CAPACITY) {
        return false;
      }
      memcpy(buffer + size, TAG_PREFIX.data(), TAG_PREFIX.size());
      size += TAG_PREFIX.size();
    } else {
      // <buffer>,tag
      if (size + TAG_DIVIDER.size() + tag.size() > RECV_BUFFER_CAPACITY) {
        return false;
      }
      memcpy(buffer + size, TAG_DIVIDER.data(), TAG_DIVIDER.size());
      size += TAG_DIVIDER.size();
    }
    memcpy(buffer + size, tag.data(), tag.size());
    size += tag.size();
    return true;
  }

  bool append_tag(char* buffer, size_t& size,
      const std::string& tag_key, const std::string& tag_value, bool is_first_tag) {
    if (is_first_tag) {
      // <buffer>|#key:value
      if (size + TAG_PREFIX.size() + tag_key.size() + TAG_KEY_VALUE_SEPARATOR.size() + tag_value.size() > RECV_BUFFER_CAPACITY) {
        return false;
      }
      memcpy(buffer + size, TAG_PREFIX.data(), TAG_PREFIX.size());
      size += TAG_PREFIX.size();
    } else {
      // <buffer>,key:value
      if (size + TAG_DIVIDER.size() + tag_key.size() + TAG_KEY_VALUE_SEPARATOR.size() + tag_value.size() > RECV_BUFFER_CAPACITY) {
        return false;
      }
      memcpy(buffer + size, TAG_DIVIDER.data(), TAG_DIVIDER.size());
      size += TAG_DIVIDER.size();
    }
    memcpy(buffer + size, tag_key.data(), tag_key.size());
    size += tag_key.size();
    memcpy(buffer + size, TAG_KEY_VALUE_SEPARATOR.data(), TAG_KEY_VALUE_SEPARATOR.size());
    size += TAG_KEY_VALUE_SEPARATOR.size();
    memcpy(buffer + size, tag_value.data(), tag_value.size());
    size += tag_value.size();
    return true;
  }
}

template <typename PortWriter>
stats::PortReader<PortWriter>::PortReader(
    boost::asio::io_service& io_service,
    std::shared_ptr<PortWriter> port_writer,
    const UDPEndpoint& requested_endpoint,
    bool annotations_enabled)
  : port_writer(port_writer),
    requested_endpoint(requested_endpoint),
    annotations_enabled(annotations_enabled),
    io_service(io_service),
    socket(io_service),
    buffer((char*) malloc(RECV_BUFFER_CAPACITY)) {
  LOG(INFO) << "Reader constructed for " << requested_endpoint.string();
}

template <typename PortWriter>
stats::PortReader<PortWriter>::~PortReader() {
  boost::system::error_code ec;
  udp_endpoint_t bound_endpoint = socket.local_endpoint(ec);
  if (ec) {
    LOG(INFO) << "Destroying reader for requested[" << requested_endpoint.string() << "] -> actual[???]";
  } else {
    LOG(INFO) << "Destroying reader for requested[" << requested_endpoint.string() << "] -> actual[" << bound_endpoint << "]";
  }
  if (socket.is_open()) {
    socket.close(ec);
    if (ec) {
      LOG(ERROR) << "Error on reader socket close: " << ec;
    }
  }
  free(buffer);
  buffer = NULL;
}

template <typename PortWriter>
Try<stats::UDPEndpoint> stats::PortReader<PortWriter>::open() {
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

  udp_endpoint_t bind_endpoint(resolved_address, requested_endpoint.port);
  socket.open(bind_endpoint.protocol(), ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to open reader socket at endpoint[" << bind_endpoint << "]: " << ec;
    return Try<stats::UDPEndpoint>::error(oss.str());
  }
  socket.bind(bind_endpoint, ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to bind reader socket at endpoint[" << bind_endpoint << "]: " << ec;
    return Try<stats::UDPEndpoint>::error(oss.str());
  }

  udp_endpoint_t bound_endpoint = socket.local_endpoint(ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to retrieve reader socket's resulting endpoint for bind at endpoint[" << bind_endpoint << "]: " << ec;
    return Try<stats::UDPEndpoint>::error(oss.str());
  }

  std::string bound_endpoint_address_str = bound_endpoint.address().to_string(ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to stringify reader socket's address[" << bound_endpoint.address() << "]: " << ec;
    return Try<stats::UDPEndpoint>::error(oss.str());
  }

  // Set endpoint (indicates open socket) and start listening AFTER all error conditions are clear
  actual_endpoint.reset(new UDPEndpoint(bound_endpoint_address_str, bound_endpoint.port()));
  start_recv();

  LOG(INFO) << "Reader listening on " << actual_endpoint->string();
  return *actual_endpoint;
}

template <typename PortWriter>
Try<stats::UDPEndpoint> stats::PortReader<PortWriter>::endpoint() const {
  if (actual_endpoint) {
    return *actual_endpoint;
  } else {
    return Try<stats::UDPEndpoint>::error("Not listening on UDP");
  }
}

template <typename PortWriter>
Try<stats::UDPEndpoint> stats::PortReader<PortWriter>::register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  registered_containers[container_id] = executor_info;
  return endpoint();
}

template <typename PortWriter>
void stats::PortReader<PortWriter>::unregister_container(const mesos::ContainerID& container_id) {
  registered_containers.erase(container_id);
}

template <typename PortWriter>
void stats::PortReader<PortWriter>::start_recv() {
  socket.async_receive_from(boost::asio::buffer(buffer, RECV_BUFFER_CAPACITY),
      sender_endpoint,
      std::bind(&PortReader<PortWriter>::recv_cb, this,
          std::placeholders::_1, std::placeholders::_2));
}

template <typename PortWriter>
void stats::PortReader<PortWriter>::recv_cb(boost::system::error_code ec, size_t bytes_transferred) {
  if (ec) {
    // FIXME handle certain errors here, eg boost::asio::error::message_size.
    LOG(WARNING) << "Error when receiving data from reader socket at "
                 << "dest[" << actual_endpoint->host << ":" << actual_endpoint->port << "] from source[" << sender_endpoint << "]: " << ec;
    start_recv();
    return;
  }

  //TODO add support for: 1) multiline packets (add annotation on each line) 2) merging annotations onto any existing tags
  if (annotations_enabled) {
    size_t original_size = bytes_transferred;
    switch (registered_containers.size()) {
      case 0:
        // No containers assigned, nothing to tag this data with.
        append_tag(buffer, bytes_transferred, MISSING_CONTAINER_TAG, true);
        break;
      case 1: {
        auto entry = *registered_containers.cbegin();
        append_tag(buffer, bytes_transferred, CONTAINER_ID_KEY, entry.first.value(), true);
        append_tag(buffer, bytes_transferred, EXECUTOR_ID_KEY, entry.second.executor_id().value(), false);
        append_tag(buffer, bytes_transferred, FRAMEWORK_ID_KEY, entry.second.framework_id().value(), false);
        break;
      }
      default:
        // Multiple containers assigned to this port. Unable to determine which container this data came from.
        // FIXME: This is where ip-per-container support would be added, using the ip provided in the 'endpoint' param.
        append_tag(buffer, bytes_transferred, UNKNOWN_CONTAINER_TAG, true);
        break;
    }
    DLOG(INFO) << "Received " << original_size << " bytes from endpoint[" << sender_endpoint << "], forwarding " << bytes_transferred << " bytes with tags";
  } else {
    DLOG(INFO) << "Received/forwarded " << bytes_transferred << " bytes from endpoint[" << sender_endpoint << "]";
  }

  port_writer->write(buffer, bytes_transferred);
  start_recv();
}

// Manually instantiate default prod type
template class stats::PortReader<stats::PortWriter>;
