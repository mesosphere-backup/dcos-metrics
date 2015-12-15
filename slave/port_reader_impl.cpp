#include "port_reader_impl.hpp"

#include <boost/asio.hpp>
#include <glog/logging.h>

#include "port_writer.hpp"
#include "sync_util.hpp"

#define UDP_MAX_PACKET_BYTES 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

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

  enum TagMode {
    FIRST_TAG,
    APPEND_TAG_NO_DELIM,
    APPEND_TAG
  };

  /**
   * Appends a tag to the end of the provided buffer, with a delimiter determined by "tag_mode".
   * Returns whether the tag was successfully added (false => buffer is too small), and updates
   * "tag_mode" to the mode to be used in any next append_tag() call.
   */
  bool append_tag(std::vector<char>& buffer, size_t& size,
      const std::string& tag, TagMode& tag_mode) {
    switch (tag_mode) {
      case FIRST_TAG:
        // <buffer>|#tag
        if (size + TAG_PREFIX.size() + tag.size() > buffer.size()) {
          return false;
        }
        memcpy(buffer.data() + size, TAG_PREFIX.data(), TAG_PREFIX.size());
        size += TAG_PREFIX.size();
        tag_mode = APPEND_TAG;
        break;
      case APPEND_TAG:
        // <buffer>,tag
        if (size + TAG_DIVIDER.size() + tag.size() > buffer.size()) {
          return false;
        }
        memcpy(buffer.data() + size, TAG_DIVIDER.data(), TAG_DIVIDER.size());
        size += TAG_DIVIDER.size();
        break;
      case APPEND_TAG_NO_DELIM:
        // <buffer>tag
        if (size + tag.size() > buffer.size()) {
          return false;
        }
        tag_mode = APPEND_TAG;
        break;
    }
    memcpy(buffer.data() + size, tag.data(), tag.size());
    size += tag.size();
    return true;
  }

  /**
   * Appends a tag to the end of the provided buffer, with a delimiter determined by "tag_mode".
   * Returns whether the tag was successfully added (false => buffer is too small), and updates
   * "tag_mode" to the mode to be used in any next append_tag() call.
   */
  bool append_tag(std::vector<char>& buffer, size_t& size,
      const std::string& tag_key, const std::string& tag_value, TagMode& tag_mode) {
    switch (tag_mode) {
      case FIRST_TAG:
        // <buffer>|#key:value
        if ((size
                + TAG_PREFIX.size()
                + tag_key.size()
                + TAG_KEY_VALUE_SEPARATOR.size()
                + tag_value.size())
            > buffer.size()) {
          return false;
        }
        memcpy(buffer.data() + size, TAG_PREFIX.data(), TAG_PREFIX.size());
        size += TAG_PREFIX.size();
        tag_mode = APPEND_TAG;
        break;
      case APPEND_TAG:
        // <buffer>,key:value
        if ((size
                + TAG_DIVIDER.size()
                + tag_key.size()
                + TAG_KEY_VALUE_SEPARATOR.size()
                + tag_value.size())
            > buffer.size()) {
          return false;
        }
        memcpy(buffer.data() + size, TAG_DIVIDER.data(), TAG_DIVIDER.size());
        size += TAG_DIVIDER.size();
        break;
      case APPEND_TAG_NO_DELIM:
        // <buffer>key:value
        if ((size
                + tag_key.size()
                + TAG_KEY_VALUE_SEPARATOR.size()
                + tag_value.size())
            > buffer.size()) {
          return false;
        }
        tag_mode = APPEND_TAG;
        break;
    }
    memcpy(buffer.data() + size, tag_key.data(), tag_key.size());
    size += tag_key.size();
    memcpy(buffer.data() + size, TAG_KEY_VALUE_SEPARATOR.data(), TAG_KEY_VALUE_SEPARATOR.size());
    size += TAG_KEY_VALUE_SEPARATOR.size();
    memcpy(buffer.data() + size, tag_value.data(), tag_value.size());
    size += tag_value.size();
    return true;
  }

  /**
   * Finds the first 'needle' in 'haystack', with explicit buffer sizes for each (\0 ignored).
   * Returns a pointer to the location of 'needle' within 'haystack', or NULL if no match was found.
   */
  char* memnmem_imp(char* haystack, size_t haystack_size, const char* needle, size_t needle_size) {
      if (haystack_size == 0 || needle_size == 0) {
      return NULL;
    }
    size_t search_start = 0;
    char* haystack_end = haystack + haystack_size;
    for (;;) {
      // Search for the first character in needle
      char* candidate =
        (char*) memchr(haystack + search_start, needle[0], haystack_size - search_start);
      if (candidate == NULL) {
        return NULL;
      }
      if (candidate + needle_size > haystack_end) {
        return NULL;
      }
      // Check whether the following characters also match
      if (memcmp(&candidate[1], &needle[1], needle_size - 1) == 0) {
        return candidate;
      } else {
        search_start = candidate - haystack + 1;
      }
    }
  }

  /**
   * Updates the provided buffer in-place for appending tag data.
   * Returns the mode to use when appending tags to this buffer.
   */
  TagMode prepare_for_tags(char* buffer, size_t size, std::vector<char>& scratch_buffer) {
    std::string orig(buffer, size);

    char* tag_section_ptr = memnmem_imp(buffer, size, TAG_PREFIX.data(), TAG_PREFIX.size());
    if (tag_section_ptr == NULL) {
      // No pre-existing tag section was found. Append tags in a new section.
      return TagMode::FIRST_TAG;
    }

    // Ensure the preexisting tag section is placed at the end of the buffer, so that we can append.
    // For example "io.mesosphere.sent.bytes=0|g|#tag1=val1,tag2=val2|@0.1"
    //          => "io.mesosphere.sent.bytes=0|g|@0.1|#tag1:val1,tag2:val2"
    // If there were multiple tag sections in a single record, this only moves the first one, but
    // if someone's generating multiple tag sections (probably a bug) then they shouldn't care that
    // we aren't merging all of them anyway.
    size_t before_tag_section_size = tag_section_ptr - buffer;
    char* after_tag_section_ptr =
      (char*) memchr(tag_section_ptr + 1, '|', size - before_tag_section_size - 1);
    if (after_tag_section_ptr != NULL) {
      // The tag section we found is followed by one or more other sections.
      // Move the tag section to the end of the buffer, swapping it with any other data:
      // "|#tag1,tag2:value2|@0.5|&amp" => "|@0.5|&amp|#tag1,tag2:value2"

      // Copy the tag section into a separate scratch buffer, which is grown on demand.
      size_t tag_section_size = after_tag_section_ptr - tag_section_ptr;
      if (scratch_buffer.size() < tag_section_size) {
        DLOG(INFO) << "Grow scratch buffer for tag order to " << tag_section_size << " bytes";
        scratch_buffer.resize(tag_section_size, '\0');
      }
      memcpy(scratch_buffer.data(), tag_section_ptr, tag_section_size);

      // Move forward the data following the tag section
      size_t after_tag_section_size = size - before_tag_section_size - tag_section_size;
      if (after_tag_section_size > (after_tag_section_ptr - tag_section_ptr)) {
        // memmove required: Copying between ranges which overlap within the same buffer.
        memmove(tag_section_ptr, after_tag_section_ptr, after_tag_section_size);
      } else {
        memcpy(tag_section_ptr, after_tag_section_ptr, after_tag_section_size);
      }

      // Copy back the separate buffer containing the tag data, following the data we moved forward.
      memcpy(tag_section_ptr + after_tag_section_size, scratch_buffer.data(), tag_section_size);
    }

    // Tag section is now at the end of the buffer, ready for append.
    switch (buffer[size - 1]) {
      case ',':
      case '#':
        // Corner case: Empty tag data. Clean this up by omitting the delimiter when we append.
        return TagMode::APPEND_TAG_NO_DELIM;
      default:
        // Typical case: Add tag with a preceding delimiter
        return TagMode::APPEND_TAG;
    }
  }
}

template <typename PortWriter>
stats::PortReaderImpl<PortWriter>::PortReaderImpl(
    const std::shared_ptr<boost::asio::io_service>& io_service,
    const std::shared_ptr<PortWriter>& port_writer,
    const UDPEndpoint& requested_endpoint,
    bool annotations_enabled)
  : port_writer(port_writer),
    requested_endpoint(requested_endpoint),
    annotations_enabled(annotations_enabled),
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
    oss << "Failed to retrieve reader socket's resulting endpoint for bind at "
        << "endpoint[" << bind_endpoint << "]: " << ec;
    return Try<stats::UDPEndpoint>::error(oss.str());
  }

  std::string bound_endpoint_address_str = bound_endpoint.address().to_string(ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to stringify reader socket's "
        << "address[" << bound_endpoint.address() << "]: " << ec;
    return Try<stats::UDPEndpoint>::error(oss.str());
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
    return Try<stats::UDPEndpoint>::error("Not listening on UDP");
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
  if (!annotations_enabled) {
    DLOG(INFO) << "Received/forwarded " << size << " byte entry from "
               << "endpoint[" << sender_endpoint << "]: " << std::string(buffer.data(), size);
    port_writer->write(buffer.data(), size);
    return;
  }

  // Prepare the data for our tags, moving any existing tags to the end as needed.
  // Note that we must avoid using a separate buffer from multiline_scratch_buffer, since
  // multiline_scratch_buffer is meant for use by our caller!
  TagMode tag_mode = prepare_for_tags(buffer.data(), size, tag_reorder_scratch_buffer);

  size_t original_size = size;
  switch (registered_containers.size()) {
    case 0:
      // No containers assigned, nothing to tag this data with.
      append_tag(buffer, size, MISSING_CONTAINER_TAG, tag_mode);
      break;
    case 1: {
      auto entry = *registered_containers.cbegin();
      append_tag(buffer, size, CONTAINER_ID_KEY, entry.first.value(), tag_mode);
      append_tag(buffer, size,
          EXECUTOR_ID_KEY, entry.second.executor_id().value(), tag_mode);
      append_tag(buffer, size,
          FRAMEWORK_ID_KEY, entry.second.framework_id().value(), tag_mode);
      break;
    }
    default:
      // Multiple containers assigned to this port. Unable to determine which container this data
      // came from.
      // FIXME: This is where ip-per-container support would be added, using the ip provided in
      // the 'endpoint' param.
      append_tag(buffer, size, UNKNOWN_CONTAINER_TAG, tag_mode);
      break;
  }
  DLOG(INFO) << "Received " << original_size << " byte entry from "
             << "endpoint[" << sender_endpoint << "], "
             << "forwarding " << size << " bytes with tags: " << std::string(buffer.data(), size);

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
