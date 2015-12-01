#include <glog/logging.h>
#include <gtest/gtest.h>

#include "port_reader.cpp"

#include "mock_port_writer.hpp"
#include "test_socket.hpp"

namespace {
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

TEST(PortReaderTests, annotations_off_burst) {
  std::shared_ptr<MockPortWriter> mock(new MockPortWriter());

  boost::asio::io_service svc;
  stats::PortReader<MockPortWriter> reader(
      svc, mock, stats::UDPEndpoint("127.0.0.1", 0), false /* annotations_enabled */);

  Try<stats::UDPEndpoint> result = reader.open();
  ASSERT_FALSE(result.isError()) << result.error();
  size_t reader_port = result->port;

  TestWriteSocket test_writer;
  test_writer.connect(reader_port);
  std::string hello("hello"), hey("hey"), hi("hi");

  test_writer.write(hello);
  test_writer.write(hey);
  test_writer.write(hi);

  EXPECT_CALL(*mock, write(::testing::StartsWith(hello), hello.size()));
  EXPECT_CALL(*mock, write(::testing::StartsWith(hey), hey.size()));
  EXPECT_CALL(*mock, write(::testing::StartsWith(hi), hi.size()));

  svc.run_one();
  svc.run_one();
  svc.run_one();
}

TEST(PortReaderTests, annotations_off_stepped) {
  std::shared_ptr<MockPortWriter> mock(new MockPortWriter());

  boost::asio::io_service svc;
  stats::PortReader<MockPortWriter> reader(
      svc, mock, stats::UDPEndpoint("127.0.0.1", 0), false /* annotations_enabled */);

  Try<stats::UDPEndpoint> result = reader.open();
  ASSERT_FALSE(result.isError()) << result.error();
  size_t reader_port = result->port;

  TestWriteSocket test_writer;
  test_writer.connect(reader_port);
  std::string hello("hello"), hey("hey"), hi("hi");

  test_writer.write(hello);
  EXPECT_CALL(*mock, write(::testing::StartsWith(hello), hello.size()));
  svc.run_one();

  test_writer.write(hey);
  EXPECT_CALL(*mock, write(::testing::StartsWith(hey), hey.size()));
  svc.run_one();

  test_writer.write(hi);
  EXPECT_CALL(*mock, write(::testing::StartsWith(hi), hi.size()));
  svc.run_one();
}

TEST(PortReaderTests, annotations_on_zero_registered) {
  std::shared_ptr<MockPortWriter> mock(new MockPortWriter());

  boost::asio::io_service svc;
  stats::PortReader<MockPortWriter> reader(
      svc, mock, stats::UDPEndpoint("127.0.0.1", 0), true /* annotations_enabled */);

  Try<stats::UDPEndpoint> result = reader.open();
  ASSERT_FALSE(result.isError()) << result.error();
  size_t reader_port = result->port;

  TestWriteSocket test_writer;
  test_writer.connect(reader_port);
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#missing_container");

  test_writer.write(hello);
  test_writer.write(hey);
  test_writer.write(hi);

  EXPECT_CALL(*mock, write(::testing::StartsWith(hello + tag), (hello + tag).size()));
  EXPECT_CALL(*mock, write(::testing::StartsWith(hey + tag), (hey + tag).size()));
  EXPECT_CALL(*mock, write(::testing::StartsWith(hi + tag), (hi + tag).size()));

  svc.run_one();
  svc.run_one();
  svc.run_one();
}

TEST(PortReaderTests, annotations_on_one_registered) {
  std::shared_ptr<MockPortWriter> mock(new MockPortWriter());

  boost::asio::io_service svc;
  stats::PortReader<MockPortWriter> reader(
      svc, mock, stats::UDPEndpoint("127.0.0.1", 0), true /* annotations_enabled */);
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
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#container_id:c,executor_id:e,framework_id:f");

  test_writer.write(hello);
  test_writer.write(hey);
  test_writer.write(hi);

  EXPECT_CALL(*mock, write(::testing::StartsWith(hello + tag), (hello + tag).size()));
  EXPECT_CALL(*mock, write(::testing::StartsWith(hey + tag), (hey + tag).size()));
  EXPECT_CALL(*mock, write(::testing::StartsWith(hi + tag), (hi + tag).size()));

  svc.run_one();
  svc.run_one();
  svc.run_one();
}

TEST(PortReaderTests, annotations_on_multi_registered) {
  std::shared_ptr<MockPortWriter> mock(new MockPortWriter());

  boost::asio::io_service svc;
  stats::PortReader<MockPortWriter> reader(
      svc, mock, stats::UDPEndpoint("127.0.0.1", 0), true /* annotations_enabled */);
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
  std::string hello("hello"), hey("hey"), hi("hi");
  std::string tag("|#unknown_container");

  test_writer.write(hello);
  test_writer.write(hey);
  test_writer.write(hi);

  EXPECT_CALL(*mock, write(::testing::StartsWith(hello + tag), (hello + tag).size()));
  EXPECT_CALL(*mock, write(::testing::StartsWith(hey + tag), (hey + tag).size()));
  EXPECT_CALL(*mock, write(::testing::StartsWith(hi + tag), (hi + tag).size()));

  svc.run_one();
  svc.run_one();
  svc.run_one();
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
