#pragma once

#include <memory>
#include <mesos/hook.hpp>

namespace stats {

  class InputAssigner;

  class EnvHook : public mesos::Hook {
   public:
    EnvHook(const mesos::Parameters& parameters);
    virtual ~EnvHook() { }

    virtual Result<mesos::Environment> slaveExecutorEnvironmentDecorator(
        const mesos::ExecutorInfo& executor_info);
   private:
    std::shared_ptr<InputAssigner> input_assigner;
  };

}
