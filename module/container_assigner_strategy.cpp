#include "container_assigner_strategy.hpp"

#include <glog/logging.h>

#include "params.hpp"
#include "io_runner.hpp"
#include "range_pool.hpp"

#define MAX_PORT 65535

namespace {
  bool valid_port(size_t port) {
    return port > 0 && port <= MAX_PORT;
  }

  // Local util struct for pairing a ContainerState with a UDPEndpoint
  class ContainerEndpoint {
   public:
    ContainerEndpoint(
        const mesos::slave::ContainerState& container, const metrics::UDPEndpoint& endpoint)
      : container(container), endpoint(endpoint) { }

    const mesos::slave::ContainerState container;
    const metrics::UDPEndpoint endpoint;
  };
}

metrics::SinglePortStrategy::SinglePortStrategy(
    std::shared_ptr<IORunner> io_runner, const mesos::Parameters& parameters)
  : io_runner(io_runner),
    single_port_value(
        params::get_uint(parameters, params::LISTEN_PORT, params::LISTEN_PORT_DEFAULT)) {
  if (!valid_port(single_port_value)) {
    LOG(FATAL) << "Invalid " << params::LISTEN_PORT << " config value: " << single_port_value;
  }
}
metrics::SinglePortStrategy::~SinglePortStrategy() { }

Try<metrics::UDPEndpoint> metrics::SinglePortStrategy::register_container(
    const mesos::ContainerID& container_id, const mesos::ExecutorInfo& executor_info) {
  Try<std::shared_ptr<ContainerReader>> reader = init_reader();
  if (reader.isError()) {
    std::ostringstream oss;
    oss << "Unable to register container[" << container_id.ShortDebugString() << "]: "
        << reader.error();
    LOG(ERROR) << oss.str();
    return Try<metrics::UDPEndpoint>(Error(oss.str()));
  }

  reader.get()->register_container(container_id, executor_info);
  return reader.get()->endpoint();
}

void metrics::SinglePortStrategy::insert_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info,
    const UDPEndpoint& endpoint) {
  Try<std::shared_ptr<ContainerReader>> reader = init_reader();
  if (reader.isError()) {
    LOG(ERROR) << "Unable to recover container[" << container_id.ShortDebugString() << "]: "
               << reader.error();
    return;
  }

  Try<UDPEndpoint> cur_endpoint = reader.get()->endpoint();
  if (cur_endpoint.isError()) {
    LOG(WARNING) << "Recovered container[" << container_id.ShortDebugString() << "] "
                 << "is being attached to broken single-port endpoint.";
  } else if (endpoint.port != cur_endpoint.get().port) {
    LOG(WARNING) << "Recovered container[" << container_id.ShortDebugString() << "] "
                 << "is using port[" << endpoint.port << "] "
                 << "while metrics config specifies port[" << cur_endpoint.get().port << ". "
                 << "Registering container against port[" << cur_endpoint.get().port << "], "
                 << "but it won't work.";
  }
  reader.get()->register_container(container_id, executor_info);
}

void metrics::SinglePortStrategy::unregister_container(
    const mesos::ContainerID& container_id) {
  if (!single_container_reader) {
    LOG(INFO) << "No single-port reader had been initialized, cannot unregister "
              << "container[" << container_id.ShortDebugString() << "].";
    return;
  }
  Try<UDPEndpoint> endpoint = single_container_reader->endpoint();
  if (endpoint.isError()) {
    LOG(WARNING) << "Unregistering container[" << container_id.ShortDebugString() << "] "
                 << "from broken single-port endpoint "
                 << "(should be port[" << single_port_value << "]).";
  } else {
    LOG(INFO) << "Unregistering container[" << container_id.ShortDebugString() << "] "
              << "from single-port endpoint[" << endpoint.get().string() << "].";
  }
  // Unassign this container from the reader, but leave the reader itself (and its port) open.
  single_container_reader->unregister_container(container_id);
}

Try<std::shared_ptr<metrics::ContainerReader>> metrics::SinglePortStrategy::init_reader() {
  if (!single_container_reader) {
    LOG(INFO) << "Creating single-port reader at port[" << single_port_value << "].";
    // Create/open/register a new port reader only if one doesn't exist.
    std::shared_ptr<ContainerReader> reader = io_runner->create_container_reader(single_port_value);
    Try<UDPEndpoint> endpoint = reader->open();
    if (endpoint.isError()) {
      std::ostringstream oss;
      oss << "Unable to open single-port reader at port[" << single_port_value << "]: "
          << endpoint.error();
      return Try<std::shared_ptr<ContainerReader>>(Error(oss.str()));
    }
    single_container_reader = reader;
  }
  return Try<std::shared_ptr<ContainerReader>>(single_container_reader);
}

// ---

metrics::EphemeralPortStrategy::EphemeralPortStrategy(std::shared_ptr<IORunner> io_runner)
  : io_runner(io_runner) { }
metrics::EphemeralPortStrategy::~EphemeralPortStrategy() { }

Try<metrics::UDPEndpoint> metrics::EphemeralPortStrategy::register_container(
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
  std::shared_ptr<ContainerReader> reader = io_runner->create_container_reader(0 /* port */);
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

void metrics::EphemeralPortStrategy::insert_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info,
    const UDPEndpoint& endpoint) {
  // Don't bother with reusing an existing reader for the container like in _register_container.
  // Assume that we're getting the latest information about this container, which should
  // override any existing local state. This shouldn't come up in practice, but just sayin...

  // Skip ephemeral behavior: Create/open/register a new reader against the specified endpoint
  std::shared_ptr<ContainerReader> reader = io_runner->create_container_reader(endpoint.port);
  Try<UDPEndpoint> new_endpoint = reader->open();
  if (new_endpoint.isError()) {
    LOG(ERROR) << "Unable to recover ephemeral-port reader at port[" << endpoint.port << "] "
               << "for container[" << container_id.ShortDebugString() << "]: "
               << new_endpoint.error();
    return;
  }
  container_to_reader[container_id] = reader;
  reader->register_container(container_id, executor_info);
  LOG(INFO) << "Recovered ephemeral-port reader for "
            << "container[" << container_id.ShortDebugString() << "]: "
            << "orig_endpoint[" << endpoint.string() << "] => "
            << "new_endpoint[" << new_endpoint.get().string() << "].";
  return;
}

void metrics::EphemeralPortStrategy::unregister_container(
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

metrics::PortRangeStrategy::PortRangeStrategy(
    std::shared_ptr<IORunner> io_runner, const mesos::Parameters& parameters)
  : io_runner(io_runner) {
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

metrics::PortRangeStrategy::~PortRangeStrategy() { }

Try<metrics::UDPEndpoint> metrics::PortRangeStrategy::register_container(
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
  std::shared_ptr<ContainerReader> reader = io_runner->create_container_reader(port.get());
  Try<UDPEndpoint> endpoint = reader->open();
  if (endpoint.isError()) {
    std::ostringstream oss;
    oss << "Unable to open port-range reader at port[" << port.get() << "]: "
        << endpoint.error();
    LOG(ERROR) << oss.str();
    // return port since we can't use it
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

void metrics::PortRangeStrategy::insert_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info,
    const UDPEndpoint& endpoint) {
  // Don't bother with reusing an existing reader like in _register_container above:
  // Assume that we're getting the latest information about this container, which should
  // override any existing local state. This shouldn't come up in practice, but just sayin...

  // Get the recovered port from the pool.
  Try<size_t> port = range_pool->get(endpoint.port);
  if (port.isError()) {
    LOG(ERROR) << "Unable to recover port[" << endpoint.port << "] for "
               << "container[" << container_id.ShortDebugString() << "]: " << port.error();
    return;
  }

  // Create/open/register a new reader against the recovered port.
  std::shared_ptr<ContainerReader> reader = io_runner->create_container_reader(port.get());
  Try<UDPEndpoint> new_endpoint = reader->open();
  if (new_endpoint.isError()) {
    LOG(ERROR) << "Unable to open recovered port-range reader at port[" << port.get() << "]: "
               << new_endpoint.error();
    // return port since we can't use it
    range_pool->put(port.get());
    return;
  }
  reader->register_container(container_id, executor_info);
  container_to_reader[container_id] = reader;
  LOG(INFO) << "Recovered port-range reader for "
            << "container[" << container_id.ShortDebugString() << "]: "
            << "orig_endpoint[" << endpoint.string() << "] => "
            << "new_endpoint[" << new_endpoint.get().string() << "].";
}

void metrics::PortRangeStrategy::unregister_container(
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
