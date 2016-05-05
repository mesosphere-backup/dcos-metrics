#pragma once

#include <list>
#include <memory>

#include <mesos/mesos.pb.h>

namespace metrics {
  class ContainerAssigner;
  class IORunner;

  /**
   * Factory for use by metrics mesos modules which manages module parameter reconciliation. Creates
   * un-initialized instances of ContainerAssigners and IORunners, then later initializes them once
   * enough parameter instances have come in.
   *
   * The retrieval function is thread-safe. See headers of respective returned objects to determine
   * their thread expectations.
   */
  class ModuleAccessFactory {
   public:
    /**
     * Returns an ContainerAssigner and saves the provided params for immediate or eventual use.
     */
    static std::shared_ptr<ContainerAssigner> get_container_assigner(
        const mesos::Parameters& module_parameters);

    /**
     * Returns an IORunner and saves the provided params for immediate or eventual use.
     */
    static std::shared_ptr<IORunner> get_io_runner(const mesos::Parameters& module_parameters);

    /**
     * Resets the internal state, wiping any locally cached ContainerAssigners which were previously
     * built via get(). Meant for testing only.
     */
    static void reset_for_test();

    virtual ~ModuleAccessFactory() { }

   private:
    ModuleAccessFactory() { }
  };
}
