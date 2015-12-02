#include <glog/logging.h>
#include <gtest/gtest.h>

#include <thread>

#include "input_assigner_factory.hpp"

#define EXPECT_DETH(a, b) { std::cerr << "Disregard the following warning:"; EXPECT_DEATH(a, b); }

class InputAssignerFactoryTests  : public ::testing::Test {
 protected:
  void TearDown() {
    stats::InputAssignerFactory::reset_for_test();
  }
};

TEST_F(InputAssignerFactoryTests, stress_multithread_usage) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::DEST_HOST);
  param->set_value("127.0.0.1");

  std::list<std::thread*> thread_ptrs;
  for (int i = 0; i < 250; ++i) {
    //Note: Tried getting AND resetting in each thread, but this led to glogging races.
    //      That behavior isn't supported anyway.
    thread_ptrs.push_back(new std::thread(std::bind(stats::InputAssignerFactory::get, params)));
  }
  for (std::thread* thread : thread_ptrs) {
    thread->join();
    delete thread;
  }
  thread_ptrs.clear();
}

TEST_F(InputAssignerFactoryTests, get_unknown) {
  mesos::Parameters params;
  mesos::Parameter* param = params.add_parameter();
  param->set_key(stats::params::LISTEN_PORT_MODE);
  param->set_value("bogus value");

  EXPECT_DETH(stats::InputAssignerFactory::get(params), "Unknown listen_port_mode.*")
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
