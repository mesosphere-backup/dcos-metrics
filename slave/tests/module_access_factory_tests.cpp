#include <glog/logging.h>
#include <gtest/gtest.h>

#include <thread>

#include "params.hpp"
#include "module_access_factory.hpp"

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

class ModuleAccessFactoryTests  : public ::testing::Test {
 protected:
  void TearDown() {
    stats::ModuleAccessFactory::reset_for_test();
  }
};

TEST_F(ModuleAccessFactoryTests, stress_multithread_usage) {
  mesos::Parameters params;

  std::list<std::thread*> thread_ptrs;
  for (int i = 0; i < 250; ++i) {
    //Note: Tried getting AND resetting in each thread, but this led to glogging races.
    //      That behavior isn't supported anyway.
    thread_ptrs.push_back(new std::thread(
            std::bind(stats::ModuleAccessFactory::get_input_assigner, params)));
  }
  for (std::thread* thread : thread_ptrs) {
    thread->join();
    delete thread;
  }
  thread_ptrs.clear();
}

TEST_F(ModuleAccessFactoryTests, get_input_assigner_drop_params) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("127.0.0.1");

  stats::ModuleAccessFactory::get_input_assigner(params);
  EXPECT_DETH(stats::ModuleAccessFactory::get_input_assigner(params),
      "These module parameters are in the wrong module!")
}

TEST_F(ModuleAccessFactoryTests, get_input_assigner_correct_params) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("127.0.0.1");

  stats::ModuleAccessFactory::get_input_assigner(params);
  stats::ModuleAccessFactory::get_input_assigner(mesos::Parameters());
}

TEST_F(ModuleAccessFactoryTests, get_input_assigner_unknown) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_MODE);
  param->set_value("bogus value");

  EXPECT_DETH(stats::ModuleAccessFactory::get_input_assigner(params), "Unknown listen_port_mode.*")
}

TEST_F(ModuleAccessFactoryTests, get_io_runner) {
  EXPECT_TRUE(false) << "TODO update above tests to include get_io_runner";
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
