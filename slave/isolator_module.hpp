#pragma once

#include <mesos/mesos.pb.h>
#include <mesos/slave/isolator.hpp>
#include <process/future.hpp>
#include <stout/option.hpp>

namespace stats {

  class IsolatorProcess;

  class IsolatorModule : public mesos::slave::Isolator {
   public:
    IsolatorModule(IsolatorProcess* process);
    virtual ~IsolatorModule();

    process::Future<Nothing> recover(
      const std::list<mesos::slave::ContainerState>& states,
      const hashset<mesos::ContainerID>& orphans);

    process::Future<Option<mesos::slave::ContainerPrepareInfo>> prepare(
      const mesos::ContainerID& container_id,
      const mesos::ExecutorInfo& executor_info,
      const std::string& directory,
      const Option<std::string>& user);

    process::Future<Nothing> cleanup(
      const mesos::ContainerID& container_id);

    // The following calls are unused by this implementation.

    process::Future<Option<int>> namespaces() {
      return None();
    }

    process::Future<Nothing> isolate(
        const mesos::ContainerID& container_id, pid_t pid) {
      return Nothing();
    }

    process::Future<mesos::slave::ContainerLimitation> watch(
        const mesos::ContainerID& container_id) {
      // Empty future required: An empty proto will result in the task being killed.
      return process::Future<mesos::slave::ContainerLimitation>();
    }

    process::Future<Nothing> update(
        const mesos::ContainerID& container_id, const mesos::Resources& resources) {
      return Nothing();
    }

    process::Future<mesos::ResourceStatistics> usage(
        const mesos::ContainerID& container_id) {
      return mesos::ResourceStatistics();
    }

   private:
    std::unique_ptr<IsolatorProcess> impl;
  };

}
