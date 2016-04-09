#include "input_assigner_factory.hpp"

#include <mutex>

#include <glog/logging.h>

#include "input_assigner.hpp"
#include "port_runner_impl.hpp"

namespace {
  std::mutex global_assigner_mutex;
  std::shared_ptr<stats::InputAssigner> global_assigner;
  mesos::Parameters used_parameters;
}

std::shared_ptr<stats::InputAssigner> stats::InputAssignerFactory::get(
    const mesos::Parameters& parameters) {
  std::unique_lock<std::mutex> lock(global_assigner_mutex);
  if (global_assigner && global_assigner.get() != NULL) {
    if (parameters.parameter_size() > 0) {
      LOG(FATAL) << "These module parameters are in the wrong module! "
                 << "Move them to the other module entry inside the modules json config: "
                 << parameters.ShortDebugString();
    } else {
      LOG(INFO) << "Reusing existing stats InputAssigner with parameters: "
                << used_parameters.ShortDebugString();
    }
    return global_assigner;
  }

  LOG(INFO) << "Creating new stats InputAssigner with parameters: " << parameters.ShortDebugString();

  std::string port_mode_str =
    params::get_str(parameters, params::LISTEN_PORT_MODE, params::LISTEN_PORT_MODE_DEFAULT);
  params::port_mode::Value port_mode = params::to_port_mode(port_mode_str);

  InputAssigner* impl;
  switch (port_mode) {
    case params::port_mode::SINGLE:
      impl = new SinglePortAssigner(PortRunnerImpl::create(parameters), parameters);
      break;
    case params::port_mode::EPHEMERAL:
      impl = new EphemeralPortAssigner(PortRunnerImpl::create(parameters), parameters);
      break;
    case params::port_mode::RANGE:
      impl = new PortRangeAssigner(PortRunnerImpl::create(parameters), parameters);
      break;
    case params::port_mode::UNKNOWN:
      LOG(FATAL) << "Unknown " << params::LISTEN_PORT_MODE << " config value: " << port_mode_str;
      break;
  }

  used_parameters = parameters;
  global_assigner.reset(impl);
  return global_assigner;
}

void stats::InputAssignerFactory::reset_for_test() {
  std::unique_lock<std::mutex> lock(global_assigner_mutex);
  LOG(INFO) << "Wiping existing InputAssigner, if any";
  global_assigner.reset();
}
