#include "statsd_output_writer.hpp"

#include <glog/logging.h>

#include "statsd_tagger.hpp"
#include "sync_util.hpp"

#define UDP_MAX_PACKET_BYTES 65536 /* UDP size limit in IPv4 (may be larger in IPv6) */

namespace {
  size_t get_chunk_size(const mesos::Parameters& parameters) {
    size_t params_size = metrics::params::get_uint(parameters,
        metrics::params::OUTPUT_STATSD_CHUNK_SIZE_BYTES, metrics::params::OUTPUT_STATSD_CHUNK_SIZE_BYTES_DEFAULT);
    if (params_size == 0) {
      LOG(WARNING) << "Ignoring invalid requested UDP packet size " << params_size << ", "
                   << "using " << metrics::params::OUTPUT_STATSD_CHUNK_SIZE_BYTES_DEFAULT;
      return metrics::params::OUTPUT_STATSD_CHUNK_SIZE_BYTES_DEFAULT;
    }
    if (params_size > UDP_MAX_PACKET_BYTES) {
      LOG(WARNING) << "Ignoring excessive requested UDP packet size " << params_size << ", "
                   << "using " << UDP_MAX_PACKET_BYTES;
      return UDP_MAX_PACKET_BYTES;
    }
    return params_size;
  }
}

metrics::output_writer_ptr_t metrics::StatsdOutputWriter::create(
    std::shared_ptr<boost::asio::io_service> io_service,
    const mesos::Parameters& parameters) {
  std::shared_ptr<UDPSender> sender(new UDPSender(
          io_service,
          params::get_str(parameters, params::OUTPUT_STATSD_HOST, params::OUTPUT_STATSD_HOST_DEFAULT),
          params::get_uint(parameters, params::OUTPUT_STATSD_PORT, params::OUTPUT_STATSD_PORT_DEFAULT),
          1000 * params::get_uint(parameters,
              params::OUTPUT_STATSD_HOST_REFRESH_SECONDS, params::OUTPUT_STATSD_HOST_REFRESH_SECONDS_DEFAULT)));
  return metrics::output_writer_ptr_t(new metrics::StatsdOutputWriter(io_service, parameters, sender));
}

metrics::StatsdOutputWriter::StatsdOutputWriter(
    std::shared_ptr<boost::asio::io_service> io_service,
    const mesos::Parameters& parameters,
    std::shared_ptr<UDPSender> sender,
    size_t chunk_timeout_ms_for_tests/*=1000*/)
  : chunking(params::get_bool(parameters, params::OUTPUT_STATSD_CHUNKING, params::OUTPUT_STATSD_CHUNKING_DEFAULT)),
    chunk_capacity(get_chunk_size(parameters)),
    chunk_timeout_ms(chunk_timeout_ms_for_tests),
    io_service(io_service),
    flush_timer(*io_service),
    output_buffer((char*) malloc(UDP_MAX_PACKET_BYTES)),
    chunk_used(0),
    sender(sender) {
  std::string annotation_mode_str = params::get_str(parameters,
      params::OUTPUT_STATSD_ANNOTATION_MODE, params::OUTPUT_STATSD_ANNOTATION_MODE_DEFAULT);
  switch (params::to_annotation_mode(annotation_mode_str)) {
    case params::annotation_mode::Value::UNKNOWN:
      LOG(FATAL) << "Unknown " << params::OUTPUT_STATSD_ANNOTATION_MODE << " config value: " << annotation_mode_str;
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
}

metrics::StatsdOutputWriter::~StatsdOutputWriter() {
  LOG(INFO) << "Asynchronously triggering StatsdOutputWriter shutdown";
  // Run the shutdown work itself from within the scheduler:
  if (sync_util::dispatch_run(
          "~StatsdOutputWriter", *io_service, std::bind(&StatsdOutputWriter::shutdown_cb, this))) {
    LOG(INFO) << "StatsdOutputWriter shutdown succeeded";
  } else {
    LOG(ERROR) << "Failed to complete StatsdOutputWriter shutdown";
  }
}

void metrics::StatsdOutputWriter::start() {
  // Only run the timer callbacks within the io_service thread:
  LOG(INFO) << "StatsdOutputWriter starting work";
  sender->start();
  if (chunking) {
    start_chunk_flush_timer();
  }
}

void metrics::StatsdOutputWriter::write_container_statsd(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* in_data, size_t in_size) {
  size_t needed_size =
    tagger->calculate_size(container_id, executor_info, in_data, in_size);

  if (needed_size > UDP_MAX_PACKET_BYTES) {
    // the buffer's just too small, period. send untagged data directly, skipping the buffer.
    // this shouldn't happen in practice.
    sender->send(in_data, in_size);
    return;
  }

  // chunking disabled
  if (!chunking) {
    // tag and send the data immediately
    tagger->tag_copy(container_id, executor_info, in_data, in_size, output_buffer);
    sender->send(output_buffer, needed_size);
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
      sender->send(output_buffer, needed_size);
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
  sender->send(output_buffer, chunk_used);
  chunk_used = 0;

  if (needed_size < chunk_capacity) {
    // add the tagged data directly to the start of the chunk (no preceding newline)
    tagger->tag_copy(container_id, executor_info, in_data, in_size, output_buffer);
    chunk_used = needed_size;
  } else {
    // still too big for a chunk, tag and send immediately
    tagger->tag_copy(container_id, executor_info, in_data, in_size, output_buffer);
    sender->send(output_buffer, needed_size);
  }
}

void metrics::StatsdOutputWriter::write_resource_usage(
    const process::Future<mesos::ResourceUsage>& usage) {
  //TODO convert to statsd, and emit via calls to write_container_statsd
  LOG(INFO) << "USAGE DUMP:\n" << usage.get().DebugString();
}

void metrics::StatsdOutputWriter::start_chunk_flush_timer() {
  flush_timer.expires_from_now(boost::posix_time::milliseconds(chunk_timeout_ms));
  flush_timer.async_wait(std::bind(&StatsdOutputWriter::chunk_flush_cb, this, std::placeholders::_1));
}

void metrics::StatsdOutputWriter::chunk_flush_cb(boost::system::error_code ec) {
  //DLOG(INFO) << "Flush triggered";
  if (ec) {
    if (boost::asio::error::operation_aborted) {
      // We're being destroyed. Don't look at local state, it may be destroyed already.
      LOG(WARNING) << "Flush timer aborted: Exiting loop immediately";
      return;
    } else {
      LOG(ERROR) << "Flush timer returned error. "
                 << "err='" << ec.message() << "'(" << ec << ")";
    }
  }

  sender->send(output_buffer, chunk_used);
  chunk_used = 0;

  start_chunk_flush_timer();
}

void metrics::StatsdOutputWriter::shutdown_cb() {
  boost::system::error_code ec;

  if (chunking) {
    chunk_flush_cb(boost::system::error_code());
  }

  flush_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Flush timer cancellation returned error. "
               << "err='" << ec.message() << "'(" << ec << ")";
  }

  free(output_buffer);
  output_buffer = NULL;
}
