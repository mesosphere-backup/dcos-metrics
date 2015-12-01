#include <glog/logging.h>
#include <gtest/gtest.h>

#include "port_writer.hpp"
#include "test_socket.hpp"

namespace {
  mesos::Parameters build_params(size_t port, size_t chunk_size) {
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
    param->set_value("127.0.0.1");
    param = params.add_parameter();
    param->set_key(stats::params::DEST_PORT);
    param->set_value(std::to_string(port));
    return params;
  }

  // Run svc until data is available at test_reader, with timeout after ~2s
  void wait_timer_triggered(boost::asio::io_service& svc, TestReadSocket& test_reader) {
    time_t start = time(NULL);
    while (test_reader.available() == 0) {
      svc.run_one();
      struct timespec wait_1ms;
      wait_1ms.tv_sec = 0;
      wait_1ms.tv_nsec = 1000000;
      nanosleep(&wait_1ms, NULL);
      ASSERT_LT(time(NULL), start + 2);
    }
  }
}

TEST(PortWriterTests, chunking_off) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  boost::asio::io_service svc;
  stats::PortWriter writer(svc, build_params(listen_port, 0 /* chunk_size */));
  Try<Nothing> result = writer.open();
  ASSERT_FALSE(result.isError()) << result.error();

  std::string hello("hello"), hey("hey");
  writer.write(hello.data(), hello.size());
  writer.write(hey.data(), hey.size());

  EXPECT_EQ("hello", test_reader.read());
  EXPECT_EQ("hey", test_reader.read());
}

TEST(PortWriterTests, chunking_on_flush_when_full) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  boost::asio::io_service svc;
  std::shared_ptr<stats::PortWriter> writer(new stats::PortWriter(
          svc,
          build_params(listen_port, 10 /* chunk_size */),
          9999999 /* chunk_timeout_ms */));
  Try<Nothing> result = writer->open();
  ASSERT_FALSE(result.isError()) << result.error();

  std::string hello("hello"), hey("hey"), hi("hi");
  writer->write(hello.data(), hello.size());// 5 bytes
  EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
  writer->write(hey.data(), hey.size());// 9 bytes (5 + 1 + 3)
  EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
  writer->write(hi.data(), hi.size());// 12 bytes (5 + 1 + 3 + 1 + 2), chunk is flushed before adding "hi"
  EXPECT_EQ("hello\nhey", test_reader.read(1 /* timeout_ms */));

  EXPECT_EQ("", test_reader.read(1 /* timeout_ms */));
  writer.reset();//force flush of buffer
  EXPECT_EQ("hi", test_reader.read(1 /* timeout_ms */));
}

TEST(PortWriterTests, chunking_on_flush_timer) {
  TestReadSocket test_reader;
  size_t listen_port = test_reader.listen();

  boost::asio::io_service svc;
  std::shared_ptr<stats::PortWriter> writer(new stats::PortWriter(
          svc,
          build_params(listen_port, 10 /* chunk_size */),
          1 /* chunk_timeout_ms */));
  Try<Nothing> result = writer->open();
  ASSERT_FALSE(result.isError()) << result.error();

  std::string hello("hello"), hey("hey"), hi("hi");
  writer->write(hello.data(), hello.size());
  EXPECT_FALSE(test_reader.available());
  wait_timer_triggered(svc, test_reader);
  EXPECT_EQ("hello", test_reader.read(100 /* timeout_ms */));

  writer->write(hey.data(), hey.size());
  writer->write(hi.data(), hi.size());
  EXPECT_FALSE(test_reader.available());
  wait_timer_triggered(svc, test_reader);
  EXPECT_EQ("hey\nhi", test_reader.read(100 /* timeout_ms */));
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
