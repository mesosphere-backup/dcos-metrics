#include <initializer_list>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "mock_port_writer.hpp"
#include "port_reader_impl.cpp"
#include "sync_util.hpp"
#include "test_socket.hpp"

using ::testing::_;
using ::testing::Return;

namespace {
  class ServiceThread {
   public:
    ServiceThread() : svc_(new boost::asio::io_service) {
      LOG(INFO) << "start thread";
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
      EXPECT_CALL(*mock, write(_,_)).WillRepeatedly(Return());
      return mock;
    }

    void join() {
      LOG(INFO) << "signal exit";
      svc_->dispatch(std::bind(&ServiceThread::exit, this));
      LOG(INFO) << "join thread";
      svc_thread->join();
      svc_thread.reset();
    }

   private:
    void run_svc() {
      LOG(INFO) << "run svc";
      svc_->run();
      LOG(INFO) << "run svc done";
    }
    void exit() {
      svc_->stop();
    }

    std::shared_ptr<boost::asio::io_service> svc_;
    std::shared_ptr<std::thread> svc_thread;
  };

  void flush_service_queue_with_noop() {
    LOG(INFO) << "async queue flushed";
  }

  void fuzz(stats::params::annotation_mode::Value annotation_mode) {
    std::random_device dev;
    std::mt19937 engine{dev()};
    std::uniform_int_distribution<int> len_dist(1, 1024);
    std::uniform_int_distribution<int> char_dist(0, 255);

    size_t pkt_count = 100;
    ServiceThread thread;

    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), annotation_mode);

    {
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

      printf("SEND START\n");
      for (size_t pkt_num = 0; pkt_num < pkt_count; ++pkt_num) {
        std::string fuzzy;
        size_t pkt_len = len_dist(engine);
        size_t insert_val_idx = len_dist(engine);
        size_t insert_tag_section_idx = len_dist(engine);
        size_t insert_other_section_idx = len_dist(engine);
        for (size_t c_num = 0; c_num < pkt_len; ++c_num) {
          fuzzy.push_back((char) char_dist(engine));
          if (c_num == insert_val_idx) {
            fuzzy.push_back(':');
          }
          if (c_num == insert_tag_section_idx) {
            fuzzy.append("|#");
          }
          if (c_num == insert_other_section_idx) {
            fuzzy.append("|@");
          }
        }
        test_writer.write(fuzzy);
      }
      printf("SEND END\n");

      printf("FLUSH START\n");
      stats::sync_util::dispatch_run("flush", *thread.svc(), &flush_service_queue_with_noop);
      printf("FLUSH END\n");
    }

    printf("JOIN START\n");
    thread.join();
    printf("JOIN END\n");
  }
}

TEST(PortReaderImplTests, datadog_annotations_fuzz_data) {
  fuzz(stats::params::annotation_mode::Value::TAG_DATADOG);
}

TEST(PortReaderImplTests, prefix_annotations_fuzz_data) {
  fuzz(stats::params::annotation_mode::Value::KEY_PREFIX);
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  //FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
