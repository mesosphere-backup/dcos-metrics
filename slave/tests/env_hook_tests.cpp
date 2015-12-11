#include <glog/logging.h>

#include "env_hook.cpp"
#include "mock_input_assigner.hpp"

using testing::Return;

MATCHER_P(ExecInfoMatch, proto_value, "mesos::ExecutorInfo") {
  return arg.executor_id().value() == proto_value.executor_id().value()
    && arg.framework_id().value() == proto_value.framework_id().value();
}

TEST(EnvHookTests, endpoint_returns_error) {
  std::shared_ptr<MockInputAssigner> mock_assigner(new MockInputAssigner());
  stats::EnvHook<MockInputAssigner> env_hook(mock_assigner);

  mesos::ExecutorInfo executor_info;
  executor_info.mutable_executor_id()->set_value("test executor");
  mesos::Environment::Variable* var = executor_info.mutable_command()->mutable_environment()->add_variables();
  var->set_name("PATH");
  var->set_value("untouched var");

  EXPECT_CALL(*mock_assigner, get_statsd_endpoint(ExecInfoMatch(executor_info)))
    .WillOnce(Return(Try<stats::UDPEndpoint>::error("test err")));
  Result<mesos::Environment> env = env_hook.slaveExecutorEnvironmentDecorator(executor_info);

  EXPECT_TRUE(env.isNone());
}

TEST(EnvHookTests, endpoint_returns_success) {
  std::shared_ptr<MockInputAssigner> mock_assigner(new MockInputAssigner());
  stats::EnvHook<MockInputAssigner> env_hook(mock_assigner);

  mesos::ExecutorInfo executor_info;
  executor_info.mutable_executor_id()->set_value("test executor");
  mesos::Environment::Variable* orig_var = executor_info.mutable_command()->mutable_environment()->add_variables();
  orig_var->set_name("PATH");
  orig_var->set_value("untouched var");

  stats::UDPEndpoint endpoint("test_host", 1234567);
  EXPECT_CALL(*mock_assigner, get_statsd_endpoint(ExecInfoMatch(executor_info)))
    .WillOnce(Return(Try<stats::UDPEndpoint>(endpoint)));
  Result<mesos::Environment> result = env_hook.slaveExecutorEnvironmentDecorator(executor_info);
  EXPECT_FALSE(result.isNone());

  mesos::Environment env = result.get();
  EXPECT_EQ(3, env.variables_size());
  EXPECT_EQ(orig_var->name(), env.variables(0).name());
  EXPECT_EQ(orig_var->value(), env.variables(0).value());
  EXPECT_EQ("STATSD_UDP_HOST", env.variables(1).name());
  EXPECT_EQ(endpoint.host, env.variables(1).value());
  EXPECT_EQ("STATSD_UDP_PORT", env.variables(2).name());
  EXPECT_EQ(std::to_string(endpoint.port), env.variables(2).value());
}

int main(int argc, char **argv) {
  ::google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
