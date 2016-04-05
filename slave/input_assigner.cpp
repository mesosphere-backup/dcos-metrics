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

void stats::InputAssigner::register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Registering "
            << "container_id[" << container_id.ShortDebugString() << "] "
            << "executor_info[" << executor_info.ShortDebugString() << "].";
  port_runner->dispatch(std::bind(&InputAssigner::_register_container, this,
          container_id, executor_info));
}

void stats::InputAssigner::register_containers(
    const std::list<mesos::slave::ContainerState>& containers) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Recovering/re-registering " << containers.size() << " containers...";
  //TODO in the common case of ephemeral port readers, this doesn't recover the previous port!
  //  for now, if a slave is restarted, any containers within that slave will stop transmitting data
  //  we want to re-open the port that this container was originally configured with.
  //  in theory, we are passed an envvar list via executor_info, but it doesn't have STATSD_UDP_*
  //  alternate option: store state somewhere on disk, then attempt to recover from that?
  for (const mesos::slave::ContainerState& container : containers) {
    LOG(INFO) << "  Registering "
              << "container_id[" << container.container_id().ShortDebugString() << "] "
              << "executor_info[" << container.executor_info().ShortDebugString() << "].";
    port_runner->dispatch(std::bind(&InputAssigner::_register_container, this,
            container.container_id(), container.executor_info()));
  }
}

void stats::InputAssigner::unregister_container(
    const mesos::ContainerID& container_id) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Unregistering container_id[" << container_id.ShortDebugString() << "].";
  port_runner->dispatch(
      std::bind(&InputAssigner::_unregister_container, this, container_id));
}

Try<stats::UDPEndpoint> stats::InputAssigner::get_statsd_endpoint(
    const mesos::ExecutorInfo& executor_info) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Retrieving registered endpoint for "
            << "executor_info[" << executor_info.ShortDebugString() << "].";
  // Dispatch the endpoint retrieval from within the io_service thread, avoiding races with any
  // other endpoint registrations/deregistrations.
  std::function<Try<stats::UDPEndpoint>()> get_statsd_endpoint_func =
    std::bind(&InputAssigner::_get_statsd_endpoint, this, std::cref(executor_info));
  std::shared_ptr<Try<stats::UDPEndpoint>> out =
    sync_util::dispatch_get<PortRunner, Try<stats::UDPEndpoint>>(
        "get_statsd_endpoint", *port_runner, get_statsd_endpoint_func);
  if (!out) {
    return Try<stats::UDPEndpoint>::error("Timed out waiting for endpoint retrieval");
  }
  return *out;
}

void stats::InputAssigner::get_and_insert_response_cb(
    mesos::ExecutorInfo executor_info, std::shared_ptr<Try<stats::UDPEndpoint>>* out) {
  Try<stats::UDPEndpoint> ret = _get_statsd_endpoint(executor_info);
  out->reset(new Try<stats::UDPEndpoint>(ret));
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

void stats::SinglePortAssigner::_register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  if (!single_port_reader) {
    LOG(INFO) << "Creating single-port reader at port[" << single_port_value << "].";
    // Create/open/register a new port reader only if one doesn't exist.
    std::shared_ptr<PortReader> reader = port_runner->create_port_reader(single_port_value);
    Try<UDPEndpoint> endpoint = reader->open();
    if (endpoint.isError()) {
      LOG(ERROR) << "Unable to open single-port reader at port[" << single_port_value << "]: "
                 << endpoint.error();
      return;
    }
    single_port_reader = reader;
  } else {
    Try<UDPEndpoint> endpoint = single_port_reader->endpoint();
    if (endpoint.isError()) {
      LOG(INFO) << "Reusing existing single-port reader at endpoint[???].";
    } else {
      LOG(INFO) << "Reusing existing single-port reader at "
                << "endpoint[" << endpoint.get().string() << "].";
    }
  }

  // Assign the container to the sole port reader.
  single_port_reader->register_container(container_id, executor_info);
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

Try<stats::UDPEndpoint> stats::SinglePortAssigner::_get_statsd_endpoint(
    const mesos::ExecutorInfo& executor_info) {
  if (!single_port_reader) {
    std::ostringstream oss;
    oss << "Unable to retrieve single-port endpoint for "
        << "executor[" << executor_info.ShortDebugString() << "]";
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>::error(oss.str());
  }

  Try<UDPEndpoint> ret = single_port_reader->endpoint();
  if (ret.isError()) {
    LOG(ERROR) << "Single-port endpoint unavailable for "
               << "executor[" << executor_info.ShortDebugString() << "] ";
  } else {
    LOG(INFO) << "Returning single-port endpoint for "
              << "executor[" << executor_info.ShortDebugString() << "] "
              << "-> endpoint[" << ret.get().string() << "].";
  }
  return ret;
}

// ---

stats::EphemeralPortAssigner::EphemeralPortAssigner(std::shared_ptr<PortRunner> port_runner)
  : InputAssigner(port_runner) {
  // No extra params to validate
}

stats::EphemeralPortAssigner::~EphemeralPortAssigner() { }

void stats::EphemeralPortAssigner::_register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  // Reuse existing reader if available.
  auto iter = container_to_reader.find(container_id);
  if (iter != container_to_reader.end()) {
    Try<UDPEndpoint> ret = iter->second->endpoint();
    if (ret.isError()) {
      LOG(ERROR) << "Existing ephemeral-port endpoint unavailable for "
                 << "container[" << container_id.ShortDebugString() << "] "
                 << "executor[" << executor_info.ShortDebugString() << "] ";
    } else {
      LOG(INFO) << "Reusing existing ephemeral-port reader for "
                << "container[" << container_id.ShortDebugString() << "] "
                << "executor[" << executor_info.ShortDebugString() << "] "
                << "at endpoint[" << ret.get().string() << "].";
    }
    return;
  }

  // Create/open/register a new reader against an ephemeral port.
  std::shared_ptr<PortReader> reader = port_runner->create_port_reader(0 /* port */);
  Try<UDPEndpoint> endpoint = reader->open();
  if (endpoint.isError()) {
    LOG(ERROR) << "Unable to open ephemeral-port reader at port[???]: "
               << endpoint.error();
    return;
  }
  endpoint = reader->register_container(container_id, executor_info);
  executor_to_container[executor_info.executor_id()] = container_id;
  container_to_reader[container_id] = reader;
  LOG(INFO) << "New ephemeral-port reader for container[" << container_id.ShortDebugString() << "] "
            << "created at endpoint[" << endpoint.get().string() << "].";
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

Try<stats::UDPEndpoint> stats::EphemeralPortAssigner::_get_statsd_endpoint(
    const mesos::ExecutorInfo& executor_info) {
  auto container_iter = executor_to_container.find(executor_info.executor_id());
  if (container_iter == executor_to_container.end()) {
    std::ostringstream oss;
    oss << "Unable to retrieve ephemeral-port container for "
        << "executor[" << executor_info.ShortDebugString() << "]";
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>::error(oss.str());
  }
  // We only needed the executor_id->container_id for this one lookup, so remove it now.
  mesos::ContainerID container_id = container_iter->second;
  executor_to_container.erase(container_iter);

  auto reader_iter = container_to_reader.find(container_id);
  if (reader_iter == container_to_reader.end()) {
    std::ostringstream oss;
    oss << "Unable to retrieve ephemeral-port endpoint for "
        << "container[" << container_id.ShortDebugString() << "] "
        << "executor[" << executor_info.ShortDebugString() << "]";
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>::error(oss.str());
  }

  Try<UDPEndpoint> ret = reader_iter->second->endpoint();
  if (ret.isError()) {
    LOG(ERROR) << "Ephemeral-port endpoint unavailable for "
              << "executor[" << executor_info.ShortDebugString() << "] ";
  } else {
    LOG(INFO) << "Returning ephemeral-port endpoint for "
              << "executor[" << executor_info.ShortDebugString() << "] "
              << "-> endpoint[" << ret.get().string() << "].";
  }
  return ret;
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

void stats::PortRangeAssigner::_register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  // Reuse existing reader if available.
  auto iter = container_to_reader.find(container_id);
  if (iter != container_to_reader.end()) {
    Try<UDPEndpoint> ret = iter->second->endpoint();
    if (ret.isError()) {
      LOG(ERROR) << "Existing ephemeral-port endpoint unavailable for "
                 << "container[" << container_id.ShortDebugString() << "] "
                 << "executor[" << executor_info.ShortDebugString() << "] ";
    } else {
      LOG(INFO) << "Reusing existing ephemeral-port reader for "
                << "container[" << container_id.ShortDebugString() << "] "
                << "executor[" << executor_info.ShortDebugString() << "] "
                << "at endpoint[" << ret.get().string() << "].";
    }
    return;
  }

  // Get an unused port from the pool range.
  Try<size_t> port = range_pool->take();
  if (port.isError()) {
    LOG(ERROR) << "Unable to monitor "
               << "container[" << container_id.ShortDebugString() << "]: " << port.error();
    return;
  }

  // Create/open/register a new reader against the obtained port.
  std::shared_ptr<PortReader> reader = port_runner->create_port_reader(port.get());
  Try<UDPEndpoint> endpoint = reader->open();
  if (endpoint.isError()) {
    LOG(ERROR) << "Unable to open port-range reader at port[" << port.get() << "]: "
               << endpoint.error();
    range_pool->put(port.get());
    return;
  }
  endpoint = reader->register_container(container_id, executor_info);
  executor_to_container[executor_info.executor_id()] = container_id;
  container_to_reader[container_id] = reader;
  LOG(INFO) << "New port-range reader for "
            << "container[" << container_id.ShortDebugString() << "] "
            << "executor[" << executor_info.ShortDebugString() << "] "
            << "created at endpoint[" << endpoint.get().string() << "].";
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

Try<stats::UDPEndpoint> stats::PortRangeAssigner::_get_statsd_endpoint(
    const mesos::ExecutorInfo& executor_info) {
  auto container_iter = executor_to_container.find(executor_info.executor_id());
  if (container_iter == executor_to_container.end()) {
    std::ostringstream oss;
    oss << "Unable to retrieve port-range container for "
        << "executor[" << executor_info.ShortDebugString() << "]";
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>::error(oss.str());
  }
  // We only needed the executor_id->container_id for this lookup, so remove it now.
  mesos::ContainerID container_id = container_iter->second;
  executor_to_container.erase(container_iter);

  auto reader_iter = container_to_reader.find(container_id);
  if (reader_iter == container_to_reader.end()) {
    std::ostringstream oss;
    oss << "Unable to retrieve port-range endpoint for "
        << "container[" << container_id.ShortDebugString() << "] "
        << "executor[" << executor_info.ShortDebugString() << "]";
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>::error(oss.str());
  }

  Try<UDPEndpoint> ret = reader_iter->second->endpoint();
  if (ret.isError()) {
    LOG(ERROR) << "Port-range endpoint unavailable for "
              << "executor[" << executor_info.ShortDebugString() << "] ";
  } else {
    LOG(INFO) << "Returning port-range endpoint for "
              << "executor[" << executor_info.ShortDebugString() << "] "
              << "-> endpoint[" << ret.get().string() << "].";
  }
  return ret;
}
