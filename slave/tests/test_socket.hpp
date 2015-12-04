#pragma once

#include <boost/asio.hpp>

#include "udp_endpoint.hpp"

class AbstractTestSocket {
 public:
  AbstractTestSocket()
    : svc(), socket(svc), listener_port(0) { }

  virtual ~AbstractTestSocket() {
    socket.close();
  }

 protected:
  const boost::asio::ip::address LOCALHOST = boost::asio::ip::address::from_string("127.0.0.1");

  boost::asio::io_service svc;
  boost::asio::ip::udp::socket socket;
  size_t listener_port;
};

class TestWriteSocket : public AbstractTestSocket {
 public:
  void connect(size_t port) {
    listener_port = port;
    socket.open(boost::asio::ip::udp::v4());
    LOG(INFO) << "(TEST) Configured for communication to destination port[" << listener_port << "]";
  }

  void write(const std::string& data) {
    boost::asio::ip::udp::endpoint endpoint(LOCALHOST, listener_port);
    socket.send_to(boost::asio::buffer(data), endpoint);
    LOG(INFO) << "(TEST) Sent message[" << data << "] from port[" << socket.local_endpoint().port() << "] to destination[" << endpoint << "]";
  }
};


class TestReadSocket : public AbstractTestSocket {
 public:
  TestReadSocket()
    : AbstractTestSocket(),
      timeout_deadline(svc),
      buffer_size(65536) {
    buffer = (char*) malloc(buffer_size);
    check_deadline(); // start timer
  }
  virtual ~TestReadSocket() {
    free(buffer);
  }

  size_t listen() {
    if (listener_port != 0) {
      return listener_port;
    }

    // Open on random OS-selected port:
    boost::asio::ip::udp::endpoint requested_endpoint(LOCALHOST, 0);
    socket.open(requested_endpoint.protocol());
    socket.bind(requested_endpoint);
    // Get resulting port selected by OS:
    LOG(INFO) << "(TEST) Listening on endpoint[" << socket.local_endpoint() << "]";
    listener_port = socket.local_endpoint().port();
    return listener_port;
  }

  size_t available() {
    return socket.available();
  }

  std::string read(size_t timeout_ms = 100) {
    timeout_deadline.expires_from_now(boost::posix_time::milliseconds(timeout_ms));

    boost::system::error_code ec = boost::asio::error::would_block;
    size_t len = 0;

    boost::asio::ip::udp::endpoint sender_endpoint;
    socket.async_receive_from(boost::asio::buffer(buffer, buffer_size),
        sender_endpoint,
        std::bind(&TestReadSocket::recv_cb, std::placeholders::_1, std::placeholders::_2, &ec, &len));

    while (ec == boost::asio::error::would_block) {
      svc.poll();
    }

    if (ec == boost::system::errc::operation_canceled) {
      LOG(INFO) << "(TEST) Timed out waiting " << timeout_ms << "ms for message on port " << listener_port;
      return "";
    } else {
      std::ostringstream oss;
      oss.write(buffer, len);
      LOG(INFO) << "(TEST) Got message[" << oss.str() << "] on port[" << listener_port << "] from sender[" << sender_endpoint << "]";
      return oss.str();
    }
  }

 private:
  void check_deadline() {
    if (timeout_deadline.expires_at() <= boost::asio::deadline_timer::traits_type::now()) {
      // timeout occurred, end any receive() calls
      socket.cancel();
      timeout_deadline.expires_at(boost::posix_time::pos_infin);
    }
    timeout_deadline.async_wait(std::bind(&TestReadSocket::check_deadline, this));
  }

  static void recv_cb(
      const boost::system::error_code& ec, size_t len,
      boost::system::error_code* out_ec, size_t* out_len) {
    // pass read outcome/size upstream
    *out_ec = ec;
    *out_len = len;
  }

  boost::asio::deadline_timer timeout_deadline;
  const size_t buffer_size;
  char* buffer;
};
