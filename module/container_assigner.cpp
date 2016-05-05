#include "container_assigner.hpp"

#include <map>
#include <glog/logging.h>

#include "container_assigner_strategy.hpp"
#include "container_state_cache_impl.hpp"
#include "params.hpp"
#include "io_runner.hpp"
#include "range_pool.hpp"
#include "sync_util.hpp"

#define MAX_PORT 65535

namespace {
  // Local util struct for pairing a ContainerState with a UDPEndpoint
  class ContainerEndpoint {
   public:
    ContainerEndpoint(
        const mesos::slave::ContainerState& container, const metrics::UDPEndpoint& endpoint)
      : container(container), endpoint(endpoint) { }

    const mesos::slave::ContainerState container;
    const metrics::UDPEndpoint endpoint;
  };

  void noop() { }
}

metrics::ContainerAssigner::ContainerAssigner() { }
metrics::ContainerAssigner::~ContainerAssigner() {
  // Dispatch a no-op job to 'flush the pipes' of any recently-dispatched actions before we exit
  sync_util::dispatch_run("~IORunnerImpl", *io_runner, noop);
  strategy.reset();
  state_cache.reset();
  io_runner.reset();
}

void metrics::ContainerAssigner::init(
    std::shared_ptr<IORunner> io_runner,
    std::shared_ptr<ContainerStateCache> state_cache,
    std::shared_ptr<ContainerAssignerStrategy> strategy) {
  std::unique_lock<std::mutex> lock(mutex);
  if (this->strategy) {
    LOG(FATAL) << "ContainerAssigner::init() was called twice";
    return;
  }
  this->io_runner = io_runner;
  this->state_cache = state_cache;
  this->strategy = strategy;
}

Try<metrics::UDPEndpoint> metrics::ContainerAssigner::register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  std::unique_lock<std::mutex> lock(mutex);
  if (!strategy) {
    std::ostringstream oss;
    oss << "ContainerAssigner::init() wasn't called before register_container()";
    LOG(FATAL) << oss.str();
    return Try<UDPEndpoint>(Error(oss.str()));
  }

  LOG(INFO) << "Registering and retrieving endpoint for "
            << "container_id[" << container_id.ShortDebugString() << "] "
            << "executor_info[" << executor_info.ShortDebugString() << "].";
  // Dispatch the endpoint retrieval from within the io_service thread, avoiding races with any
  // other endpoint registrations/deregistrations.
  std::function<Try<UDPEndpoint>()> register_container_func =
    std::bind(&ContainerAssigner::register_and_update_cache, this, container_id, executor_info);
  std::shared_ptr<Try<UDPEndpoint>> out =
    sync_util::dispatch_get<IORunner, Try<UDPEndpoint>>(
        "register_and_update_cache", *io_runner, register_container_func);
  if (!out) {
    return Try<UDPEndpoint>(Error("Timed out waiting for endpoint retrieval"));
  }
  return *out;
}

void metrics::ContainerAssigner::recover_containers(
    const std::list<mesos::slave::ContainerState>& containers) {
  std::unique_lock<std::mutex> lock(mutex);
  if (!strategy) {
    LOG(FATAL) << "ContainerAssigner::init() wasn't called before recover_containers()";
    return;
  }

  // Dispatch the endpoint recovery from within the io_service thread, avoiding races with any
  // other endpoint registrations/deregistrations.
  io_runner->dispatch(
      std::bind(&ContainerAssigner::recover_containers_imp, this, containers));
}

void metrics::ContainerAssigner::unregister_container(const mesos::ContainerID& container_id) {
  std::unique_lock<std::mutex> lock(mutex);
  if (!strategy) {
    LOG(FATAL) << "ContainerAssigner::init() wasn't called before unregister_container()";
    return;
  }

  LOG(INFO) << "Unregistering container_id[" << container_id.ShortDebugString() << "].";
  io_runner->dispatch(
      std::bind(&ContainerAssigner::unregister_and_update_cache, this, container_id));
}

// ---- Private:

void metrics::ContainerAssigner::recover_containers_imp(
    const std::list<mesos::slave::ContainerState>& containers) {
  // use ordered maps to have some consistent ordering in logs:
  container_id_ord_map<mesos::slave::ContainerState> recovered_containers;
  for (const mesos::slave::ContainerState container : containers) {
    recovered_containers[container.container_id()] = container;
  }
  container_id_ord_map<UDPEndpoint> disk_containers;
  {
    container_id_map<UDPEndpoint> disk_containers_unord = state_cache->get_containers();
    disk_containers.insert(disk_containers_unord.begin(), disk_containers_unord.end());
  }

  LOG(INFO) << "Syncing " << recovered_containers.size() << " recovered containers against "
            << disk_containers.size() << " state cache containers in "
            << "path[" << state_cache->path() << "]";

  // Reconcile between 'recovered_containers' and 'disk_containers':
  // 1) found in both: pass both to _insert_container() to register against cached endpoint
  // 2) found only in state_cache: unregister and delete entry from state_cache
  // 3) found only in recovered_containers: welp, try _register_container() with unknown endpoint as
  //    a last-ditch effort to repair. in practice this case shouldn't happen, it implies on-disk
  //    state was deleted but that containers were kept alive
  // Also note that we only expect this function to be called on start-up, so any internal state
  // in the ContainerAssigner or PortReader(s) *should* be empty.

  std::vector<ContainerEndpoint> containers_to_insert;
  std::vector<mesos::ContainerID> containers_to_remove;
  for (auto state_container : disk_containers) {
    auto recovered_container = recovered_containers.find(state_container.first);
    if (recovered_container != recovered_containers.end()) { // #1
      containers_to_insert.push_back(
          ContainerEndpoint(recovered_container->second, state_container.second));
    } else { // #2
      containers_to_remove.push_back(state_container.first);
    }
  }

  std::vector<mesos::slave::ContainerState> containers_to_register;
  for (auto recovered_container : recovered_containers) {
    if (disk_containers.find(recovered_container.first) == disk_containers.end()) { // #3
      containers_to_register.push_back(recovered_container.second);
    }
  }

  // With the containers sorted above, update their state on our side:

  if (!containers_to_insert.empty()) { // #1
    LOG(INFO) << "Recovering " << containers_to_insert.size()
              << " container endpoints using state cache:";
    for (const ContainerEndpoint& insertme : containers_to_insert) {
      LOG(INFO) << "container["
                << insertme.container.container_id().ShortDebugString() << "] => "
                << insertme.endpoint.string() << " ...";
      // don't need to add to state_cache: it's already there!
      strategy->insert_container(
          insertme.container.container_id(), insertme.container.executor_info(), insertme.endpoint);
    }
  } else {
    LOG(INFO) << "No containers to be recovered using state cache.";
  }

  if (!containers_to_remove.empty()) { // #2
    LOG(INFO) << "Clearing " << containers_to_remove.size()
              << " no-longer-existent containers from state cache:";
    for (const mesos::ContainerID& removeme : containers_to_remove) {
      LOG(INFO) << "container[" << removeme.ShortDebugString() << "] ...";
      unregister_and_update_cache(removeme);
    }
  } else {
    LOG(INFO) << "No containers to clear from state cache.";
  }

  if (!containers_to_register.empty()) { // #3 (shouldn't happen in practice)
    LOG(WARNING) << "Blindly attempting to re-register " << containers_to_register.size()
                 << " recovered containers that weren't listed in on-disk history. "
                 << "These containers may lack functioning metrics until they've been restarted:";
    for (const mesos::slave::ContainerState& registerme : containers_to_register) {
      LOG(WARNING) << "container[" << registerme.container_id().ShortDebugString() << "] => "
                   << "??? ...";
      register_and_update_cache(registerme.container_id(), registerme.executor_info());
    }
  }

  LOG(INFO) << "Container recovery complete";
}

Try<metrics::UDPEndpoint> metrics::ContainerAssigner::register_and_update_cache(
    const mesos::ContainerID container_id, const mesos::ExecutorInfo executor_info) {
  Try<metrics::UDPEndpoint> endpoint = strategy->register_container(container_id, executor_info);
  if (endpoint.isSome()) {
    state_cache->add_container(container_id, endpoint.get());
  }
  return endpoint;
}

void metrics::ContainerAssigner::unregister_and_update_cache(const mesos::ContainerID container_id) {
  strategy->unregister_container(container_id);
  state_cache->remove_container(container_id);
}
