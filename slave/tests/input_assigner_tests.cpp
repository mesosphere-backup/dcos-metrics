#include <glog/logging.h>

#include <thread>

#include "input_assigner.hpp"
#include "mock_input_state_cache.hpp"
#include "mock_port_reader.hpp"
#include "mock_port_runner.hpp"

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

class InputAssignerTests : public ::testing::Test {
 public:
  InputAssignerTests()
    : mock_reader1(new MockPortReader),
      mock_reader2(new MockPortReader),
      mock_runner(new MockPortRunner),
      mock_state_cache(new MockInputStateCache) { }

 protected:
  std::shared_ptr<MockPortReader> mock_reader1, mock_reader2;
  std::shared_ptr<MockPortRunner> mock_runner;
  std::shared_ptr<MockInputStateCache> mock_state_cache;
};

TEST_F(InputAssignerTests, single_port_bad_port) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("host1");
  EXPECT_DETH(new stats::SinglePortAssigner(mock_runner, mock_state_cache, params),
      "Invalid listen_port config value: 0");

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  param->set_value("0");
  EXPECT_DETH(new stats::SinglePortAssigner(mock_runner, mock_state_cache, params),
      "Invalid listen_port config value: 0");

  param->set_value("65536");
  EXPECT_DETH(new stats::SinglePortAssigner(mock_runner, mock_state_cache, params),
      "Invalid listen_port config value: 65536");
}

TEST_F(InputAssignerTests, single_port) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  const size_t create_port = 9999;
  param->set_value(std::to_string(create_port)); // passed to PortReader creation, otherwise unused

  stats::SinglePortAssigner spa(mock_runner, mock_state_cache, params);
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
  EXPECT_CALL(*mock_state_cache,
      add_container(ContainerIdMatch(ci1), stats::UDPEndpoint(host1, port1)));
  Try<stats::UDPEndpoint> endpt = spa.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 reuses reader and succeeds
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)))
    .WillOnce(Return(try_endpoint(host1, port2)));
  EXPECT_CALL(*mock_state_cache,
      add_container(ContainerIdMatch(ci2), stats::UDPEndpoint(host1, port2)));
  endpt = spa.register_container(ci2, ei2);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2
  EXPECT_CALL(*mock_reader1, endpoint()).WillOnce(Return(try_endpoint(host1, port2)));
  EXPECT_CALL(*mock_reader1, unregister_container(ContainerIdMatch(ci2)));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci2)));
  spa.unregister_container(ci2);
}

TEST_F(InputAssignerTests, single_port_recovery) {
  // Permutations:
  //   | recovery | disk | registered || expect result
  // --+----------+------+------------++--------------------------
  // 1 | Y        | Y    | Y          || insert with disk endpoint (#1)
  // 2 | Y        | Y    | N          || insert with disk endpoint (#1)
  // 3 | Y        | N    | Y          || register without endpoint (#3)
  // 4 | Y        | N    | N          || register without endpoint (#3)
  // 5 | N        | Y    | Y          || remove/unregister (#2)
  // 6 | N        | Y    | N          || remove/unregister (#2)
  // 7 | N        | N    | Y          || no-op
  // 8 | N        | N    | N          || (doesn't exist!)

  const size_t create_port = 9999;
  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  std::list<mesos::slave::ContainerState> recover_input; // Y**

  recover_input.push_back(container_state("YYY", "fid1", "eid1"));
  recover_input.push_back(container_state("YYN", "fid2", "eid2"));
  recover_input.push_back(container_state("YNY", "fid3", "eid3"));
  recover_input.push_back(container_state("YNN", "fid4", "eid4"));

  stats::container_id_map<stats::UDPEndpoint> disk_input; // *Y*

  disk_input.insert({container_id("YYY"), stats::UDPEndpoint("host1", 1)});
  disk_input.insert({container_id("YYN"), stats::UDPEndpoint("host2", 2)});
  disk_input.insert({container_id("NYY"), stats::UDPEndpoint("host5", 5)});
  disk_input.insert({container_id("NYN"), stats::UDPEndpoint("host6", 6)});

  // set up expected outcomes when we call recover:

  EXPECT_CALL(*mock_state_cache, get_containers()).WillOnce(Return(disk_input));
  EXPECT_CALL(*mock_state_cache, path()).WillOnce(ReturnRef(PATH));
  EXPECT_CALL(*mock_reader1, endpoint())
    .WillRepeatedly(Return(try_endpoint("ignored", create_port)));

  // 1: init_reader (runner.create_reader, reader.open), reader.endpoint, reader.register
  EXPECT_CALL(*mock_runner, create_port_reader(create_port)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint("ignored", 123)));

  EXPECT_CALL(*mock_reader1, register_container(
          ContainerStrMatch("YYY"), ExecInfoMatch(exec_info("fid1", "eid1"))))
    .WillOnce(Return(try_endpoint("ignored1", 54321)));

  // 2: reader.endpoint, reader.register
  EXPECT_CALL(*mock_reader1, register_container(
          ContainerStrMatch("YYN"), ExecInfoMatch(exec_info("fid2", "eid2"))))
    .WillOnce(Return(try_endpoint("ignored1", 54321)));

  // 3,4: reader.register, cache.add
  EXPECT_CALL(*mock_reader1, register_container(
          ContainerStrMatch("YNY"), ExecInfoMatch(exec_info("fid3", "eid3"))))
    .WillOnce(Return(try_endpoint("registered3", 3)));
  EXPECT_CALL(*mock_state_cache, add_container(
          ContainerStrMatch("YNY"), stats::UDPEndpoint("registered3", 3)));

  EXPECT_CALL(*mock_reader1, register_container(
          ContainerStrMatch("YNN"), ExecInfoMatch(exec_info("fid4", "eid4"))))
    .WillOnce(Return(try_endpoint("registered4", 4)));
  EXPECT_CALL(*mock_state_cache, add_container(
          ContainerStrMatch("YNN"), stats::UDPEndpoint("registered4", 4)));

  // 5,6: reader.endpoint, reader.unregister, cache.remove
  EXPECT_CALL(*mock_reader1, unregister_container(ContainerStrMatch("NYY")));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerStrMatch("NYY")));

  EXPECT_CALL(*mock_reader1, unregister_container(ContainerStrMatch("NYN")));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerStrMatch("NYN")));

  // 7: nil

  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  param->set_value(std::to_string(create_port)); // passed to PortReader creation, otherwise unused

  stats::SinglePortAssigner spa(mock_runner, mock_state_cache, params);
  spa.recover_containers(recover_input);
}

TEST_F(InputAssignerTests, single_port_multithread) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT);
  const std::string host = "host";
  const size_t port = 9999;
  param->set_value(std::to_string(port));

  stats::SinglePortAssigner spa(mock_runner, mock_state_cache, params);
  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));
  EXPECT_CALL(*mock_runner, create_port_reader(port)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillRepeatedly(Return(try_endpoint(host, port)));
  EXPECT_CALL(*mock_reader1, endpoint()).WillRepeatedly(Return(try_endpoint(host, port)));
  EXPECT_CALL(*mock_reader1, register_container(_, _))
    .WillRepeatedly(Return(try_endpoint("ignored", 0)));
  EXPECT_CALL(*mock_state_cache, add_container(_, _)).Times(AtLeast(1));
  EXPECT_CALL(*mock_reader1, unregister_container(_)).WillRepeatedly(Return());
  EXPECT_CALL(*mock_state_cache, remove_container(_)).Times(AtLeast(1));

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
  stats::EphemeralPortAssigner epa(mock_runner, mock_state_cache);
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
  EXPECT_CALL(*mock_state_cache,
      add_container(ContainerIdMatch(ci1), stats::UDPEndpoint(host1, port1)));
  Try<stats::UDPEndpoint> endpt = epa.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 creates a new separate reader
  EXPECT_CALL(*mock_runner, create_port_reader(0)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint(host2, port2)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)))
    .WillOnce(Return(try_endpoint("ignored2", 54321)));
  EXPECT_CALL(*mock_state_cache,
      add_container(ContainerIdMatch(ci2), stats::UDPEndpoint(host2, port2)));
  endpt = epa.register_container(ci2, ei2);
  EXPECT_EQ(host2, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(Return(try_endpoint(host2, port2)));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci2)));
  epa.unregister_container(ci2);
  // Unregister same thing again, no reader access this time
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci2)));
  epa.unregister_container(ci2);

  // Unregister ci1/ei1 with broken endpoint. Still works.
  EXPECT_CALL(*mock_reader1, endpoint())
    .WillOnce(Return(Try<stats::UDPEndpoint>(Error("ignored"))));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci1)));
  epa.unregister_container(ci1);
  // Unregister same thing again, no reader access this time
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci1)));
  epa.unregister_container(ci1);
}

TEST_F(InputAssignerTests, ephemeral_recovery) {
  stats::EphemeralPortAssigner epa(mock_runner, mock_state_cache);

  // Permutations:
  //   | recovery | disk | registered || expect result
  // --+----------+------+------------++--------------------------
  // 1 | Y        | Y    | Y          || insert with disk endpoint (#1)
  // 2 | Y        | Y    | N          || insert with disk endpoint (#1)
  // 3 | Y        | N    | Y          || register without endpoint (#3)
  // 4 | Y        | N    | N          || register without endpoint (#3)
  // 5 | N        | Y    | Y          || remove/unregister (#2)
  // 6 | N        | Y    | N          || remove/unregister (#2)
  // 7 | N        | N    | Y          || no-op
  // 8 | N        | N    | N          || (doesn't exist!)

  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  std::list<mesos::slave::ContainerState> recover_input; // Y**

  recover_input.push_back(container_state("YYY", "fid1", "eid1"));
  recover_input.push_back(container_state("YYN", "fid2", "eid2"));
  recover_input.push_back(container_state("YNY", "fid3", "eid3"));
  recover_input.push_back(container_state("YNN", "fid4", "eid4"));

  stats::container_id_map<stats::UDPEndpoint> disk_input; // *Y*

  disk_input.insert({container_id("YYY"), stats::UDPEndpoint("host1", 1)});
  disk_input.insert({container_id("YYN"), stats::UDPEndpoint("host2", 2)});
  disk_input.insert({container_id("NYY"), stats::UDPEndpoint("host5", 5)});
  disk_input.insert({container_id("NYN"), stats::UDPEndpoint("host6", 6)});

  std::vector<ContainerInfo> registered; // **Y

  registered.push_back(
      ContainerInfo(1, "YYY", exec_info("fid1", "eid1"), stats::UDPEndpoint("host1", 1)));
  registered.push_back(
      ContainerInfo(3, "YNY", exec_info("fid3", "eid3"), stats::UDPEndpoint("host3", 3)));
  registered.push_back(
      ContainerInfo(5, "NYY", exec_info("fid5", "eid5"), stats::UDPEndpoint("host5", 5)));
  registered.push_back(
      ContainerInfo(7, "NNY", exec_info("fid7", "eid7"), stats::UDPEndpoint("host7", 7)));

  // make a bunch of unique readers, one per scenario
  std::vector<std::shared_ptr<MockPortReader>> mock_readers;
  for (size_t i = 0; i <= 8; ++i) {
    mock_readers.push_back(std::shared_ptr<MockPortReader>(new MockPortReader));
  }

  // register the 'registered' containers:

  for (auto info : registered) {
    EXPECT_CALL(*mock_runner, create_port_reader(0))
      .WillOnce(Return(mock_readers[info.index]));
    EXPECT_CALL(*mock_readers[info.index], open())
      .WillOnce(Return(try_endpoint(info.endpoint.host, info.endpoint.port)));
    EXPECT_CALL(*mock_readers[info.index],
        register_container(ContainerIdMatch(info.cid), ExecInfoMatch(info.ei)))
      .WillOnce(Return(try_endpoint("ignored", 123)));
    EXPECT_CALL(*mock_state_cache, add_container(ContainerIdMatch(info.cid), info.endpoint));
    Try<stats::UDPEndpoint> endpt = epa.register_container(info.cid, info.ei);
    EXPECT_EQ(info.endpoint.host, endpt.get().host);
    EXPECT_EQ(info.endpoint.port, endpt.get().port);
  }


  // set up expected outcomes when we call recover:

  EXPECT_CALL(*mock_state_cache, get_containers()).WillOnce(Return(disk_input));
  EXPECT_CALL(*mock_state_cache, path()).WillOnce(ReturnRef(PATH));

  // 1,2:
  EXPECT_CALL(*mock_runner, create_port_reader(1)).WillOnce(Return(mock_readers[1]));
  EXPECT_CALL(*mock_readers[1], open()).WillOnce(Return(try_endpoint("ignored1a", 123)));
  EXPECT_CALL(*mock_readers[1], register_container(
          ContainerStrMatch("YYY"), ExecInfoMatch(exec_info("fid1", "eid1"))))
    .WillOnce(Return(try_endpoint("ignored1b", 54321)));

  EXPECT_CALL(*mock_runner, create_port_reader(2)).WillOnce(Return(mock_readers[2]));
  EXPECT_CALL(*mock_readers[2], open()).WillOnce(Return(try_endpoint("ignored2a", 123)));
  EXPECT_CALL(*mock_readers[2], register_container(
          ContainerStrMatch("YYN"), ExecInfoMatch(exec_info("fid2", "eid2"))))
    .WillOnce(Return(try_endpoint("ignored2b", 54321)));

  // 3: found in mapping, reused for cache.add
  EXPECT_CALL(*mock_readers[3], endpoint()).WillOnce(Return(try_endpoint("host3", 3)));
  EXPECT_CALL(*mock_state_cache, add_container(
          ContainerStrMatch("YNY"), stats::UDPEndpoint("host3", 3)));

  // 4: created from scratch as a new registration
  EXPECT_CALL(*mock_runner, create_port_reader(0))
    .WillOnce(Return(mock_readers[4]));
  EXPECT_CALL(*mock_readers[4], open()).WillOnce(Return(try_endpoint("host4", 4)));
  EXPECT_CALL(*mock_readers[4], register_container(
          ContainerStrMatch("YNN"), ExecInfoMatch(exec_info("fid4", "eid4"))))
    .WillOnce(Return(try_endpoint("ignored4", 432)));
  EXPECT_CALL(*mock_state_cache, add_container(
    ContainerStrMatch("YNN"), stats::UDPEndpoint("host4", 4)));

  // 5: endpoint lookup then cache.remove
  EXPECT_CALL(*mock_readers[5], endpoint()).WillOnce(Return(try_endpoint("ignored5", 543)));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerStrMatch("NYY")));

  // 6: cache.remove only (wasn't registered so no endpoint lookup)
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerStrMatch("NYN")));

  // 7: nil

  epa.recover_containers(recover_input);
}

TEST_F(InputAssignerTests, ephemeral_port_multithread) {
  const std::string host = "host";
  const size_t port = 9999;

  stats::EphemeralPortAssigner epa(mock_runner, mock_state_cache);
  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));
  EXPECT_CALL(*mock_runner, create_port_reader(0)).WillRepeatedly(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillRepeatedly(Return(try_endpoint(host, port)));
  EXPECT_CALL(*mock_reader1, endpoint()).WillRepeatedly(Return(try_endpoint(host, port)));
  EXPECT_CALL(*mock_reader1, register_container(_, _))
    .WillRepeatedly(Return(try_endpoint("ignored", 0)));
  EXPECT_CALL(*mock_state_cache, add_container(_, _)).Times(AtLeast(1));
  EXPECT_CALL(*mock_reader1, unregister_container(_)).WillRepeatedly(Return());
  EXPECT_CALL(*mock_state_cache, remove_container(_)).Times(AtLeast(1));

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
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, mock_state_cache, params),
      "Invalid listen_port_start config value: 0");

  // Test listen_port_start (end unset)
  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_START);
  param->set_value("0");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, mock_state_cache, params),
      "Invalid listen_port_start config value: 0");

  param->set_value("65536");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, mock_state_cache, params),
      "Invalid listen_port_start config value: 65536");

  // Set a valid start to test listen_port_end
  param->set_value("12345");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, mock_state_cache, params),
      "Invalid listen_port_end config value: 0");

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_END);
  param->set_value("0");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, mock_state_cache, params),
      "Invalid listen_port_end config value: 0");

  param->set_value("12344");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, mock_state_cache, params),
      "listen_port_start \\(=12345\\) must be less than listen_port_end \\(=12344\\)");

  param->set_value("12345");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, mock_state_cache, params),
      "listen_port_start \\(=12345\\) must be less than listen_port_end \\(=12345\\)");

  param->set_value("65536");
  EXPECT_DETH(new stats::PortRangeAssigner(mock_runner, mock_state_cache, params),
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

  stats::PortRangeAssigner pra(mock_runner, mock_state_cache, params);
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
  EXPECT_CALL(*mock_state_cache,
      add_container(ContainerIdMatch(ci1), stats::UDPEndpoint(host1, port1)));
  Try<stats::UDPEndpoint> endpt = pra.register_container(ci1, ei1);
  EXPECT_EQ(host1, endpt.get().host);
  EXPECT_EQ(port1, endpt.get().port);

  // Registration of ci2/ei2 creates a new separate reader against the next port in the pool
  EXPECT_CALL(*mock_runner, create_port_reader(port2)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint(host2, port2)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)))
    .WillOnce(Return(try_endpoint("ignored2", 54321)));
  EXPECT_CALL(*mock_state_cache,
      add_container(ContainerIdMatch(ci2), stats::UDPEndpoint(host2, port2)));
  endpt = pra.register_container(ci2, ei2);
  EXPECT_EQ(host2, endpt.get().host);
  EXPECT_EQ(port2, endpt.get().port);

  // Unregister ci2/ei2 with successful endpoint, which is returned
  EXPECT_CALL(*mock_reader2, endpoint()).WillOnce(Return(try_endpoint(host2, port2)));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci2)));
  pra.unregister_container(ci2);
  // Unregister same thing again, no reader access this time
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci2)));
  pra.unregister_container(ci2);

  // Unregister ci1/ei1 with broken endpoint, which cannot be returned to the pool
  EXPECT_CALL(*mock_reader1, endpoint())
    .WillOnce(Return(Try<stats::UDPEndpoint>(Error("ignored"))));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci1)));
  pra.unregister_container(ci1);
  // Unregister same thing again, no reader access this time
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerIdMatch(ci1)));
  pra.unregister_container(ci1);

  // Then re-register ci1/ei1, which gets port2 this time since port1 couldn't be freed
  EXPECT_CALL(*mock_runner, create_port_reader(port2)).WillOnce(Return(mock_reader1));
  EXPECT_CALL(*mock_reader1, open()).WillOnce(Return(try_endpoint(host1, port2)));
  EXPECT_CALL(*mock_reader1, register_container(ContainerIdMatch(ci1), ExecInfoMatch(ei1)))
    .WillOnce(Return(try_endpoint("ignored1", 54321)));
  EXPECT_CALL(*mock_state_cache,
      add_container(ContainerIdMatch(ci1), stats::UDPEndpoint(host1, port2)));
  pra.register_container(ci1, ei1);

  // And re-register ci2/ei2, which now gets port3
  EXPECT_CALL(*mock_runner, create_port_reader(port3)).WillOnce(Return(mock_reader2));
  EXPECT_CALL(*mock_reader2, open()).WillOnce(Return(try_endpoint(host2, port3)));
  EXPECT_CALL(*mock_reader2, register_container(ContainerIdMatch(ci2), ExecInfoMatch(ei2)))
    .WillOnce(Return(try_endpoint("ignored2", 54321)));
  EXPECT_CALL(*mock_state_cache,
      add_container(ContainerIdMatch(ci2), stats::UDPEndpoint(host2, port3)));
  pra.register_container(ci2, ei2);
}

TEST_F(InputAssignerTests, port_range_recovery) {
  mesos::Parameters params;

  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_START);
  const size_t start_port = 1;
  param->set_value(std::to_string(start_port));

  param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_END);
  const size_t end_port = 100;
  param->set_value(std::to_string(end_port));

  stats::PortRangeAssigner pra(mock_runner, mock_state_cache, params);

  // Permutations:
  //   | recovery | disk | registered || expect result
  // --+----------+------+------------++--------------------------
  // 1 | Y        | Y    | Y          || insert with disk endpoint (#1)
  // 2 | Y        | Y    | N          || insert with disk endpoint (#1)
  // 3 | Y        | N    | Y          || register without endpoint (#3)
  // 4 | Y        | N    | N          || register without endpoint (#3)
  // 5 | N        | Y    | Y          || remove/unregister (#2)
  // 6 | N        | Y    | N          || remove/unregister (#2)
  // 7 | N        | N    | Y          || no-op
  // 8 | N        | N    | N          || (doesn't exist!)

  EXPECT_CALL(*mock_runner, dispatch(_)).WillRepeatedly(Invoke(execute));

  std::list<mesos::slave::ContainerState> recover_input; // Y**

  recover_input.push_back(container_state("YYY", "fid1", "eid1"));
  recover_input.push_back(container_state("YYN", "fid2", "eid2"));
  recover_input.push_back(container_state("YNY", "fid3", "eid3"));
  recover_input.push_back(container_state("YNN", "fid4", "eid4"));

  stats::container_id_map<stats::UDPEndpoint> disk_input; // *Y*

  disk_input.insert({container_id("YYY"), stats::UDPEndpoint("host1", 1)});// 'already being used'
  disk_input.insert({container_id("YYN"), stats::UDPEndpoint("host2", 12)});
  disk_input.insert({container_id("NYY"), stats::UDPEndpoint("host5", 3)});// 'already being used'
  disk_input.insert({container_id("NYN"), stats::UDPEndpoint("host6", 16)});

  std::vector<ContainerInfo> registered; // **Y

  // ports are just tied to the order in which things are added:
  registered.push_back(
      ContainerInfo(1, "YYY", exec_info("fid1", "eid1"), stats::UDPEndpoint("host1", 1)));
  registered.push_back(
      ContainerInfo(3, "YNY", exec_info("fid3", "eid3"), stats::UDPEndpoint("host3", 2)));
  registered.push_back(
      ContainerInfo(5, "NYY", exec_info("fid5", "eid5"), stats::UDPEndpoint("host5", 3)));
  registered.push_back(
      ContainerInfo(7, "NNY", exec_info("fid7", "eid7"), stats::UDPEndpoint("host7", 4)));

  // make a bunch of unique readers, one per scenario
  std::vector<std::shared_ptr<MockPortReader>> mock_readers;
  for (size_t i = 0; i <= 8; ++i) {
    mock_readers.push_back(std::shared_ptr<MockPortReader>(new MockPortReader));
  }

  // register the 'registered' containers (which will get ports 1 thru 4):

  for (auto info : registered) {
    EXPECT_CALL(*mock_runner, create_port_reader(info.endpoint.port))
      .WillOnce(Return(mock_readers[info.index]));
    EXPECT_CALL(*mock_readers[info.index], open())
      .WillOnce(Return(try_endpoint(info.endpoint.host, info.endpoint.port)));
    EXPECT_CALL(*mock_readers[info.index],
        register_container(ContainerIdMatch(info.cid), ExecInfoMatch(info.ei)))
      .WillOnce(Return(try_endpoint("ignored", 123)));
    EXPECT_CALL(*mock_state_cache,
        add_container(ContainerIdMatch(info.cid), info.endpoint));
    Try<stats::UDPEndpoint> endpt = pra.register_container(info.cid, info.ei);
    EXPECT_EQ(info.endpoint.host, endpt.get().host);
    EXPECT_EQ(info.endpoint.port, endpt.get().port);
  }

  // set up expected outcomes when we call recover:

  EXPECT_CALL(*mock_state_cache, get_containers()).WillOnce(Return(disk_input));
  EXPECT_CALL(*mock_state_cache, path()).WillOnce(ReturnRef(PATH));

  // 1: port 1 already being used, errors out and doesn't do anything

  // 2: fresh registration on port 12
  EXPECT_CALL(*mock_runner, create_port_reader(12)).WillOnce(Return(mock_readers[2]));
  EXPECT_CALL(*mock_readers[2], open()).WillOnce(Return(try_endpoint("ignored2a", 123)));
  EXPECT_CALL(*mock_readers[2], register_container(
          ContainerStrMatch("YYN"), ExecInfoMatch(exec_info("fid2", "eid2"))))
    .WillOnce(Return(try_endpoint("ignored2b", 54321)));

  // 3: found in mapping, reused for cache.add
  EXPECT_CALL(*mock_readers[3], endpoint()).WillOnce(Return(try_endpoint("host3", 3)));
  EXPECT_CALL(*mock_state_cache, add_container(
          ContainerStrMatch("YNY"), stats::UDPEndpoint("host3", 3)));

  // 4: created from scratch as a new registration, against port 3 which was just freed (#5 below happens first)
  EXPECT_CALL(*mock_runner, create_port_reader(3))
    .WillOnce(Return(mock_readers[4]));
  EXPECT_CALL(*mock_readers[4], open()).WillOnce(Return(try_endpoint("host4", 4)));
  EXPECT_CALL(*mock_readers[4], register_container(
          ContainerStrMatch("YNN"), ExecInfoMatch(exec_info("fid4", "eid4"))))
    .WillOnce(Return(try_endpoint("ignored4", 432)));
  EXPECT_CALL(*mock_state_cache, add_container(
    ContainerStrMatch("YNN"), stats::UDPEndpoint("host4", 4)));

  // 5: endpoint lookup then cache.remove. see host5:3 registration above
  EXPECT_CALL(*mock_readers[5], endpoint()).WillOnce(Return(try_endpoint("host5", 3)));
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerStrMatch("NYY")));

  // 6: cache.remove only (wasn't registered so no endpoint lookup)
  EXPECT_CALL(*mock_state_cache, remove_container(ContainerStrMatch("NYN")));

  // 7: nil

  pra.recover_containers(recover_input);
}

// no port_range_multithread test: mock would need to pass through the range pool's returned ports

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  // avoid non-threadsafe logging code for these tests
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
