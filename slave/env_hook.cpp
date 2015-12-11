#include "env_hook.hpp"

#include <glog/logging.h>
#include <mesos/module.hpp>
#include <mesos/module/hook.hpp>

#include "input_assigner.hpp"
#include "input_assigner_factory.hpp"

namespace {
  const std::string STATSD_ENV_NAME_HOST = "STATSD_UDP_HOST";
  const std::string STATSD_ENV_NAME_PORT = "STATSD_UDP_PORT";
}

template <typename InputAssigner>
stats::EnvHook<InputAssigner>::EnvHook(std::shared_ptr<InputAssigner> input_assigner)
  : input_assigner(input_assigner) { }

template <typename InputAssigner>
Result<mesos::Environment> stats::EnvHook<InputAssigner>::slaveExecutorEnvironmentDecorator(
    const mesos::ExecutorInfo& executor_info) {
  Try<UDPEndpoint> endpoint = input_assigner->get_statsd_endpoint(executor_info);
  if (endpoint.isError()) {
    LOG(ERROR) << "InputAssigner doesn't have container registered. "
               << "No statsd endpoint to inject: " << executor_info.ShortDebugString();
    return None();
  }

  LOG(INFO) << "Injecting statsd "
            << "endpoint[" << endpoint.get().host << ":" << endpoint.get().port << "] "
            << "into environment: " << executor_info.ShortDebugString();

  mesos::Environment environment;
  if (executor_info.command().has_environment()) {
    environment.CopyFrom(executor_info.command().environment());
  }

  mesos::Environment::Variable* variable = environment.add_variables();
  variable->set_name(STATSD_ENV_NAME_HOST);
  variable->set_value(endpoint.get().host);

  variable = environment.add_variables();
  variable->set_name(STATSD_ENV_NAME_PORT);
  variable->set_value(std::to_string(endpoint.get().port));

  DLOG(INFO) << "Updated environment is now: " << environment.ShortDebugString();

  return environment;
}

namespace {
  mesos::Hook* create_hook_cb(const mesos::Parameters& parameters) {
    Try<mesos::Hook*> result =
      new stats::EnvHook<stats::InputAssigner>(stats::InputAssignerFactory::get(parameters));
    if (result.isError()) {
      return NULL;
    }
    return result.get();
  }
}

mesos::modules::Module<mesos::Hook> com_mesosphere_StatsEnvHook(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Mesosphere",
    "mesosphere@mesosphere.com",
    "Stats Environment Hook module",
    NULL,
    create_hook_cb);
