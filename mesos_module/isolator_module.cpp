#include "isolator_module.hpp"

#include <mesos/module/isolator.hpp>
#include <mesos/slave/containerizer.hpp>
#include <process/process.hpp>
#include <stout/try.hpp>

#include "container_assigner.hpp"
#include "module_access_factory.hpp"


using mesos::slave::ContainerClass;

namespace {
  const std::string STATSD_ENV_NAME_HOST = "STATSD_UDP_HOST";
  const std::string STATSD_ENV_NAME_PORT = "STATSD_UDP_PORT";

  void set_env(mesos::slave::ContainerLaunchInfo& launch_info, const metrics::UDPEndpoint& endpoint) {
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

namespace metrics {
  /**
   * Templated to allow mockery of ContainerAssigner.
   */
  template <typename ContainerAssigner>
  class IsolatorProcess : public process::Process<IsolatorProcess<ContainerAssigner>> {
   public:
    IsolatorProcess(std::shared_ptr<ContainerAssigner> container_assigner)
      : container_assigner(container_assigner) { }
    virtual ~IsolatorProcess() { }

    process::Future<Nothing> recover(
        const std::list<mesos::slave::ContainerState>& states,
        const hashset<mesos::ContainerID>& orphans) {
      LOG(INFO) << "Container recovery "
                << "(" << states.size() << " containers, " << orphans.size() << " orphans):";
      for (const mesos::slave::ContainerState& state : states) {
        LOG(INFO) << "  container_state[" << state.ShortDebugString() << "]";
      }
      container_assigner->recover_containers(states);
      return Nothing();
    }

    process::Future<Option<mesos::slave::ContainerLaunchInfo>> prepare(
        const mesos::ContainerID& container_id,
        const mesos::slave::ContainerConfig& container_config) {
      // If we are a nested container in the `DEBUG` class, then
      // we don't want to emit metrics from this container.
      if (container_id.has_parent() &&
          container_config.has_container_class() &&
          container_config.container_class() == ContainerClass::DEBUG) {
        return None();
      }

      LOG(INFO) << "Container prepare: "
                << "container_id[" << container_id.ShortDebugString() << "] "
                << "container_config[" << container_config.ShortDebugString() << "]";
      Try<UDPEndpoint> endpoint =
        container_assigner->register_container(container_id, container_config.executor_info());
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
        const mesos::ContainerID& container_id,
        const mesos::slave::ContainerConfig& container_config) {
      // If we are a nested container in the `DEBUG` class, then we don't
      // emit metrics from this container and hence have nothing to cleanup.
      if (!(container_id.has_parent() &&
            container_config.has_container_class() &&
            container_config.container_class() == ContainerClass::DEBUG)) {
        container_assigner->unregister_container(container_id);
      }
      return Nothing();
    }

   private:
    std::shared_ptr<ContainerAssigner> container_assigner;
  };
}

template <typename ContainerAssigner>
metrics::IsolatorModule<ContainerAssigner>::IsolatorModule(
    std::shared_ptr<ContainerAssigner> container_assigner)
  : impl(new IsolatorProcess<ContainerAssigner>(container_assigner)) {
  process::spawn(*impl);
}

template <typename ContainerAssigner>
metrics::IsolatorModule<ContainerAssigner>::~IsolatorModule() {
  process::terminate(*impl);
  process::wait(*impl);
}

template <typename ContainerAssigner>
bool metrics::IsolatorModule<ContainerAssigner>::supportsNesting() {
  return true;
}

template <typename ContainerAssigner>
process::Future<Nothing> metrics::IsolatorModule<ContainerAssigner>::recover(
    const std::list<mesos::slave::ContainerState>& states,
    const hashset<mesos::ContainerID>& orphans) {
  return process::dispatch(*impl,
      &IsolatorProcess<ContainerAssigner>::recover,
      states,
      orphans);
}

template <typename ContainerAssigner>
process::Future<Option<mesos::slave::ContainerLaunchInfo>> metrics::IsolatorModule<ContainerAssigner>::prepare(
    const mesos::ContainerID& container_id,
    const mesos::slave::ContainerConfig& container_config) {
  return process::dispatch(*impl,
      &IsolatorProcess<ContainerAssigner>::prepare,
      container_id,
      container_config);
}

template <typename ContainerAssigner>
process::Future<Nothing> metrics::IsolatorModule<ContainerAssigner>::cleanup(
    const mesos::ContainerID& container_id) {
  return process::dispatch(*impl,
      &IsolatorProcess<ContainerAssigner>::cleanup,
      container_id);
}

namespace {
  mesos::slave::Isolator* create_isolator_cb(const mesos::Parameters& parameters) {
    return new metrics::IsolatorModule<metrics::ContainerAssigner>(
        metrics::ModuleAccessFactory::get_container_assigner(parameters));
  }
}

mesos::modules::Module<mesos::slave::Isolator> com_mesosphere_MetricsIsolatorModule(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Mesosphere",
    "mesosphere@mesosphere.com",
    "Metrics Isolator module",
    NULL,
    create_isolator_cb);
