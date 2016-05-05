#include "module_access_factory.hpp"

#include <mutex>

#include <glog/logging.h>

#include "container_assigner.hpp"
#include "container_assigner_strategy.hpp"
#include "container_state_cache_impl.hpp"
#include "io_runner_impl.hpp"

namespace {
  /**
   * Params can potentially be distributed across multiple module sections in modules.json.
   * This code goes to a lot of effort to only initialize params AFTER all expected sections have
   * been processed. This prevents the module breaking if params are read "too early" or "too late".
   *
   * We expect to be called twice:
   * - Isolator module fetches the ContainerAssigner
   * - Resource estimator module fetches the IORunner
   * These are both init()ed only after *both* have been fetched, to ensure that we have all params.
   */
  const size_t EXPECTED_MODULE_COUNT = 2;

  std::mutex global_state_mutex;
  std::shared_ptr<metrics::ContainerAssigner> global_container_assigner;
  std::shared_ptr<metrics::IORunnerImpl> global_io_runner;
  std::vector<mesos::Parameters> all_parameters;

  std::shared_ptr<metrics::IORunnerImpl> get_global_io_runner() {
    if (global_io_runner) {
      return global_io_runner;
    }
    global_io_runner.reset(new metrics::IORunnerImpl);
    return global_io_runner;
  }

  std::shared_ptr<metrics::ContainerAssigner> get_global_container_assigner() {
    if (global_container_assigner) {
      return global_container_assigner;
    }
    global_container_assigner.reset(new metrics::ContainerAssigner);
    return global_container_assigner;
  }

  void init_everything_if_enough_instantiations(const mesos::Parameters& params) {
    all_parameters.push_back(params);
    if (all_parameters.size() < EXPECTED_MODULE_COUNT) {
      LOG(INFO) << "Got " << all_parameters.size() << " module instantiations, waiting for "
                << EXPECTED_MODULE_COUNT << " before initializing anything.";
      return;
    } else if (all_parameters.size() > EXPECTED_MODULE_COUNT) {
      LOG(FATAL) << "Got " << all_parameters.size() << " module instantiations, but only expected "
                 << EXPECTED_MODULE_COUNT << ". Misconfigured modules.json?";
      return;
    }

    // We've gotten all the params we expect to get, so initialize the module components with the
    // combined params. Note: A single param duplicated in two places has no guarantees as to which
    // version will 'win'.
    mesos::Parameters merged_parameters;
    for (const mesos::Parameters& params : all_parameters) {
      for (const mesos::Parameter& param : params.parameter()) {
        *merged_parameters.add_parameter() = param;
      }
    }
    LOG(INFO) << "Initializing module with " << merged_parameters.parameter_size() << " merged "
              << "parameters across " << all_parameters.size() << " module instantiations: "
              << merged_parameters.ShortDebugString();

    std::shared_ptr<metrics::IORunnerImpl> io_runner = get_global_io_runner();
    io_runner->init(merged_parameters);

    std::shared_ptr<metrics::ContainerAssignerStrategy> strategy;
    {
      std::string port_mode_str = metrics::params::get_str(merged_parameters,
          metrics::params::LISTEN_PORT_MODE, metrics::params::LISTEN_PORT_MODE_DEFAULT);
      switch (metrics::params::to_port_mode(port_mode_str)) {
        case metrics::params::port_mode::SINGLE:
          strategy.reset(new metrics::SinglePortStrategy(io_runner, merged_parameters));
          break;
        case metrics::params::port_mode::EPHEMERAL:
          strategy.reset(new metrics::EphemeralPortStrategy(io_runner));
          break;
        case metrics::params::port_mode::RANGE:
          strategy.reset(new metrics::PortRangeStrategy(io_runner, merged_parameters));
          break;
        case metrics::params::port_mode::UNKNOWN:
          LOG(FATAL) << "Unknown " << metrics::params::LISTEN_PORT_MODE << " config value: "
                     << port_mode_str;
          break;
      }
    }
    std::shared_ptr<metrics::ContainerStateCacheImpl> state_cache(
        new metrics::ContainerStateCacheImpl(merged_parameters));
    get_global_container_assigner()->init(io_runner, state_cache, strategy);
  }
}

std::shared_ptr<metrics::ContainerAssigner> metrics::ModuleAccessFactory::get_container_assigner(
    const mesos::Parameters& module_parameters) {
  std::unique_lock<std::mutex> lock(global_state_mutex);
  init_everything_if_enough_instantiations(module_parameters);
  LOG(INFO) << "Returning ContainerAssigner to module";
  return get_global_container_assigner();
}

std::shared_ptr<metrics::IORunner> metrics::ModuleAccessFactory::get_io_runner(
    const mesos::Parameters& module_parameters) {
  std::unique_lock<std::mutex> lock(global_state_mutex);
  init_everything_if_enough_instantiations(module_parameters);
  LOG(INFO) << "Returning IORunner to module";
  return get_global_io_runner();
}

void metrics::ModuleAccessFactory::reset_for_test() {
  std::unique_lock<std::mutex> lock(global_state_mutex);
  LOG(INFO) << "Wiping existing state, if any";
  global_container_assigner.reset();
  global_io_runner.reset();
  all_parameters.clear();
}
