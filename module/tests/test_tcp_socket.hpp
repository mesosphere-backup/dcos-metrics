#pragma once

#include <mutex>
#include <queue>

#include <boost/asio.hpp>
#include <glog/logging.h>

#include "sync_util.hpp"

class TestTCPReadSession {
 public:
  typedef std::shared_ptr<std::string> str_ptr_t;

  TestTCPReadSession(size_t port = 23456)
    : buffer_size(65536),
      shutdown(false),
      port_(port),
      acceptor(svc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    buffer = (char*) malloc(buffer_size);
    start_accept();
    thread.reset(new std::thread(std::bind(&TestTCPReadSession::run_io, this)));
  }

  virtual ~TestTCPReadSession() {
    shutdown = true;
    if (thread) {
      svc.stop();
      thread->join();
      thread.reset();
      svc.reset();
    }
    free(buffer);
  }

  bool wait_for_available(size_t secs = 5) {
    for (size_t i = 0; i < (secs * 10); ++i) {
      usleep(1000 * 100);
      if (available()) {
        return true;
      }
    }
    return false;
  }

  bool available() {
    std::unique_lock<std::mutex> lock(pkts_mutex);
    return !pkts.empty();
  }

  size_t port() {
    return port_;
  }

  str_ptr_t read() {
    std::unique_lock<std::mutex> lock(pkts_mutex);
    if (pkts.empty()) {
      return str_ptr_t(new std::string(""));
    }
    str_ptr_t oldest_pkt = pkts.front();
    pkts.pop();
    return oldest_pkt;
  }

private:
  void run_io() {
    svc.run();
  }

  void start_accept() {
    LOG(INFO) << "start accept";
    socket.reset(new boost::asio::ip::tcp::socket(svc));
    acceptor.async_accept(*socket,
        std::bind(&TestTCPReadSession::handle_accept, this, std::placeholders::_1));
  }

  void handle_accept(boost::system::error_code ec) {
    if (shutdown) {
      LOG(INFO) << "SHUTDOWN IN ACCEPT";
      return;
    }
    if (ec) {
      LOG(INFO) << "error when accepting: " << ec.message();
      start_accept();
      return;
    }
    LOG(INFO) << "accept complete, start read";
    start_read();
  }

  void start_read() {
    LOG(INFO) << "start read";
    socket->async_read_some(boost::asio::buffer(buffer, buffer_size),
        std::bind(&TestTCPReadSession::handle_read, this, std::placeholders::_1, std::placeholders::_2));
  }

  void handle_read(boost::system::error_code ec, size_t bytes) {
    if (shutdown) {
      LOG(INFO) << "SHUTDOWN IN READ";
      return;
    }
    if (ec) {
      if (ec == boost::asio::error::eof) {
        // session has ended, exit
        LOG(INFO) << "exiting due to session end when reading: " << ec.message();
        return;
      }
      LOG(INFO) << "error when reading: " << ec.message();
    }
    str_ptr_t pkt(new std::string(buffer, bytes));
    {
      std::unique_lock<std::mutex> lock(pkts_mutex);
      LOG(INFO) << "storing " << bytes << " bytes";
      pkts.push(pkt);
    }
    start_read();
  }

  static void do_nothing() { }

  const size_t buffer_size;
  char* buffer;

  boost::asio::io_service svc;
  bool shutdown;
  size_t port_;
  boost::asio::ip::tcp::acceptor acceptor;
  std::shared_ptr<boost::asio::ip::tcp::socket> socket;

  std::unique_ptr<std::thread> thread;
  std::queue<str_ptr_t> pkts;
  std::mutex pkts_mutex;
};
