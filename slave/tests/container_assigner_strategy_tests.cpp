#include <glog/logging.h>

#include <thread>

#include "container_assigner_strategy.hpp"
#include "mock_container_reader.hpp"
#include "mock_io_runner.hpp"

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;

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
  inline mesos::slave::ContainerState container_state(
      const std::string& cid, const std::string& fid, const std::string& eid) {
    mesos::slave::ContainerState state;
    *state.mutable_container_id() = container_id(cid);
    *state.mutable_executor_info() = exec_info(fid, eid);
    return state;
  }

  class ContainerInfo {
   public:
    ContainerInfo(
        size_t index,
        const std::string& cid,
        const mesos::ExecutorInfo& ei,
        const stats::UDPEndpoint& endpoint)
      : index(index), cid(container_id(cid)), ei(ei), endpoint(endpoint) { }

    const size_t index;
    const mesos::ContainerID cid;
    const mesos::ExecutorInfo ei;
    const stats::UDPEndpoint endpoint;
  };

  // Simple boilerplate utility to simulate IORunner's async scheduler, which just runs the
  // function synchronously
  void execute(std::function<void()> func) {
    func();
  }

  const std::string PATH("SOME PATH");
}

MATCHER_P(ContainerIdMatch, proto_value, "mesos::ContainerID") {
  return arg.value() == proto_value.value();
}

MATCHER_P(ContainerStrMatch, str_value, "mesos::ContainerID") {
  return arg.value() == str_value;
}

MATCHER_P(ExecInfoMatch, proto_value, "mesos::ExecutorInfo") {
  return arg.executor_id().value() == proto_value.executor_id().value()
    && arg.framework_id().value() == proto_value.framework_id().value();
}

class ContainerAssignerStategyTests : public ::testing::Test {
 public:
  ContainerAssignerStategyTests()
    : mock_reader1(new MockContainerReader),
      mock_reader2(new MockContainerReader),
      mock_runner(new MockIORunner) { }

 protected:
  std::shared_ptr<MockContainerReader> mock_reader1, mock_reader2;
  std::shared_ptr<MockIORunner> mock_runner;
};

TEST_F(ContainerAssignerStategyTests, single_port_bad_port) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("host1");
  EXPECT_DETH(new stats::SinglePortStrategy(mock_runner, params),
      "Invalid listen_port config value: 0");

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  param->set_value("0");
  EXPECT_DETH(new stats::SinglePortStrategy(mock_runner, params),
      "Invalid listen_port config value: 0");

  param->set_value("65536");
  EXPECT_DETH(new stats::SinglePortStrategy(mock_runner, params),
      "Invalid listen_port config value: 65536");
}

TEST_F(ContainerAssignerStategyTests, single_port) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  const size_t create_port = 9999;
  param->set_value(std::to_string(create_port)); // passed to PortReader creation, otherwise unused

  stats::SinglePortStrategy strategy(mock_runner, params);
  mesos::ContainerID ci1 = container_id("cid1"),
    ci2 = container_id("cid2"),
    ci3 = container_id("cid3");
  mesos::ExecutorInfo ei1 = exec_info("fid1", "eid1"),
    ei2 = exec_info("fid2", "eid2"),
    ei3 = exec_info("fid3", "eid3");
  const std::string host1("host1");
  const size_t port1 = 1234, port2 = 2345;

  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  // Registration of ci1/ei1 fails
  EXPECT_CALL(*mock_runner, create_container_reader(create_port)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_TRUE(strategy.register_container(ci1, ei1).isError());

  // Registration of ci1/ei1 creates reader and succeeds
  EXPECT_CALL(*mock_runner, create_container_reader(create_port)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint("ignored", 0)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)));
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(Return(try_endpoint(host1, port1)));
  Try<stats::UDPEndpoint> endpt = strategy.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 reuses reader and succeeds
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)));
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(Return(try_endpoint(host1, port2)));
  endpt = strategy.register_container(ci2, ei2);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2. Endpoint fails but deregistration proceeds anyway
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(Return(
          Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_CALL(*mock_reader1, unregister_container(ContainerIdMatch(ci2)));
  strategy.unregister_container(ci2);

  // Insertion of ci3/ei3 fails to get endpoint for comparison (just continues with a warning)
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(
      Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci3), ExecInfoMatch(ei3)));
  strategy.insert_container(ci3, ei3, stats::UDPEndpoint(host1, port1));

  // Insertion of ci3/ei3 against mismatched port number is ignored (with a warning)
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(Return(try_endpoint(host1, port2)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci3), ExecInfoMatch(ei3)));
  strategy.insert_container(ci3, ei3, stats::UDPEndpoint(host1, port1));

  // Insert ci3/ei3 succeeds
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(Return(try_endpoint(host1, port1)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci3), ExecInfoMatch(ei3)));
  strategy.insert_container(ci3, ei3, stats::UDPEndpoint(host1, port1));

  // Unregister ci3/ei3 against failed endpoint is ignored (with a warning)
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(
      Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_CALL(*mock_reader1, unregister_container(ContainerIdMatch(ci3)));
  strategy.unregister_container(ci3);

  // Unregister ci3/ei3 succeeds
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(Return(try_endpoint(host1, port1)));
  EXPECT_CALL(*mock_reader1, unregister_container(ContainerIdMatch(ci3)));
  strategy.unregister_container(ci3);
}

// ---

TEST_F(ContainerAssignerStategyTests, ephemeral_port) {
  stats::EphemeralPortStrategy strategy(mock_runner);
  mesos::ContainerID ci1 = container_id("cid1"),
    ci2 = container_id("cid2"),
    ci3 = container_id("cid3");
  mesos::ExecutorInfo ei1 = exec_info("fid1", "eid1"),
    ei2 = exec_info("fid2", "eid2"),
    ei3 = exec_info("fid3", "eid3");
  const std::string host1("host1"), host2("host2");
  const size_t port1 = 1234, port2 = 4321;

  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  // Registration of ci1/ei1 fails
  EXPECT_CALL(*mock_runner, create_container_reader(0)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_TRUE(strategy.register_container(ci1, ei1).isError());

  // Registration of ci1/ei1 creates reader and succeeds
  EXPECT_CALL(*mock_runner, create_container_reader(0)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint(host1, port1)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)));
  Try<stats::UDPEndpoint> endpt = strategy.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 creates a new separate reader
  EXPECT_CALL(*mock_runner, create_container_reader(0)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint(host2, port2)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)));
  endpt = strategy.register_container(ci2, ei2);
  EXPECT_EQ(host2, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(Return(try_endpoint(host2, port2)));
  strategy.unregister_container(ci2);
  // Unregister same thing again, no reader access this time
  strategy.unregister_container(ci2);

  // Unregister ci1/ei1 with broken endpoint. Still works.
  EXPECT_CALL(*mock_reader1, endpoint())
    .WillOnce(Return(Try<stats::UDPEndpoint>(Error("ignored"))));
  strategy.unregister_container(ci1);
  // Unregister same thing again, no reader access this time
  strategy.unregister_container(ci1);

  // Insertion of ci3/ei3 fails to get endpoint
  EXPECT_CALL(*mock_runner, create_container_reader(port1)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  strategy.insert_container(ci3, ei3, stats::UDPEndpoint("ignored", port1));

  // Insert ci3/ei3 succeeds
  EXPECT_CALL(*mock_runner, create_container_reader(port2)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint("ignored", 1231231)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci3), ExecInfoMatch(ei3)));
  strategy.insert_container(ci3, ei3, stats::UDPEndpoint(host2, port2));

  // Unregister ci3/ei3 with broken endpoint. Still works.
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(
      Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  strategy.unregister_container(ci3);
  // Unregister ci3/ei3 again, no reader access this time
  strategy.unregister_container(ci3);
}

// ---

TEST_F(ContainerAssignerStategyTests, port_range_bad_ports) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("host1");
  EXPECT_DETH(new stats::PortRangeStrategy(mock_runner, params),
      "Invalid listen_port_start config value: 0");

  // Test listen_port_start (end unset)
  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_START);
  param->set_value("0");
  EXPECT_DETH(new stats::PortRangeStrategy(mock_runner, params),
      "Invalid listen_port_start config value: 0");

  param->set_value("65536");
  EXPECT_DETH(new stats::PortRangeStrategy(mock_runner, params),
      "Invalid listen_port_start config value: 65536");

  // Set a valid start to test listen_port_end
  param->set_value("12345");
  EXPECT_DETH(new stats::PortRangeStrategy(mock_runner, params),
      "Invalid listen_port_end config value: 0");

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_END);
  param->set_value("0");
  EXPECT_DETH(new stats::PortRangeStrategy(mock_runner, params),
      "Invalid listen_port_end config value: 0");

  param->set_value("12344");
  EXPECT_DETH(new stats::PortRangeStrategy(mock_runner, params),
      "listen_port_start \\(=12345\\) must be less than listen_port_end \\(=12344\\)");

  param->set_value("12345");
  EXPECT_DETH(new stats::PortRangeStrategy(mock_runner, params),
      "listen_port_start \\(=12345\\) must be less than listen_port_end \\(=12345\\)");

  param->set_value("65536");
  EXPECT_DETH(new stats::PortRangeStrategy(mock_runner, params),
      "Invalid listen_port_end config value: 65536");
}

TEST_F(ContainerAssignerStategyTests, port_range) {
  mesos::Parameters params;

  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_START);
  const size_t start_port = 100;
  param->set_value(std::to_string(start_port));

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_END);
  const size_t end_port = 200;
  param->set_value(std::to_string(end_port));

  stats::PortRangeStrategy strategy(mock_runner, params);
  mesos::ContainerID ci1 = container_id("cid1"),
    ci2 = container_id("cid2"),
    ci3 = container_id("cid3");
  mesos::ExecutorInfo ei1 = exec_info("fid1", "eid1"),
    ei2 = exec_info("fid2", "eid2"),
    ei3 = exec_info("fid3", "eid3");
  const std::string host1("host1"), host2("host2");
  const size_t port1 = 100, port2 = 101, port3 = 102;

  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  // Registration of ci1/ei1 fails
  EXPECT_CALL(*mock_runner, create_container_reader(port1)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  EXPECT_TRUE(strategy.register_container(ci1, ei1).isError());

  // Registration of ci1/ei1 succeeds (against same port; it was put back after the fail)
  EXPECT_CALL(*mock_runner, create_container_reader(port1)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint(host1, port1)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)));
  Try<stats::UDPEndpoint> endpt = strategy.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 creates a new separate reader against the next port in the pool
  EXPECT_CALL(*mock_runner, create_container_reader(port2)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint(host2, port2)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)));
  endpt = strategy.register_container(ci2, ei2);
  EXPECT_EQ(host2, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2 with successful endpoint, which is returned
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(Return(try_endpoint(host2, port2)));
  strategy.unregister_container(ci2);
  // Unregister same thing again, no reader access this time
  strategy.unregister_container(ci2);

  // Unregister ci1/ei1 with broken endpoint, which cannot be returned to the pool
  EXPECT_CALL(*mock_reader1, endpoint())
    .WillOnce(Return(Try<stats::UDPEndpoint>(Error("ignored"))));
  strategy.unregister_container(ci1);
  // Unregister same thing again, no reader access this time
  strategy.unregister_container(ci1);

  // Then re-register ci1/ei1, which gets port2 this time since port1 couldn't be freed
  EXPECT_CALL(*mock_runner, create_container_reader(port2)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint(host1, port2)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)));
  strategy.register_container(ci1, ei1);

  // And re-register ci2/ei2, which now gets port3
  EXPECT_CALL(*mock_runner, create_container_reader(port3)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint(host2, port3)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)));
  strategy.register_container(ci2, ei2);

  // Insertion of ci3/ei3 against port3 doesn't proceed since it's already taken
  strategy.insert_container(ci3, ei3, stats::UDPEndpoint("ignored", port3));

  // Unregister ci2 to recover port3, then re-attempt insert on port3, which fails to open
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(Return(try_endpoint(host2, port3)));
  strategy.unregister_container(ci2);

  // port3 fails to open. port3 should be returned to the pool before the call exits w/o registering
  EXPECT_CALL(*mock_runner, create_container_reader(port3)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  strategy.insert_container(ci3, ei3, stats::UDPEndpoint("ignored", port3));

  // Finally, try again and get port3 successfully this time
  EXPECT_CALL(*mock_runner, create_container_reader(port3)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint("ignored", 1231231)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci3), ExecInfoMatch(ei3)));
  strategy.insert_container(ci3, ei3, stats::UDPEndpoint("ignored", port3));

  // Unregister ci3/ei3 with broken endpoint. Still works.
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(
      Return(Try<stats::UDPEndpoint>(Error("test fail"))));
  strategy.unregister_container(ci3);
  // Unregister ci3/ei3 again, no reader access this time
  strategy.unregister_container(ci3);
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
