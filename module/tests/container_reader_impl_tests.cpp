#include <initializer_list>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "container_reader_impl.hpp"
#include "mock_output_writer.hpp"
#include "sync_util.hpp"
#include "test_udp_socket.hpp"

using ::testing::_;
using ::testing::Invoke;

namespace {
  const std::string
    received_some_statsd_msg = "dcos.metrics.container_received_bytes_per_sec:20|g",
    throttled_some_statsd_msg = "dcos.metrics.container_throttled_bytes_per_sec:20|g",
    received_none_statsd_msg = "dcos.metrics.container_received_bytes_per_sec:0|g",
    throttled_none_statsd_msg = "dcos.metrics.container_throttled_bytes_per_sec:0|g";

  class Record {
   public:
    Record(const std::string& str,
        const mesos::ContainerID* container_id_, const mesos::ExecutorInfo* executor_info_)
      : str(str),
        container_id(container_id_ == NULL ? NULL : new mesos::ContainerID(*container_id_)),
        executor_info(executor_info_ == NULL ? NULL : new mesos::ExecutorInfo(*executor_info_)) { }

    bool operator==(const Record& r) const {
      if (container_id && executor_info) {
        return str == r.str && container_id->value() == r.container_id->value();
      } else {
        return str == r.str && !r.container_id && !r.executor_info;
      }
    }

    std::string string() const {
      std::ostringstream oss;
      if (container_id && executor_info) {
        oss << str
            << " " << container_id->ShortDebugString()
            << " " << executor_info->ShortDebugString();
      } else {
        oss << str << " (no container)";
      }
      return oss.str();
    }

    const std::string str;
    const std::shared_ptr<mesos::ContainerID> container_id;
    const std::shared_ptr<mesos::ExecutorInfo> executor_info;
  };

  class ServiceThread {
   public:
    ServiceThread()
      : svc_(new boost::asio::io_service),
        check_timer(*svc_),
        shutdown(false) {
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

    std::vector<metrics::output_writer_ptr_t> mocks() {
      std::vector<metrics::output_writer_ptr_t> mocks;
      std::shared_ptr<MockOutputWriter> mock(new MockOutputWriter);
      EXPECT_CALL(*mock, write_container_statsd(_,_,_,_)).WillRepeatedly(Invoke(
              std::bind(&ServiceThread::dispatch_add_pkt_cb, this,
                  std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3, std::placeholders::_4)));
      mocks.push_back(mock);
      return mocks;
    }

    void join() {
      LOG(INFO) << "join thread";
      shutdown = true;
      svc_thread->join();
      svc_thread.reset();
    }

    void expect_contains(std::initializer_list<Record> vals) const {
      std::ostringstream oss;
      if (pkts_recvd.empty()) {
        oss << "[EMPTY]";
      } else {
        oss << "[ ";
        for (std::vector<Record>::const_iterator i = pkts_recvd.begin(); ; ) {
          oss << i->string();
          if (++i == pkts_recvd.end()) {
            oss << " ]";
            break;
          } else {
            oss << " , ";
          }
        }
      }

      EXPECT_EQ(vals.size(), pkts_recvd.size()) << "got " << oss.str();
      for (const Record& val : vals) {
        EXPECT_EQ(1, std::count(pkts_recvd.begin(), pkts_recvd.end(), val))
          << "missing " << val.string() << " in " << oss.str();
      }
    }

   private:
    void run_svc() {
      LOG(INFO) << "run svc";
      svc_->run();
      LOG(INFO) << "run svc done";
    }
    void check_pkts_cb() {
      if (shutdown) {
        LOG(INFO) << "exit";
        svc_->stop();
      } else {
        check_timer.expires_from_now(boost::posix_time::milliseconds(100));
        check_timer.async_wait(std::bind(&ServiceThread::check_pkts_cb, this));
      }
    }
    void dispatch_add_pkt_cb(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* bytes, size_t size) {
      // Copy data then dispatch
      LOG(INFO) << "dispatch add pkt";
      svc_->dispatch(std::bind(&ServiceThread::add_pkt_cb, this,
              Record(std::string(bytes, size), container_id, executor_info)));
    }
    void add_pkt_cb(Record pkt) {
      LOG(INFO) << "add pkt: " << pkt.string();
      pkts_recvd.push_back(pkt);
    }

    std::vector<Record> pkts_recvd;
    std::shared_ptr<boost::asio::io_service> svc_;
    boost::asio::deadline_timer check_timer;
    std::shared_ptr<std::thread> svc_thread;
    std::atomic_bool shutdown;
  };

  void flush_service_queue_with_noop() {
    LOG(INFO) << "async queue flushed";
  }
}

TEST(ContainerReaderImplTests, one_line) {
  Record hello("hello", NULL, NULL), hey("hey", NULL, NULL), hi("hi", NULL, NULL);

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 1000, 1024);

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello.str);
    test_writer.write(hey.str);
    test_writer.write(hi.str);

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(ContainerReaderImplTests, one_line_throttled) {
  Record hello("hello", NULL, NULL), hey("hey", NULL, NULL), hi("hi", NULL, NULL);

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 500, 0);

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello.str);
    test_writer.write(hey.str);
    test_writer.write(hi.str);

    sleep(1); // sleep long enough for two flushes to occur

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({
        Record(received_some_statsd_msg, NULL, NULL),
        Record(throttled_some_statsd_msg, NULL, NULL),
        Record(received_none_statsd_msg, NULL, NULL),
        Record(throttled_none_statsd_msg, NULL, NULL)});
}

TEST(ContainerReaderImplTests, multiline) {
  Record hello("hello", NULL, NULL), hey("hey", NULL, NULL), hi("hi", NULL, NULL);
  std::string multi(hello.str + "\n" + hey.str + "\n\n" + hi.str);

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 500, 1024);

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(ContainerReaderImplTests, multiline_leading_ending_newlines) {
  Record hello("hello", NULL, NULL), hey("hey", NULL, NULL), hi("hi", NULL, NULL);
  std::string multi("\n" + hello.str + "\n" + hey.str + "\n\n" + hi.str + "\n");

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 500, 1024);

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(ContainerReaderImplTests, zero_registered_containers) {
  Record hello("hello", NULL, NULL), hey("hey", NULL, NULL), hi("hi", NULL, NULL);

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 500, 1024);

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello.str);
    test_writer.write(hey.str);
    test_writer.write(hi.str);

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(ContainerReaderImplTests, one_registered_container) {
  mesos::ContainerID container_id;
  container_id.set_value("a");
  mesos::ExecutorInfo exec_info;
  Record hello("hello", &container_id, &exec_info),
    hey("hey", &container_id, &exec_info),
    hi("hi", &container_id, &exec_info);

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 500, 1024);

    reader.register_container(container_id, exec_info);

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello.str);
    test_writer.write(hey.str);
    test_writer.write(hi.str);

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(ContainerReaderImplTests, one_registered_container_throttled) {
  mesos::ContainerID container_id;
  container_id.set_value("a");
  mesos::ExecutorInfo exec_info;
  Record hello("hello", &container_id, &exec_info),
    hey("hey", &container_id, &exec_info),
    hi("hi", &container_id, &exec_info);

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 500, 0);

    reader.register_container(container_id, exec_info);

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello.str);
    test_writer.write(hey.str);
    test_writer.write(hi.str);

    sleep(1); // sleep long enough for two flushes to occur

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({
        Record(received_some_statsd_msg, &container_id, &exec_info),
        Record(throttled_some_statsd_msg, &container_id, &exec_info),
        Record(received_none_statsd_msg, &container_id, &exec_info),
        Record(throttled_none_statsd_msg, &container_id, &exec_info)});
}

TEST(ContainerReaderImplTests, one_registered_container_multiline_leading_ending_newlines) {
  mesos::ContainerID container_id;
  container_id.set_value("a");
  mesos::ExecutorInfo exec_info;
  Record hello("hello", &container_id, &exec_info),
    hey("hey", &container_id, &exec_info),
    hi("hi", &container_id, &exec_info);
  std::string multi("\n" + hello.str + "\n" + hey.str + "\n\n" + hi.str + "\n");

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 500, 1024);

    reader.register_container(container_id, exec_info);

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(multi);

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

TEST(ContainerReaderImplTests, multi_registered_containers) {
  Record hello("hello", NULL, NULL), hey("hey", NULL, NULL), hi("hi", NULL, NULL);

  ServiceThread thread;
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0), 500, 1024);

    mesos::ContainerID container_id;
    container_id.set_value("a");
    reader.register_container(container_id, mesos::ExecutorInfo());
    container_id.set_value("b");
    reader.register_container(container_id, mesos::ExecutorInfo());

    Try<metrics::UDPEndpoint> result = reader.open();
    EXPECT_FALSE(result.isError()) << result.error();
    size_t reader_port = result.get().port;

    TestUDPWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello.str);
    test_writer.write(hey.str);
    test_writer.write(hi.str);

    metrics::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  thread.expect_contains({hello, hey, hi});
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
