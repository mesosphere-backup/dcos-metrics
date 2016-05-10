#include <atomic>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "statsd_output_writer.hpp"
#include "stub_socket_sender.hpp"
#include "sync_util.hpp"
#include "test_socket.hpp"

namespace {
  const std::string HELLO("hello"), HEY("hey"), HI("hi");

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
    EXECUTOR_INFO2 = exec_info("f2", "e2");

  const boost::asio::ip::udp::endpoint DEST_LOCAL_ENDPOINT(
      boost::asio::ip::address::from_string("127.0.0.1"), 0 /* port */);
  const boost::asio::ip::udp::endpoint DEST_LOCAL_ENDPOINT6(
      boost::asio::ip::address::from_string("::1"), 0 /* port */);

  mesos::Parameters build_params(
      const std::string& annotation_mode, size_t chunk_size = 0) {
    mesos::Parameters params;
    mesos::Parameter* param;
    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE);
    param->set_value(annotation_mode);
    if (chunk_size > 0) {
      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_STATSD_CHUNKING);
      param->set_value("true");
      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_STATSD_CHUNK_SIZE_BYTES);
      param->set_value(std::to_string(chunk_size));
    } else {
      param = params.add_parameter();
      param->set_key(metrics::params::OUTPUT_STATSD_CHUNKING);
      param->set_value("false");
    }
    return params;
  }

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

TEST(StatsdOutputWriterTests, chunking_off) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port)));
    writer->start();

    // value is dropped because we didn't give writer a chance to resolve the host:
    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    EXPECT_FALSE(test_reader.available());

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HELLO.data(), HELLO.size());
    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HEY.data(), HEY.size());

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
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
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG, 150 /* chunk_size */),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port),
            9999999 /* chunk_timeout_ms */));
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());// 53 bytes
    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());// 51 bytes
    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HI.data(), HI.size());// 50 bytes (FLUSH)
    EXPECT_EQ("hello|#framework_id:f1,executor_id:e1,container_id:c1\n"
        "hey|#framework_id:f2,executor_id:e2,container_id:c2", test_reader.read(1 /* timeout_ms */));

    EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ("hi|#framework_id:f1,executor_id:e1,container_id:c1", test_reader.read(1 /* timeout_ms */));
}

TEST(StatsdOutputWriterTests, chunking_on_flush_timer) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX, 100 /* chunk_size */),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port),
            1 /* chunk_timeout_ms */));
    writer->start();

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    EXPECT_FALSE(test_reader.available());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("f1.e1.c1.hello", test_reader.read(100 /* timeout_ms */));

    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HI.data(), HI.size());
    EXPECT_FALSE(test_reader.available());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("f2.e2.c2.hey\nf1.e1.c1.hi", test_reader.read(100 /* timeout_ms */));

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, chunked_annotations_off) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_NONE, 100 /* chunk_size */),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port),
            1 /* chunk_timeout_ms */));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    writer->write_container_statsd(NULL, NULL, HI.data(), HI.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello\nhey\nhi", test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, chunked_datadog_annotations_no_containerinfo) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG, 100 /* chunk_size */),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port),
            1 /* chunk_timeout_ms */));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(NULL, NULL, HELLO.data(), HELLO.size());
    writer->write_container_statsd(NULL, NULL, HEY.data(), HEY.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello|#unknown_container\nhey|#unknown_container", test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, chunked_datadog_annotations) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG, 150 /* chunk_size */),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port),
            1 /* chunk_timeout_ms */));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello|#framework_id:f1,executor_id:e1,container_id:c1\n"
        "hey|#framework_id:f2,executor_id:e2,container_id:c2", test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, datadog_annotations_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#|@0.3");
  std::string tag("container_id:c,executor_id:e,framework_id:f");

  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port)));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, hello.data(), hello.size());
    EXPECT_EQ("hello|#tag1,framework_id:f1,executor_id:e1,container_id:c1|@0.1",
        test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hey.data(), hey.size());
    EXPECT_EQ("hey|@0.2|#tag2,framework_id:f2,executor_id:e2,container_id:c2",
        test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hi.data(), hi.size());
    EXPECT_EQ("hi|#framework_id:f2,executor_id:e2,container_id:c2|@0.3",
        test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, chunked_datadog_annotations_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#|@0.3");
  std::string tag("container_id:c,executor_id:e,framework_id:f");

  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG, 150 /* chunk_size */),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port),
            1 /* chunk_timeout_ms */));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, hello.data(), hello.size());
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hey.data(), hey.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hello|#tag1,framework_id:f1,executor_id:e1,container_id:c1|@0.1\n"
        "hey|@0.2|#tag2,framework_id:f2,executor_id:e2,container_id:c2",
        test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hi.data(), hi.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("hi|#framework_id:f2,executor_id:e2,container_id:c2|@0.3",
        test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, prefix_annotations_no_containerinfo) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port)));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(NULL, NULL, HELLO.data(), HELLO.size());
    EXPECT_EQ("unknown_container." + HELLO, test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(NULL, NULL, HEY.data(), HEY.size());
    EXPECT_EQ("unknown_container." + HEY, test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(NULL, NULL, HI.data(), HI.size());
    EXPECT_EQ("unknown_container." + HI, test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, prefix_annotations) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port)));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, HELLO.data(), HELLO.size());
    EXPECT_EQ("f1.e1.c1." + HELLO, test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HEY.data(), HEY.size());
    EXPECT_EQ("f2.e2.c2." + HEY, test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, HI.data(), HI.size());
    EXPECT_EQ("f2.e2.c2." + HI, test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, chunked_prefix_annotations_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#|@0.3");

  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX, 150 /* chunk_size */),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port),
            1 /* chunk_timeout_ms */));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, hello.data(), hello.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hey.data(), hey.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hi.data(), hi.size());
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
    EXPECT_EQ("f1.e1.c1." + hello + "\nf2.e2.c2." + hey + "\nf2.e2.c2." + hi,
        test_reader.read(100 /* timeout_ms */));
  }
  thread.join();
}

TEST(StatsdOutputWriterTests, prefix_annotations_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#|@0.3");

  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  ServiceThread thread;
  {
    metrics::output_writer_ptr_t writer(new metrics::StatsdOutputWriter(
            thread.svc(),
            build_params(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_KEY_PREFIX),
            StubSocketSender<boost::asio::ip::udp>::success(thread.svc(), listen_port)));
    writer->start();
    // let resolve finish before sending data:
    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);

    writer->write_container_statsd(&CONTAINER_ID1, &EXECUTOR_INFO1, hello.data(), hello.size());
    EXPECT_EQ("f1.e1.c1." + hello, test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hey.data(), hey.size());
    EXPECT_EQ("f2.e2.c2." + hey, test_reader.read(100 /* timeout_ms */));
    writer->write_container_statsd(&CONTAINER_ID2, &EXECUTOR_INFO2, hi.data(), hi.size());
    EXPECT_EQ("f2.e2.c2." + hi, test_reader.read(100 /* timeout_ms */));
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
