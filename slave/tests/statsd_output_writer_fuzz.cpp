#include <initializer_list>
#include <thread>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "stub_lookup_statsd_output_writer.hpp"
#include "sync_util.hpp"
#include "test_socket.hpp"

//using ::testing::_;
//using ::testing::Return;

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

  void fuzz(const std::string& annotation_mode, bool chunking) {
    std::random_device dev;
    std::mt19937 engine{dev()};
    std::uniform_int_distribution<int> len_dist(1, 1024);
    std::uniform_int_distribution<int> char_dist(0, 255);

    TestReadSocket test_reader;
    size_t listen_port = test_reader.listen();

    size_t pkt_count = 100;
    ServiceThread thread;

    writer_ptr_t writer;
    if (chunking) {
      writer = StubLookupStatsdOutputWriter::with_lookup_result_chunking(
          annotation_mode, thread.svc(), listen_port, std::vector<boost::asio::ip::udp::endpoint>(), 10, 100);
    } else {
      writer = StubLookupStatsdOutputWriter::with_lookup_result(
          annotation_mode, thread.svc(), listen_port, std::vector<boost::asio::ip::udp::endpoint>());
    }
    writer->start();

    {
      mesos::ContainerID container_id;
      container_id.set_value("c");
      mesos::ExecutorInfo executor_info;
      executor_info.mutable_executor_id()->set_value("e");
      executor_info.mutable_framework_id()->set_value("f");

      printf("SEND START\n");
      for (size_t pkt_num = 0; pkt_num < pkt_count; ++pkt_num) {
        std::vector<char> fuzzy;
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
            fuzzy.push_back('|');
            fuzzy.push_back('#');
          }
          if (c_num == insert_other_section_idx) {
            fuzzy.push_back('|');
            fuzzy.push_back('@');
          }
        }
        writer->write_container_statsd(&container_id, &executor_info, fuzzy.data(), fuzzy.size());
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

TEST(StatsdOutputWriterFuzzTests, datadog_annotations_unchunked) {
  fuzz(stats::params::ANNOTATION_MODE_TAG_DATADOG, false);
}

TEST(StatsdOutputWriterFuzzTests, prefix_annotations_unchunked) {
  fuzz(stats::params::ANNOTATION_MODE_KEY_PREFIX, false);
}

TEST(StatsdOutputWriterFuzzTests, datadog_annotations_chunked) {
  fuzz(stats::params::ANNOTATION_MODE_TAG_DATADOG, true);
}

TEST(StatsdOutputWriterFuzzTests, prefix_annotations_chunked) {
  fuzz(stats::params::ANNOTATION_MODE_KEY_PREFIX, true);
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  //FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
