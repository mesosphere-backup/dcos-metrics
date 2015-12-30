#include "port_writer.hpp"
#include "sync_util.hpp"

#include <glog/logging.h>

#define UDP_MAX_PACKET_BYTES 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

namespace stats {
  size_t select_buffer_capacity(const mesos::Parameters& parameters) {
    size_t params_size =
      params::get_uint(parameters, params::CHUNK_SIZE_BYTES, params::CHUNK_SIZE_BYTES_DEFAULT);
    if (params_size == 0) {
      LOG(WARNING) << "Ignoring invalid requested UDP packet size " << params_size << ", "
                   << "using " << params::CHUNK_SIZE_BYTES_DEFAULT;
      return params::CHUNK_SIZE_BYTES_DEFAULT;
    }
    if (params_size > UDP_MAX_PACKET_BYTES) {
      LOG(WARNING) << "Ignoring excessive requested UDP packet size " << params_size << ", "
                   << "using " << UDP_MAX_PACKET_BYTES;
      return UDP_MAX_PACKET_BYTES;
    }
    return params_size;
  }

  bool add_to_buffer(char* buffer, size_t buffer_capacity, size_t& buffer_used,
      const char* to_add, size_t to_add_size) {
    if (to_add_size == 0) {
      return true;
    }
    if (buffer_used == 0) {
      // Buffer is empty, no newline separator is needed yet.
      if (to_add_size > buffer_capacity) {
        return false;// Too big
      }
    } else {
      // Appending to existing buffer, with newline char as separator.
      if ((buffer_used + to_add_size + 1) > buffer_capacity) {// "1" includes newline char
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

stats::PortWriter::PortWriter(std::shared_ptr<boost::asio::io_service> io_service,
    const mesos::Parameters& parameters,
    size_t chunk_timeout_ms_for_tests/*=1000*/,
    size_t resolve_period_ms_for_tests/*=0*/)
  : send_host(params::get_str(parameters, params::DEST_HOST, params::DEST_HOST_DEFAULT)),
    send_port(params::get_uint(parameters, params::DEST_PORT, params::DEST_PORT_DEFAULT)),
    buffer_capacity(select_buffer_capacity(parameters)),
    chunking(params::get_bool(parameters, params::CHUNKING, params::CHUNKING_DEFAULT)),
    chunk_timeout_ms(chunk_timeout_ms_for_tests),
    resolve_period_ms((resolve_period_ms_for_tests != 0)
        ? resolve_period_ms_for_tests
        : 1000 * params::get_uint(parameters,
            params::DEST_REFRESH_SECONDS, params::DEST_REFRESH_SECONDS_DEFAULT)),
    io_service(io_service),
    flush_timer(*io_service),
    resolve_timer(*io_service),
    socket(*io_service),
    buffer((char*) malloc(buffer_capacity)),
    buffer_used(0),
    shutdown(false) {
  LOG(INFO) << "Writer constructed for " << send_host << ":" << send_port;
  if (resolve_period_ms == 0) {
    LOG(FATAL) << "Invalid " << params::DEST_REFRESH_SECONDS << " value: must be non-zero";
  }
}

stats::PortWriter::~PortWriter() {
  LOG(INFO) << "Asynchronously triggering PortWriter shutdown for "
            << send_host << ":" << send_port;
  cancel_timers();
  if (sync_util::dispatch_run(
          "~PortWriter", *io_service, std::bind(&PortWriter::shutdown_cb, this))) {
    LOG(INFO) << "PortWriter shutdown succeeded";
  } else {
    LOG(ERROR) << "Failed to complete PortWriter shutdown for " << send_host << ":" << send_port;
  }
}

void stats::PortWriter::start() {
  // Only run the timer callbacks within the io_service thread:
  io_service->dispatch(std::bind(&PortWriter::dest_resolve_cb, this, boost::system::error_code()));
  if (chunking) {
    start_chunk_flush_timer();
  }
}

void stats::PortWriter::write(const char* bytes, size_t size) {
  if (chunking) {
    if (add_to_buffer(buffer, buffer_capacity, buffer_used, bytes, size)) {
      // Data added to buffer. Will be flushed when buffer is full or when flush timer goes off.
      return;
    }

    // New data doesn't fit in the buffer's available space. Send/empty the buffer and try again.
    send_raw_bytes(buffer, buffer_used);
    buffer_used = 0;
    if (add_to_buffer(buffer, buffer_capacity, buffer_used, bytes, size)) {
      // Data now fit in buffer after freeing space with a manual flush. To be flushed again later.
      return;
    }

    // Data is too big to fit in an empty buffer. Skip the buffer and send the data as-is (below).
    // Note that the data doesn't jump the 'queue' since we've already sent the contents of the
    // current buffer.
    LOG(WARNING) << "Ignoring requested packet max[" << buffer_capacity << "]: "
                 << "Sending metric of size " << size;
  }

  send_raw_bytes(bytes, size);
}

stats::PortWriter::udp_resolver_t::iterator stats::PortWriter::resolve(
    boost::system::error_code& ec) {
  udp_resolver_t resolver(*io_service);
  return resolver.resolve(udp_resolver_t::query(send_host, ""), ec);
}

void stats::PortWriter::cancel_timers() {
  boost::system::error_code ec;
  flush_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Flush timer cancellation returned error. err=" << ec;
  }
  resolve_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Resolve timer cancellation returned error. err=" << ec;
  }
}

void stats::PortWriter::start_dest_resolve_timer() {
  resolve_timer.expires_from_now(boost::posix_time::milliseconds(resolve_period_ms));
  resolve_timer.async_wait(std::bind(&PortWriter::dest_resolve_cb, this, std::placeholders::_1));
}

void stats::PortWriter::dest_resolve_cb(boost::system::error_code ec) {
  if (ec) {
    if (boost::asio::error::operation_aborted) {
      // We're being destroyed. Don't look at local state, it may be destroyed already.
      LOG(WARNING) << "Resolve timer aborted: Exiting loop immediately";
      return;
    } else {
      LOG(ERROR) << "Resolve timer returned error. err=" << ec;
    }
  }

  udp_resolver_t::iterator iter = resolve(ec);
  boost::asio::ip::address selected_address;
  if (ec) {
    // dns lookup failed, fall back to parsing the host string as a literal ip
    boost::system::error_code ec2;
    selected_address = boost::asio::ip::address::from_string(send_host, ec2);
    if (ec2) {
      // using host as-is also failed, give up and try again later
      if (socket.is_open()) {
        LOG(ERROR) << "Error when resolving host[" << send_host << "]. "
                   << "Sending data to old endpoint[" << current_endpoint << "] "
                   << "and trying again in " << resolve_period_ms / 1000. << " seconds. "
                   << "err=" << ec << ", err2=" << ec2;
      } else {
        LOG(ERROR) << "Error when resolving host[" << send_host << "]. "
                   << "Dropping data "
                   << "and trying again in " << resolve_period_ms / 1000. << " seconds. "
                   << "err=" << ec << ", err2=" << ec2;
      }
      start_dest_resolve_timer();
      return;
    }
    // parsing the host as a literal ip succeeded. skip random address selection below since we only
    // have a single entry anyway.
  } else if (iter == udp_resolver_t::iterator()) {
    // dns lookup had no results, fall back to parsing the host string as a literal ip
    selected_address = boost::asio::ip::address::from_string(send_host, ec);
    if (ec) {
      // using host as-is also failed, give up and try again later
      if (socket.is_open()) {
        LOG(ERROR) << "No results when resolving host[" << send_host << "]. "
                   << "Sending data to old endpoint[" << current_endpoint << "] "
                   << "and trying again in " << resolve_period_ms / 1000. << " seconds. "
                   << "err=" << ec;
      } else {
        LOG(ERROR) << "No results when resolving host[" << send_host << "]. "
                   << "Dropping data "
                   << "and trying again in " << resolve_period_ms / 1000. << " seconds. "
                   << "err=" << ec;
      }
      start_dest_resolve_timer();
      return;
    }
    // parsing the host as a literal ip succeeded. skip random address selection below since we only
    // have a single entry anyway.
  } else {
    // resolved successfully. pick a new endpoint only if the list of endpoints has changed since
    // the last refresh. since we are performing our own randomization below, detect and normalize
    // any randomized ordering produced by the dns server, only changing our destination endpoint
    // if the list changes beyond superficial reordering.
    std::multiset<boost::asio::ip::address> sorted_resolved_addresses;
    for (; iter != udp_resolver_t::iterator(); ++iter) {
      sorted_resolved_addresses.insert(iter->endpoint().address());
    }
    if (sorted_resolved_addresses == last_resolved_addresses) {
      LOG(INFO) << "No change in resolved addresses[size=" << sorted_resolved_addresses.size() << "], "
                << "leaving socket as-is and checking again in "
                << resolve_period_ms / 1000. << " seconds.";
      start_dest_resolve_timer();
      return;
    }

    // list has changed, switch to a new random entry (redistribute load across new list)
    size_t rand_index;
    {
      std::random_device dev;
      std::mt19937 engine{dev()};
      std::uniform_int_distribution<size_t> dist(0, sorted_resolved_addresses.size() - 1);
      rand_index = dist(engine);
    }

    std::multiset<boost::asio::ip::address>::const_iterator iter = sorted_resolved_addresses.begin();
    for (size_t i = 0; i < rand_index; ++i) {
      ++iter;
    }
    selected_address = *iter;

    LOG(INFO) << "Resolved dest host[" << send_host << "] "
              << "-> results[size=" << sorted_resolved_addresses.size() << "] "
              << "-> selected[" << selected_address << "]";
    last_resolved_addresses = sorted_resolved_addresses;
  }

  udp_endpoint_t new_endpoint(selected_address, send_port);
  if (socket.is_open()) {
    // Before closing the current socket, check that the endpoint has changed.
    if (new_endpoint == current_endpoint) {
      DLOG(INFO) << "No change in selected endpoint[" << current_endpoint << "], "
                 << "leaving socket as-is and checking again in "
                 << resolve_period_ms / 1000. << " seconds.";
      start_dest_resolve_timer();
      return;
    }
    // Socket is moing to a new endpoint. Close socket before reopening.
    socket.close(ec);
    if (ec) {
      LOG(ERROR) << "Failed to close writer socket for move from "
                 << "old_endpoint[" << current_endpoint << "] to "
                 << "new_endpoint[" << new_endpoint << "] err=" << ec;
    }
  }
  socket.open(new_endpoint.protocol(), ec);
  if (ec) {
    LOG(ERROR) << "Failed to open writer socket to endpoint[" << new_endpoint << "] err=" << ec;
  } else {
    LOG(INFO) << "Updated dest endpoint[" << current_endpoint << "] to endpoint[" << new_endpoint << "]";
    current_endpoint = new_endpoint;
  }
  start_dest_resolve_timer();
}

void stats::PortWriter::start_chunk_flush_timer() {
  flush_timer.expires_from_now(boost::posix_time::milliseconds(chunk_timeout_ms));
  flush_timer.async_wait(std::bind(&PortWriter::chunk_flush_cb, this, std::placeholders::_1));
}

void stats::PortWriter::chunk_flush_cb(boost::system::error_code ec) {
  //DLOG(INFO) << "Flush triggered";
  if (ec) {
    if (boost::asio::error::operation_aborted) {
      // We're being destroyed. Don't look at local state, it may be destroyed already.
      LOG(WARNING) << "Flush timer aborted: Exiting loop immediately";
      return;
    } else {
      LOG(ERROR) << "Flush timer returned error. err=" << ec;
    }
  }

  send_raw_bytes(buffer, buffer_used);
  buffer_used = 0;

  start_chunk_flush_timer();
}

void stats::PortWriter::send_raw_bytes(const char* bytes, size_t size) {
  if (size == 0) {
    //DLOG(INFO) << "Skipping scheduled send of zero bytes";
    return;
  }

  if (!socket.is_open()) {
    LOG(WARNING) << "Dropped " << size << " bytes of data due to lack of open writer socket to "
                 << send_host << ":" << send_port;
    return;
  }

  DLOG(INFO) << "Send " << size << " bytes to " << send_host << ":" << send_port;
  boost::system::error_code ec;
  size_t sent = socket.send_to(boost::asio::buffer(bytes, size), current_endpoint, 0 /* flags */, ec);
  if (ec) {
    LOG(ERROR) << "Failed to send " << size << " bytes of data to ["
               << send_host << ":" << send_port << "] err=" << ec;
  }
  if (sent != size) {
    LOG(WARNING) << "Sent size=" << sent << " doesn't match requested size=" << size;
  }
}

void stats::PortWriter::shutdown_cb() {
  boost::system::error_code ec;
  if (socket.is_open()) {
    if (chunking) {
      chunk_flush_cb(boost::system::error_code());
    }
    socket.close(ec);
    if (ec) {
      LOG(ERROR) << "Error on writer socket close. err=" << ec;
    }
  }
  free(buffer);
  buffer = NULL;
}
