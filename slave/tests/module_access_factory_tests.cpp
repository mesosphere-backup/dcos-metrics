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

  void noop() {
    LOG(INFO) << "noop";
  }
}

class ModuleAccessFactoryTests  : public ::testing::Test {
 protected:
  void TearDown() {
    stats::ModuleAccessFactory::reset_for_test();
  }
};

TEST_F(ModuleAccessFactoryTests, too_many_calls) {
  stats::ModuleAccessFactory::get_container_assigner(mesos::Parameters());
  stats::ModuleAccessFactory::get_container_assigner(mesos::Parameters());
  EXPECT_DETH(stats::ModuleAccessFactory::get_container_assigner(mesos::Parameters()),
      ".*Got 3 module instantiations, but only expected 2.*");

  stats::ModuleAccessFactory::reset_for_test();

  stats::ModuleAccessFactory::get_io_runner(mesos::Parameters());
  stats::ModuleAccessFactory::get_io_runner(mesos::Parameters());
  EXPECT_DETH(stats::ModuleAccessFactory::get_io_runner(mesos::Parameters()),
      ".*Got 3 module instantiations, but only expected 2.*");

  stats::ModuleAccessFactory::reset_for_test();

  stats::ModuleAccessFactory::get_io_runner(mesos::Parameters());
  stats::ModuleAccessFactory::get_container_assigner(mesos::Parameters());
  EXPECT_DETH(stats::ModuleAccessFactory::get_io_runner(mesos::Parameters()),
      ".*Got 3 module instantiations, but only expected 2.*");

  stats::ModuleAccessFactory::reset_for_test();

  stats::ModuleAccessFactory::get_container_assigner(mesos::Parameters());
  stats::ModuleAccessFactory::get_io_runner(mesos::Parameters());
  EXPECT_DETH(stats::ModuleAccessFactory::get_container_assigner(mesos::Parameters()),
      ".*Got 3 module instantiations, but only expected 2.*");

  stats::ModuleAccessFactory::reset_for_test();

}

TEST_F(ModuleAccessFactoryTests, params_via_input_assigner_first) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("127.0.0.1");

  {
    std::shared_ptr<stats::ContainerAssigner> assigner =
      stats::ModuleAccessFactory::get_container_assigner(params);
    EXPECT_DETH(assigner->unregister_container(container_id("hi")),
        ".*init\\(\\) wasn't called before unregister_container\\(\\).*");
  }

  {
    std::shared_ptr<stats::IORunner> runner =
      stats::ModuleAccessFactory::get_io_runner(mesos::Parameters());
    runner->dispatch(noop);
  }

  stats::ModuleAccessFactory::reset_for_test();
}

TEST_F(ModuleAccessFactoryTests, params_via_input_assigner_second) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("127.0.0.1");

  {
    std::shared_ptr<stats::IORunner> runner =
      stats::ModuleAccessFactory::get_io_runner(mesos::Parameters());
    EXPECT_DETH(runner->dispatch(noop),
        ".*init\\(\\) wasn't called before dispatch\\(\\).*");
  }

  {
    std::shared_ptr<stats::ContainerAssigner> assigner =
      stats::ModuleAccessFactory::get_container_assigner(params);
    assigner->unregister_container(container_id("hi"));
  }

  stats::ModuleAccessFactory::reset_for_test();
}

TEST_F(ModuleAccessFactoryTests, params_via_io_runner_first) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("127.0.0.1");

  {
    std::shared_ptr<stats::IORunner> runner =
      stats::ModuleAccessFactory::get_io_runner(params);
    EXPECT_DETH(runner->dispatch(noop),
      ".*init\\(\\) wasn't called before dispatch\\(\\).*");
  }

  {
    std::shared_ptr<stats::ContainerAssigner> assigner =
      stats::ModuleAccessFactory::get_container_assigner(mesos::Parameters());
    assigner->unregister_container(container_id("hi"));
  }

  stats::ModuleAccessFactory::reset_for_test();
}

TEST_F(ModuleAccessFactoryTests, params_via_io_runner_second) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("127.0.0.1");

  {
    std::shared_ptr<stats::ContainerAssigner> assigner =
      stats::ModuleAccessFactory::get_container_assigner(mesos::Parameters());
    EXPECT_DETH(assigner->unregister_container(container_id("hi")),
        ".*init\\(\\) wasn't called before unregister_container\\(\\).*");
  }

  {
    std::shared_ptr<stats::IORunner> runner =
      stats::ModuleAccessFactory::get_io_runner(params);
    runner->dispatch(noop);
  }

  stats::ModuleAccessFactory::reset_for_test();
}

TEST_F(ModuleAccessFactoryTests, unknown_port_mode) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_MODE);
  param->set_value("bogus value");

  stats::ModuleAccessFactory::get_io_runner(params);
  EXPECT_DETH(stats::ModuleAccessFactory::get_container_assigner(
          mesos::Parameters()), "Unknown listen_port_mode.*")
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
