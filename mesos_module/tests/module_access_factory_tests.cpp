#include <glog/logging.h>
#include <gtest/gtest.h>

#include <thread>

#include "container_assigner.hpp"
#include "io_runner.hpp"
#include "module_access_factory.hpp"
#include "params.hpp"

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

namespace {
  inline mesos::ContainerID container_id(const std::string& id) {
    mesos::ContainerID cid;
    cid.set_value(id);
    return cid;
  }
}

class ModuleAccessFactoryTests  : public ::testing::Test {
 protected:
  void TearDown() {
    metrics::ModuleAccessFactory::reset_for_test();
  }
};

TEST_F(ModuleAccessFactoryTests, too_many_calls) {
  metrics::ModuleAccessFactory::get_container_assigner(mesos::Parameters());
  EXPECT_DETH(metrics::ModuleAccessFactory::get_container_assigner(mesos::Parameters()),
      ".*Got 2 module instantiations, but only expected 1.*");

  metrics::ModuleAccessFactory::reset_for_test();

}

TEST_F(ModuleAccessFactoryTests, params_via_input_assigner) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(metrics::params::OUTPUT_STATSD_HOST);
  param->set_value("127.0.0.1");

  {
    std::shared_ptr<metrics::ContainerAssigner> assigner =
      metrics::ModuleAccessFactory::get_container_assigner(params);
    assigner->unregister_container(container_id("hi"));
  }

  metrics::ModuleAccessFactory::reset_for_test();
}

TEST_F(ModuleAccessFactoryTests, unknown_port_mode) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(metrics::params::LISTEN_PORT_MODE);
  param->set_value("bogus value");

  EXPECT_DETH(metrics::ModuleAccessFactory::get_container_assigner(params),
      "Unknown listen_port_mode.*")

  metrics::ModuleAccessFactory::reset_for_test();
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
