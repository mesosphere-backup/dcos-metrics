#include "resource_estimator_module.hpp"

#include <mesos/module/resource_estimator.hpp>
#include <process/dispatch.hpp>
#include <process/process.hpp>

#include "input_assigner.hpp"
#include "module_access_factory.hpp"

namespace stats {
  class ResourceEstimatorProcess : public process::Process<ResourceEstimatorProcess> {
   public:
    ResourceEstimatorProcess(std::shared_ptr<IORunner> io_runner)
      : io_runner(io_runner), usage(NULL) { }
    virtual ~ResourceEstimatorProcess() { }

    void initialize(const lambda::function<process::Future<mesos::ResourceUsage>()>& usage) {
      LOG(INFO) << "Initializing resource usage callback.";
      this->usage = usage;
    }

    process::Future<mesos::Resources> oversubscribable() {
      if (usage == NULL) {
        LOG(ERROR) << "ResourceEstimator::oversubscribable() was called without initialize()!";
        return mesos::Resources(); // no-op
      }
      LOG(INFO) << "ResourceEstimator::oversubscribable() was called. Fetching usage.";
      io_runner->update_usage(usage());
      return mesos::Resources(); // no-op
    }

   private:
    std::shared_ptr<IORunner> io_runner;
    lambda::function<process::Future<mesos::ResourceUsage>()> usage;
  };
}

stats::ResourceEstimatorModule::ResourceEstimatorModule(
    std::shared_ptr<IORunner> io_runner)
  : impl(new ResourceEstimatorProcess(io_runner)) {
  process::spawn(*impl);
}

stats::ResourceEstimatorModule::~ResourceEstimatorModule() {
  process::terminate(*impl);
  process::wait(*impl);
}

Try<Nothing> stats::ResourceEstimatorModule::initialize(
    const lambda::function<process::Future<mesos::ResourceUsage>()>& usage) {
  process::dispatch(*impl, &ResourceEstimatorProcess::initialize, usage);
  return Nothing();
}

process::Future<mesos::Resources> stats::ResourceEstimatorModule::oversubscribable() {
  return process::dispatch(*impl, &ResourceEstimatorProcess::oversubscribable);
}

namespace {
  mesos::slave::ResourceEstimator* create_isolator_cb(const mesos::Parameters& parameters) {
    return new stats::ResourceEstimatorModule(
        stats::ModuleAccessFactory::get_io_runner(parameters));
  }
}

mesos::modules::Module<mesos::slave::ResourceEstimator> com_mesosphere_StatsResourceEstimatorModule(
    MESOS_MODULE_API_VERSION,
    MESOS_VERSION,
    "Mesosphere",
    "mesosphere@mesosphere.com",
    "Stats ResourceEstimator module",
    NULL,
    create_isolator_cb);
