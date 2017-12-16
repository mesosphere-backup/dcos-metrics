#pragma once

#include <mesos/mesos.pb.h>
#include <mesos/slave/isolator.hpp>
#include <process/future.hpp>
#include <stout/option.hpp>

namespace metrics {

  template <typename ContainerAssigner>
  class IsolatorProcess;

  /**
   * Templated to allow mockery of ContainerAssigner.
   */
  template <typename ContainerAssigner>
  class IsolatorModule : public mesos::slave::Isolator {
   public:
    IsolatorModule(std::shared_ptr<ContainerAssigner> container_assigner);
    virtual ~IsolatorModule();

    virtual bool supportsNesting() override;

    process::Future<Nothing> recover(
      const std::list<mesos::slave::ContainerState>& states,
      const hashset<mesos::ContainerID>& orphans);

    process::Future<Option<mesos::slave::ContainerLaunchInfo>> prepare(
      const mesos::ContainerID& container_id,
      const mesos::slave::ContainerConfig& container_config);

    process::Future<Nothing> cleanup(
      const mesos::ContainerID& container_id,
      const mesos::slave::ContainerConfig& container_config);

    // The following calls are unused by this implementation.

    process::Future<Option<int>> namespaces() {
      return None();
    }

    process::Future<Nothing> isolate(const mesos::ContainerID&, pid_t) {
      return Nothing();
    }

    process::Future<mesos::slave::ContainerLimitation> watch(const mesos::ContainerID&) {
      // Empty future required: An empty proto will result in the task being killed.
      return process::Future<mesos::slave::ContainerLimitation>();
    }

    process::Future<Nothing> update(const mesos::ContainerID&, const mesos::Resources&) {
      return Nothing();
    }

    process::Future<mesos::ResourceStatistics> usage(const mesos::ContainerID&) {
      return mesos::ResourceStatistics();
    }

   private:
    std::unique_ptr<IsolatorProcess<ContainerAssigner>> impl;
  };

}
