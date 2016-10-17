#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "stub_udp_sender.hpp"
#include "test_udp_socket.hpp"
#include "sync_util.hpp"

namespace {
  const std::string HELLO("hello"), HEY("hey"), HI("hi");

  const boost::asio::ip::udp::endpoint DEST_LOCAL_ENDPOINT(
      boost::asio::ip::address::from_string("127.0.0.1"), 0 /* port */);
  const boost::asio::ip::udp::endpoint DEST_LOCAL_ENDPOINT6(
      boost::asio::ip::address::from_string("::1"), 0 /* port */);

  void flush_service_queue_with_noop() {
    LOG(INFO) << "async queue flushed";
  }

  class ServiceThread {
   public:
    ServiceThread()
      : svc_(new boost::asio::io_service),
        check_timer(*svc_),
        shutdown(false) {
      LOG(INFO) << "start thread";
      check_timer.expires_from_now(boost::posix_time::milliseconds(100));
      check_timer.async_wait(std::bind(&ServiceThread::check_exit_cb, this));
      svc_thread.reset(new std::thread(std::bind(&ServiceThread::run_svc, this)));
    }
    virtual ~ServiceThread() {
      EXPECT_FALSE((bool)svc_thread) << "ServiceThread.join() must be called before destructor";
    }

    std::shared_ptr<boost::asio::io_service> svc() {
      return svc_;
    }

    void join() {
      LOG(INFO) << "join thread";
      shutdown = true;
      svc_thread->join();
      svc_thread.reset();
    }

   private:
    void run_svc() {
      LOG(INFO) << "run svc";
      svc_->run();
      LOG(INFO) << "run svc done";
    }
    void check_exit_cb() {
      if (shutdown) {
        LOG(INFO) << "exit";
        svc_->stop();
      } else {
        check_timer.expires_from_now(boost::posix_time::milliseconds(100));
        check_timer.async_wait(std::bind(&ServiceThread::check_exit_cb, this));
      }
    }

    std::shared_ptr<boost::asio::io_service> svc_;
    boost::asio::deadline_timer check_timer;
    std::shared_ptr<std::thread> svc_thread;
    std::atomic_bool shutdown;
  };
}

TEST(UDPSenderTests, udp_resolve_fails_data_dropped) {
  TestUDPReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    StubUDPSender::ptr_t sender = StubUDPSender::error(
        thread.svc(), listen_port, boost::asio::error::netdb_errors::host_not_found);
    sender->start();

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    sender->send(HELLO.data(), HELLO.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    sender->send(HEY.data(), HEY.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    sender->send(HI.data(), HI.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());
  }
  thread.join();

  EXPECT_FALSE(test_reader.available());
}

TEST(UDPSenderTests, udp_resolve_empty_data_dropped) {
  TestUDPReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    StubUDPSender::ptr_t sender = StubUDPSender::custom_success(
        thread.svc(), listen_port, std::vector<boost::asio::ip::udp::endpoint>());
    sender->start();

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    sender->send(HELLO.data(), HELLO.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    sender->send(HEY.data(), HEY.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    sender->send(HI.data(), HI.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());
  }
  thread.join();

  EXPECT_FALSE(test_reader.available());
}

TEST(UDPSenderTests, udp_resolve_reshuffle_data_sent_single_destination) {
  TestUDPReadSocket test_reader4, test_reader6;
  size_t listen_port = test_reader4.listen(DEST_LOCAL_ENDPOINT.address(), 0);
  size_t listen_port6 = test_reader6.listen(DEST_LOCAL_ENDPOINT6.address(), listen_port);
  EXPECT_EQ(listen_port, listen_port6);

  ServiceThread thread;
  {
    // have the resolver return a weighted mix of ipv4 + ipv6 endpoints for localhost
    std::vector<boost::asio::ip::udp::endpoint> endpoints;
    endpoints.push_back(DEST_LOCAL_ENDPOINT);
    endpoints.push_back(DEST_LOCAL_ENDPOINT);
    endpoints.push_back(DEST_LOCAL_ENDPOINT6);
    endpoints.push_back(DEST_LOCAL_ENDPOINT);
    endpoints.push_back(DEST_LOCAL_ENDPOINT6);
    StubUDPSender::ptr_t sender = StubUDPSender::custom_success(
        thread.svc(), listen_port, endpoints);
    sender->start();

    // flush a bunch to ensure resolve code is exercised between writes:
    metrics::sync_util::dispatch_run("flush1", *thread.svc(), &flush_service_queue_with_noop);
    sender->send(HELLO.data(), HELLO.size());
    metrics::sync_util::dispatch_run("flush2", *thread.svc(), &flush_service_queue_with_noop);
    sender->send(HEY.data(), HEY.size());
    metrics::sync_util::dispatch_run("flush3", *thread.svc(), &flush_service_queue_with_noop);
    sender->send(HI.data(), HI.size());
    metrics::sync_util::dispatch_run("flush4", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  std::set<std::string> recv4, recv6;
  while (test_reader4.available()) {
    recv4.insert(test_reader4.read());
  }
  while (test_reader6.available()) {
    recv6.insert(test_reader6.read());
  }
  // all sent data should have only been sent to one of the two endpoints
  EXPECT_TRUE((recv4.size() == 3) ^ (recv6.size() == 3));
  if (!recv4.empty()) {
    EXPECT_EQ(1, recv4.count("hello"));
    EXPECT_EQ(1, recv4.count("hey"));
    EXPECT_EQ(1, recv4.count("hi"));
  }
  if (!recv6.empty()) {
    EXPECT_EQ(1, recv6.count("hello"));
    EXPECT_EQ(1, recv6.count("hey"));
    EXPECT_EQ(1, recv6.count("hi"));
  }
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
