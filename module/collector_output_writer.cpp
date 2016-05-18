#include "collector_output_writer.hpp"

#include <glog/logging.h>

#include "avro_encoder.hpp"
#include "sync_util.hpp"
#include "tcp_sender.hpp"

namespace {
  boost::asio::ip::address get_collector_ip(const mesos::Parameters& parameters) {
    std::string ip_str = metrics::params::get_str(parameters,
        metrics::params::OUTPUT_COLLECTOR_IP, metrics::params::OUTPUT_COLLECTOR_IP_DEFAULT);
    boost::system::error_code ec;
    boost::asio::ip::address ip = boost::asio::ip::address::from_string(ip_str, ec);
    if (ec) {
      LOG(FATAL) << "Unable to parse " << metrics::params::OUTPUT_COLLECTOR_IP << " as IP "
                 << "err='" << ec.message() << "'(" << ec << ")";
    }
    return ip;
  }
}

metrics::output_writer_ptr_t metrics::CollectorOutputWriter::create(
    std::shared_ptr<boost::asio::io_service> io_service,
    const mesos::Parameters& parameters) {
  std::shared_ptr<TCPSender> sender(new TCPSender(
          io_service,
          AvroEncoder::header(),
          get_collector_ip(parameters),
          params::get_uint(parameters,
              params::OUTPUT_COLLECTOR_PORT, params::OUTPUT_COLLECTOR_PORT_DEFAULT)));
  return metrics::output_writer_ptr_t(
      new metrics::CollectorOutputWriter(io_service, parameters, sender));
}

metrics::CollectorOutputWriter::CollectorOutputWriter(
    std::shared_ptr<boost::asio::io_service> io_service,
    const mesos::Parameters& parameters,
    std::shared_ptr<TCPSender> sender,
    size_t chunk_timeout_ms_for_tests/*=0*/)
  : chunking(
        params::get_bool(parameters,
            params::OUTPUT_COLLECTOR_CHUNKING,
            params::OUTPUT_COLLECTOR_CHUNKING_DEFAULT)),
    chunk_timeout_ms((chunk_timeout_ms_for_tests != 0)
      ? chunk_timeout_ms_for_tests
      : 1000 * params::get_uint(parameters,
          params::OUTPUT_COLLECTOR_CHUNK_TIMEOUT_SECONDS,
          params::OUTPUT_COLLECTOR_CHUNK_TIMEOUT_SECONDS_DEFAULT)),
    datapoint_capacity(
        params::get_uint(parameters,
            params::OUTPUT_COLLECTOR_CHUNK_SIZE_DATAPOINTS,
            params::OUTPUT_COLLECTOR_CHUNK_SIZE_DATAPOINTS_DEFAULT)),
    io_service(io_service),
    flush_timer(*io_service),
    sender(sender) { }

metrics::CollectorOutputWriter::~CollectorOutputWriter() {
  LOG(INFO) << "Asynchronously triggering CollectorOutputWriter shutdown";
  // Run the shutdown work itself from within the scheduler:
  if (sync_util::dispatch_run("~CollectorOutputWriter", *io_service,
          std::bind(&CollectorOutputWriter::shutdown_cb, this))) {
    LOG(INFO) << "CollectorOutputWriter shutdown succeeded";
  } else {
    LOG(ERROR) << "Failed to complete CollectorOutputWriter shutdown";
  }
}

void metrics::CollectorOutputWriter::start() {
  // Only run the timer callbacks within the io_service thread:
  LOG(INFO) << "CollectorOutputWriter starting work";
  sender->start();
  if (chunking) {
    start_chunk_flush_timer();
  }
}

void metrics::CollectorOutputWriter::write_container_statsd(
    const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
    const char* in_data, size_t in_size) {
  datapoint_count += AvroEncoder::statsd_to_map(
      container_id, executor_info, in_data, in_size, container_map);
  if (!chunking || datapoint_count >= datapoint_capacity) {
    flush();
  }
}

void metrics::CollectorOutputWriter::write_resource_usage(
    const process::Future<mesos::ResourceUsage>& usage) {
  datapoint_count += AvroEncoder::resources_to_map(usage.get(), container_map);
  if (!chunking || datapoint_count >= datapoint_capacity) {
    flush();
  }
}

void metrics::CollectorOutputWriter::start_chunk_flush_timer() {
  flush_timer.expires_from_now(boost::posix_time::milliseconds(chunk_timeout_ms));
  flush_timer.async_wait(
      std::bind(&CollectorOutputWriter::chunk_flush_cb, this, std::placeholders::_1));
}

void metrics::CollectorOutputWriter::flush() {
  if (container_map.empty()) {
    return; // nothing to flush
  }

  TCPSender::buf_ptr_t buf(new boost::asio::streambuf);
  {
    std::ostream ostream(buf.get());
    AvroEncoder::encode_metrics_block(container_map, ostream);
  }
  container_map.clear();
  if (buf->size() != 0) {
    sender->send(buf);
  }
}

void metrics::CollectorOutputWriter::chunk_flush_cb(boost::system::error_code ec) {
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

  if (datapoint_count > 0) {
    flush();
  }

  start_chunk_flush_timer();
}

void metrics::CollectorOutputWriter::shutdown_cb() {
  boost::system::error_code ec;
  flush_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Flush timer cancellation returned error. "
               << "err='" << ec.message() << "'(" << ec << ")";
  }

  flush();
}
