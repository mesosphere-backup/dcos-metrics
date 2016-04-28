#pragma once

#include <list>
#include <memory>

#include <mesos/mesos.pb.h>

namespace stats {
  class InputAssigner;
  class IORunner;

  /**
   * Factory for use by Stats mesos modules which manages module parameter reconciliation. Creates
   * un-initialized instances of InputAssigners and IORunners, then later initializes them once
   * enough parameter instances have come in.
   *
   * The retrieval function is thread-safe. See headers of respective returned objects to determine
   * their thread expectations.
   */
  class ModuleAccessFactory {
   public:
    /**
     * Returns an InputAssigner and saves the provided params for immediate or eventual use.
     */
    static std::shared_ptr<InputAssigner> get_input_assigner(
        const mesos::Parameters& module_parameters);

    /**
     * Returns an IORunner and saves the provided params for immediate or eventual use.
     */
    static std::shared_ptr<IORunner> get_io_runner(const mesos::Parameters& module_parameters);

    /**
     * Resets the internal state, wiping any locally cached InputAssigners which were previously
     * built via get(). Meant for testing only.
     */
    static void reset_for_test();

    virtual ~ModuleAccessFactory() { }

   private:
    ModuleAccessFactory() { }
  };
}
