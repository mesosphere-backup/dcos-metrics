#define NDEBUG
#include "metrics_tcp_sender.hpp"

#include <glog/logging.h>

#include "socket_util.hpp"
#include "sync_util.hpp"

namespace sp = std::placeholders;

#define MAX_RECONNECT_DELAY 60
#define CONNECT_TIMEOUT_SECS 60

metrics::MetricsTCPSender::MetricsTCPSender(
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
    report_bytes_timer(*io_service),
    socket(*io_service),
    socket_state(NOT_STARTED),
    reconnect_delay(1),
    pending_bytes(0),
    sent_bytes(0),
    dropped_bytes(0),
    failed_bytes(0) {
  LOG(INFO) << "MetricsTCPSender constructed for " << send_ip << ":" << send_port;
}

metrics::MetricsTCPSender::~MetricsTCPSender() {
  LOG(INFO) << "Asynchronously triggering MetricsTCPSender shutdown for "
            << send_ip << ":" << send_port;
  socket_state = SHUTDOWN;
  // Run the shutdown work itself from within the scheduler:
  if (sync_util::dispatch_run(
          "~MetricsTCPSender", *io_service, std::bind(&MetricsTCPSender::shutdown_cb, this))) {
    LOG(INFO) << "MetricsTCPSender shutdown succeeded";
  } else {
    LOG(ERROR) << "Failed to complete MetricsTCPSender shutdown for " << send_ip << ":" << send_port;
  }
}

void metrics::MetricsTCPSender::start() {
  // Only run the timer callbacks within the io_service thread:
  LOG(INFO) << "MetricsTCPSender starting work";
  if (socket_state != NOT_STARTED) {
    LOG(FATAL) << "start() called when already started! current state is: "
               << to_string(socket_state);
  }
  socket_state = DISCONNECTED;
  io_service->dispatch(std::bind(&MetricsTCPSender::start_report_bytes_timer, this));
  io_service->dispatch(std::bind(&MetricsTCPSender::start_connect, this));
}

void metrics::MetricsTCPSender::send(buf_ptr_t buf) {
  if (socket_state == SHUTDOWN || !buf || buf->size() == 0) {
    //DLOG(INFO) << "Skipping scheduled send of zero bytes";
    return;
  }

  if (socket_state != CONNECTED_DATA_READY) {
    DLOG(INFO) << "Drop " << buf->size() << " bytes to " << send_ip << ":" << send_port
               << " (pending " << pending_bytes << ") due to state " << to_string(socket_state);
    dropped_bytes += buf->size();
    return;
  } else if (pending_bytes + buf->size() > pending_limit) {
    DLOG(INFO) << "Drop " << buf->size() << " bytes to " << send_ip << ":" << send_port
               << " (pending " << pending_bytes << ") due to buffer too large";
    dropped_bytes += buf->size();
    return;
  }

  pending_bytes += buf->size();
  DLOG(INFO) << "Send " << buf->size() << " bytes to " << send_ip << ":" << send_port
             << " (now pending " << pending_bytes << ")";
  // Pass buf into send_cb to ensure that it stays in scope until the send has completed:
  boost::asio::async_write(
      socket, *buf,
      std::bind(&MetricsTCPSender::send_cb, this, sp::_1, sp::_2, buf));
}

void metrics::MetricsTCPSender::set_state_schedule_connect() {
  if (socket_state == CONNECT_PENDING || socket_state == CONNECT_IN_PROGRESS) {
    DLOG(INFO) << "Reconnect already scheduled.";
    return;
  }
  LOG(INFO) << "Scheduling reconnect to " << send_ip << ":" << send_port << " in "
            << reconnect_delay << "s...";

  socket_state = CONNECT_PENDING;
  connect_retry_timer.expires_from_now(boost::posix_time::seconds(reconnect_delay));
  connect_retry_timer.async_wait(std::bind(&MetricsTCPSender::start_connect, this));
  reconnect_delay *= 2; // exponential backoff ..
  if (reconnect_delay > MAX_RECONNECT_DELAY) {
    reconnect_delay = MAX_RECONNECT_DELAY;
  }
}

void metrics::MetricsTCPSender::start_connect() {
  if (socket_state == SHUTDOWN) {
    return;
  }

  socket_state = CONNECT_IN_PROGRESS;
  LOG(INFO) << "Attempting to open connection to " << send_ip << ":" << send_port;
  connect_deadline_timer.expires_from_now(boost::posix_time::seconds(CONNECT_TIMEOUT_SECS));
  connect_deadline_timer.async_wait(std::bind(&MetricsTCPSender::connect_deadline_cb, this));
  socket.async_connect(boost::asio::ip::tcp::endpoint(send_ip, send_port),
      std::bind(&MetricsTCPSender::connect_outcome_cb, this, sp::_1));
}

void metrics::MetricsTCPSender::connect_deadline_cb() {
  if (socket_state == SHUTDOWN
      || socket_state == CONNECTED_DATA_NOT_READY
      || socket_state == CONNECTED_DATA_READY) {
    DLOG(INFO) << "Connect timeout checked, looks fine! (state " << to_string(socket_state) << ")";
    return;
  }
  if (connect_deadline_timer.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
    LOG(INFO) << "Timed out when opening metrics connection to " << send_ip << ":" << send_port
              << ". This is expected if no Metrics Collector is running on this agent."
              << " (state " << to_string(socket_state) << ")";
    socket.close(); // this will fire connect_outcome
  }
}

void metrics::MetricsTCPSender::connect_outcome_cb(boost::system::error_code ec) {
  if (socket_state == SHUTDOWN) {
    return;
  }
  if (!socket.is_open() || ec) {
    if (ec) {
      LOG(INFO) << "Got error '" << ec.message() << "'(" << ec << ")"
                << " when connecting to metrics service at " << send_ip << ":" << send_port
                << ". This is expected if no Metrics Collector is running on this agent."
                << " (state " << to_string(socket_state) << ")";
    } else if (!socket.is_open()) {
      LOG(INFO) << "Metrics socket not open after connecting to " << send_ip << ":" << send_port
                << ". This is expected if no Metrics Collector is running on this agent."
                << " (state " << to_string(socket_state) << ")";
    }
    socket_state = DISCONNECTED;
    set_state_schedule_connect();
    return;
  }

  reconnect_delay = 1; // reset delay

  boost::asio::socket_base::keep_alive option(true);
  socket.set_option(option);
  set_cloexec(socket, send_ip, send_port);

  socket_state = CONNECTED_DATA_NOT_READY;
  DLOG(INFO) << "Connected to metrics service at " << send_ip << ":" << send_port << ". "
             << "Inserting " << session_header.size() << " byte header data before first packet";
  buf_ptr_t hdr_buf(new boost::asio::streambuf);
  std::ostream ostream(hdr_buf.get());
  ostream << session_header;
  // Intentionally omit the header message from enforcement of pending_bytes
  // (Just to keep things a bit simpler)
  boost::asio::async_write(
      socket, *hdr_buf,
      std::bind(&MetricsTCPSender::send_cb, this, sp::_1, sp::_2, hdr_buf));
}

void metrics::MetricsTCPSender::send_cb(
    boost::system::error_code ec, size_t bytes_transferred, buf_ptr_t keepalive) {
  if (socket_state == SHUTDOWN) {
    return;
  }

  keepalive.reset();
  if (ec) {
    LOG(WARNING) << "Got error '" << ec.message() << "'(" << ec << ")"
                 << " when sending data to metrics service at " << send_ip << ":" << send_port
                 << " (state " << to_string(socket_state) << ")";
    pending_bytes = 0;
    failed_bytes += bytes_transferred;
    socket.close();
    set_state_schedule_connect();
  } else if (!socket.is_open()) {
    LOG(WARNING) << "Socket not open after sending metrics data to " << send_ip << ":" << send_port
                 << " (state " << to_string(socket_state) << ")";
    pending_bytes = 0;
    failed_bytes += bytes_transferred;
    set_state_schedule_connect();
  } else {
    if (socket_state == CONNECTED_DATA_NOT_READY) {
      socket_state = CONNECTED_DATA_READY; // we likely just successfully sent the header
      DLOG(INFO) << "Header sent, socket is now " << to_string(socket_state);
    }
    if (bytes_transferred > pending_bytes) {
      pending_bytes = 0; //just in case
    } else {
      pending_bytes -= bytes_transferred;
    }
    sent_bytes += bytes_transferred;
    DLOG(INFO) << "Sent " << bytes_transferred << " bytes "
               << "(now pending " << pending_bytes << ", state " << to_string(socket_state) << ")";
  }
}

void metrics::MetricsTCPSender::shutdown_cb() {
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

  report_bytes_timer.cancel(ec);
  if (ec) {
    LOG(ERROR) << "Report bytes timer cancellation returned error. "
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

void metrics::MetricsTCPSender::start_report_bytes_timer() {
  if (socket_state == SHUTDOWN) {
    return;
  }
  report_bytes_timer.expires_from_now(boost::posix_time::seconds(60));
  report_bytes_timer.async_wait(std::bind(&MetricsTCPSender::report_bytes_cb, this));
}

void metrics::MetricsTCPSender::report_bytes_cb() {
  if (socket_state == SHUTDOWN) {
    return;
  }
  LOG(INFO) << "TCP Throughput (bytes): "
            << "sent=" << sent_bytes << ", "
            << "dropped=" << dropped_bytes << ", "
            << "failed=" << failed_bytes << ", "
            << "pending=" << pending_bytes
            << " (state " << to_string(socket_state) << ")";
  sent_bytes = 0;
  dropped_bytes = 0;
  failed_bytes = 0;
  start_report_bytes_timer();
}
