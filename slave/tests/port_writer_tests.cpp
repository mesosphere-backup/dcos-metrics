#include <atomic>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "port_writer.hpp"
#include "sync_util.hpp"
#include "test_socket.hpp"

namespace {
  const std::string DEST_HOSTNAME = "fakehost";
  const boost::asio::ip::udp::endpoint DEST_LOCAL_ENDPOINT(
      boost::asio::ip::address::from_string("127.0.0.1"), 0 /* port */);
  const boost::asio::ip::udp::endpoint DEST_LOCAL_ENDPOINT6(
      boost::asio::ip::address::from_string("::1"), 0 /* port */);

  mesos::Parameters build_params(size_t port, size_t chunk_size) {
    mesos::Parameters params;
    mesos::Parameter* param;
    if (chunk_size > 0) {
      param = params.add_parameter();
      param->set_key(stats::params::CHUNKING);
      param->set_value("true");
      param = params.add_parameter();
      param->set_key(stats::params::CHUNK_SIZE_BYTES);
      param->set_value(std::to_string(chunk_size));
    } else {
      param = params.add_parameter();
      param->set_key(stats::params::CHUNKING);
      param->set_value("false");
    }
    param = params.add_parameter();
    param->set_key(stats::params::DEST_HOST);
    param->set_value(DEST_HOSTNAME);
    param = params.add_parameter();
    param->set_key(stats::params::DEST_PORT);
    param->set_value(std::to_string(port));
    return params;
  }

  void flush_service_queue_with_noop() {
    LOG(INFO) << "async queue flushed";
  }

  typedef std::shared_ptr<stats::PortWriter> writer_ptr_t;

  class StubLookupPortWriter : public stats::PortWriter {
   public:
    static writer_ptr_t with_lookup_result(
        std::shared_ptr<boost::asio::io_service> io_service,
        size_t port,
        std::vector<boost::asio::ip::udp::endpoint> lookup_result) {
      return writer_ptr_t(new StubLookupPortWriter(io_service, port,
              lookup_result, boost::system::error_code()));
    }

    static writer_ptr_t with_lookup_error(
        std::shared_ptr<boost::asio::io_service> io_service,
        size_t port,
        boost::system::error_code lookup_error) {
      return writer_ptr_t(new StubLookupPortWriter(io_service, port,
              std::vector<boost::asio::ip::udp::endpoint>(), lookup_error));
    }

    static writer_ptr_t without_chunking(
        std::shared_ptr<boost::asio::io_service> io_service,
        size_t port) {
      std::vector<boost::asio::ip::udp::endpoint> endpoints;
      endpoints.push_back(DEST_LOCAL_ENDPOINT);
      return writer_ptr_t(new StubLookupPortWriter(io_service, port,
              endpoints, boost::system::error_code()));
    }

    static writer_ptr_t with_chunk_size(
        std::shared_ptr<boost::asio::io_service> io_service,
        size_t port,
        size_t chunk_size,
        size_t chunk_timeout_ms) {
      std::vector<boost::asio::ip::udp::endpoint> endpoints;
      endpoints.push_back(DEST_LOCAL_ENDPOINT);
      return writer_ptr_t(new StubLookupPortWriter(io_service, port,
              endpoints, boost::system::error_code(),
              chunk_size, chunk_timeout_ms));
    }

    virtual ~StubLookupPortWriter() {
      cancel_timers();
      stats::sync_util::dispatch_run(
          "~StubLookupPortWriter", *io_service, std::bind(&flush_service_queue_with_noop));
    }

   protected:
    udp_resolver_t::iterator resolve(boost::system::error_code& ec) {
      if (lookup_error) {
        ec = lookup_error;
        return udp_resolver_t::iterator();
      } else if (lookup_result.empty()) {
        return udp_resolver_t::iterator();
      } else {
        std::vector<boost::asio::ip::udp::endpoint> shuffled(lookup_result);
        std::random_shuffle(shuffled.begin(), shuffled.end());
#if BOOST_VERSION >= 105500
        // >=1.55.0 supports passing iterator directly:
        return udp_resolver_t::iterator::create(
            shuffled.begin(), shuffled.end(), DEST_HOSTNAME, "");
#else
        // 1.54.0 and older require getaddrinfo-formatted entries:
        std::vector<addrinfo> lookup_conv(shuffled.size());
        for (size_t i = 0; i < shuffled.size(); ++i) {
          addrinfo& out = lookup_conv[i];
          out.ai_flags = 0;
          out.ai_family = AF_INET;
          out.ai_socktype = SOCK_DGRAM;
          out.ai_protocol = 0;
          out.ai_addrlen = sizeof(sockaddr_in);
          out.ai_addr = shuffled[i].data();
          out.ai_canonname = NULL;
          if ((i + 1) < shuffled.size()) {
            out.ai_next = &lookup_conv[i+1];
          } else {
            out.ai_next = NULL;
          }
        }
        return udp_resolver_t::iterator::create(&lookup_conv[0], DEST_HOSTNAME, "");
#endif
      }
    }

   private:
    StubLookupPortWriter(std::shared_ptr<boost::asio::io_service> io_service,
        size_t port,
        std::vector<boost::asio::ip::udp::endpoint> lookup_result,
        boost::system::error_code lookup_error,
        size_t chunk_size = 0,
        size_t chunk_timeout_ms = stats::PortWriter::DEFAULT_CHUNK_TIMEOUT_MS)
      : PortWriter(
          io_service, build_params(port, chunk_size), chunk_timeout_ms, 1 /* resolve_period_ms */),
        io_service(io_service),
        lookup_result(lookup_result),
        lookup_error(lookup_error) { }

    const std::shared_ptr<boost::asio::io_service> io_service;
    const std::vector<boost::asio::ip::udp::endpoint> lookup_result;
    const boost::system::error_code lookup_error;
  };

  class ServiceThread {
   public:
    ServiceThread()
      : svc_(new boost::asio::io_service),
        check_timer(*svc_) {
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
        LOG(INFO) << "recheck";
        check_timer.expires_from_now(boost::posix_time::milliseconds(1));
        check_timer.async_wait(std::bind(&ServiceThread::check_exit_cb, this));
      }
    }

    std::shared_ptr<boost::asio::io_service> svc_;
    boost::asio::deadline_timer check_timer;
    std::shared_ptr<std::thread> svc_thread;
    std::atomic_bool shutdown;
  };
}

TEST(PortWriterTests, resolve_fails_data_dropped) {
  const std::string hello("hello"), hey("hey"), hi("hi");
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupPortWriter::with_lookup_error(
        thread.svc(), listen_port, boost::asio::error::netdb_errors::host_not_found);
    writer->start();

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write(hello.data(), hello.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer->write(hey.data(), hey.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer->write(hi.data(), hi.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());
  }
  thread.join();

  EXPECT_FALSE(test_reader.available());
}

TEST(PortWriterTests, resolve_empty_data_dropped) {
  const std::string hello("hello"), hey("hey"), hi("hi");
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupPortWriter::with_lookup_result(
        thread.svc(), listen_port, std::vector<boost::asio::ip::udp::endpoint>());
    writer->start();

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write(hello.data(), hello.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer->write(hey.data(), hey.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer->write(hi.data(), hi.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());
  }
  thread.join();

  EXPECT_FALSE(test_reader.available());
}

TEST(PortWriterTests, resolve_reshuffle_data_sent_single_destination) {
  const std::string hello("hello"), hey("hey"), hi("hi");
  TestReadSocket test_reader4, test_reader6;
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
    writer_ptr_t writer = StubLookupPortWriter::with_lookup_result(
        thread.svc(), listen_port, endpoints);
    writer->start();

    // flush a bunch to ensure resolve code is exercised between writes:
    stats::sync_util::dispatch_run("flush1", *thread.svc(), &flush_service_queue_with_noop);
    writer->write(hello.data(), hello.size());
    stats::sync_util::dispatch_run("flush2", *thread.svc(), &flush_service_queue_with_noop);
    writer->write(hey.data(), hey.size());
    stats::sync_util::dispatch_run("flush3", *thread.svc(), &flush_service_queue_with_noop);
    writer->write(hi.data(), hi.size());
    stats::sync_util::dispatch_run("flush4", *thread.svc(), &flush_service_queue_with_noop);
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
  EXPECT_TRUE(recv4.size() == 3 ^ recv6.size() == 3);
  if (!recv4.empty()) {
    EXPECT_EQ(1, recv4.count(hello));
    EXPECT_EQ(1, recv4.count(hey));
    EXPECT_EQ(1, recv4.count(hi));
  }
  if (!recv6.empty()) {
    EXPECT_EQ(1, recv6.count(hello));
    EXPECT_EQ(1, recv6.count(hey));
    EXPECT_EQ(1, recv6.count(hi));
  }
}

TEST(PortWriterTests, chunking_off) {
  const std::string hello("hello"), hey("hey");
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupPortWriter::without_chunking(thread.svc(), listen_port);
    writer->start();

    // value is dropped because we didn't give writer a chance to resolve the host:
    writer->write(hello.data(), hello.size());
    EXPECT_FALSE(test_reader.available());

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write(hello.data(), hello.size());
    writer->write(hey.data(), hey.size());

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ("hello", test_reader.read());
  EXPECT_EQ("hey", test_reader.read());
}

TEST(PortWriterTests, chunking_on_flush_when_full) {
  const std::string hello("hello"), hey("hey"), hi("hi");
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupPortWriter::with_chunk_size(
        thread.svc(), listen_port, 10 /* chunk_size */, 9999999 /* chunk_timeout_ms */);
    writer->start();

    writer->write(hello.data(), hello.size());// 5 bytes
    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
    writer->write(hey.data(), hey.size());// 9 bytes (5 + 1 + 3)
    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
    writer->write(hi.data(), hi.size());// 12 bytes (5 + 1 + 3 + 1 + 2), chunk is flushed before adding "hi"
    EXPECT_EQ("hello\nhey", test_reader.read(1 /* timeout_ms */));

    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ("hi", test_reader.read(1 /* timeout_ms */));
}

TEST(PortWriterTests, chunking_on_flush_timer) {
  const std::string hello("hello"), hey("hey"), hi("hi");
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupPortWriter::with_chunk_size(
        thread.svc(), listen_port, 10 /* chunk_size */, 1 /* chunk_timeout_ms */);
    writer->start();

    writer->write(hello.data(), hello.size());
    EXPECT_FALSE(test_reader.available());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello", test_reader.read(100 /* timeout_ms */));

    writer->write(hey.data(), hey.size());
    writer->write(hi.data(), hi.size());
    EXPECT_FALSE(test_reader.available());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hey\nhi", test_reader.read(100 /* timeout_ms */));

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  //FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
