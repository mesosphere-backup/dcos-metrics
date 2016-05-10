#include "socket_sender.hpp"

#include <glog/logging.h>

#include "sync_util.hpp"

template <typename AsioProtocol>
metrics::SocketSender<AsioProtocol>::SocketSender(
    std::shared_ptr<boost::asio::io_service> io_service,
    const std::string& host,
    size_t port,
    size_t resolve_period_ms)
  : send_host(host),
    send_port(port),
    resolve_period_ms(resolve_period_ms),
    io_service(io_service),
    resolve_timer(*io_service),
    socket(*io_service),
    dropped_bytes(0) {
  LOG(INFO) << "SocketSender constructed for " << send_host << ":" << send_port;
  if (resolve_period_ms == 0) {
    LOG(FATAL) << "Invalid " << params::OUTPUT_STATSD_HOST_REFRESH_SECONDS << " value: must be non-zero";
  }
}

template <typename AsioProtocol>
metrics::SocketSender<AsioProtocol>::~SocketSender() {
  shutdown();
}

template <typename AsioProtocol>
void metrics::SocketSender<AsioProtocol>::start() {
  // Only run the timer callbacks within the io_service thread:
  LOG(INFO) << "SocketSender starting work";
  io_service->dispatch(std::bind(&SocketSender<AsioProtocol>::dest_resolve_cb, this, boost::system::error_code()));
}

template <typename AsioProtocol>
void metrics::SocketSender<AsioProtocol>::send(const char* /*bytes*/, size_t /*size*/) {
  DLOG(FATAL) << "send() function not implemented";
}

template <>
void metrics::SocketSender<boost::asio::ip::tcp>::send(const char* /*bytes*/, size_t size) {
  if (size == 0) {
    //DLOG(INFO) << "Skipping scheduled send of zero bytes";
    return;
  }

  if (!socket.is_open()) {
    // Log dropped data for periodic cumulative reporting in the resolve callback
    dropped_bytes += size;
    return;
  }

  DLOG(INFO) << "Send " << size << " bytes to " << send_host << ":" << send_port;
  //TODO tcp version of send
}

template <>
void metrics::SocketSender<boost::asio::ip::udp>::send(const char* bytes, size_t size) {
  if (size == 0) {
    //DLOG(INFO) << "Skipping scheduled send of zero bytes";
    return;
  }

  if (!socket.is_open()) {
    // Log dropped data for periodic cumulative reporting in the resolve callback
    dropped_bytes += size;
    return;
  }

  DLOG(INFO) << "Send " << size << " bytes to " << send_host << ":" << send_port;
  boost::system::error_code ec;
  size_t sent = socket.send_to(boost::asio::buffer(bytes, size), current_endpoint, 0 /* flags */, ec);
  if (ec) {
    LOG(ERROR) << "Failed to send " << size << " bytes of data to ["
               << send_host << ":" << send_port << "] err=" << ec;
  }
  if (sent != size) {
    LOG(WARNING) << "Sent size=" << sent << " doesn't match requested size=" << size;
  }
}

template <typename AsioProtocol>
typename AsioProtocol::resolver::iterator metrics::SocketSender<AsioProtocol>::resolve(
    boost::system::error_code& ec) {
  typename AsioProtocol::resolver resolver(*io_service);
  return resolver.resolve(typename AsioProtocol::resolver::query(send_host, ""), ec);
}

template <typename AsioProtocol>
void metrics::SocketSender<AsioProtocol>::shutdown() {
  LOG(INFO) << "Asynchronously triggering SocketSender shutdown for "
            << send_host << ":" << send_port;
  // Run the shutdown work itself from within the scheduler:
  if (sync_util::dispatch_run(
          "~SocketSender", *io_service, std::bind(&SocketSender<AsioProtocol>::shutdown_cb, this))) {
    LOG(INFO) << "SocketSender shutdown succeeded";
  } else {
    LOG(ERROR) << "Failed to complete SocketSender shutdown for " << send_host << ":" << send_port;
  }
}

template <typename AsioProtocol>
void metrics::SocketSender<AsioProtocol>::start_dest_resolve_timer() {
  resolve_timer.expires_from_now(boost::posix_time::milliseconds(resolve_period_ms));
  resolve_timer.async_wait(std::bind(&SocketSender<AsioProtocol>::dest_resolve_cb, this, std::placeholders::_1));
}

template <typename AsioProtocol>
void metrics::SocketSender<AsioProtocol>::dest_resolve_cb(boost::system::error_code ec) {
  if (ec) {
    if (boost::asio::error::operation_aborted) {
      // We're being destroyed. Don't look at local state, it may be destroyed already.
      LOG(WARNING) << "Resolve timer aborted: Exiting loop immediately";
      return;
    } else {
      LOG(ERROR) << "Resolve timer returned error. err=" << ec;
    }
  }

  // Warn periodically when data is being dropped due to lack of outgoing connection
  if (dropped_bytes > 0) {
    LOG(WARNING) << "Recently dropped " << dropped_bytes
                 << " bytes due to lack of open writer socket to host[" << send_host << "]";
    dropped_bytes = 0;
  }

  typename udp_resolver_t::iterator iter = resolve(ec);
  boost::asio::ip::address selected_address;
  if (ec) {
    // dns lookup failed, fall back to parsing the host string as a literal ip
    boost::system::error_code ec2;
    selected_address = boost::asio::ip::address::from_string(send_host, ec2);
    if (ec2) {
      // using host as-is also failed, give up and try again later
      if (socket.is_open()) {
        // Log as error: User used to have a working host!
        LOG(ERROR) << "Error when resolving host[" << send_host << "]. "
                   << "Sending data to old endpoint[" << current_endpoint << "] "
                   << "and trying again in " << resolve_period_ms / 1000. << " seconds. "
                   << "err=" << ec << ", err2=" << ec2;
      } else {
        // Log as warning: User may not have brought up their metrics service yet.
        LOG(WARNING) << "Error when resolving host[" << send_host << "]. "
                     << "Dropping data and trying again in "
                     << resolve_period_ms / 1000. << " seconds. "
                     << "err=" << ec << ", err2=" << ec2;
      }
      start_dest_resolve_timer();
      return;
    }
    // parsing the host as a literal ip succeeded. skip random address selection below since we only
    // have a single entry anyway.
  } else if (iter == typename udp_resolver_t::iterator()) {
    // dns lookup had no results, fall back to parsing the host string as a literal ip
    selected_address = boost::asio::ip::address::from_string(send_host, ec);
    if (ec) {
      // using host as-is also failed, give up and try again later
      if (socket.is_open()) {
        // Log as error: User used to have a working host!
        LOG(ERROR) << "No results when resolving host[" << send_host << "]. "
                   << "Sending data to old endpoint[" << current_endpoint << "] "
                   << "and trying again in " << resolve_period_ms / 1000. << " seconds. "
                   << "err=" << ec;
      } else {
        // Log as warning: User may not have brought up their metrics service yet.
        LOG(WARNING) << "No results when resolving host[" << send_host << "]. "
                     << "Dropping data and trying again in "
                     << resolve_period_ms / 1000. << " seconds. "
                     << "err=" << ec;
      }
      start_dest_resolve_timer();
      return;
    }
    // parsing the host as a literal ip succeeded. skip random address selection below since we only
    // have a single entry anyway.
  } else {
    // resolved successfully. pick a new endpoint only if the list of endpoints has changed since
    // the last refresh. since we are performing our own randomization below, detect and normalize
    // any randomized ordering produced by the dns server, only changing our destination endpoint
    // if the list changes beyond superficial reordering.
    std::multiset<boost::asio::ip::address> sorted_resolved_addresses;
    for (; iter != typename udp_resolver_t::iterator(); ++iter) {
      sorted_resolved_addresses.insert(iter->endpoint().address());
    }
    if (sorted_resolved_addresses == last_resolved_addresses) {
      LOG(INFO) << "No change in resolved addresses[size=" << sorted_resolved_addresses.size() << "], "
                << "leaving socket as-is and checking again in "
                << resolve_period_ms / 1000. << " seconds.";
      start_dest_resolve_timer();
      return;
    }

    // list has changed, switch to a new random entry (redistribute load across new list)
    size_t rand_index;
    {
      std::random_device dev;
      std::mt19937 engine{dev()};
      std::uniform_int_distribution<size_t> dist(0, sorted_resolved_addresses.size() - 1);
      rand_index = dist(engine);
    }

    std::multiset<boost::asio::ip::address>::const_iterator iter = sorted_resolved_addresses.begin();
    for (size_t i = 0; i < rand_index; ++i) {
      ++iter;
    }
    selected_address = *iter;

    LOG(INFO) << "Resolved dest host[" << send_host << "] "
              << "-> results[size=" << sorted_resolved_addresses.size() << "] "
              << "-> selected[" << selected_address << "]";
    last_resolved_addresses = sorted_resolved_addresses;
  }

  endpoint_t new_endpoint(selected_address, send_port);
  if (socket.is_open()) {
    // Before closing the current socket, check that the endpoint has changed.
    if (new_endpoint == current_endpoint) {
      DLOG(INFO) << "No change in selected endpoint[" << current_endpoint << "], "
                 << "leaving socket as-is and checking again in "
                 << resolve_period_ms / 1000. << " seconds.";
      start_dest_resolve_timer();
      return;
    }
    // Socket is moing to a new endpoint. Close socket before reopening.
    socket.close(ec);
    if (ec) {
      LOG(ERROR) << "Failed to close writer socket for move from "
                 << "old_endpoint[" << current_endpoint << "] to "
                 << "new_endpoint[" << new_endpoint << "] err=" << ec;
    }
  }
  socket.open(new_endpoint.protocol(), ec);
  if (ec) {
    LOG(ERROR) << "Failed to open writer socket to endpoint[" << new_endpoint << "] err=" << ec;
  } else {
    LOG(INFO) << "Updated dest endpoint[" << current_endpoint << "] to endpoint[" << new_endpoint << "]";
    current_endpoint = new_endpoint;
  }
  start_dest_resolve_timer();
}

template <typename AsioProtocol>
void metrics::SocketSender<AsioProtocol>::shutdown_cb() {
  boost::system::error_code ec;

  resolve_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Resolve timer cancellation returned error. err=" << ec;
  }

  if (socket.is_open()) {
    socket.close(ec);
    if (ec) {
      LOG(ERROR) << "Error on writer socket close. err=" << ec;
    }
  }
}

template class metrics::SocketSender<boost::asio::ip::tcp>;
template class metrics::SocketSender<boost::asio::ip::udp>;
