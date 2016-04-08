#include "input_assigner.hpp"

#include <glog/logging.h>

#include "sync_util.hpp"
#include "params.hpp"
#include "range_pool.hpp"

#define MAX_PORT 65535

namespace {
  bool valid_port(size_t port) {
    return port > 0 && port <= MAX_PORT;
  }
}

stats::InputAssigner::InputAssigner(std::shared_ptr<PortRunner> port_runner)
  : port_runner(port_runner) { }

stats::InputAssigner::~InputAssigner() { }

Try<stats::UDPEndpoint> stats::InputAssigner::register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Registering and retrieving endpoint for "
            << "container_id[" << container_id.ShortDebugString() << "] "
            << "executor_info[" << executor_info.ShortDebugString() << "].";
  // Dispatch the endpoint retrieval from within the io_service thread, avoiding races with any
  // other endpoint registrations/deregistrations.
  std::function<Try<stats::UDPEndpoint>()> register_container_func =
    std::bind(&InputAssigner::_register_container, this,
        std::cref(container_id), std::cref(executor_info));
  std::shared_ptr<Try<stats::UDPEndpoint>> out =
    sync_util::dispatch_get<PortRunner, Try<stats::UDPEndpoint>>(
        "register_container", *port_runner, register_container_func);
  if (!out) {
    return Try<stats::UDPEndpoint>(Error("Timed out waiting for endpoint retrieval"));
  }
  return *out;
}

void stats::InputAssigner::recover_containers(
    const std::list<mesos::slave::ContainerState>& containers) {
  std::unique_lock<std::mutex> lock(mutex);
  //TODO:
  //  we want to recover the state that the container was originally associated with (id -> ip:port)
  //  the STATSD_UDP_* envvars aren't returned in ContainerState, so we need to cache state to disk,
  //  then recover from that when this function is called. the on-disk state should be wiped of
  //  any containers that aren't present in the provided list.
  if (!containers.empty()) {
    LOG(WARNING) << "Recovering endpoints of preexisting containers is unsupported."
                 << " These preexisting containers will not have metrics until they are restarted:";
    for (const mesos::slave::ContainerState& container : containers) {
      LOG(WARNING) << "  container_id[" << container.container_id().ShortDebugString() << "].";
    }
  }
}

void stats::InputAssigner::unregister_container(
    const mesos::ContainerID& container_id) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Unregistering container_id[" << container_id.ShortDebugString() << "].";
  port_runner->dispatch(
      std::bind(&InputAssigner::_unregister_container, this, container_id));
}

// ----

stats::SinglePortAssigner::SinglePortAssigner(
    std::shared_ptr<PortRunner> port_runner, const mesos::Parameters& parameters)
  : InputAssigner(port_runner),
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

stats::EphemeralPortAssigner::EphemeralPortAssigner(std::shared_ptr<PortRunner> port_runner)
  : InputAssigner(port_runner) {
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
  : InputAssigner(port_runner) {
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
