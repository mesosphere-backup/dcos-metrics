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
      svc_thread->join();
      svc_thread.reset();
    }

    void expect_contains(std::initializer_list<Record> vals) const {
      std::ostringstream oss;
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

      EXPECT_EQ(expected_pkts, pkts_recvd.size()) << "got " << oss.str();
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
      if (pkts_recvd.size() < expected_pkts) {
        LOG(INFO) << "recheck (have " << pkts_recvd.size() << ", want " << expected_pkts << ")";
        check_timer.expires_from_now(boost::posix_time::milliseconds(1));
        check_timer.async_wait(std::bind(&ServiceThread::check_pkts_cb, this));
      } else {
        LOG(INFO) << "no recheck";
        svc_->stop();
      }
    }
    void dispatch_add_pkt_cb(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* bytes, size_t size) {
      // Copy data then dispatch
      LOG(INFO) << "dispatch add pkt";
      std::string str(bytes, size);
      Record pkt(str, container_id, executor_info);
      svc_->dispatch(std::bind(&ServiceThread::add_pkt_cb, this, pkt));
    }
    void add_pkt_cb(Record pkt) {
      LOG(INFO) << "add pkt: " << pkt.string();
      pkts_recvd.push_back(pkt);
    }

    const size_t expected_pkts;
    std::vector<Record> pkts_recvd;
    std::shared_ptr<boost::asio::io_service> svc_;
    boost::asio::deadline_timer check_timer;
    std::shared_ptr<std::thread> svc_thread;
  };

  void flush_service_queue_with_noop() {
    LOG(INFO) << "async queue flushed";
  }
}

TEST(ContainerReaderImplTests, one_line) {
  Record hello("hello", NULL, NULL), hey("hey", NULL, NULL), hi("hi", NULL, NULL);

  ServiceThread thread(3);
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0));

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

TEST(ContainerReaderImplTests, multiline) {
  Record hello("hello", NULL, NULL), hey("hey", NULL, NULL), hi("hi", NULL, NULL);
  std::string multi(hello.str + "\n" + hey.str + "\n\n" + hi.str);

  ServiceThread thread(3);
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0));

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

  ServiceThread thread(3);
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0));

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

  ServiceThread thread(3);
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0));

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

  ServiceThread thread(3);
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0));

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

TEST(ContainerReaderImplTests, one_registered_container_multiline_leading_ending_newlines) {
  mesos::ContainerID container_id;
  container_id.set_value("a");
  mesos::ExecutorInfo exec_info;
  Record hello("hello", &container_id, &exec_info),
    hey("hey", &container_id, &exec_info),
    hi("hi", &container_id, &exec_info);
  std::string multi("\n" + hello.str + "\n" + hey.str + "\n\n" + hi.str + "\n");

  ServiceThread thread(3);
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0));

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

  ServiceThread thread(3);
  {
    metrics::ContainerReaderImpl reader(
        thread.svc(), thread.mocks(), metrics::UDPEndpoint("127.0.0.1", 0));

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
  //FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
