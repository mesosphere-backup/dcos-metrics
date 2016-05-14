#include "tcp_sender.hpp"

#include <glog/logging.h>

#include "sync_util.hpp"

namespace sp = std::placeholders;

metrics::TCPSender::TCPSender(
    std::shared_ptr<boost::asio::io_service> io_service,
    const std::string& session_header,
    const boost::asio::ip::address& ip,
    size_t port,
    size_t pending_limit)
  : session_header(session_header),
    send_ip(ip),
    send_port(port),
    pending_limit(pending_limit),
    io_service(io_service),
    connect_deadline_timer(*io_service),
    connect_retry_timer(*io_service),
    report_dropped_timer(*io_service),
    socket(*io_service),
    is_reconnect_scheduled(false),
    reconnect_delay(1),
    sent_session_header(false),
    pending_bytes(0),
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
  io_service->dispatch(std::bind(&TCPSender::start_report_dropped_timer, this));
  io_service->dispatch(std::bind(&TCPSender::start_connect, this));
}

void metrics::TCPSender::send(buf_ptr_t buf) {
  if (shutdown || !buf || buf->size() == 0) {
    //DLOG(INFO) << "Skipping scheduled send of zero bytes";
    return;
  }

  if (!socket.is_open() || pending_bytes + buf->size() > pending_limit) {
    // Log dropped data for periodic cumulative reporting
    DLOG(INFO) << "Drop " << buf->size() << " bytes to " << send_ip << ":" << send_port
               << " (pending " << pending_bytes << ")";
    dropped_bytes += buf->size();
    return;
  }

  if (!sent_session_header) {
    // Enforce header in the send() call itself. If we did this in connect_outcome_cb, there'd be a
    // race window where data could be send()ed before connect_outcome_cb is called (with
    // socket.is_open somehow?)
    DLOG(INFO) << "Inserting header before first packet";
    buf_ptr_t hdr_buf(new boost::asio::streambuf);
    std::ostream ostream(hdr_buf.get());
    ostream << session_header;
    // Intentionally omit the header message from enforcement of pending_bytes
    // (Just to keep things a bit simpler)
    boost::asio::async_write(
        socket, *hdr_buf,
        std::bind(&TCPSender::send_cb, this, sp::_1, sp::_2, hdr_buf));
    sent_session_header = true;
  }

  pending_bytes += buf->size();
  DLOG(INFO) << "Send " << buf->size() << " bytes to " << send_ip << ":" << send_port
             << " (now pending " << pending_bytes << ")";
  // Pass buf into send_cb to ensure that it stays in scope until the send has completed:
  boost::asio::async_write(
      socket, *buf,
      std::bind(&TCPSender::send_cb, this, sp::_1, sp::_2, buf));
}

void metrics::TCPSender::schedule_connect() {
  if (is_reconnect_scheduled) {
    LOG(INFO) << "Reconnect already scheduled.";
    return;
  }
  LOG(INFO) << "Scheduling reconnect to " << send_ip << ":" << send_port << " in "
            << reconnect_delay << "s...";

  is_reconnect_scheduled = true;
  connect_retry_timer.expires_from_now(boost::posix_time::seconds(reconnect_delay));
  connect_retry_timer.async_wait(std::bind(&TCPSender::start_connect, this));
  if (reconnect_delay < 60) {
    reconnect_delay *= 2; // exponential backoff
  }
}

void metrics::TCPSender::start_connect() {
  if (shutdown) {
    return;
  }
  is_reconnect_scheduled = false;
  LOG(INFO) << "Attempting to open connection to " << send_ip << ":" << send_port;
  connect_deadline_timer.expires_from_now(boost::posix_time::seconds(60));
  connect_deadline_timer.async_wait(std::bind(&TCPSender::connect_deadline_cb, this));
  socket.async_connect(boost::asio::ip::tcp::endpoint(send_ip, send_port),
      std::bind(&TCPSender::connect_outcome_cb, this, sp::_1));
}

void metrics::TCPSender::connect_deadline_cb() {
  if (shutdown) {
    return;
  }
  if (connect_deadline_timer.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
    LOG(WARNING) << "Timed out when opening connection to " << send_ip << ":" << send_port;
    socket.close();
    schedule_connect();
  }
}

void metrics::TCPSender::connect_outcome_cb(boost::system::error_code ec) {
  if (shutdown) {
    return;
  }
  if (!socket.is_open() || ec) {
    if (!socket.is_open()) {
      LOG(WARNING) << "Socket not open after connecting to " << send_ip << ":" << send_port;
    } else if (ec) {
      LOG(WARNING) << "Got error '" << ec.message() << "'(" << ec << ")"
                   << " when connecting to " << send_ip << ":" << send_port;
    }
    schedule_connect();
    return;
  }

  reconnect_delay = 1; // reset delay

  boost::asio::socket_base::keep_alive option(true);
  socket.set_option(option);
}

void metrics::TCPSender::send_cb(
    boost::system::error_code ec, size_t bytes_transferred, buf_ptr_t keepalive) {
  if (shutdown) {
    return;
  }

  keepalive.reset();
  if (!socket.is_open()) {
    LOG(WARNING) << "Socket not open after sending data to " << send_ip << ":" << send_port;
    sent_session_header = false;
    pending_bytes = 0;
    schedule_connect();
  } else if (ec) {
    LOG(WARNING) << "Got error '" << ec.message() << "'(" << ec << ")"
                 << " when sending data to " << send_ip << ":" << send_port;
    sent_session_header = false;
    pending_bytes = 0;
    socket.close();
    schedule_connect();
  } else {
    if (bytes_transferred > pending_bytes) {
      pending_bytes = 0; //just in case
    } else {
      pending_bytes -= bytes_transferred;
    }
    DLOG(INFO) << "Sent " << bytes_transferred << " bytes (now pending " << pending_bytes << ")";
  }
}

void metrics::TCPSender::shutdown_cb() {
  boost::system::error_code ec;

  connect_deadline_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Connect deadline timer cancellation returned error. "
               << "err='" << ec.message() << "'(" << ec << ")";
  }

  connect_retry_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Connect retry timer cancellation returned error. "
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
                 << " bytes due to lack of open collector socket to ip[" << send_ip << "] "
                 << "(pending: " << pending_bytes << " bytes)";
    dropped_bytes = 0;
  }
  start_report_dropped_timer();
}
