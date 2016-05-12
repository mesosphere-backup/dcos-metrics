#include "tcp_sender.hpp"

#include <glog/logging.h>

#include "sync_util.hpp"

metrics::TCPSender::TCPSender(
    std::shared_ptr<boost::asio::io_service> io_service,
    const std::string& session_header,
    const boost::asio::ip::address& ip,
    size_t port)
  : session_header(session_header),
    send_ip(ip),
    send_port(port),
    io_service(io_service),
    connect_deadline_timer(*io_service),
    report_dropped_timer(*io_service),
    socket(*io_service),
    dropped_bytes(0),
    shutdown(false) {
  LOG(INFO) << "TCPSender constructed for " << send_ip << ":" << send_port;
}

metrics::TCPSender::~TCPSender() {
  LOG(INFO) << "Asynchronously triggering TCPSender shutdown for "
            << send_ip << ":" << send_port;
  shutdown = true;
  // Run the shutdown work itself from within the scheduler:
  if (sync_util::dispatch_run(
          "~TCPSender", *io_service, std::bind(&TCPSender::shutdown_cb, this))) {
    LOG(INFO) << "TCPSender shutdown succeeded";
  } else {
    LOG(ERROR) << "Failed to complete TCPSender shutdown for " << send_ip << ":" << send_port;
  }
}

void metrics::TCPSender::start() {
  // Only run the timer callbacks within the io_service thread:
  LOG(INFO) << "TCPSender starting work";
  io_service->dispatch(std::bind(&TCPSender::report_dropped_cb, this));
  io_service->dispatch(std::bind(&TCPSender::connect, this));
  connect_deadline_timer.async_wait(std::bind(&TCPSender::connect_deadline_cb, this));
}

void metrics::TCPSender::send(const char* bytes, size_t size) {
  if (size == 0) {
    //DLOG(INFO) << "Skipping scheduled send of zero bytes";
    return;
  }

  if (!socket.is_open()) {
    // Log dropped data for periodic cumulative reporting in the resolve callback
    dropped_bytes += size;
    return;
  }

  DLOG(INFO) << "Send " << size << " bytes to " << send_ip << ":" << send_port;
  boost::asio::async_write(
      socket,
      boost::asio::buffer(bytes, size),//TODO add the data to a buffer which is then written
      std::bind(&TCPSender::send_cb, this, std::placeholders::_1));
}

void metrics::TCPSender::connect() {
  connect_deadline_timer.expires_from_now(boost::posix_time::seconds(60));
  boost::asio::socket_base::keep_alive keepalive(true);
  socket.set_option(keepalive);
  socket.async_connect(endpoint_t(send_ip, send_port),
      std::bind(&TCPSender::connect_outcome_cb, this, std::placeholders::_1));
}

void metrics::TCPSender::connect_deadline_cb() {
  if (shutdown) {
    return;
  }
  if (connect_deadline_timer.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
    socket.close();
    connect_deadline_timer.expires_at(boost::posix_time::pos_infin);
  }
  connect_deadline_timer.async_wait(std::bind(&TCPSender::connect_deadline_cb, this));
}

void metrics::TCPSender::connect_outcome_cb(boost::system::error_code ec) {
  if (shutdown) {
    return;
  }
  if (!socket.is_open()) {
    LOG(WARNING) << "Socket not open after connecting to " << send_ip << ":" << send_port << ", "
                 << "retrying...";
    connect();
    return;
  } else if (ec) {
    LOG(WARNING) << "Got error '" << ec.message() << "'(" << ec << ")"
                 << " when connecting to " << send_ip << ":" << send_port << ", retrying...";
    socket.close();
    connect();
    return;
  }
  boost::asio::async_write(
      socket,
      boost::asio::buffer(session_header.data(), session_header.size()),
      std::bind(&TCPSender::send_cb, this, std::placeholders::_1));
}

void metrics::TCPSender::send_cb(boost::system::error_code ec) {
  if (shutdown) {
    return;
  }
  if (!socket.is_open()) {
    LOG(WARNING) << "Socket not open after sending data to " << send_ip << ":" << send_port << ", "
                 << "reconnecting...";
    connect();
  } else if (ec) {
    LOG(WARNING) << "Got error '" << ec.message() << "'(" << ec << ")"
                 << " when sending data to " << send_ip << ":" << send_port << ", reconnecting...";
    socket.close();
    connect();
  }

  //TODO send data, swap buffers, something something
}

void metrics::TCPSender::shutdown_cb() {
  boost::system::error_code ec;

  connect_deadline_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Connect deadline timer cancellation returned error. "
               << "err='" << ec.message() << "'(" << ec << ")";
  }

  report_dropped_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Connect deadline timer cancellation returned error. "
               << "err='" << ec.message() << "'(" << ec << ")";
  }

  if (socket.is_open()) {
    socket.close(ec);
    if (ec) {
      LOG(ERROR) << "Error on writer socket close. "
                 << "err='" << ec.message() << "'(" << ec << ")";
    }
  }
}

void metrics::TCPSender::start_report_dropped_timer() {
  report_dropped_timer.expires_from_now(boost::posix_time::seconds(60));
  report_dropped_timer.async_wait(std::bind(&TCPSender::report_dropped_cb, this));
}

void metrics::TCPSender::report_dropped_cb() {
  if (shutdown) {
    return;
  }
  // Warn periodically when data is being dropped due to lack of outgoing connection
  if (dropped_bytes > 0) {
    LOG(WARNING) << "Recently dropped " << dropped_bytes
                 << " bytes due to lack of open collector socket to ip[" << send_ip << "]";
    dropped_bytes = 0;
  }
  start_report_dropped_timer();
}
