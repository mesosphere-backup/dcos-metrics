#pragma once

#include <memory>
#include <mesos/hook.hpp>

namespace stats {

  class InputAssigner;

  /**
   * Templated to allow mockery of InputAssigner.
   */
  template <typename InputAssigner>
  class EnvHook : public mesos::Hook {
   public:
    EnvHook(std::shared_ptr<InputAssigner> input_assigner);
    virtual ~EnvHook() { }

    virtual Result<mesos::Environment> slaveExecutorEnvironmentDecorator(
        const mesos::ExecutorInfo& executor_info);

   private:
    std::shared_ptr<InputAssigner> input_assigner;
  };

}
