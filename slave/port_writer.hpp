#pragma once

#include <boost/asio.hpp>
#include <mesos/mesos.pb.h>
#include <stout/nothing.hpp>
#include <stout/try.hpp>

#include "params.hpp"

namespace stats {

  /**
   * A PortWriter accepts data from one or more PortReaders and forwards it to an external endpoint.
   * The data may be buffered into chunks before being sent out -- statsd supports separating
   * multiple metrics by newlines.
   * In practice, there is one singleton PortWriter instance per mesos-slave.
   */
  class PortWriter {
   public:
    /**
     * Default maximum time to wait before sending a pending chunk.
     * Statsd messages are time-sensitive so don't sit on them too long.
     */
    const static size_t DEFAULT_CHUNK_TIMEOUT_MS = 1000;

    /**
     * Creates a PortWriter which shares the provided io_service for async operations.
     * Additional arguments are exposed here to allow customization in unit tests.
     *
     * open() must be called before write()ing data, or else that data will be lost.
     */
    PortWriter(std::shared_ptr<boost::asio::io_service> io_service,
        const mesos::Parameters& parameters,
        size_t chunk_timeout_ms_for_tests = DEFAULT_CHUNK_TIMEOUT_MS,
        size_t resolve_period_ms_for_tests = 0);
    virtual ~PortWriter();

    /**
     * Starts internal timers for flushing data and refreshing the host.
     */
    void start();

    /**
     * Writes the provided payload to the socket, either immediately or
     * after letting it sit in a chunk buffer.
     */
    void write(const char* bytes, size_t size);

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
    const size_t buffer_capacity;
    const bool chunking;
    const size_t chunk_timeout_ms;
    const size_t resolve_period_ms;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::deadline_timer flush_timer;
    boost::asio::deadline_timer resolve_timer;
    udp_endpoint_t current_endpoint;
    std::vector<boost::asio::ip::address> last_resolved_addresses;
    boost::asio::ip::udp::socket socket;
    char* buffer;
    size_t buffer_used;
    bool shutdown;
  };

}
