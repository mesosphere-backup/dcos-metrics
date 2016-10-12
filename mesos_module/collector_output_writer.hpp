#pragma once

#include <boost/asio.hpp>

#include "mesos_hash.hpp"
#include "metrics_schema_struct.hpp"
#include "output_writer.hpp"
#include "params.hpp"

namespace metrics {
  class MetricsTCPSender;
  class ContainerMetrics;

  /**
   * A CollectorOutputWriter accepts data from one or more ContainerReaders, then tags and forwards it
   * to an external statsd endpoint. The data may be buffered into chunks before being sent out --
   * statsd supports separating multiple metrics by newlines.
   * In practice, there is one singleton CollectorOutputWriter instance per mesos-slave.
   */
  class CollectorOutputWriter : public OutputWriter {
   public:
    /**
     * Creates a CollectorOutputWriter which shares the provided io_service for async operations.
     * Additional arguments are exposed here to allow customization in unit tests.
     *
     * start() must be called before write()ing data, or else that data will be lost.
     */
    static output_writer_ptr_t create(
        std::shared_ptr<boost::asio::io_service> io_service,
        const mesos::Parameters& parameters);

    /**
     * Use create(). This is meant for access by tests.
     */
    CollectorOutputWriter(
        std::shared_ptr<boost::asio::io_service> io_service,
        const mesos::Parameters& parameters,
        std::shared_ptr<MetricsTCPSender> sender,
        size_t chunk_timeout_ms_for_tests = 0 /* default = use params setting (secs) */);

    virtual ~CollectorOutputWriter();

    /**
     * Starts internal timers for flushing data and refreshing the host.
     */
    void start();

    /**
     * Outputs the provided statsd message associated with the given container information, or NULL
     * container information if none is available. The provided data should only be for a single
     * statsd message. Multiline payloads should be passed individually.
     */
    void write_container_statsd(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* data, size_t size);

   private:
    void start_chunk_flush_timer();
    void flush();
    void chunk_flush_cb(boost::system::error_code ec);

    void shutdown_cb();

    const bool chunking;
    const size_t chunk_timeout_ms;
    const size_t datapoint_capacity;
    size_t datapoint_count;

    container_id_ord_map<ContainerMetrics> container_map;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::deadline_timer flush_timer;
    std::string output_buffer;

    std::shared_ptr<MetricsTCPSender> sender;
  };

}
