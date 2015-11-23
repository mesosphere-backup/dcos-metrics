#include "isolator_module.hpp"

#include <mesos/module/isolator.hpp>
#include <process/process.hpp>
#include <stout/try.hpp>

#include "input_assigner.hpp"

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
        const hashset<mesos::ContainerID>& /* orphans */) {
      input_assigner->register_containers(states);
      return Nothing();
    }

    process::Future<Option<mesos::slave::ContainerPrepareInfo>> prepare(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info,
        const std::string& /* directory */,
        const Option<std::string>& /* user */) {
      input_assigner->register_container(container_id, executor_info);
      return None();
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
process::Future<Option<mesos::slave::ContainerPrepareInfo>> stats::IsolatorModule<InputAssigner>::prepare(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info,
    const std::string& directory,
    const Option<std::string>& user) {
  return process::dispatch(*impl,
      &IsolatorProcess<InputAssigner>::prepare,
      container_id,
      executor_info,
      directory,
      user);
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
    return new stats::IsolatorModule<stats::InputAssigner>(stats::InputAssigner::get(parameters));
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
