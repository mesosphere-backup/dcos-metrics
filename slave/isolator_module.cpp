#include "isolator_module.hpp"

#include <mesos/module/isolator.hpp>
#include <process/process.hpp>
#include <stout/try.hpp>

#include "input_assigner.hpp"
#include "module_access_factory.hpp"

namespace {
  const std::string STATSD_ENV_NAME_HOST = "STATSD_UDP_HOST";
  const std::string STATSD_ENV_NAME_PORT = "STATSD_UDP_PORT";

  void set_env(mesos::slave::ContainerLaunchInfo& launch_info, const stats::UDPEndpoint& endpoint) {
    mesos::Environment* environment = launch_info.mutable_environment();

    mesos::Environment::Variable* variable = environment->add_variables();
    variable->set_name(STATSD_ENV_NAME_HOST);
    variable->set_value(endpoint.host);

    variable = environment->add_variables();
    variable->set_name(STATSD_ENV_NAME_PORT);
    variable->set_value(std::to_string(endpoint.port));

    DLOG(INFO) << "Returning environment: " << environment->ShortDebugString();
  }
}

namespace stats {
  /**
   * Templated to allow mockery of InputAssigner.
   */
  template <typename InputAssigner>
  class IsolatorProcess : public process::Process<IsolatorProcess<InputAssigner>> {
   public:
    IsolatorProcess(std::shared_ptr<InputAssigner> input_assigner)
      : input_assigner(input_assigner) { }
    virtual ~IsolatorProcess() { }

    process::Future<Nothing> recover(
        const std::list<mesos::slave::ContainerState>& states,
        const hashset<mesos::ContainerID>& orphans) {
      LOG(INFO) << "Container recovery "
                << "(" << states.size() << " containers, " << orphans.size() << " orphans):";
      for (const mesos::slave::ContainerState& state : states) {
        LOG(INFO) << "  container_state[" << state.ShortDebugString() << "]";
      }
      input_assigner->recover_containers(states);
      return Nothing();
    }

    process::Future<Option<mesos::slave::ContainerLaunchInfo>> prepare(
        const mesos::ContainerID& container_id,
        const mesos::slave::ContainerConfig& container_config) {
      LOG(INFO) << "Container prepare: "
                << "container_id[" << container_id.ShortDebugString() << "] "
                << "container_config[" << container_config.ShortDebugString() << "]";
      Try<UDPEndpoint> endpoint =
        input_assigner->register_container(container_id, container_config.executorinfo());
      if (endpoint.isError()) {
        LOG(ERROR) << "Failed to register container, no statsd endpoint to inject: "
                   << container_id.ShortDebugString();
        return None();
      }
      mesos::slave::ContainerLaunchInfo launch_info;
      set_env(launch_info, endpoint.get());
      return launch_info;
    }

    process::Future<Nothing> cleanup(
        const mesos::ContainerID& container_id) {
      input_assigner->unregister_container(container_id);
      return Nothing();
    }

   private:
    std::shared_ptr<InputAssigner> input_assigner;
  };
}

template <typename InputAssigner>
stats::IsolatorModule<InputAssigner>::IsolatorModule(std::shared_ptr<InputAssigner> input_assigner)
  : impl(new IsolatorProcess<InputAssigner>(input_assigner)) {
  process::spawn(*impl);
}

template <typename InputAssigner>
stats::IsolatorModule<InputAssigner>::~IsolatorModule() {
  process::terminate(*impl);
  process::wait(*impl);
}

template <typename InputAssigner>
process::Future<Nothing> stats::IsolatorModule<InputAssigner>::recover(
    const std::list<mesos::slave::ContainerState>& states,
    const hashset<mesos::ContainerID>& orphans) {
  return process::dispatch(*impl,
      &IsolatorProcess<InputAssigner>::recover,
      states,
      orphans);
}

template <typename InputAssigner>
process::Future<Option<mesos::slave::ContainerLaunchInfo>> stats::IsolatorModule<InputAssigner>::prepare(
    const mesos::ContainerID& container_id,
    const mesos::slave::ContainerConfig& container_config) {
  return process::dispatch(*impl,
      &IsolatorProcess<InputAssigner>::prepare,
      container_id,
      container_config);
}

template <typename InputAssigner>
process::Future<Nothing> stats::IsolatorModule<InputAssigner>::cleanup(
    const mesos::ContainerID& container_id) {
  return process::dispatch(*impl,
      &IsolatorProcess<InputAssigner>::cleanup,
      container_id);
}

namespace {
  mesos::slave::Isolator* create_isolator_cb(const mesos::Parameters& parameters) {
    return new stats::IsolatorModule<stats::InputAssigner>(
        stats::ModuleAccessFactory::get_input_assigner(parameters));
  }
}

mesos::modules::Module<mesos::slave::Isolator> com_mesosphere_StatsIsolatorModule(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Mesosphere",
    "mesosphere@mesosphere.com",
    "Stats Isolator module",
    NULL,
    create_isolator_cb);
