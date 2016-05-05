#pragma once

#include <boost/asio.hpp>

#include "output_writer.hpp"
#include "params.hpp"

namespace metrics {

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
     * open() must be called before write()ing data, or else that data will be lost.
     */
    StatsdOutputWriter(std::shared_ptr<boost::asio::io_service> io_service,
        const mesos::Parameters& parameters,
        size_t chunk_timeout_ms_for_tests = DEFAULT_CHUNK_TIMEOUT_MS,
        size_t resolve_period_ms_for_tests = 0);

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

   protected:
    typedef boost::asio::ip::udp::resolver udp_resolver_t;

    /**
     * DNS lookup operation. Broken out for easier mocking in tests.
     */
    virtual udp_resolver_t::iterator resolve(boost::system::error_code& ec);

    /**
     * Cancels running timers. Subclasses should call this in their destructor, to avoid the default
     * resolve() being called in the timespan between ~<Subclass>() and ~StatsdOutputWriter().
     */
    void shutdown();

   private:
    typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

    void start_dest_resolve_timer();
    void dest_resolve_cb(boost::system::error_code ec);

    void start_chunk_flush_timer();
    void chunk_flush_cb(boost::system::error_code ec);

    void send_raw_bytes(const char* bytes, size_t size);
    void shutdown_cb();

    const std::string send_host;
    const size_t send_port;
    const bool chunking;
    const size_t chunk_capacity;
    const size_t chunk_timeout_ms;
    const size_t resolve_period_ms;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::deadline_timer flush_timer;
    boost::asio::deadline_timer resolve_timer;
    udp_endpoint_t current_endpoint;
    std::multiset<boost::asio::ip::address> last_resolved_addresses;
    boost::asio::ip::udp::socket socket;
    char* output_buffer;
    size_t chunk_used;
    size_t dropped_bytes;
    std::shared_ptr<StatsdTagger> tagger;
  };

}
