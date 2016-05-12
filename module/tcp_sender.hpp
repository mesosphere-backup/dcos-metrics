#pragma once

#include <boost/asio.hpp>
#include <set>

#include "params.hpp"

namespace metrics {

  /**
   * A TCPSender is the underlying implementation of getting data to a TCP endpoint. It handles
   * keeping the connection alive, and sending any passed data when feasible.
   */
  class TCPSender {
   public:
    typedef std::shared_ptr<boost::asio::streambuf> buf_ptr_t;

    /**
     * Creates a TCPSender which shares the provided io_service for async operations.
     * Additional arguments are exposed here to allow customization in unit tests.
     *
     * start() must be called before send()ing data, or else that data will be lost.
     */
    TCPSender(std::shared_ptr<boost::asio::io_service> io_service,
        const std::string& session_header,
        const boost::asio::ip::address& ip,
        size_t port);

    virtual ~TCPSender();

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
    typedef boost::asio::ip::tcp::endpoint endpoint_t;

    void start_connect();
    void connect_deadline_cb();
    void connect_outcome_cb(boost::system::error_code ec);
    void send_cb(boost::system::error_code ec, size_t bytes_transferred, buf_ptr_t keepalive);
    void shutdown_cb();
    void start_report_dropped_timer();
    void report_dropped_cb();

    const std::string session_header;
    const boost::asio::ip::address send_ip;
    const size_t send_port;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::deadline_timer connect_deadline_timer;
    boost::asio::deadline_timer report_dropped_timer;
    boost::asio::ip::tcp::socket socket;
    // TODO double buffering (with limits on amount buffered):
    //std::shared_ptr<boost::asio::streambuf> accum_buf, send_buf;
    //bool sending;
    size_t dropped_bytes;
    bool shutdown;
  };
}
