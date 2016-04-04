#include <initializer_list>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "mock_port_writer.hpp"
#include "port_reader_impl.cpp"
#include "sync_util.hpp"
#include "test_socket.hpp"

using ::testing::_;
using ::testing::Invoke;

namespace {
  class ServiceThread {
   public:
    ServiceThread(size_t expected_pkts)
      : expected_pkts(expected_pkts),
        svc_(new boost::asio::io_service),
        check_timer(*svc_) {
      LOG(INFO) << "start thread";
      check_timer.expires_from_now(boost::posix_time::milliseconds(100));
      check_timer.async_wait(std::bind(&ServiceThread::check_pkts_cb, this));
      svc_thread.reset(new std::thread(std::bind(&ServiceThread::run_svc, this)));
    }
    virtual ~ServiceThread() {
      EXPECT_FALSE((bool)svc_thread) << "ServiceThread.join() must be called before destructor";
    }

    std::shared_ptr<boost::asio::io_service> svc() {
      return svc_;
    }

    std::shared_ptr<MockPortWriter> mock() {
      std::shared_ptr<MockPortWriter> mock(new MockPortWriter());
      EXPECT_CALL(*mock, write(_,_)).WillRepeatedly(Invoke(
              std::bind(&ServiceThread::dispatch_add_pkt_cb, this,
                  std::placeholders::_1, std::placeholders::_2)));
      return mock;
    }

    void join() {
      LOG(INFO) << "join thread";
      svc_thread->join();
      svc_thread.reset();
    }

    void expect_contains(std::initializer_list<std::string> vals) const {
      std::ostringstream oss;
      oss << "[ ";
      for (std::vector<std::string>::const_iterator i = pkts_recvd.begin(); ; ) {
        oss << *i;
        if (++i == pkts_recvd.end()) {
          oss << " ]";
          break;
        } else {
          oss << " , ";
        }
      }

      EXPECT_EQ(expected_pkts, pkts_recvd.size()) << "got " << oss.str();
      for (const std::string& val : vals) {
        EXPECT_EQ(1, std::count(pkts_recvd.begin(), pkts_recvd.end(), val))
          << "missing " << val << " in " << oss.str();
      }
    }

   private:
    void run_svc() {
      LOG(INFO) << "run svc";
      svc_->run();
      LOG(INFO) << "run svc done";
    }
    void check_pkts_cb() {
      if (pkts_recvd.size() < expected_pkts) {
        LOG(INFO) << "recheck (have " << pkts_recvd.size() << ", want " << expected_pkts << ")";
        check_timer.expires_from_now(boost::posix_time::milliseconds(1));
        check_timer.async_wait(std::bind(&ServiceThread::check_pkts_cb, this));
      } else {
        LOG(INFO) << "no recheck";
        svc_->stop();
      }
    }
    void dispatch_add_pkt_cb(const char* bytes, size_t size) {
      // Copy data then dispatch
      LOG(INFO) << "dispatch add pkt";
      std::string str(bytes, size);
      svc_->dispatch(std::bind(&ServiceThread::add_pkt_cb, this, str));
    }
    void add_pkt_cb(std::string pkt) {
      LOG(INFO) << "add pkt: " << pkt;
      pkts_recvd.push_back(pkt);
    }

    const size_t expected_pkts;
    std::vector<std::string> pkts_recvd;
    std::shared_ptr<boost::asio::io_service> svc_;
    boost::asio::deadline_timer check_timer;
    std::shared_ptr<std::thread> svc_thread;
  };

  void flush_service_queue_with_noop() {
    LOG(INFO) << "async queue flushed";
  }
}

TEST(PortReaderImplTests, annotations_off) {
  std::string hello("hello"), hey("hey"), hi("hi");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::NONE);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(PortReaderImplTests, annotations_off_multiline) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string multi(hello + "\n" + hey + "\n\n" + hi);

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::NONE);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(PortReaderImplTests, annotations_off_multiline_leading_ending_newlines) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string multi("\n" + hello + "\n" + hey + "\n\n" + hi + "\n");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::NONE);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(PortReaderImplTests, datadog_annotations_zero_registered) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#missing_container");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::TAG_DATADOG);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello + tag, hey + tag, hi + tag});
}

TEST(PortReaderImplTests, datadog_annotations_multi_registered) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#unknown_container");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::TAG_DATADOG);

    mesos::ContainerID container_id;
    container_id.set_value("a");
    reader.register_container(container_id, mesos::ExecutorInfo());
    container_id.set_value("b");
    reader.register_container(container_id, mesos::ExecutorInfo());

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello + tag, hey + tag, hi + tag});
}

TEST(PortReaderImplTests, datadog_annotations) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#container_id:c,executor_id:e,framework_id:f");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::TAG_DATADOG);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello + tag, hey + tag, hi + tag});
}

TEST(PortReaderImplTests, datadog_annotations_multiline) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string multi(hello + "\n" + hey + "\n" + hi);
  std::string tag("|#container_id:c,executor_id:e,framework_id:f");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::TAG_DATADOG);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello + tag, hey + tag, hi + tag});
}

TEST(PortReaderImplTests, datadog_annotations_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#|@0.3");
  std::string tag("container_id:c,executor_id:e,framework_id:f");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::TAG_DATADOG);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({"hello|@0.1|#tag1," + tag, hey + "," + tag, "hi|@0.3|#" + tag});
}

TEST(PortReaderImplTests, datadog_annotations_multiline_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#tag3|@0.3");
  std::string multi(hello + "\n\n" + hey + "\n\n" + hi);
  std::string tag("container_id:c,executor_id:e,framework_id:f");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::TAG_DATADOG);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({"hello|@0.1|#tag1," + tag, hey + "," + tag, "hi|@0.3|#tag3," + tag});
}

TEST(PortReaderImplTests, prefix_annotations_zero_registered) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string prefix("missing_container.");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::KEY_PREFIX);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({prefix + hello, prefix + hey, prefix + hi});
}

TEST(PortReaderImplTests, prefix_annotations_multi_registered) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string prefix("unknown_container.");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::KEY_PREFIX);

    mesos::ContainerID container_id;
    container_id.set_value("a");
    reader.register_container(container_id, mesos::ExecutorInfo());
    container_id.set_value("b");
    reader.register_container(container_id, mesos::ExecutorInfo());

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({prefix + hello, prefix + hey, prefix + hi});
}

TEST(PortReaderImplTests, prefix_annotations) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string prefix("f.e.c.");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::KEY_PREFIX);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({prefix + hello, prefix + hey, prefix + hi});
}

TEST(PortReaderImplTests, prefix_annotations_multiline) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string multi(hello + "\n" + hey + "\n" + hi);
  std::string prefix("f.e.c.");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::KEY_PREFIX);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({prefix + hello, prefix + hey, prefix + hi});
}

TEST(PortReaderImplTests, prefix_annotations_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#|@0.3");
  std::string prefix("f.e.c.");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::KEY_PREFIX);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({prefix + hello, prefix + hey, prefix + hi});
}

TEST(PortReaderImplTests, prefix_annotations_multiline_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#tag3|@0.3");
  std::string multi(hello + "\n\n" + hey + "\n\n" + hi);
  std::string prefix("f.e.c.");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), stats::params::annotation_mode::Value::KEY_PREFIX);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({prefix + hello, prefix + hey, prefix + hi});
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  //FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
