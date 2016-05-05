#include <unordered_set>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "io_runner_impl.hpp"
#include "test_socket.hpp"

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

namespace {
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

  inline std::string annotated_row(const std::string& msg,
      const mesos::ContainerID& cid, const mesos::ExecutorInfo& einfo,
      const std::string& tag_prefix = "|#") {
    std::ostringstream oss;
    oss << msg << tag_prefix
        << "framework_id:" << einfo.framework_id().value()
        << ",executor_id:" << einfo.executor_id().value()
        << ",container_id:" << cid.value();
    return oss.str();
  }
  inline std::string annotated_row_unregistered(const std::string& msg,
      const std::string& tag_prefix = "|#", const std::string& tag_suffix = "") {
    std::ostringstream oss;
    oss << msg << tag_prefix << "unknown_container" << tag_suffix;
    return oss.str();
  }

  mesos::Parameters get_params(size_t port,
      const std::string& annotation_mode = metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG) {
    mesos::Parameters params;

    mesos::Parameter* param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_HOST);
    param->set_value("127.0.0.1");

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_PORT);
    param->set_value(std::to_string(port));

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE);
    param->set_value(annotation_mode);

    return params;
  }

  void noop() { }
}

/**
 * Tests for data going through a full pipeline over localhost:
 * TestWriteSocket(s) -> IORunner[ContainerReader(s) -> PortWriter] -> TestReadSocket
 */

TEST(IORunnerImplTests, write_then_immediate_shutdown) {
  TestReadSocket reader;
  size_t output_port = reader.listen();

  mesos::Parameters params = get_params(output_port);

  metrics::IORunnerImpl runner;
  runner.init(params);

  std::shared_ptr<metrics::ContainerReader> reader1 = runner.create_container_reader(0);
  size_t input_port1 = reader1->open().get().port;
  mesos::ContainerID container1 = container_id("cid1");
  mesos::ExecutorInfo executor1 = exec_info("fid1", "eid1");
  reader1->register_container(container1, executor1);
  TestWriteSocket writer1;
  writer1.connect(input_port1);

  writer1.write("writer1:1");

  std::shared_ptr<metrics::ContainerReader> reader2 = runner.create_container_reader(0);
  size_t input_port2 = reader2->open().get().port;
  // no container registered
  TestWriteSocket writer2;
  writer2.connect(input_port2);

  writer2.write("writer2:1");

  std::shared_ptr<metrics::ContainerReader> reader3 = runner.create_container_reader(0);
  size_t input_port3 = reader3->open().get().port;
  mesos::ContainerID container3 = container_id("cid3");
  mesos::ExecutorInfo executor3 = exec_info("fid3", "eid3");
  reader3->register_container(container3, executor3);
  TestWriteSocket writer3;
  writer3.connect(input_port3);

  writer2.write("writer2:2|#tag2|@0.5");
  writer1.write("writer1:2|@0.2");
  writer2.write("writer2:3|#tag3");
  writer3.write("writer3:1\nwriter3:2|@0.3|#tag2\nwriter3:3");
  writer1.write("writer1:3");

  // Immediately shut things down in the correct order (readers THEN runner).
  reader1.reset();
  reader2.reset();
  reader3.reset();
}

TEST(IORunnerImplTests, data_flow_multi_stream) {
  TestReadSocket reader;
  size_t output_port = reader.listen();

  mesos::Parameters params = get_params(output_port);

  metrics::IORunnerImpl runner;
  runner.init(params);

  std::shared_ptr<metrics::ContainerReader> reader1 = runner.create_container_reader(0);
  size_t input_port1 = reader1->open().get().port;
  mesos::ContainerID container1 = container_id("cid1");
  mesos::ExecutorInfo executor1 = exec_info("fid1", "eid1");
  reader1->register_container(container1, executor1);
  TestWriteSocket writer1;
  writer1.connect(input_port1);

  writer1.write("writer1:1");

  std::shared_ptr<metrics::ContainerReader> reader2 = runner.create_container_reader(0);
  size_t input_port2 = reader2->open().get().port;
  // no container registered
  TestWriteSocket writer2;
  writer2.connect(input_port2);

  writer2.write("writer2:1");

  std::shared_ptr<metrics::ContainerReader> reader3 = runner.create_container_reader(0);
  size_t input_port3 = reader3->open().get().port;
  mesos::ContainerID container3 = container_id("cid3");
  mesos::ExecutorInfo executor3 = exec_info("fid3", "eid3");
  reader3->register_container(container3, executor3);
  TestWriteSocket writer3;
  writer3.connect(input_port3);

  writer2.write("writer2:2|#tag2|@0.5");
  writer1.write("writer1:2|@0.2");
  writer2.write("writer2:3|#tag3");
  writer3.write("writer3:1\nwriter3:2|@0.3|#tag2\nwriter3:3");
  writer1.write("writer1:3");

  // Wait up to (30 * 100ms) = 3s for the above 9 rows to show up in the output:
  std::unordered_set<std::string> stat_rows;
  for (size_t i = 0; i < 30 && stat_rows.size() != 9; i++) {
    std::string chunk = reader.read(100 /*ms*/);
    if (chunk.empty()) {
      continue;
    }
    std::istringstream iss(chunk);
    std::copy(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(),
        std::inserter(stat_rows, stat_rows.begin()));
  }

  EXPECT_EQ(9, stat_rows.size());
  EXPECT_TRUE(stat_rows.count(annotated_row("writer1:1", container1, executor1)));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer1:2|@0.2", container1, executor1)));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer1:3", container1, executor1)));
  EXPECT_TRUE(stat_rows.count(annotated_row_unregistered("writer2:1")));
  EXPECT_TRUE(stat_rows.count(annotated_row_unregistered("writer2:2", "|#tag2,", "|@0.5")));
  EXPECT_TRUE(stat_rows.count(annotated_row_unregistered("writer2:3", "|#tag3,")));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer3:1", container3, executor3)));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer3:2", container3, executor3, "|@0.3|#tag2,")));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer3:3", container3, executor3)));
}

TEST(IORunnerImplTests, data_flow_multi_stream_unchunked) {
  TestReadSocket reader;
  size_t output_port = reader.listen();

  mesos::Parameters params = get_params(output_port);

  mesos::Parameter* param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_CHUNKING);
  param->set_value("false");

  metrics::IORunnerImpl runner;
  runner.init(params);

  std::shared_ptr<metrics::ContainerReader> reader1 = runner.create_container_reader(0);
  size_t input_port1 = reader1->open().get().port;
  mesos::ContainerID container1 = container_id("cid1");
  mesos::ExecutorInfo executor1 = exec_info("fid1", "eid1");
  reader1->register_container(container1, executor1);
  TestWriteSocket writer1;
  writer1.connect(input_port1);

  writer1.write("writer1:1");

  std::shared_ptr<metrics::ContainerReader> reader2 = runner.create_container_reader(0);
  size_t input_port2 = reader2->open().get().port;
  // no container registered
  TestWriteSocket writer2;
  writer2.connect(input_port2);

  writer2.write("writer2:1");

  std::shared_ptr<metrics::ContainerReader> reader3 = runner.create_container_reader(0);
  size_t input_port3 = reader3->open().get().port;
  mesos::ContainerID container3 = container_id("cid3");
  mesos::ExecutorInfo executor3 = exec_info("fid3", "eid3");
  reader3->register_container(container3, executor3);
  TestWriteSocket writer3;
  writer3.connect(input_port3);

  writer2.write("writer2:2|#tag2|@0.5");
  writer1.write("writer1:2|@0.2");
  writer2.write("writer2:3|#tag3");
  writer3.write("writer3:1\nwriter3:2|@0.3|#tag2\nwriter3:3");
  writer1.write("writer1:3");

  // Wait up to (30 * 100ms) = 3s for the above 9 rows to show up in the output:
  std::unordered_set<std::string> stat_rows;
  for (size_t i = 0; i < 30 && stat_rows.size() != 9; i++) {
    std::string chunk = reader.read(100 /*ms*/);
    if (chunk.empty()) {
      continue;
    }
    std::istringstream iss(chunk);
    std::copy(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(),
        std::inserter(stat_rows, stat_rows.begin()));
  }

  EXPECT_EQ(9, stat_rows.size());
  EXPECT_TRUE(stat_rows.count(annotated_row("writer1:1", container1, executor1)));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer1:2|@0.2", container1, executor1)));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer1:3", container1, executor1)));
  EXPECT_TRUE(stat_rows.count(annotated_row_unregistered("writer2:1")));
  EXPECT_TRUE(stat_rows.count(annotated_row_unregistered("writer2:2", "|#tag2,", "|@0.5")));
  EXPECT_TRUE(stat_rows.count(annotated_row_unregistered("writer2:3", "|#tag3,")));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer3:1", container3, executor3)));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer3:2", container3, executor3, "|@0.3|#tag2,")));
  EXPECT_TRUE(stat_rows.count(annotated_row("writer3:3", container3, executor3)));
}

TEST(IORunnerImplTests, data_flow_multi_stream_unannotated) {
  TestReadSocket reader;
  size_t output_port = reader.listen();

  mesos::Parameters params = get_params(output_port, metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_NONE);

  metrics::IORunnerImpl runner;
  runner.init(params);

  std::shared_ptr<metrics::ContainerReader> reader1 = runner.create_container_reader(0);
  size_t input_port1 = reader1->open().get().port;
  mesos::ContainerID container1 = container_id("cid1");
  mesos::ExecutorInfo executor1 = exec_info("fid1", "eid1");
  reader1->register_container(container1, executor1);
  TestWriteSocket writer1;
  writer1.connect(input_port1);

  writer1.write("writer1:1");

  std::shared_ptr<metrics::ContainerReader> reader2 = runner.create_container_reader(0);
  size_t input_port2 = reader2->open().get().port;
  // no container registered
  TestWriteSocket writer2;
  writer2.connect(input_port2);

  writer2.write("writer2:1");

  std::shared_ptr<metrics::ContainerReader> reader3 = runner.create_container_reader(0);
  size_t input_port3 = reader3->open().get().port;
  mesos::ContainerID container3 = container_id("cid3");
  mesos::ExecutorInfo executor3 = exec_info("fid3", "eid3");
  reader3->register_container(container3, executor3);
  TestWriteSocket writer3;
  writer3.connect(input_port3);

  writer2.write("writer2:2|#tag2|@0.5");
  writer1.write("writer1:2|@0.2");
  writer2.write("writer2:3|#tag3");
  writer3.write("writer3:1\nwriter3:2|@0.3|#tag2\nwriter3:3");
  writer1.write("writer1:3");

  // Wait up to (30 * 100ms) = 3s for the above 9 rows to show up in the output:
  std::unordered_set<std::string> stat_rows;
  for (size_t i = 0; i < 30 && stat_rows.size() != 9; i++) {
    std::string chunk = reader.read(100 /*ms*/);
    if (chunk.empty()) {
      continue;
    }
    std::istringstream iss(chunk);
    std::copy(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(),
        std::inserter(stat_rows, stat_rows.begin()));
  }

  EXPECT_EQ(9, stat_rows.size());
  EXPECT_TRUE(stat_rows.count("writer1:1"));
  EXPECT_TRUE(stat_rows.count("writer1:2|@0.2"));
  EXPECT_TRUE(stat_rows.count("writer1:3"));
  EXPECT_TRUE(stat_rows.count("writer2:1"));
  EXPECT_TRUE(stat_rows.count("writer2:2|#tag2|@0.5"));
  EXPECT_TRUE(stat_rows.count("writer2:3|#tag3"));
  EXPECT_TRUE(stat_rows.count("writer3:1"));
  EXPECT_TRUE(stat_rows.count("writer3:2|@0.3|#tag2"));
  EXPECT_TRUE(stat_rows.count("writer3:3"));
}

TEST(IORunnerImplTests, init_fails) {
  metrics::IORunnerImpl runner;
  EXPECT_DETH(runner.dispatch(std::bind(noop)), ".*init\\(\\) wasn't called before dispatch\\(\\).*");
  EXPECT_DETH(runner.create_container_reader(0),
      ".*init\\(\\) wasn't called before create_container_reader\\(\\)");
  EXPECT_DETH(runner.update_usage(process::Future<mesos::ResourceUsage>()),
      ".*init\\(\\) wasn't called before update_usage\\(\\)");

  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE);
  param->set_value("asdf");

  EXPECT_DETH(runner.init(params), ".*Unknown " + metrics::params::OUTPUT_STATSD_ANNOTATION_MODE + " config value: asdf.*");

  param->set_value(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_NONE);

  TestReadSocket reader;
  size_t output_port = reader.listen();

  param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_HOST);
  param->set_value("127.0.0.1");

  param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_PORT);
  param->set_value(std::to_string(output_port));

  runner.init(params);

  EXPECT_DETH(runner.init(params), ".*init\\(\\) was called twice.*");
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  //FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
