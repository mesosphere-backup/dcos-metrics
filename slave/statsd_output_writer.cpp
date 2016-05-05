#include "statsd_output_writer.hpp"

#include <glog/logging.h>

#include "statsd_tagger.hpp"
#include "sync_util.hpp"

#define UDP_MAX_PACKET_BYTES 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

namespace {
  size_t get_chunk_size(const mesos::Parameters& parameters) {
    size_t params_size = stats::params::get_uint(
        parameters, stats::params::CHUNK_SIZE_BYTES, stats::params::CHUNK_SIZE_BYTES_DEFAULT);
    if (params_size == 0) {
      LOG(WARNING) << "Ignoring invalid requested UDP packet size " << params_size << ", "
                   << "using " << stats::params::CHUNK_SIZE_BYTES_DEFAULT;
      return stats::params::CHUNK_SIZE_BYTES_DEFAULT;
    }
    if (params_size > UDP_MAX_PACKET_BYTES) {
      LOG(WARNING) << "Ignoring excessive requested UDP packet size " << params_size << ", "
                   << "using " << UDP_MAX_PACKET_BYTES;
      return UDP_MAX_PACKET_BYTES;
    }
    return params_size;
  }
}

stats::StatsdOutputWriter::StatsdOutputWriter(std::shared_ptr<boost::asio::io_service> io_service,
    const mesos::Parameters& parameters,
    size_t chunk_timeout_ms_for_tests/*=1000*/,
    size_t resolve_period_ms_for_tests/*=0*/)
  : send_host(params::get_str(parameters, params::DEST_HOST, params::DEST_HOST_DEFAULT)),
    send_port(params::get_uint(parameters, params::DEST_PORT, params::DEST_PORT_DEFAULT)),
    chunking(params::get_bool(parameters, params::CHUNKING, params::CHUNKING_DEFAULT)),
    chunk_capacity(get_chunk_size(parameters)),
    chunk_timeout_ms(chunk_timeout_ms_for_tests),
    resolve_period_ms((resolve_period_ms_for_tests != 0)
        ? resolve_period_ms_for_tests
        : 1000 * params::get_uint(parameters,
            params::DEST_REFRESH_SECONDS, params::DEST_REFRESH_SECONDS_DEFAULT)),
    io_service(io_service),
    flush_timer(*io_service),
    resolve_timer(*io_service),
    socket(*io_service),
    output_buffer((char*) malloc(UDP_MAX_PACKET_BYTES)),
    chunk_used(0),
    dropped_bytes(0) {
  LOG(INFO) << "StatsdOutputWriter constructed for " << send_host << ":" << send_port;
  switch (params::to_annotation_mode(
          params::get_str(parameters, params::ANNOTATION_MODE, params::ANNOTATION_MODE_DEFAULT))) {
    case params::annotation_mode::Value::UNKNOWN:
      LOG(FATAL) << "Unknown " << params::ANNOTATION_MODE << " config value: "
                 << params::get_str(
                     parameters, params::ANNOTATION_MODE, params::ANNOTATION_MODE_DEFAULT);
      break;
    case params::annotation_mode::Value::NONE:
      tagger.reset(new NullTagger);
      break;
    case params::annotation_mode::Value::TAG_DATADOG:
      tagger.reset(new DatadogTagger);
      break;
    case params::annotation_mode::Value::KEY_PREFIX:
      tagger.reset(new KeyPrefixTagger);
      break;
  }
  if (resolve_period_ms == 0) {
    LOG(FATAL) << "Invalid " << params::DEST_REFRESH_SECONDS << " value: must be non-zero";
  }
}

stats::StatsdOutputWriter::~StatsdOutputWriter() {
  shutdown();
}

void stats::StatsdOutputWriter::start() {
  // Only run the timer callbacks within the io_service thread:
  LOG(INFO) << "StatsdOutputWriter starting work";
  io_service->dispatch(std::bind(&StatsdOutputWriter::dest_resolve_cb, this, boost::system::error_code()));
  if (chunking) {
    start_chunk_flush_timer();
  }
}

void stats::StatsdOutputWriter::write_container_statsd(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* in_data, size_t in_size) {
  size_t needed_size =
    tagger->calculate_size(container_id, executor_info, in_data, in_size);

  if (needed_size > UDP_MAX_PACKET_BYTES) {
    // the buffer's just too small, period. send untagged data directly, skipping the buffer.
    // this shouldn't happen in practice.
    send_raw_bytes(in_data, in_size);
    return;
  }

  // chunking disabled
  if (!chunking) {
    // tag and send the data immediately
    tagger->tag_copy(container_id, executor_info, in_data, in_size, output_buffer);
    send_raw_bytes(output_buffer, needed_size);
    return;
  }

  // starting a new chunk
  if (chunk_used == 0) {
    if (needed_size < chunk_capacity) {
      // add the tagged data directly to the start of the chunk (no preceding newline)
      tagger->tag_copy(container_id, executor_info, in_data, in_size, output_buffer);
      chunk_used = needed_size;
    } else {
      // too big for a chunk, tag and send immediately
      tagger->tag_copy(container_id, executor_info, in_data, in_size, output_buffer);
      send_raw_bytes(output_buffer, needed_size);
    }
    return;
  }

  // appending to existing chunk
  if (needed_size + 1 < chunk_capacity - chunk_used) {//include newline char
    // the data fits in the current chunk. append the data and exit
    output_buffer[chunk_used] = '\n';
    ++chunk_used;
    tagger->tag_copy(container_id, executor_info, in_data, in_size,
        output_buffer + chunk_used);
    chunk_used += needed_size;
    return;
  }

  // the space needed exceeds the current chunk. send the current buffer before continuing.
  send_raw_bytes(output_buffer, chunk_used);
  chunk_used = 0;

  if (needed_size < chunk_capacity) {
    // add the tagged data directly to the start of the chunk (no preceding newline)
    tagger->tag_copy(container_id, executor_info, in_data, in_size, output_buffer);
    chunk_used = needed_size;
  } else {
    // still too big for a chunk, tag and send immediately
    tagger->tag_copy(container_id, executor_info, in_data, in_size, output_buffer);
    send_raw_bytes(output_buffer, needed_size);
  }
}

void stats::StatsdOutputWriter::write_resource_usage(
    const process::Future<mesos::ResourceUsage>& usage) {
  //TODO convert to statsd, and emit via calls to write_container_statsd
  LOG(INFO) << "USAGE DUMP:\n" << usage.get().DebugString();
}

stats::StatsdOutputWriter::udp_resolver_t::iterator stats::StatsdOutputWriter::resolve(
    boost::system::error_code& ec) {
  udp_resolver_t resolver(*io_service);
  return resolver.resolve(udp_resolver_t::query(send_host, ""), ec);
}

void stats::StatsdOutputWriter::shutdown() {
  LOG(INFO) << "Asynchronously triggering StatsdOutputWriter shutdown for "
            << send_host << ":" << send_port;
  // Run the shutdown work itself from within the scheduler:
  if (sync_util::dispatch_run(
          "~StatsdOutputWriter", *io_service, std::bind(&StatsdOutputWriter::shutdown_cb, this))) {
    LOG(INFO) << "StatsdOutputWriter shutdown succeeded";
  } else {
    LOG(ERROR) << "Failed to complete StatsdOutputWriter shutdown for " << send_host << ":" << send_port;
  }
}

void stats::StatsdOutputWriter::start_dest_resolve_timer() {
  resolve_timer.expires_from_now(boost::posix_time::milliseconds(resolve_period_ms));
  resolve_timer.async_wait(std::bind(&StatsdOutputWriter::dest_resolve_cb, this, std::placeholders::_1));
}

void stats::StatsdOutputWriter::dest_resolve_cb(boost::system::error_code ec) {
  if (ec) {
    if (boost::asio::error::operation_aborted) {
      // We're being destroyed. Don't look at local state, it may be destroyed already.
      LOG(WARNING) << "Resolve timer aborted: Exiting loop immediately";
      return;
    } else {
      LOG(ERROR) << "Resolve timer returned error. err=" << ec;
    }
  }

  // Warn periodically when data is being dropped due to lack of outgoing connection
  if (dropped_bytes > 0) {
    LOG(WARNING) << "Recently dropped " << dropped_bytes
                 << " bytes due to lack of open writer socket to host[" << send_host << "]";
    dropped_bytes = 0;
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
        // Log as error: User used to have a working host!
        LOG(ERROR) << "Error when resolving host[" << send_host << "]. "
                   << "Sending data to old endpoint[" << current_endpoint << "] "
                   << "and trying again in " << resolve_period_ms / 1000. << " seconds. "
                   << "err=" << ec << ", err2=" << ec2;
      } else {
        // Log as warning: User may not have brought up their metrics service yet.
        LOG(WARNING) << "Error when resolving host[" << send_host << "]. "
                     << "Dropping data and trying again in "
                     << resolve_period_ms / 1000. << " seconds. "
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
        // Log as error: User used to have a working host!
        LOG(ERROR) << "No results when resolving host[" << send_host << "]. "
                   << "Sending data to old endpoint[" << current_endpoint << "] "
                   << "and trying again in " << resolve_period_ms / 1000. << " seconds. "
                   << "err=" << ec;
      } else {
        // Log as warning: User may not have brought up their metrics service yet.
        LOG(WARNING) << "No results when resolving host[" << send_host << "]. "
                     << "Dropping data and trying again in "
                     << resolve_period_ms / 1000. << " seconds. "
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

void stats::StatsdOutputWriter::start_chunk_flush_timer() {
  flush_timer.expires_from_now(boost::posix_time::milliseconds(chunk_timeout_ms));
  flush_timer.async_wait(std::bind(&StatsdOutputWriter::chunk_flush_cb, this, std::placeholders::_1));
}

void stats::StatsdOutputWriter::chunk_flush_cb(boost::system::error_code ec) {
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

  send_raw_bytes(output_buffer, chunk_used);
  chunk_used = 0;

  start_chunk_flush_timer();
}

void stats::StatsdOutputWriter::send_raw_bytes(const char* bytes, size_t size) {
  if (size == 0) {
    //DLOG(INFO) << "Skipping scheduled send of zero bytes";
    return;
  }

  if (!socket.is_open()) {
    // Log dropped data for periodic cumulative reporting in the resolve callback
    dropped_bytes += size;
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

void stats::StatsdOutputWriter::shutdown_cb() {
  boost::system::error_code ec;

  flush_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Flush timer cancellation returned error. err=" << ec;
  }

  resolve_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Resolve timer cancellation returned error. err=" << ec;
  }

  if (socket.is_open()) {
    if (chunking) {
      chunk_flush_cb(boost::system::error_code());
    }
    socket.close(ec);
    if (ec) {
      LOG(ERROR) << "Error on writer socket close. err=" << ec;
    }
  }
  free(output_buffer);
  output_buffer = NULL;
}
