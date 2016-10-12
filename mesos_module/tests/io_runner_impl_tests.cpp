#include <unordered_set>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <avro/DataFile.hh>

#include "avro_encoder.hpp"
#include "io_runner_impl.hpp"
#include "test_tcp_socket.hpp"
#include "test_udp_socket.hpp"

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

  mesos::Parameters get_params(size_t udp_port, size_t tcp_port,
      const std::string& annotation_mode = metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_TAG_DATADOG) {
    mesos::Parameters params;

    mesos::Parameter* param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_ENABLED);
    param->set_value("true");

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_HOST);
    param->set_value("127.0.0.1");

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_PORT);
    param->set_value(std::to_string(udp_port));

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE);
    param->set_value(annotation_mode);

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_COLLECTOR_IP);
    param->set_value("127.0.0.1");

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_COLLECTOR_PORT);
    param->set_value(std::to_string(tcp_port));

    param = params.add_parameter();
    param->set_key(metrics::params::OUTPUT_COLLECTOR_CHUNK_TIMEOUT_SECONDS);
    param->set_value("1");

    return params;
  }

  void noop() { }
}

/**
 * Tests for data going through a full pipeline over localhost:
 * TestUDPWriteSocket(s) -> IORunner[ContainerReader(s) -> PortWriter] -> TestUDPReadSocket & TestTCPReadSocket
 */

class IORunnerImplTests : public ::testing::Test {
 protected:
  virtual void TearDown() {
    for (const std::string& tmpfile : tmpfiles) {
      LOG(INFO) << "Deleting tmp file: " << tmpfile;
      unlink(tmpfile.data());
    }
  }

  std::string write_tmp(const std::string& data = std::string()) {
    std::string tmpname("io_runner_impl_tests-XXXXXX");
    int tmpfd = mkstemp((char*)tmpname.data());

    // get path to tmpfile
    std::ostringstream readpath;
    readpath << "/proc/self/fd/" << tmpfd;
    std::string buf(1024, '\0');
    int wrote = readlink(readpath.str().data(), (char*)buf.data(), buf.size());
    std::string tmppath = std::string(buf.data(), wrote);

    if (!data.empty()) {
      FILE* tmpfp = fdopen(tmpfd, "w");
      fwrite(data.data(), data.size(), 1, tmpfp);
      fclose(tmpfp);
    }

    LOG(INFO) << "Created tmp file (with " << data.size() << "b): " << tmppath;
    tmpfiles.push_back(tmppath);
    return tmppath;
  }

 private:
  std::vector<std::string> tmpfiles;
};

TEST_F(IORunnerImplTests, write_then_immediate_shutdown) {
  TestUDPReadSocket udp_reader;
  size_t udp_output_port = udp_reader.listen();
  TestTCPReadSession tcp_reader;
  size_t tcp_output_port = tcp_reader.port();

  mesos::Parameters params = get_params(udp_output_port, tcp_output_port);

  metrics::IORunnerImpl runner;
  runner.init(params);

  std::shared_ptr<metrics::ContainerReader> reader1 = runner.create_container_reader(0);
  size_t input_port1 = reader1->open().get().port;
  mesos::ContainerID container1 = container_id("cid1");
  mesos::ExecutorInfo executor1 = exec_info("fid1", "eid1");
  reader1->register_container(container1, executor1);
  TestUDPWriteSocket writer1;
  writer1.connect(input_port1);

  writer1.write("writer1:1");

  std::shared_ptr<metrics::ContainerReader> reader2 = runner.create_container_reader(0);
  size_t input_port2 = reader2->open().get().port;
  // no container registered
  TestUDPWriteSocket writer2;
  writer2.connect(input_port2);

  writer2.write("writer2:1");

  std::shared_ptr<metrics::ContainerReader> reader3 = runner.create_container_reader(0);
  size_t input_port3 = reader3->open().get().port;
  mesos::ContainerID container3 = container_id("cid3");
  mesos::ExecutorInfo executor3 = exec_info("fid3", "eid3");
  reader3->register_container(container3, executor3);
  TestUDPWriteSocket writer3;
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

TEST_F(IORunnerImplTests, data_flow_multi_stream) {
  TestUDPReadSocket udp_reader;
  size_t udp_output_port = udp_reader.listen();
  TestTCPReadSession tcp_reader(23458);
  size_t tcp_output_port = tcp_reader.port();

  mesos::Parameters params = get_params(udp_output_port, tcp_output_port);

  metrics::IORunnerImpl runner;
  runner.init(params);

  std::shared_ptr<metrics::ContainerReader> reader1 = runner.create_container_reader(0);
  size_t input_port1 = reader1->open().get().port;
  mesos::ContainerID container1 = container_id("cid1");
  mesos::ExecutorInfo executor1 = exec_info("fid1", "eid1");
  reader1->register_container(container1, executor1);
  TestUDPWriteSocket writer1;
  writer1.connect(input_port1);

  // wait for the TCP sender to finish sending its header.
  // otherwise it'll reject our data:
  tcp_reader.wait_for_available(5);

  writer1.write("writer1:1");

  std::shared_ptr<metrics::ContainerReader> reader2 = runner.create_container_reader(0);
  size_t input_port2 = reader2->open().get().port;
  // no container registered
  TestUDPWriteSocket writer2;
  writer2.connect(input_port2);

  writer2.write("writer2:1");

  std::shared_ptr<metrics::ContainerReader> reader3 = runner.create_container_reader(0);
  size_t input_port3 = reader3->open().get().port;
  mesos::ContainerID container3 = container_id("cid3");
  mesos::ExecutorInfo executor3 = exec_info("fid3", "eid3");
  reader3->register_container(container3, executor3);
  TestUDPWriteSocket writer3;
  writer3.connect(input_port3);

  writer2.write("writer2:2|#tag2|@0.5");
  writer1.write("writer1:2|@0.2");
  writer2.write("writer2:3|#tag3");
  writer3.write("writer3:1\nwriter3:2|@0.3|#tag2\nwriter3:3");
  writer1.write("writer1:3");

  // Wait up to (30 * 100ms) = 3s for the above 9 rows to show up in the output:
  std::unordered_set<std::string> udp_stat_rows;
  for (size_t i = 0; i < 30 && udp_stat_rows.size() != 9; i++) {
    std::string chunk = udp_reader.read(100 /*ms*/);
    if (chunk.empty()) {
      continue;
    }
    std::istringstream iss(chunk);
    std::copy(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(),
        std::inserter(udp_stat_rows, udp_stat_rows.begin()));
  }

  EXPECT_EQ(9, udp_stat_rows.size());
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer1:1", container1, executor1)));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer1:2|@0.2", container1, executor1)));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer1:3", container1, executor1)));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row_unregistered("writer2:1")));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row_unregistered("writer2:2", "|#tag2,", "|@0.5")));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row_unregistered("writer2:3", "|#tag3,")));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer3:1", container3, executor3)));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer3:2", container3, executor3, "|@0.3|#tag2,")));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer3:3", container3, executor3)));

  std::ostringstream tcp_oss;
  for (size_t i = 0; i < 10 && tcp_reader.wait_for_available(1); i++) {
    while (tcp_reader.available()) {
      std::string chunk = *tcp_reader.read();
      LOG(INFO) << "\n\n" << chunk << "\n\n";
      tcp_oss << chunk;
    }
  }

  // verify that content parses as an avro file
  LOG(INFO) << tcp_oss.str();
  std::string tmppath = write_tmp(tcp_oss.str());
  avro::DataFileReader<metrics_schema::MetricList> avro_reader(tmppath.data());
  metrics_schema::MetricList flist;
  size_t datapoints = 0;
  while (avro_reader.read(flist)) {
    datapoints += flist.datapoints.size();
  }
  EXPECT_EQ(9, datapoints);
}

TEST_F(IORunnerImplTests, data_flow_multi_stream_unchunked) {
  TestUDPReadSocket udp_reader;
  size_t udp_output_port = udp_reader.listen();
  TestTCPReadSession tcp_reader(23457);
  size_t tcp_output_port = tcp_reader.port();

  mesos::Parameters params = get_params(udp_output_port, tcp_output_port);

  mesos::Parameter* param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_CHUNKING);
  param->set_value("false");

  param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_COLLECTOR_CHUNKING);
  param->set_value("false");

  metrics::IORunnerImpl runner;
  runner.init(params);

  std::shared_ptr<metrics::ContainerReader> reader1 = runner.create_container_reader(0);
  size_t input_port1 = reader1->open().get().port;
  mesos::ContainerID container1 = container_id("cid1");
  mesos::ExecutorInfo executor1 = exec_info("fid1", "eid1");
  reader1->register_container(container1, executor1);
  TestUDPWriteSocket writer1;
  writer1.connect(input_port1);

  // wait for the TCP sender to finish sending its header.
  // otherwise it'll reject our data:
  tcp_reader.wait_for_available(5);

  writer1.write("writer1:1");

  std::shared_ptr<metrics::ContainerReader> reader2 = runner.create_container_reader(0);
  size_t input_port2 = reader2->open().get().port;
  // no container registered
  TestUDPWriteSocket writer2;
  writer2.connect(input_port2);

  writer2.write("writer2:1");

  std::shared_ptr<metrics::ContainerReader> reader3 = runner.create_container_reader(0);
  size_t input_port3 = reader3->open().get().port;
  mesos::ContainerID container3 = container_id("cid3");
  mesos::ExecutorInfo executor3 = exec_info("fid3", "eid3");
  reader3->register_container(container3, executor3);
  TestUDPWriteSocket writer3;
  writer3.connect(input_port3);

  writer2.write("writer2:2|#tag2|@0.5");
  writer1.write("writer1:2|@0.2");
  writer2.write("writer2:3|#tag3");
  writer3.write("writer3:1\nwriter3:2|@0.3|#tag2\nwriter3:3");
  writer1.write("writer1:3");

  // Wait up to (30 * 100ms) = 3s for the above 9 rows to show up in the output:
  std::unordered_set<std::string> udp_stat_rows;
  for (size_t i = 0; i < 30 && udp_stat_rows.size() != 9; i++) {
    std::string chunk = udp_reader.read(100 /*ms*/);
    if (chunk.empty()) {
      continue;
    }
    std::istringstream iss(chunk);
    std::copy(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(),
        std::inserter(udp_stat_rows, udp_stat_rows.begin()));
  }

  EXPECT_EQ(9, udp_stat_rows.size());
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer1:1", container1, executor1)));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer1:2|@0.2", container1, executor1)));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer1:3", container1, executor1)));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row_unregistered("writer2:1")));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row_unregistered("writer2:2", "|#tag2,", "|@0.5")));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row_unregistered("writer2:3", "|#tag3,")));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer3:1", container3, executor3)));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer3:2", container3, executor3, "|@0.3|#tag2,")));
  EXPECT_TRUE(udp_stat_rows.count(annotated_row("writer3:3", container3, executor3)));

  std::ostringstream tcp_oss;
  for (size_t i = 0; i < 10 && tcp_reader.wait_for_available(1); i++) {
    while (tcp_reader.available()) {
      std::string chunk = *tcp_reader.read();
      LOG(INFO) << "\n\n" << chunk << "\n\n";
      tcp_oss << chunk;
    }
  }

  // verify that content parses as an avro file
  LOG(INFO) << tcp_oss.str();
  std::string tmppath = write_tmp(tcp_oss.str());
  avro::DataFileReader<metrics_schema::MetricList> avro_reader(tmppath.data());
  metrics_schema::MetricList flist;
  size_t datapoints = 0;
  while (avro_reader.read(flist)) {
    datapoints += flist.datapoints.size();
  }
  EXPECT_EQ(9, datapoints);
}

TEST_F(IORunnerImplTests, data_flow_multi_stream_unannotated) {
  TestUDPReadSocket udp_reader;
  size_t udp_output_port = udp_reader.listen();
  TestTCPReadSession tcp_reader(23459);
  size_t tcp_output_port = tcp_reader.port();

  mesos::Parameters params = get_params(udp_output_port, tcp_output_port,
      metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_NONE);

  metrics::IORunnerImpl runner;
  runner.init(params);

  std::shared_ptr<metrics::ContainerReader> reader1 = runner.create_container_reader(0);
  size_t input_port1 = reader1->open().get().port;
  mesos::ContainerID container1 = container_id("cid1");
  mesos::ExecutorInfo executor1 = exec_info("fid1", "eid1");
  reader1->register_container(container1, executor1);
  TestUDPWriteSocket writer1;
  writer1.connect(input_port1);

  // wait for the TCP sender to finish sending its header.
  // otherwise it'll reject our data:
  tcp_reader.wait_for_available(5);

  writer1.write("writer1:1");

  std::shared_ptr<metrics::ContainerReader> reader2 = runner.create_container_reader(0);
  size_t input_port2 = reader2->open().get().port;
  // no container registered
  TestUDPWriteSocket writer2;
  writer2.connect(input_port2);

  writer2.write("writer2:1");

  std::shared_ptr<metrics::ContainerReader> reader3 = runner.create_container_reader(0);
  size_t input_port3 = reader3->open().get().port;
  mesos::ContainerID container3 = container_id("cid3");
  mesos::ExecutorInfo executor3 = exec_info("fid3", "eid3");
  reader3->register_container(container3, executor3);
  TestUDPWriteSocket writer3;
  writer3.connect(input_port3);

  writer2.write("writer2:2|#tag2|@0.5");
  writer1.write("writer1:2|@0.2");
  writer2.write("writer2:3|#tag3");
  writer3.write("writer3:1\nwriter3:2|@0.3|#tag2\nwriter3:3");
  writer1.write("writer1:3");

  // Wait up to (30 * 100ms) = 3s for the above 9 rows to show up in the output:
  std::unordered_set<std::string> udp_stat_rows;
  for (size_t i = 0; i < 30 && udp_stat_rows.size() != 9; i++) {
    for (;;) {
      std::string chunk = udp_reader.read(100 /*ms*/);
      if (chunk.empty()) {
        break;
      }
      std::istringstream iss(chunk);
      std::copy(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(),
          std::inserter(udp_stat_rows, udp_stat_rows.begin()));
    }
  }

  EXPECT_EQ(9, udp_stat_rows.size());
  EXPECT_TRUE(udp_stat_rows.count("writer1:1"));
  EXPECT_TRUE(udp_stat_rows.count("writer1:2|@0.2"));
  EXPECT_TRUE(udp_stat_rows.count("writer1:3"));
  EXPECT_TRUE(udp_stat_rows.count("writer2:1"));
  EXPECT_TRUE(udp_stat_rows.count("writer2:2|#tag2|@0.5"));
  EXPECT_TRUE(udp_stat_rows.count("writer2:3|#tag3"));
  EXPECT_TRUE(udp_stat_rows.count("writer3:1"));
  EXPECT_TRUE(udp_stat_rows.count("writer3:2|@0.3|#tag2"));
  EXPECT_TRUE(udp_stat_rows.count("writer3:3"));

  std::ostringstream tcp_oss;
  for (size_t i = 0; i < 10 && tcp_reader.wait_for_available(1); i++) {
    while (tcp_reader.available()) {
      std::string chunk = *tcp_reader.read();
      tcp_oss << chunk;
    }
  }

  // verify that content parses as an avro file
  LOG(INFO) << tcp_oss.str();
  std::string tmppath = write_tmp(tcp_oss.str());
  avro::DataFileReader<metrics_schema::MetricList> avro_reader(tmppath.data());
  metrics_schema::MetricList flist;
  size_t datapoints = 0;
  while (avro_reader.read(flist)) {
    datapoints += flist.datapoints.size();
  }
  EXPECT_EQ(9, datapoints);
}

TEST_F(IORunnerImplTests, init_fails) {
  metrics::IORunnerImpl runner;
  EXPECT_DETH(runner.dispatch(std::bind(noop)), ".*init\\(\\) wasn't called before dispatch\\(\\).*");
  EXPECT_DETH(runner.create_container_reader(0),
      ".*init\\(\\) wasn't called before create_container_reader\\(\\)");

  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_ENABLED);
  param->set_value("true");

  param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE);
  param->set_value("asdf");

  EXPECT_DETH(runner.init(params), ".*Unknown " + metrics::params::OUTPUT_STATSD_ANNOTATION_MODE + " config value: asdf.*");

  param->set_value(metrics::params::OUTPUT_STATSD_ANNOTATION_MODE_NONE);

  TestUDPReadSocket udp_reader;
  size_t udp_output_port = udp_reader.listen();

  param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_HOST);
  param->set_value("127.0.0.1");

  param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_PORT);
  param->set_value(std::to_string(udp_output_port));

  runner.init(params);

  EXPECT_DETH(runner.init(params), ".*init\\(\\) was called twice.*");
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
