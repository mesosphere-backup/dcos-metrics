#include <atomic>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "stub_lookup_statsd_output_writer.hpp"
#include "sync_util.hpp"
#include "test_socket.hpp"

namespace {
  std::vector<char> HELLO = {'h', 'e', 'l', 'l', 'o'},
    HEY = {'h', 'e', 'y'},
    HI = {'h', 'i'};

  inline mesos::ContainerID container_id(const std::string& id) {
    mesos::ContainerID cid;
    cid.set_value(id);
    return cid;
  }
  inline mesos::ExecutorInfo exec_info(const std::string& fid, const std::string& eid) {
    mesos::ExecutorInfo ei;
    ei.mutable_framework_id()->set_value(fid);
    ei.mutable_executor_id()->set_value(eid);
    return ei;
  }

  const mesos::ContainerID CONTAINER_ID1 = container_id("c1"), CONTAINER_ID2 = container_id("c2");
  const mesos::ExecutorInfo EXECUTOR_INFO1 = exec_info("f1", "e1"),
    EXECUTOR_INFO2 = exec_info("ftwo", "etwo");

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

TEST(StatsdOutputWriterTests, resolve_fails_data_dropped) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::with_lookup_error(
        stats::params::ANNOTATION_MODE_NONE,
        thread.svc(), listen_port, boost::asio::error::netdb_errors::host_not_found);
    writer->start();

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(NULL, NULL, HELLO.data(), HELLO.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer->write_container_statsd(NULL, NULL, HEY.data(), HEY.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer->write_container_statsd(NULL, NULL, HI.data(), HI.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());
  }
  thread.join();

  EXPECT_FALSE(test_reader.available());
}

TEST(StatsdOutputWriterTests, resolve_empty_data_dropped) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::with_lookup_result(
        stats::params::ANNOTATION_MODE_NONE,
        thread.svc(), listen_port, std::vector<boost::asio::ip::udp::endpoint>());
    writer->start();

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(NULL, NULL, HELLO.data(), HELLO.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer->write_container_statsd(NULL, NULL, HEY.data(), HEY.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());

    writer->write_container_statsd(NULL, NULL, HI.data(), HI.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_FALSE(test_reader.available());
  }
  thread.join();

  EXPECT_FALSE(test_reader.available());
}

TEST(StatsdOutputWriterTests, resolve_reshuffle_data_sent_single_destination) {
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
    writer_ptr_t writer = StubLookupStatsdOutputWriter::with_lookup_result(
        stats::params::ANNOTATION_MODE_NONE,
        thread.svc(), listen_port, endpoints);
    writer->start();

    // flush a bunch to ensure resolve code is exercised between writes:
    stats::sync_util::dispatch_run("flush1", *thread.svc(), &flush_service_queue_with_noop);
    writer->write_container_statsd(NULL, NULL, HELLO.data(), HELLO.size());
    stats::sync_util::dispatch_run("flush2", *thread.svc(), &flush_service_queue_with_noop);
    writer->write_container_statsd(NULL, NULL, HEY.data(), HEY.size());
    stats::sync_util::dispatch_run("flush3", *thread.svc(), &flush_service_queue_with_noop);
    writer->write_container_statsd(NULL, NULL, HI.data(), HI.size());
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

TEST(StatsdOutputWriterTests, chunking_off) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::without_chunking(
        stats::params::ANNOTATION_MODE_KEY_PREFIX, thread.svc(), listen_port);
    writer->start();

    // value is dropped because we didn't give writer a chance to resolve the host:
    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    EXPECT_FALSE(test_reader.available());

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HELLO.data(), HELLO.size());
    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HEY.data(), HEY.size());

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ("f2.e2.c2.hello", test_reader.read());
  EXPECT_EQ("f1.e1.c1.hey", test_reader.read());
}

TEST(StatsdOutputWriterTests, chunking_on_flush_when_full) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::with_chunk_size(
        stats::params::ANNOTATION_MODE_TAG_DATADOG,
        thread.svc(), listen_port, 10 /* chunk_size */, 9999999 /* chunk_timeout_ms */);
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());// 5 bytes
    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());// 9 bytes (5 + 1 + 3)
    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HI.data(), HI.size());// 12 bytes (5 + 1 + 3 + 1 + 2), chunk is flushed before adding "hi"
    EXPECT_EQ("hello\nhey", test_reader.read(1 /* timeout_ms */));

    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ("hi|#tags", test_reader.read(1 /* timeout_ms */));
}

TEST(StatsdOutputWriterTests, chunking_on_flush_timer) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::with_chunk_size(
        stats::params::ANNOTATION_MODE_KEY_PREFIX,
        thread.svc(), listen_port, 10 /* chunk_size */, 1 /* chunk_timeout_ms */);
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    EXPECT_FALSE(test_reader.available());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("f1.e1.c1.hello", test_reader.read(100 /* timeout_ms */));

    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HI.data(), HI.size());
    EXPECT_FALSE(test_reader.available());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("f2.e2.c2.hey\nf1.e1.c1.hi", test_reader.read(100 /* timeout_ms */));

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, annotations_off) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::without_chunking(
        stats::params::ANNOTATION_MODE_NONE, thread.svc(), listen_port);
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    writer->write_container_statsd(NULL, NULL, HEY.data(), HI.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello\nhey\nhi", test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, datadog_annotations_no_containerinfo) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::without_chunking(
        stats::params::ANNOTATION_MODE_TAG_DATADOG, thread.svc(), listen_port);
    writer->start();

    writer->write_container_statsd(NULL, NULL, HELLO.data(), HELLO.size());
    writer->write_container_statsd(NULL, NULL, HEY.data(), HEY.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello|#unknown_container\nhey|#unknown_container", test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, datadog_annotations) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::without_chunking(
        stats::params::ANNOTATION_MODE_TAG_DATADOG, thread.svc(), listen_port);
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello|#unknown_container\nhey|#unknown_container", test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, datadog_annotations_tagged_input) {
  std::vector<char>
    hello = {'h', 'e', 'l', 'l', 'o', '|', '#', 't', 'a', 'g', '1', '|', '@', '0', '.', '1'},
    hey = {'h', 'e', 'y', '|', '@', '0', '.', '2', '|', '#', 't', 'a', 'g', '2'},
    hi = {'h', 'i', '|', '#', '|', '@', '0', '.', '3'};
  std::string tag("container_id:c,executor_id:e,framework_id:f");

  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::without_chunking(
        stats::params::ANNOTATION_MODE_TAG_DATADOG, thread.svc(), listen_port);
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, hello.data(), hello.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hey.data(), hey.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hi.data(), hi.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello|@0.1|#tag1,unknown_container\nhey|@0.2|#tag2,unknown_container\nhi|@0.3|#unknown_container",
        test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, prefix_annotations_no_containerinfo) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::without_chunking(
        stats::params::ANNOTATION_MODE_KEY_PREFIX, thread.svc(), listen_port);
    writer->start();

    writer->write_container_statsd(NULL, NULL, HELLO.data(), HELLO.size());
    writer->write_container_statsd(NULL, NULL, HEY.data(), HEY.size());
    writer->write_container_statsd(NULL, NULL, HI.data(), HI.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("unknown_container.hello\nunknown_container.hey\nunknown_container.hi",
        test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, prefix_annotations) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string prefix("f.e.c.");

  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::without_chunking(
        stats::params::ANNOTATION_MODE_KEY_PREFIX, thread.svc(), listen_port);
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HI.data(), HI.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("f.e.c.hello\nf.e.c.hey\nf.e.c.hi",
        test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, prefix_annotations_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#|@0.3");
  std::string prefix("f.e.c.");

  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    writer_ptr_t writer = StubLookupStatsdOutputWriter::without_chunking(
        stats::params::ANNOTATION_MODE_KEY_PREFIX, thread.svc(), listen_port);
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HI.data(), HI.size());
    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("f.e.c.hello|#tag1|@0.1\nf.e.c.hey|@0.2|#tag2\nf.e.c.hi|#|@0.3",
        test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
