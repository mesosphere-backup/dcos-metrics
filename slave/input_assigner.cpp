#include "input_assigner.hpp"

#include <mutex>

#include <glog/logging.h>

#include "input_assigner_impl.hpp"

namespace {
  std::mutex global_assigner_mutex;
  std::shared_ptr<stats::InputAssigner> global_assigner;
}

std::shared_ptr<stats::InputAssigner> stats::InputAssigner::get(const mesos::Parameters& parameters) {
  std::unique_lock<std::mutex> lock(global_assigner_mutex);
  if (global_assigner) {
    LOG(INFO) << "Reusing existing InputAssigner, ignoring parameters: " << parameters.ShortDebugString();
    return global_assigner;
  }

  LOG(INFO) << "Creating new InputAssigner with parameters: " << parameters.ShortDebugString();

  std::string port_mode_str = params::get_str(parameters, params::LISTEN_PORT_MODE, params::LISTEN_PORT_MODE_DEFAULT);
  params::port_mode::Value port_mode = params::to_port_mode(port_mode_str);

  InputAssignerImpl* impl;
  switch (port_mode) {
    case params::port_mode::SINGLE:
      impl = new SinglePortAssignerImpl(parameters);
      break;
    case params::port_mode::EPHEMERAL:
      impl = new EphemeralPortAssignerImpl(parameters);
      break;
    case params::port_mode::RANGE:
      impl = new PortRangeAssignerImpl(parameters);
      break;
    case params::port_mode::UNKNOWN:
      LOG(FATAL) << "Unknown " << params::LISTEN_PORT_MODE << " config value: " << port_mode_str;
      break;
  }

  global_assigner.reset(new InputAssigner(impl));
  return global_assigner;
}

stats::InputAssigner::InputAssigner(InputAssignerImpl* impl)
  : impl(impl) { }

stats::InputAssigner::~InputAssigner() { }

void stats::InputAssigner::register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  impl->register_container(container_id, executor_info);
}

void stats::InputAssigner::register_containers(
    const std::list<mesos::slave::ContainerState>& containers) {
  impl->register_containers(containers);
}

void stats::InputAssigner::unregister_container(
    const mesos::ContainerID& container_id) {
  impl->unregister_container(container_id);
}

Try<stats::UDPEndpoint> stats::InputAssigner::get_statsd_endpoint(
    const mesos::ExecutorInfo& executor_info) {
  return impl->get_statsd_endpoint(executor_info);
}
