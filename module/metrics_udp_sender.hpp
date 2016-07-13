#pragma once

#include <boost/asio.hpp>
#include <set>

#include "params.hpp"

namespace metrics {

  /**
   * A MetricsUDPSender is the underlying implementation of getting data to a UDP endpoint. It
   * handles periodically refreshing the destination endpoint for changes, along with passing any
   * data to the endpoint.
   */
  class MetricsUDPSender {
   public:
    /**
     * Creates a MetricsUDPSender which shares the provided io_service for async operations.
     * Additional arguments are exposed here to allow customization in unit tests.
     *
     * start() must be called before send()ing data, or else that data will be lost.
     */
    MetricsUDPSender(std::shared_ptr<boost::asio::io_service> io_service,
        const std::string& host,
        size_t port,
        size_t resolve_period_ms);

    virtual ~MetricsUDPSender();

    /**
     * Starts internal timers for refreshing the host.
     */
    void start();

    /**
     * Sends data to the current endpoint, or fails silently if the endpoint isn't available.
     * This call should only be performed from within the IO thread.
     */
    void send(const char* bytes, size_t size);

   protected:
    typedef boost::asio::ip::udp::resolver udp_resolver_t;

    /**
     * DNS lookup operation. Broken out for easier mocking in tests.
     */
    virtual boost::asio::ip::udp::resolver::iterator resolve(boost::system::error_code& ec);

    /**
     * Cancels running timers. Subclasses should call this in their destructor, to avoid the default
     * resolve() being called in the timespan between ~<Subclass>() and ~MetricsUDPSender().
     */
    void shutdown();

   private:
    typedef boost::asio::ip::udp::endpoint endpoint_t;

    void start_dest_resolve_timer();
    void dest_resolve_cb(boost::system::error_code ec);

    void shutdown_cb();

    const std::string send_host;
    const size_t send_port;
    const size_t resolve_period_ms;

    std::shared_ptr<boost::asio::io_service> io_service;
    boost::asio::deadline_timer resolve_timer;
    endpoint_t current_endpoint;
    std::multiset<boost::asio::ip::address> last_resolved_addresses;
    boost::asio::ip::udp::socket socket;
    size_t sent_bytes, dropped_bytes;
  };
}
