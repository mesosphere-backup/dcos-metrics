#pragma once

#include <boost/asio.hpp>

#include "output_writer.hpp"
#include "params.hpp"

namespace metrics {

  class MetricsUDPSender;
  class StatsdTagger;

  /**
   * A StatsdOutputWriter accepts data from one or more ContainerReaders, then tags and forwards it
   * to an external statsd endpoint. The data may be buffered into chunks before being sent out --
   * statsd supports separating multiple metrics by newlines.
   * In practice, there is one singleton StatsdOutputWriter instance per mesos-slave.
   */
  class StatsdOutputWriter : public OutputWriter {
   public:
    /**
     * Default maximum time to wait before sending a pending chunk.
     * Statsd messages are time-sensitive so don't sit on them too long, at most a second or so.
     */
    const static size_t DEFAULT_CHUNK_TIMEOUT_MS = 1000;

    /**
     * Creates a StatsdOutputWriter which shares the provided io_service for async operations.
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
    StatsdOutputWriter(
        std::shared_ptr<boost::asio::io_service> io_service,
        const mesos::Parameters& parameters,
        std::shared_ptr<MetricsUDPSender> sender,
        size_t chunk_timeout_ms_for_tests = DEFAULT_CHUNK_TIMEOUT_MS);

    virtual ~StatsdOutputWriter();

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

    void write_resource_usage(const process::Future<mesos::ResourceUsage>& usage);

   private:
    void start_chunk_flush_timer();
    void chunk_flush_cb(boost::system::error_code ec);

    void shutdown_cb();

    const bool chunking;
    const size_t chunk_capacity;
    const size_t chunk_timeout_ms;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::deadline_timer flush_timer;
    char* output_buffer;
    size_t chunk_used;

    std::shared_ptr<MetricsUDPSender> sender;
    std::shared_ptr<StatsdTagger> tagger;
  };

}
