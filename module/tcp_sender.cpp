#include "tcp_sender.hpp"

#include <glog/logging.h>

#include "sync_util.hpp"

namespace sp = std::placeholders;

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
  // configure deadline timer, but don't set deadline yet
  connect_deadline_timer.async_wait(std::bind(&TCPSender::connect_deadline_cb, this));

  io_service->dispatch(std::bind(&TCPSender::start_report_dropped_timer, this));
  io_service->dispatch(std::bind(&TCPSender::start_connect, this));
}

void metrics::TCPSender::send(std::shared_ptr<boost::asio::streambuf> buf) {
  if (shutdown || !buf || buf->size() == 0) {
    //DLOG(INFO) << "Skipping scheduled send of zero bytes";
    return;
  }

  if (!socket.is_open()) {
    // Log dropped data for periodic cumulative reporting in the resolve callback
    dropped_bytes += buf->size();
    return;
  }

  DLOG(INFO) << "Send " << buf->size() << " bytes to " << send_ip << ":" << send_port;
  boost::asio::async_write(
      socket, *buf,
      std::bind(&TCPSender::send_cb, this, sp::_1, sp::_2, buf));
}

void metrics::TCPSender::start_connect() {
  if (shutdown) {
    return;
  }
  connect_deadline_timer.expires_from_now(boost::posix_time::seconds(60));
  //boost::asio::socket_base::keep_alive keepalive(true);
  //socket.set_option(keepalive);
  socket.async_connect(endpoint_t(send_ip, send_port),
      std::bind(&TCPSender::connect_outcome_cb, this, sp::_1));
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
    start_connect();
    return;
  } else if (ec) {
    LOG(WARNING) << "Got error '" << ec.message() << "'(" << ec << ")"
                 << " when connecting to " << send_ip << ":" << send_port << ", retrying...";
    socket.close();
    start_connect();//TODO delayed trigger?
    return;
  }
  buf_ptr_t buf(new boost::asio::streambuf);
  {
    std::ostream ostream(buf.get());
    ostream << session_header;
  }
  DLOG(INFO) << "Sending session header";
  send(buf);
}

void metrics::TCPSender::send_cb(
    boost::system::error_code ec, size_t bytes_transferred, buf_ptr_t keepalive) {
  if (shutdown) {
    return;
  }
  keepalive.reset();
  if (!socket.is_open()) {
    LOG(WARNING) << "Socket not open after sending data to " << send_ip << ":" << send_port << ", "
                 << "reconnecting...";
    start_connect();
  } else if (ec) {
    LOG(WARNING) << "Got error '" << ec.message() << "'(" << ec << ")"
                 << " when sending data to " << send_ip << ":" << send_port << ", reconnecting...";
    socket.close();
    start_connect();
  } else {
    DLOG(INFO) << "Sent " << bytes_transferred << " bytes.";
  }
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
  if (shutdown) {
    return;
  }
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
