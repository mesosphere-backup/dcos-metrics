#include "input_assigner.hpp"

#include <glog/logging.h>

#include "input_state_cache.hpp"
#include "params.hpp"
#include "range_pool.hpp"
#include "sync_util.hpp"

#define MAX_PORT 65535

namespace {
  bool valid_port(size_t port) {
    return port > 0 && port <= MAX_PORT;
  }

  // Local util struct for pairing a ContainerState reference with a UDPEndpoint reference
  class ContainerEndpointRefs {
   public:
    ContainerEndpointRefs(
        const mesos::slave::ContainerState& container, const stats::UDPEndpoint& endpoint)
      : container(container), endpoint(endpoint) { }

    const mesos::slave::ContainerState& container;
    const stats::UDPEndpoint& endpoint;
  };
}

stats::InputAssigner::InputAssigner(
    std::shared_ptr<PortRunner> port_runner, const mesos::Parameters& parameters)
  : port_runner(port_runner), state_cache(new InputStateCache(parameters)) { }

stats::InputAssigner::~InputAssigner() { }

Try<stats::UDPEndpoint> stats::InputAssigner::register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Registering and retrieving endpoint for "
            << "container_id[" << container_id.ShortDebugString() << "] "
            << "executor_info[" << executor_info.ShortDebugString() << "].";
  // Dispatch the endpoint retrieval from within the io_service thread, avoiding races with any
  // other endpoint registrations/deregistrations.
  std::function<Try<UDPEndpoint>()> register_container_func =
    std::bind(&InputAssigner::register_and_update_cache, this, container_id, executor_info);
  std::shared_ptr<Try<UDPEndpoint>> out =
    sync_util::dispatch_get<PortRunner, Try<UDPEndpoint>>(
        "register_and_update_cache", *port_runner, register_container_func);
  if (!out) {
    return Try<UDPEndpoint>(Error("Timed out waiting for endpoint retrieval"));
  }
  return *out;
}

void stats::InputAssigner::recover_containers(
    const std::list<mesos::slave::ContainerState>& containers) {
  std::unique_lock<std::mutex> lock(mutex);

  // Dispatch the endpoint recovery from within the io_service thread, avoiding races with any
  // other endpoint registrations/deregistrations.
  port_runner->dispatch(
      std::bind(&InputAssigner::recover_containers_imp, this, containers));
}

void stats::InputAssigner::unregister_container(
    const mesos::ContainerID& container_id) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Unregistering container_id[" << container_id.ShortDebugString() << "].";
  port_runner->dispatch(
      std::bind(&InputAssigner::unregister_and_update_cache, this, container_id));
}

// ----

void stats::InputAssigner::recover_containers_imp(
    const std::list<mesos::slave::ContainerState>& containers) {
  container_id_map<mesos::slave::ContainerState> recovered_containers;
  for (const mesos::slave::ContainerState container : containers) {
    recovered_containers[container.container_id()] = container;
  }

  container_id_map<UDPEndpoint> state_containers = state_cache->get_containers();

  // Reconcile between 'recovered_containers' and 'state_containers':
  // 1) present in both: pass to _insert_container()
  // 2) missing in recovered_containers: delete entry from state_cache
  // 3) missing in state_containers: welp, try _register_container() as best-effort repair
  //    (in practice this 3rd case shouldn't happen, unless on-disk state was lost)

  std::vector<ContainerEndpointRefs> containers_to_insert;
  std::vector<mesos::ContainerID> containers_to_remove;
  for (auto state_container : state_containers) {
    auto recovered_container = recovered_containers.find(state_container.first);
    if (recovered_container == recovered_containers.end()) { // #2
      containers_to_remove.push_back(state_container.first);
    } else { // #1
      containers_to_insert.push_back(
          ContainerEndpointRefs(recovered_container->second, state_container.second));
    }
  }

  std::vector<mesos::slave::ContainerState> containers_to_register;
  for (auto recovered_container : recovered_containers) {
    if (state_containers.find(recovered_container.first) == state_containers.end()) { // #3
      containers_to_register.push_back(recovered_container.second);
    }
  }

  // With the containers sorted above, update their state on our side:

  if (!containers_to_remove.empty()) { // #2
    LOG(INFO) << "Removing no-longer-existent containers from on-disk history:";
    for (const mesos::ContainerID& removeme : containers_to_remove) {
      LOG(INFO) << "  container_id[" << removeme.ShortDebugString() << "]";
      unregister_and_update_cache(removeme);
    }
  }

  if (!containers_to_insert.empty()) { // #1
    LOG(INFO) << "Recovering container endpoints using on-disk history:";
    for (const ContainerEndpointRefs& insertme : containers_to_insert) {
      LOG(INFO) << "  container_id["
                << insertme.container.container_id().ShortDebugString() << "].";
      // don't need to add to state_cache: it's already there!
      _insert_container(
          insertme.container.container_id(), insertme.container.executor_info(), insertme.endpoint);
    }
  }

  if (!containers_to_register.empty()) { // #3
    LOG(WARNING) << "Re-registering containers that weren't listed in on-disk history. These"
                 << " containers may lack functioning metrics until they've been restarted.";
    for (const mesos::slave::ContainerState& registerme : containers_to_register) {
      LOG(INFO) << "  container_id["
                << registerme.container_id().ShortDebugString() << "].";
      register_and_update_cache(registerme.container_id(), registerme.executor_info());
    }
  }

  LOG(INFO) << "Container recovery complete";
}

Try<stats::UDPEndpoint> stats::InputAssigner::register_and_update_cache(
    const mesos::ContainerID container_id,
    const mesos::ExecutorInfo executor_info) {
  Try<stats::UDPEndpoint> endpoint = _register_container(container_id, executor_info);
  if (endpoint.isSome()) {
    state_cache->add_container(container_id, endpoint.get());
  }
  return endpoint;
}

void stats::InputAssigner::unregister_and_update_cache(
    const mesos::ContainerID container_id) {
  _unregister_container(container_id);
  state_cache->remove_container(container_id);
}

// ----

stats::SinglePortAssigner::SinglePortAssigner(
    std::shared_ptr<PortRunner> port_runner, const mesos::Parameters& parameters)
  : InputAssigner(port_runner, parameters),
    single_port_value(
        params::get_uint(parameters, params::LISTEN_PORT, params::LISTEN_PORT_DEFAULT)) {
  if (!valid_port(single_port_value)) {
    LOG(FATAL) << "Invalid " << params::LISTEN_PORT << " config value: " << single_port_value;
  }
}

stats::SinglePortAssigner::~SinglePortAssigner() { }

Try<stats::UDPEndpoint> stats::SinglePortAssigner::_register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  if (!single_port_reader) {
    LOG(INFO) << "Creating single-port reader at port[" << single_port_value << "].";
    // Create/open/register a new port reader only if one doesn't exist.
    std::shared_ptr<PortReader> reader = port_runner->create_port_reader(single_port_value);
    Try<UDPEndpoint> endpoint = reader->open();
    if (endpoint.isError()) {
      std::ostringstream oss;
      oss << "Unable to open single-port reader at port[" << single_port_value << "]: "
          << endpoint.error();
      LOG(ERROR) << oss.str();
      return Try<UDPEndpoint>(Error(oss.str()));
    }
    single_port_reader = reader;
  }
  return single_port_reader->register_container(container_id, executor_info);
}

void stats::SinglePortAssigner::_insert_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info,
    const UDPEndpoint& endpoint) {
  //TODO
}

void stats::SinglePortAssigner::_unregister_container(
    const mesos::ContainerID& container_id) {
  if (!single_port_reader) {
    LOG(WARNING) << "No single-port reader had been initialized, cannot unregister "
                 << "container[" << container_id.ShortDebugString() << "]";
    return;
  }
  Try<UDPEndpoint> endpoint = single_port_reader->endpoint();
  if (endpoint.isError()) {
    LOG(WARNING) << "Unregistering container[" << container_id.ShortDebugString() << "] "
                 << "from broken single-port endpoint "
                 << "(should be port[" << single_port_value << "]).";
  } else {
    LOG(INFO) << "Unregistering container[" << container_id.ShortDebugString() << "] "
              << "from single-port endpoint[" << endpoint.get().string() << "].";
  }
  // Unassign this container from the reader, but leave the reader itself (and its port) open.
  single_port_reader->unregister_container(container_id);
}

// ---

stats::EphemeralPortAssigner::EphemeralPortAssigner(
    std::shared_ptr<PortRunner> port_runner, const mesos::Parameters& parameters)
  : InputAssigner(port_runner, parameters) {
  // No extra params to validate
}

stats::EphemeralPortAssigner::~EphemeralPortAssigner() { }

Try<stats::UDPEndpoint> stats::EphemeralPortAssigner::_register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  // Reuse existing reader if available.
  // This isn't expected to happen in practice, but just in case..
  auto iter = container_to_reader.find(container_id);
  if (iter != container_to_reader.end()) {
    Try<UDPEndpoint> ret = iter->second->endpoint();
    if (ret.isError()) {
      std::ostringstream oss;
      oss << "Existing ephemeral-port endpoint unavailable for "
          << "container[" << container_id.ShortDebugString() << "] "
          << "executor[" << executor_info.ShortDebugString() << "] ";
      LOG(ERROR) << oss.str();
      return Try<UDPEndpoint>(Error(oss.str()));
    }
    LOG(INFO) << "Reusing existing ephemeral-port reader for "
              << "container[" << container_id.ShortDebugString() << "] "
              << "executor[" << executor_info.ShortDebugString() << "] "
              << "at endpoint[" << ret.get().string() << "].";
    return ret;
  }

  // Create/open/register a new reader against an ephemeral port.
  std::shared_ptr<PortReader> reader = port_runner->create_port_reader(0 /* port */);
  Try<UDPEndpoint> endpoint = reader->open();
  if (endpoint.isError()) {
    std::ostringstream oss;
    oss << "Unable to open ephemeral-port reader at port[???]: "
        << endpoint.error();
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>(Error(oss.str()));
  }
  container_to_reader[container_id] = reader;
  reader->register_container(container_id, executor_info);
  LOG(INFO) << "New ephemeral-port reader for container[" << container_id.ShortDebugString() << "] "
            << "created at endpoint[" << endpoint.get().string() << "].";
  return endpoint;
}

void stats::EphemeralPortAssigner::_insert_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info,
    const UDPEndpoint& endpoint) {
  //TODO
}

void stats::EphemeralPortAssigner::_unregister_container(
    const mesos::ContainerID& container_id) {
  auto iter = container_to_reader.find(container_id);
  if (iter == container_to_reader.end()) {
    LOG(WARNING) << "No ephemeral-port reader had been assigned to "
                 << "container[" << container_id.ShortDebugString() << "], cannot unregister";
    return;
  }
  // Delete the reader (which closes its socket)
  Try<UDPEndpoint> endpoint = iter->second->endpoint();
  if (endpoint.isError()) {
    LOG(WARNING) << "Closing ephemeral-port reader for "
                 << "container[" << container_id.ShortDebugString() << "] at broken endpoint.";
  } else {
    LOG(INFO) << "Closing ephemeral-port reader for "
              << "container[" << container_id.ShortDebugString() << "] at "
              << "endpoint[" << endpoint.get().string() << "].";
  }
  container_to_reader.erase(iter);
}

// ---

stats::PortRangeAssigner::PortRangeAssigner(
    std::shared_ptr<PortRunner> port_runner, const mesos::Parameters& parameters)
  : InputAssigner(port_runner, parameters) {
  size_t port_range_start =
    params::get_uint(parameters, params::LISTEN_PORT_START, params::LISTEN_PORT_START_DEFAULT);
  if (!valid_port(port_range_start)) {
    LOG(FATAL) << "Invalid " << params::LISTEN_PORT_START << " config value: " << port_range_start;
  }
  size_t port_range_end =
    params::get_uint(parameters, params::LISTEN_PORT_END, params::LISTEN_PORT_END_DEFAULT);
  if (!valid_port(port_range_end)) {
    LOG(FATAL) << "Invalid " << params::LISTEN_PORT_END << " config value: " << port_range_end;
  }
  if (port_range_end <= port_range_start) {
    LOG(FATAL) << params::LISTEN_PORT_START << " (=" << port_range_start << ")"
               << " must be less than "
               << params::LISTEN_PORT_END << " (=" << port_range_end << ")";
  }
  range_pool.reset(new RangePool(port_range_start, port_range_end));
}

stats::PortRangeAssigner::~PortRangeAssigner() { }

Try<stats::UDPEndpoint> stats::PortRangeAssigner::_register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  // Reuse existing reader if available.
  // This isn't expected to happen in practice, but just in case..
  auto iter = container_to_reader.find(container_id);
  if (iter != container_to_reader.end()) {
    Try<UDPEndpoint> ret = iter->second->endpoint();
    if (ret.isError()) {
      std::ostringstream oss;
      oss << "Existing port-range endpoint unavailable for "
          << "container[" << container_id.ShortDebugString() << "] "
          << "executor[" << executor_info.ShortDebugString() << "] ";
      LOG(ERROR) << oss.str();
      return Try<UDPEndpoint>(Error(oss.str()));
    } else {
      LOG(INFO) << "Reusing existing port-range reader for "
                << "container[" << container_id.ShortDebugString() << "] "
                << "executor[" << executor_info.ShortDebugString() << "] "
                << "at endpoint[" << ret.get().string() << "].";
      return ret;
    }
  }

  // Get an unused port from the pool range.
  Try<size_t> port = range_pool->take();
  if (port.isError()) {
    std::ostringstream oss;
    oss << "Unable to monitor "
        << "container[" << container_id.ShortDebugString() << "]: " << port.error();
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>(Error(oss.str()));
  }

  // Create/open/register a new reader against the obtained port.
  std::shared_ptr<PortReader> reader = port_runner->create_port_reader(port.get());
  Try<UDPEndpoint> endpoint = reader->open();
  if (endpoint.isError()) {
    std::ostringstream oss;
    oss << "Unable to open port-range reader at port[" << port.get() << "]: "
        << endpoint.error();
    LOG(ERROR) << oss.str();
    range_pool->put(port.get());
    return Try<UDPEndpoint>(Error(oss.str()));
  }
  reader->register_container(container_id, executor_info);
  container_to_reader[container_id] = reader;
  LOG(INFO) << "New port-range reader for "
            << "container[" << container_id.ShortDebugString() << "] "
            << "executor[" << executor_info.ShortDebugString() << "] "
            << "created at endpoint[" << endpoint.get().string() << "].";
  return endpoint;
}

void stats::PortRangeAssigner::_insert_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info,
    const UDPEndpoint& endpoint) {
  //TODO
}

void stats::PortRangeAssigner::_unregister_container(
    const mesos::ContainerID& container_id) {
  auto iter = container_to_reader.find(container_id);
  if (iter == container_to_reader.end()) {
    LOG(WARNING) << "No port-range reader had been assigned to "
                 << "container[" << container_id.ShortDebugString() << "], cannot unregister";
    return;
  }
  // Return the reader's port to the pool, then close/delete the reader.
  Try<UDPEndpoint> endpoint_to_close = iter->second->endpoint();
  if (endpoint_to_close.isError()) {
    LOG(ERROR) << "Endpoint is missing from port reader for "
               << "container[" << container_id.ShortDebugString() << "], "
               << "cannot return port to range pool";
  } else {
    LOG(INFO) << "Closing port-range reader for "
              << "container[" << container_id.ShortDebugString() << "] at "
              << "endpoint[" << endpoint_to_close.get().string() << "].";
    range_pool->put(endpoint_to_close.get().port);
  }
  container_to_reader.erase(iter);
}
