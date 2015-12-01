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
    const size_t DEFAULT_CHUNK_TIMEOUT_MS = 1000;

    /**
     * Creates a PortWriter which shares the provided io_service for async operations.
     * chunk_timeout_ms is exposed here to allow customization in unit tests.
     */
    PortWriter(std::shared_ptr<boost::asio::io_service> io_service,
        const mesos::Parameters& parameters,
        size_t chunk_timeout_ms = 1000);
    virtual ~PortWriter();

    /**
     * Opens a UDP socket to the Param-defined destination.
     * Returns Nothing on success, or an error otherwise.
     */
    Try<Nothing> open();

    /**
     * Writes the provided payload to the socket, either immediately or
     * after letting it sit in a chunk buffer.
     */
    void write(const char* bytes, size_t size);

   private:
    typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

    void start_chunk_flush_timer();
    void chunk_flush_cb(const boost::system::error_code& ec);
    void send_raw_bytes(const char* bytes, size_t size);

    const std::string send_host;
    const size_t send_port;
    const size_t buffer_capacity;
    const bool chunking;
    const size_t chunk_timeout_ms;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::deadline_timer flush_timer;
    udp_endpoint_t send_endpoint;
    boost::asio::ip::udp::socket socket;
    char* buffer;
    size_t buffer_used;
  };

}
