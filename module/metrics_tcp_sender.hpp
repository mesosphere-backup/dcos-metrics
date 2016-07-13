#pragma once

#include <boost/asio.hpp>
#include <set>

#include "params.hpp"

namespace metrics {

  /**
   * A MetricsTCPSender is the underlying implementation of getting data to a TCP endpoint. It
   * handles keeping the connection alive, and sending any passed data when feasible.
   */
  class MetricsTCPSender {
   public:
    typedef std::shared_ptr<boost::asio::streambuf> buf_ptr_t;

    /**
     * Sets a limit on how many bytes may be pending on the outgoing socket at a time.
     * If this limit is exceeded, sent data will be dropped until the pending data
     * has come back under the limit.
     */
    static const size_t PENDING_LIMIT = 256 * 1024;

    /**
     * Creates a MetricsTCPSender which shares the provided io_service for async operations.
     * Additional arguments are exposed here to allow customization in unit tests.
     *
     * start() must be called before send()ing data, or else that data will be lost.
     */
    MetricsTCPSender(std::shared_ptr<boost::asio::io_service> io_service,
        const std::string& session_header,
        const boost::asio::ip::address& ip,
        size_t port,
        size_t pending_limit_for_tests = PENDING_LIMIT);

    virtual ~MetricsTCPSender();

    /**
     * Triggers setting up the TCP session.
     */
    void start();

    /**
     * Sends data to the current endpoint, or fails silently if the endpoint isn't available.
     * This call should only be performed from within the IO thread.
     */
    void send(buf_ptr_t buf);

   private:
    void set_state_schedule_connect();
    void start_connect();
    void connect_deadline_cb();
    void connect_outcome_cb(boost::system::error_code ec);
    void send_cb(boost::system::error_code ec, size_t bytes_transferred, buf_ptr_t keepalive);
    void shutdown_cb();
    void start_report_bytes_timer();
    void report_bytes_cb();

    const std::string session_header;
    const boost::asio::ip::address send_ip;
    const size_t send_port;
    const size_t pending_limit;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::deadline_timer connect_deadline_timer;
    boost::asio::deadline_timer connect_retry_timer;
    boost::asio::deadline_timer report_bytes_timer;
    boost::asio::ip::tcp::socket socket;

    enum SocketState {
      UNKNOWN = 0,
      NOT_STARTED = 1,
      DISCONNECTED = 2,
      CONNECT_PENDING = 3,
      CONNECT_IN_PROGRESS = 4,
      CONNECTED_DATA_NOT_READY = 5,
      CONNECTED_DATA_READY = 6,
      SHUTDOWN = 7
    };
    static const char* to_string(SocketState state) {
      switch (state) {
        case UNKNOWN: return "UNKNOWN";
        case NOT_STARTED: return "NOT_STARTED";
        case DISCONNECTED: return "DISCONNECTED";
        case CONNECT_PENDING: return "CONNECT_PENDING";
        case CONNECT_IN_PROGRESS: return "CONNECT_IN_PROGRESS";
        case CONNECTED_DATA_NOT_READY: return "CONNECTED_DATA_NOT_READY";
        case CONNECTED_DATA_READY: return "CONNECTED_DATA_READY";
        case SHUTDOWN: return "SHUTDOWN";
      }
      return "???";
    }
    SocketState socket_state;

    size_t reconnect_delay;
    size_t pending_bytes, sent_bytes, dropped_bytes, failed_bytes;
  };
}
