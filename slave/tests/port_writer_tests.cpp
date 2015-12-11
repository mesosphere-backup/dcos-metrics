#include <atomic>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "port_writer.hpp"
#include "sync_util.hpp"
#include "test_socket.hpp"

namespace {
  mesos::Parameters build_params(size_t port, size_t chunk_size, std::string host = "127.0.0.1") {
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
    param->set_value(host);
    param = params.add_parameter();
    param->set_key(stats::params::DEST_PORT);
    param->set_value(std::to_string(port));
    return params;
  }

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

  void flush_service_queue_with_noop() {
    LOG(INFO) << "async queue flushed";
  }
}

TEST(PortWriterTests, resolve_fails_data_dropped) {
  const std::string hello("hello"), hey("hey"), hi("hi");
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    stats::PortWriter writer(thread.svc(),
        build_params(listen_port, 0 /* chunk_size */, "bad_host_test"),
        stats::PortWriter::DEFAULT_CHUNK_TIMEOUT_MS,
        1);
    writer.start();

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer.write(hello.data(), hello.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer.write(hey.data(), hey.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer.write(hi.data(), hi.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());
  }
  thread.join();

  EXPECT_FALSE(test_reader.available());
}

TEST(PortWriterTests, resolve_success_data_kept) {
  const std::string hello("hello"), hey("hey"), hi("hi");
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    stats::PortWriter writer(thread.svc(),
        build_params(listen_port, 0 /* chunk_size */),
        stats::PortWriter::DEFAULT_CHUNK_TIMEOUT_MS,
        1);
    writer.start();

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer.write(hello.data(), hello.size());
    writer.write(hey.data(), hey.size());
    writer.write(hi.data(), hi.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ("hello", test_reader.read());
  EXPECT_EQ("hey", test_reader.read());
  EXPECT_EQ("hi", test_reader.read());

  EXPECT_FALSE(test_reader.available());
}

TEST(PortWriterTests, chunking_off) {
  const std::string hello("hello"), hey("hey");
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    stats::PortWriter writer(thread.svc(), build_params(listen_port, 0 /* chunk_size */));
    writer.start();

    // value is dropped because we didn't give writer a chance to resolve the host:
    writer.write(hello.data(), hello.size());
    EXPECT_FALSE(test_reader.available());

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer.write(hello.data(), hello.size());
    writer.write(hey.data(), hey.size());

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
    stats::PortWriter writer(
        thread.svc(),
        build_params(listen_port, 10 /* chunk_size */),
        9999999 /* chunk_timeout_ms */);
    writer.start();

    writer.write(hello.data(), hello.size());// 5 bytes
    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
    writer.write(hey.data(), hey.size());// 9 bytes (5 + 1 + 3)
    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
    writer.write(hi.data(), hi.size());// 12 bytes (5 + 1 + 3 + 1 + 2), chunk is flushed before adding "hi"
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
    stats::PortWriter writer(
        thread.svc(),
        build_params(listen_port, 10 /* chunk_size */),
        1 /* chunk_timeout_ms */);
    writer.start();

    writer.write(hello.data(), hello.size());
    EXPECT_FALSE(test_reader.available());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello", test_reader.read(100 /* timeout_ms */));

    writer.write(hey.data(), hey.size());
    writer.write(hi.data(), hi.size());
    EXPECT_FALSE(test_reader.available());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hey\nhi", test_reader.read(100 /* timeout_ms */));

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
