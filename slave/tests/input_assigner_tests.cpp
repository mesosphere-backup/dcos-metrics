#include <glog/logging.h>

#include <thread>

#include "input_assigner.hpp"
#include "mock_port_reader.hpp"
#include "mock_port_runner.hpp"

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

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
  inline Try<stats::UDPEndpoint> try_endpoint(const std::string& host, size_t port) {
    return stats::UDPEndpoint(host, port);
  }

  void register_get_unregister(stats::InputAssigner& assigner, size_t id) {
    std::cout << "thread " << id << " start" << std::endl;
    std::ostringstream oss;
    oss << "c" << id;
    mesos::ContainerID cid = container_id(oss.str());

    oss.clear();
    oss << "f" << id;
    std::string fid = oss.str();
    oss.clear();
    oss << "e" << id;
    mesos::ExecutorInfo einfo = exec_info(fid, oss.str());

    EXPECT_FALSE(assigner.register_container(cid, einfo).isError()) << "thread " << id;
    assigner.unregister_container(cid);
    std::cout << "thread " << id << " end" << std::endl;
  }

  // Simple boilerplate utility to simulate PortRunner's async scheduler, which just runs the
  // function synchronously
  void execute(std::function<void()> func) {
    func();
  }
}

MATCHER_P(ContainerIdMatch, proto_value, "mesos::ContainerID") {
  return arg.value() == proto_value.value();
}

MATCHER_P(ExecInfoMatch, proto_value, "mesos::ExecutorInfo") {
  return arg.executor_id().value() == proto_value.executor_id().value()
    && arg.framework_id().value() == proto_value.framework_id().value();
}

class InputAssignerTests : public ::testing::Test {
 public:
  InputAssignerTests()
    : mock_reader1(new MockPortReader),
      mock_reader2(new MockPortReader),
      mock_runner(new MockPortRunner) { }

 protected:
  std::shared_ptr<MockPortReader> mock_reader1;
  std::shared_ptr<MockPortReader> mock_reader2;
  std::shared_ptr<MockPortRunner> mock_runner;
};

TEST_F(InputAssignerTests, single_port_bad_port) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("host1");
  EXPECT_DETH(new stats::SinglePortAssigner(mock_runner, params),
      "Invalid listen_port config value: 0");

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  param->set_value("0");
  EXPECT_DETH(new stats::SinglePortAssigner(mock_runner, params),
      "Invalid listen_port config value: 0");

  param->set_value("65536");
  EXPECT_DETH(new stats::SinglePortAssigner(mock_runner, params),
      "Invalid listen_port config value: 65536");
}

TEST_F(InputAssignerTests, single_port) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  const size_t create_port = 9999;
  param->set_value(std::to_string(create_port)); // passed to PortReader creation, otherwise unused

  stats::SinglePortAssigner spa(mock_runner, params);
  mesos::ContainerID ci1 = container_id("cid1"), ci2 = container_id("cid2");
  mesos::ExecutorInfo ei1 = exec_info("fid1", "eid1"), ei2 = exec_info("fid2", "eid2");
  const std::string host1("host1");
  const size_t port1 = 1234, port2 = 4321;

  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  // Registration of ci1/ei1 fails
  EXPECT_CALL(*mock_runner, create_port_reader(create_port)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_TRUE(spa.register_container(ci1, ei1).isError());

  // Registration of ci1/ei1 creates reader and succeeds
  EXPECT_CALL(*mock_runner, create_port_reader(create_port)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint("ignored", 0)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)))
    .WillOnce(Return(try_endpoint(host1, port1)));
  Try<stats::UDPEndpoint> endpt = spa.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 reuses reader and succeeds
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)))
    .WillOnce(Return(try_endpoint(host1, port2)));
  endpt = spa.register_container(ci2, ei2);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(Return(try_endpoint(host1, port2)));
  EXPECT_CALL(*mock_reader1, unregister_container(ContainerIdMatch(ci2)));
  spa.unregister_container(ci2);
}

TEST_F(InputAssignerTests, single_port_multithread) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  const std::string host = "host";
  const size_t port = 9999;
  param->set_value(std::to_string(port));

  stats::SinglePortAssigner spa(mock_runner, params);
  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));
  EXPECT_CALL(*mock_runner, create_port_reader(port)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillRepeatedly(Return(try_endpoint(host, port)));
  EXPECT_CALL(*mock_reader1, endpoint()).WillRepeatedly(Return(try_endpoint(host, port)));
  EXPECT_CALL(*mock_reader1, register_container(_, _))
    .WillRepeatedly(Return(try_endpoint("ignored", 0)));
  EXPECT_CALL(*mock_reader1, unregister_container(_)).WillRepeatedly(Return());

  std::list<std::thread*> thread_ptrs;
  for (int i = 0; i < 250; ++i) {
    //Note: Tried getting AND resetting in each thread, but this led to glogging races.
    //      That behavior isn't supported anyway.
    thread_ptrs.push_back(new std::thread(std::bind(register_get_unregister, std::ref(spa), i)));
  }
  for (std::thread* thread : thread_ptrs) {
    thread->join();
    delete thread;
  }
  thread_ptrs.clear();
}

TEST_F(InputAssignerTests, ephemeral_port) {
  stats::EphemeralPortAssigner epa(mock_runner);
  mesos::ContainerID ci1 = container_id("cid1"), ci2 = container_id("cid2");
  mesos::ExecutorInfo ei1 = exec_info("fid1", "eid1"), ei2 = exec_info("fid2", "eid2");
  const std::string host1("host1"), host2("host2");
  const size_t port1 = 1234, port2 = 4321;

  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  // Registration of ci1/ei1 fails
  EXPECT_CALL(*mock_runner, create_port_reader(0)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_TRUE(epa.register_container(ci1, ei1).isError());

  // Registration of ci1/ei1 creates reader and succeeds
  EXPECT_CALL(*mock_runner, create_port_reader(0)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint(host1, port1)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)))
    .WillOnce(Return(try_endpoint("ignored1", 54321)));
  Try<stats::UDPEndpoint> endpt = epa.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 creates a new separate reader
  EXPECT_CALL(*mock_runner, create_port_reader(0)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint(host2, port2)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)))
    .WillOnce(Return(try_endpoint("ignored2", 54321)));
  endpt = epa.register_container(ci2, ei2);
  EXPECT_EQ(host2, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(Return(try_endpoint(host2, port2)));
  epa.unregister_container(ci2);
  // Unregister same thing again, no reader access this time
  epa.unregister_container(ci2);

  // Unregister ci1/ei1 with broken endpoint. Still works.
  EXPECT_CALL(*mock_reader1, endpoint())
    .WillOnce(Return(Try<stats::UDPEndpoint>(Error("ignored"))));
  epa.unregister_container(ci1);
  // Unregister same thing again, no reader access this time
  epa.unregister_container(ci1);
}

TEST_F(InputAssignerTests, ephemeral_port_multithread) {
  const std::string host = "host";
  const size_t port = 9999;

  stats::EphemeralPortAssigner epa(mock_runner);
  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));
  EXPECT_CALL(*mock_runner, create_port_reader(0)).WillRepeatedly(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillRepeatedly(Return(try_endpoint(host, port)));
  EXPECT_CALL(*mock_reader1, endpoint()).WillRepeatedly(Return(try_endpoint(host, port)));
  EXPECT_CALL(*mock_reader1, register_container(_, _))
    .WillRepeatedly(Return(try_endpoint("ignored", 0)));
  EXPECT_CALL(*mock_reader1, unregister_container(_)).WillRepeatedly(Return());

  std::list<std::thread*> thread_ptrs;
  for (int i = 0; i < 250; ++i) {
    //Note: Tried getting AND resetting in each thread, but this led to glogging races.
    //      That behavior isn't supported anyway.
    thread_ptrs.push_back(new std::thread(std::bind(register_get_unregister, std::ref(epa), i)));
  }
  for (std::thread* thread : thread_ptrs) {
    thread->join();
    delete thread;
  }
  thread_ptrs.clear();
}

TEST_F(InputAssignerTests, port_range_bad_ports) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("host1");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, params),
      "Invalid listen_port_start config value: 0");

  // Test listen_port_start (end unset)
  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_START);
  param->set_value("0");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, params),
      "Invalid listen_port_start config value: 0");

  param->set_value("65536");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, params),
      "Invalid listen_port_start config value: 65536");

  // Set a valid start to test listen_port_end
  param->set_value("12345");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, params),
      "Invalid listen_port_end config value: 0");

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_END);
  param->set_value("0");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, params),
      "Invalid listen_port_end config value: 0");

  param->set_value("12344");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, params),
      "listen_port_start \\(=12345\\) must be less than listen_port_end \\(=12344\\)");

  param->set_value("12345");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, params),
      "listen_port_start \\(=12345\\) must be less than listen_port_end \\(=12345\\)");

  param->set_value("65536");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, params),
      "Invalid listen_port_end config value: 65536");
}

TEST_F(InputAssignerTests, port_range) {
  mesos::Parameters params;

  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_START);
  const size_t start_port = 100;
  param->set_value(std::to_string(start_port));

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_END);
  const size_t end_port = 200;
  param->set_value(std::to_string(end_port));

  stats::PortRangeAssigner pra(mock_runner, params);
  mesos::ContainerID ci1 = container_id("cid1"), ci2 = container_id("cid2");
  mesos::ExecutorInfo ei1 = exec_info("fid1", "eid1"), ei2 = exec_info("fid2", "eid2");
  const std::string host1("host1"), host2("host2");
  const size_t port1 = 100, port2 = 101, port3 = 102;

  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  // Registration of ci1/ei1 fails
  EXPECT_CALL(*mock_runner, create_port_reader(port1)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_TRUE(pra.register_container(ci1, ei1).isError());

  // Registration of ci1/ei1 succeeds (against same port; it was put back after the fail)
  EXPECT_CALL(*mock_runner, create_port_reader(port1)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint(host1, port1)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)))
    .WillOnce(Return(try_endpoint("ignored1", 54321)));
  Try<stats::UDPEndpoint> endpt = pra.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 creates a new separate reader against the next port in the pool
  EXPECT_CALL(*mock_runner, create_port_reader(port2)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint(host2, port2)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)))
    .WillOnce(Return(try_endpoint("ignored2", 54321)));
  endpt = pra.register_container(ci2, ei2);
  EXPECT_EQ(host2, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2 with successful endpoint, which is returned
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(Return(try_endpoint(host2, port2)));
  pra.unregister_container(ci2);
  // Unregister same thing again, no reader access this time
  pra.unregister_container(ci2);

  // Unregister ci1/ei1 with broken endpoint, which cannot be returned to the pool
  EXPECT_CALL(*mock_reader1, endpoint())
    .WillOnce(Return(Try<stats::UDPEndpoint>(Error("ignored"))));
  pra.unregister_container(ci1);
  // Unregister same thing again, no reader access this time
  pra.unregister_container(ci1);

  // Then re-register ci1/ei1, which gets port2 this time since port1 couldn't be freed
  EXPECT_CALL(*mock_runner, create_port_reader(port2)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint("ignored1", 54321)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)))
    .WillOnce(Return(try_endpoint(host1, port2)));
  pra.register_container(ci1, ei1);

  // And re-register ci2/ei2, which now gets port3
  EXPECT_CALL(*mock_runner, create_port_reader(port3)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint("ignored2", 54321)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)))
    .WillOnce(Return(try_endpoint(host2, port3)));
  pra.register_container(ci2, ei2);
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
