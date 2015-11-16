#include "env_hook.hpp"

#include <glog/logging.h>
#include <mesos/module.hpp>
#include <mesos/module/hook.hpp>

#include "input_assigner.hpp"

namespace {
  const std::string STATSD_ENV_NAME_HOST = "STATSD_UDP_HOST";
  const std::string STATSD_ENV_NAME_PORT = "STATSD_UDP_PORT";
}

stats::EnvHook::EnvHook(const mesos::Parameters& parameters)
  : input_assigner(InputAssigner::get(parameters)) { }

Result<mesos::Environment> stats::EnvHook::slaveExecutorEnvironmentDecorator(
    const mesos::ExecutorInfo& executor_info) {
  LOG(ERROR) << "STATSH Executing 'slaveExecutorEnvironmentDecorator' hook: " << executor_info.DebugString();

  Try<UDPEndpoint> endpoint = input_assigner->get_statsd_endpoint(executor_info);
  if (endpoint.isError()) {
    return None();
  }

  mesos::Environment environment;
  if (executor_info.command().has_environment()) {
    environment.CopyFrom(executor_info.command().environment());
  }

  mesos::Environment::Variable* variable = environment.add_variables();
  variable->set_name(STATSD_ENV_NAME_HOST);
  variable->set_value(endpoint->host);

  variable = environment.add_variables();
  variable->set_name(STATSD_ENV_NAME_PORT);
  variable->set_value(stringify(endpoint->port));

  return environment;
}

namespace {
  mesos::Hook* create_hook_cb(const mesos::Parameters& parameters) {
    Try<mesos::Hook*> result =
      new stats::EnvHook(parameters);
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
