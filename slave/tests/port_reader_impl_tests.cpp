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

    const std::vector<std::string>& pkts() const {
      return pkts_recvd;
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
        stats::UDPEndpoint("127.0.0.1", 0), false /* annotations_enabled */);

    Try<stats::UDPEndpoint> result = reader.open();
    ASSERT_FALSE(result.isError()) << result.error();
    size_t reader_port = result->port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ(3, thread.pkts().size());
  EXPECT_EQ(hello, thread.pkts()[0]);
  EXPECT_EQ(hey, thread.pkts()[1]);
  EXPECT_EQ(hi, thread.pkts()[2]);
}

TEST(PortReaderImplTests, annotations_on_zero_registered) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#missing_container");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), true /* annotations_enabled */);

    Try<stats::UDPEndpoint> result = reader.open();
    ASSERT_FALSE(result.isError()) << result.error();
    size_t reader_port = result->port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ(3, thread.pkts().size());
  EXPECT_EQ(hello + tag, thread.pkts()[0]);
  EXPECT_EQ(hey + tag, thread.pkts()[1]);
  EXPECT_EQ(hi + tag, thread.pkts()[2]);
}

TEST(PortReaderImplTests, annotations_on_one_registered) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#container_id:c,executor_id:e,framework_id:f");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), true /* annotations_enabled */);

    mesos::ContainerID container_id;
    container_id.set_value("c");
    mesos::ExecutorInfo executor_info;
    executor_info.mutable_executor_id()->set_value("e");
    executor_info.mutable_framework_id()->set_value("f");
    reader.register_container(container_id, executor_info);

    Try<stats::UDPEndpoint> result = reader.open();
    ASSERT_FALSE(result.isError()) << result.error();
    size_t reader_port = result->port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ(3, thread.pkts().size());
  EXPECT_EQ(hello + tag, thread.pkts()[0]);
  EXPECT_EQ(hey + tag, thread.pkts()[1]);
  EXPECT_EQ(hi + tag, thread.pkts()[2]);
}

TEST(PortReaderImplTests, annotations_on_multi_registered) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#unknown_container");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), true /* annotations_enabled */);

    mesos::ContainerID container_id;
    container_id.set_value("a");
    reader.register_container(container_id, mesos::ExecutorInfo());
    container_id.set_value("b");
    reader.register_container(container_id, mesos::ExecutorInfo());

    Try<stats::UDPEndpoint> result = reader.open();
    ASSERT_FALSE(result.isError()) << result.error();
    size_t reader_port = result->port;

    TestWriteSocket test_writer;
    test_writer.connect(reader_port);

    test_writer.write(hello);
    test_writer.write(hey);
    test_writer.write(hi);

    stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
  }
  thread.join();

  EXPECT_EQ(3, thread.pkts().size());
  EXPECT_EQ(hello + tag, thread.pkts()[0]);
  EXPECT_EQ(hey + tag, thread.pkts()[1]);
  EXPECT_EQ(hi + tag, thread.pkts()[2]);
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
