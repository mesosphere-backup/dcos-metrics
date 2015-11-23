#include "isolator_module.hpp"

#include <mesos/module/isolator.hpp>
#include <process/process.hpp>
#include <stout/try.hpp>

#include "input_assigner.hpp"
#include "input_assigner_factory.hpp"

namespace stats {
  class IsolatorProcess : public process::Process<IsolatorProcess> {
   public:
    IsolatorProcess(const mesos::Parameters& parameters)
      : input_assigner(InputAssignerFactory::get(parameters)) { }
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

stats::IsolatorModule::IsolatorModule(const mesos::Parameters& parameters)
  : impl(new IsolatorProcess(parameters)) {
  process::spawn(*impl);
}

stats::IsolatorModule::~IsolatorModule() {
  process::terminate(*impl);
  process::wait(*impl);
}

process::Future<Nothing> stats::IsolatorModule::recover(
    const std::list<mesos::slave::ContainerState>& states,
    const hashset<mesos::ContainerID>& orphans) {
  return process::dispatch(*impl,
      &IsolatorProcess::recover,
      states,
      orphans);
}

process::Future<Option<mesos::slave::ContainerPrepareInfo>> stats::IsolatorModule::prepare(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info,
    const std::string& directory,
    const Option<std::string>& user) {
  return process::dispatch(*impl,
      &IsolatorProcess::prepare,
      container_id,
      executor_info,
      directory,
      user);
}

process::Future<Nothing> stats::IsolatorModule::cleanup(
    const mesos::ContainerID& container_id) {
  return process::dispatch(*impl, &IsolatorProcess::cleanup, container_id);
}

namespace {
  mesos::slave::Isolator* create_isolator_cb(const mesos::Parameters& parameters) {
    Try<mesos::slave::Isolator*> result = new stats::IsolatorModule(parameters);
    if (result.isError()) {
      return NULL;
    }
    return result.get();
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
