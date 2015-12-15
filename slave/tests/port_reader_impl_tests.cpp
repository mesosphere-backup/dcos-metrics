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

TEST(PortReaderImplTests, memnmem) {
  std::string hello("hello"), hey("hey"), hi("hi"), empty("");
  EXPECT_EQ(NULL, memnmem_imp((char*) empty.data(), empty.size(), empty.data(), empty.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hi.data(), hi.size(), empty.data(), empty.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) empty.data(), empty.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hey.data(), hey.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hello.data(), hello.size(), hey.data(), hey.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hey.data(), hey.size(), hi.data(), hi.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hey.data(), hey.size(), hello.data(), hello.size()));
  EXPECT_EQ(NULL, memnmem_imp((char*) hi.data(), hi.size(), hey.data(), hey.size()));
  EXPECT_EQ(hi.data(), memnmem_imp((char*) hi.data(), hi.size(), hi.data(), hi.size()));
  EXPECT_EQ(hey.data(), memnmem_imp((char*) hey.data(), hey.size(), hey.data(), hey.size()));
  EXPECT_EQ(hello.data(),
      memnmem_imp((char*) hello.data(), hello.size(), hello.data(), hello.size()));
  std::string hihey("hihey"), heyhello("heyhello");
  EXPECT_EQ(hihey.data(), memnmem_imp((char*) hihey.data(), hihey.size(), hi.data(), hi.size()));
  EXPECT_STREQ(hey.data(), memnmem_imp((char*) hihey.data(), hihey.size(), hey.data(), hey.size()));
  EXPECT_EQ(heyhello.data(),
      memnmem_imp((char*) heyhello.data(), heyhello.size(), hey.data(), hey.size()));
  EXPECT_STREQ(hello.data(),
      memnmem_imp((char*) heyhello.data(), heyhello.size(), hello.data(), hello.size()));
  std::string h("h"), hhiheyhello("hhiheyhello"), hiheyhello("hiheyhello");
  EXPECT_EQ(hhiheyhello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), h.data(), h.size()));
  EXPECT_STREQ(hiheyhello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), hi.data(), hi.size()));
  EXPECT_STREQ(heyhello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), hey.data(), hey.size()));
  EXPECT_STREQ(hello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), hello.data(), hello.size()));
  EXPECT_STREQ(heyhello.data(),
      memnmem_imp((char*) hhiheyhello.data(), hhiheyhello.size(), heyhello.data(), heyhello.size()));
}

TEST(PortReaderImplTests, prepare_for_tags) {
  std::vector<char> scratch_buffer;
  std::string hello("hello"), hi("hi"), h("h"), empty("");

  EXPECT_EQ(TagMode::FIRST_TAG,
      prepare_for_tags((char*) hello.data(), hello.size(), scratch_buffer));
  EXPECT_STREQ("hello", hello.data());
  EXPECT_EQ(0, scratch_buffer.size());

  EXPECT_EQ(TagMode::FIRST_TAG,
      prepare_for_tags((char*) hi.data(), hi.size(), scratch_buffer));
  EXPECT_STREQ("hi", hi.data());
  EXPECT_EQ(0, scratch_buffer.size());

  EXPECT_EQ(TagMode::FIRST_TAG,
      prepare_for_tags((char*) h.data(), h.size(), scratch_buffer));
  EXPECT_STREQ("h", h.data());
  EXPECT_EQ(0, scratch_buffer.size());

  EXPECT_EQ(TagMode::FIRST_TAG,
      prepare_for_tags((char*) empty.data(), empty.size(), scratch_buffer));
  EXPECT_STREQ("", empty.data());
  EXPECT_EQ(0, scratch_buffer.size());

  std::string hello_1tag("hello|#tag");
  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_1tag.data(), hello_1tag.size(), scratch_buffer));
  EXPECT_STREQ("hello|#tag", hello_1tag.data());
  EXPECT_EQ(0, scratch_buffer.size());// not grown, wasn't used

  std::string hello_2endtag("hello|@0.5|#tag"), hello_2starttag("hello|#ta|@0.5");

  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_2endtag.data(), hello_2endtag.size(), scratch_buffer));
  EXPECT_STREQ("hello|@0.5|#tag", hello_2endtag.data());
  EXPECT_EQ(0, scratch_buffer.size());// not grown, wasn't used

  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_2starttag.data(), hello_2starttag.size(), scratch_buffer));
  EXPECT_STREQ("hello|@0.5|#ta", hello_2starttag.data());
  EXPECT_EQ(4, scratch_buffer.size());// grown to fit

  std::string
    hello_3endtag("hello|&other|@0.5|#tag"),
    hello_3midtag("hello|&other|#tag|@0.5"),
    hello_3starttag("hello|#t|&other|@0.5");

  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_3endtag.data(), hello_3endtag.size(), scratch_buffer));
  EXPECT_STREQ("hello|&other|@0.5|#tag", hello_3endtag.data());
  EXPECT_EQ(4, scratch_buffer.size());// not grown, wasn't used

  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_3midtag.data(), hello_3midtag.size(), scratch_buffer));
  EXPECT_STREQ("hello|&other|@0.5|#tag", hello_3midtag.data());
  EXPECT_EQ(5, scratch_buffer.size());// grown to fit

  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_3starttag.data(), hello_3starttag.size(), scratch_buffer));
  EXPECT_STREQ("hello|&other|@0.5|#t", hello_3starttag.data());
  EXPECT_EQ(5, scratch_buffer.size());// only grown, never shrunk

  // corrupt/weird data: doesn't repair, but avoids segfaulting
  std::string
    hello_1emptytag("hello|#"),
    hello_1emptytagval("hello|#,"),
    hello_1emptyend("hello|"),
    hello_2tag_empty("hello|#tag1|"),
    hello_2empty_empty("hello||"),
    hello_2empty_tag("hello||#tag1"),
    hello_2tag_tag("hello|#tag1|#tag2"),
    hello_2empty_emptytag("hello||#"),
    hello_2emptytag_empty("hello|#|"),
    hello_2emptytagval_empty("hello|#,|");

  EXPECT_EQ(TagMode::APPEND_TAG_NO_DELIM,
      prepare_for_tags((char*) hello_1emptytagval.data(), hello_1emptytagval.size(), scratch_buffer));
  EXPECT_STREQ("hello|#,", hello_1emptytagval.data());

  EXPECT_EQ(TagMode::APPEND_TAG_NO_DELIM,
      prepare_for_tags((char*) hello_1emptytag.data(), hello_1emptytag.size(), scratch_buffer));
  EXPECT_STREQ("hello|#", hello_1emptytag.data());

  EXPECT_EQ(TagMode::FIRST_TAG,
      prepare_for_tags((char*) hello_1emptyend.data(), hello_1emptyend.size(), scratch_buffer));
  EXPECT_STREQ("hello|", hello_1emptyend.data());

  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_2tag_empty.data(), hello_2tag_empty.size(), scratch_buffer));
  EXPECT_STREQ("hello||#tag1", hello_2tag_empty.data());

  EXPECT_EQ(TagMode::FIRST_TAG,
      prepare_for_tags((char*) hello_2empty_empty.data(), hello_2empty_empty.size(), scratch_buffer));
  EXPECT_STREQ("hello||", hello_2empty_empty.data());

  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_2empty_tag.data(), hello_2empty_tag.size(), scratch_buffer));
  EXPECT_STREQ("hello||#tag1", hello_2empty_tag.data());

  EXPECT_EQ(TagMode::APPEND_TAG,
      prepare_for_tags((char*) hello_2tag_tag.data(), hello_2tag_tag.size(), scratch_buffer));
  EXPECT_STREQ("hello|#tag2|#tag1", hello_2tag_tag.data());

  EXPECT_EQ(TagMode::APPEND_TAG_NO_DELIM,
      prepare_for_tags((char*) hello_2empty_emptytag.data(), hello_2empty_emptytag.size(), scratch_buffer));
  EXPECT_STREQ("hello||#", hello_2empty_emptytag.data());

  EXPECT_EQ(TagMode::APPEND_TAG_NO_DELIM,
      prepare_for_tags((char*) hello_2emptytag_empty.data(), hello_2emptytag_empty.size(), scratch_buffer));
  EXPECT_STREQ("hello||#", hello_2emptytag_empty.data());

  EXPECT_EQ(TagMode::APPEND_TAG_NO_DELIM,
      prepare_for_tags((char*) hello_2emptytagval_empty.data(), hello_2emptytagval_empty.size(), scratch_buffer));
  EXPECT_STREQ("hello||#,", hello_2emptytagval_empty.data());
}

TEST(PortReaderImplTests, annotations_off) {
  std::string hello("hello"), hey("hey"), hi("hi");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), false /* annotations_enabled */);

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
        stats::UDPEndpoint("127.0.0.1", 0), false /* annotations_enabled */);

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
        stats::UDPEndpoint("127.0.0.1", 0), false /* annotations_enabled */);

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

TEST(PortReaderImplTests, annotations_on_zero_registered) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#missing_container");

  ServiceThread thread(3);
  {
    stats::PortReaderImpl<MockPortWriter> reader(thread.svc(), thread.mock(),
        stats::UDPEndpoint("127.0.0.1", 0), true /* annotations_enabled */);

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

TEST(PortReaderImplTests, annotations_on) {
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

TEST(PortReaderImplTests, annotations_on_multiline) {
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string multi(hello + "\n" + hey + "\n" + hi);
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

TEST(PortReaderImplTests, annotations_on_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#|@0.3");
  std::string tag("container_id:c,executor_id:e,framework_id:f");

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

TEST(PortReaderImplTests, annotations_on_multiline_tagged_input) {
  std::string hello("hello|#tag1|@0.1"), hey("hey|@0.2|#tag2"), hi("hi|#tag3|@0.3");
  std::string multi(hello + "\n\n" + hey + "\n\n" + hi);
  std::string tag("container_id:c,executor_id:e,framework_id:f");

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

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  //FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
