#include "input_assigner.hpp"

#ifdef LINUX_PRCTL_AVAILABLE
#include <sys/prctl.h>
#endif

#include <mutex>
#include <unordered_map>

#include <glog/logging.h>

#include "params.hpp"
#include "port_reader.hpp"
#include "port_writer.hpp"
#include "range_pool.hpp"

#define MAX_PORT 65535
#define THREAD_NAME "stats-io-service"

namespace {
  bool valid_port(size_t port) {
    return port > 0 && port < MAX_PORT;
  }
}

stats::InputAssigner::InputAssigner(const mesos::Parameters& parameters)
  : listen_host(params::get_str(parameters, params::LISTEN_HOST, params::LISTEN_HOST_DEFAULT)),
    annotations_enabled(params::get_bool(parameters, params::ANNOTATIONS, params::ANNOTATIONS_DEFAULT)),
    io_service(),
    writer(new PortWriter(io_service, parameters)) {
  io_service_thread.reset(new std::thread(std::bind(&InputAssigner::run_io_service, this)));
}

stats::InputAssigner::~InputAssigner() {
  // Clean shutdown of all sockets, followed by the IO Service thread itself
  std::unique_lock<std::mutex> lock(mutex);
  writer.reset();
  io_service.stop();
  io_service_thread->join();
  io_service.reset();
  io_service_thread.reset();
}

Try<stats::UDPEndpoint> stats::InputAssigner::register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Registering "
            << "container_id[" << container_id.value() << "] "
            << "executor_info[" << executor_info.ShortDebugString() << "].";
  //TODO shouldnt this just dispatch _register_container() within io_service? (but how to get data back..)
  return _register_container(container_id, executor_info);
}

std::list<Try<stats::UDPEndpoint>> stats::InputAssigner::register_containers(
    const std::list<mesos::slave::ContainerState>& containers) {
  std::unique_lock<std::mutex> lock(mutex);
  std::list<Try<UDPEndpoint>> list;
  LOG(INFO) << "Recovering/re-registering " << containers.size() << " containers...";
  for (const mesos::slave::ContainerState& container : containers) {
    LOG(INFO) << "  Registering "
              << "container_id[" << container.container_id().value() << "] "
              << "executor_info[" << container.executor_info().ShortDebugString() << "].";
    //TODO shouldnt this just dispatch _register_container() within io_service? (but how to get data back..)
    list.push_back(_register_container(container.container_id(), container.executor_info()));
  }
  return list;
}

void stats::InputAssigner::unregister_container(const mesos::ContainerID& container_id) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Unregistering container_id[" << container_id.value() << "].";
  //TODO shouldnt this just dispatch _unregister_container() within io_service?
  _unregister_container(container_id);
}

Try<stats::UDPEndpoint> stats::InputAssigner::get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) {
  std::unique_lock<std::mutex> lock(mutex);
  LOG(INFO) << "Retrieving registered endpoint for executor_info[" << executor_info.ShortDebugString() << "].";
  //TODO shouldnt this just dispatch _get_statsd_endpoint() within io_service? (but how to get data back..)
  return _get_statsd_endpoint(executor_info);
}

void stats::InputAssigner::run_io_service() {
#if defined(LINUX_PRCTL_AVAILABLE) && defined(PR_SET_NAME)
  // Set the thread name to help with any debugging/tracing (uses Linux-specific API)
  prctl(PR_SET_NAME, THREAD_NAME, 0, 0, 0);
#endif
  try {
    LOG(INFO) << "Starting io_service";
    io_service.run();
    LOG(INFO) << "Exited io_service.run()";
  } catch (const std::exception& e) {
    LOG(ERROR) << "io_service.run() threw exception, exiting: " << e.what();
  }
}

// ----

stats::SinglePortAssigner::SinglePortAssigner(const mesos::Parameters& parameters)
  : InputAssigner(parameters),
    single_port_value(params::get_uint(parameters, params::LISTEN_PORT, params::LISTEN_PORT_DEFAULT)) {
  if (!valid_port(single_port_value)) {
    LOG(FATAL) << "Invalid " << params::LISTEN_PORT << " config value.";
  }
}

stats::SinglePortAssigner::~SinglePortAssigner() { }

Try<stats::UDPEndpoint> stats::SinglePortAssigner::_register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  if (!single_port_reader) {
    LOG(INFO) << "Creating single-port reader at endpoint[" << listen_host << ":" << single_port_value << "].";
    // Create/open/register a new port reader only if one doesn't exist.
    std::shared_ptr<port_reader_t> reader(new port_reader_t(
            io_service,
            writer,
            UDPEndpoint(listen_host, single_port_value),
            annotations_enabled));
    Try<UDPEndpoint> endpoint = reader->open();
    if (endpoint.isError()) {
      LOG(ERROR) << "Unable to open single-port reader at endpoint[" << listen_host << ":" << single_port_value << "]:" << endpoint.error();
      return endpoint;
    }
    single_port_reader = reader;
  } else {
    LOG(INFO) << "Reusing existing single-port reader at endpoint[" << listen_host << ":" << single_port_value << "].";
  }

  // Assign the container to the sole port reader.
  return single_port_reader->register_container(container_id, executor_info);
}

void stats::SinglePortAssigner::_unregister_container(const mesos::ContainerID& container_id) {
  if (!single_port_reader) {
    LOG(WARNING) << "No single-port reader had been initialized, cannot unregister container=" << container_id.value();
    return;
  }
  Try<UDPEndpoint> endpoint = single_port_reader->endpoint();
  if (endpoint.isError()) {
    LOG(WARNING) << "Unregistering container[" << container_id.value() << "] from broken single-port endpoint (should be port[" << single_port_value << "]).";
  } else {
    LOG(INFO) << "Unregistering container[" << container_id.value() << "] from single-port endpoint[" << endpoint->string() << "].";
  }
  // Unassign this container from the reader, but leave the reader itself (and its port) open.
  single_port_reader->unregister_container(container_id);
}

Try<stats::UDPEndpoint> stats::SinglePortAssigner::_get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) {
  if (!single_port_reader) {
    std::ostringstream oss;
    oss << "Unable to retrieve single-port endpoint for executor=" << executor_info.ShortDebugString();
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
              << "-> endpoint[" << ret->string() << "].";
  }
  return ret;
}

// ---

stats::EphemeralPortAssigner::EphemeralPortAssigner(const mesos::Parameters& parameters)
  : InputAssigner(parameters) {
  // No extra params to validate
}
stats::EphemeralPortAssigner::~EphemeralPortAssigner() { }

Try<stats::UDPEndpoint> stats::EphemeralPortAssigner::_register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  // Reuse existing reader if available.
  auto iter = container_to_reader.find(container_id);
  if (iter != container_to_reader.end()) {
    Try<UDPEndpoint> ret = iter->second->endpoint();
    if (ret.isError()) {
      LOG(ERROR) << "Existing ephemeral-port endpoint unavailable for "
                << "container[" << container_id.value() << "] executor[" << executor_info.ShortDebugString() << "] ";
    } else {
      LOG(INFO) << "Reusing existing ephemeral-port reader for "
                << "container[" << container_id.value() << "] executor[" << executor_info.ShortDebugString() << "] "
                << "at endpoint[" << ret->string() << "].";
    }
    return ret;
  }

  // Create/open/register a new reader against an ephemeral port.
  std::shared_ptr<port_reader_t> reader(new port_reader_t(
          io_service,
          writer,
          UDPEndpoint(listen_host, 0),
          annotations_enabled));
  Try<UDPEndpoint> endpoint = reader->open();
  if (endpoint.isError()) {
    LOG(ERROR) << "Unable to open ephemeral-port reader at endpoint[" << listen_host << ":???]: " << endpoint.error();
    return endpoint;
  }
  endpoint = reader->register_container(container_id, executor_info);
  executor_to_container[executor_info.executor_id()] = container_id;
  container_to_reader[container_id] = reader;
  LOG(INFO) << "New ephemeral-port reader for container[" << container_id.value() << "] created at endpoint[" << endpoint->string() << "].";
  return endpoint;
}

void stats::EphemeralPortAssigner::_unregister_container(const mesos::ContainerID& container_id) {
  auto iter = container_to_reader.find(container_id);
  if (iter == container_to_reader.end()) {
    LOG(WARNING) << "No ephemeral-port reader had been assigned to container=" << container_id.value() << ", cannot unregister";
    return;
  }
  // Delete the reader (which closes its socket)
  Try<UDPEndpoint> endpoint = iter->second->endpoint();
  if (endpoint.isError()) {
    LOG(WARNING) << "Closing ephemeral-port reader for container[" << container_id.value() << "] at broken endpoint.";
  } else {
    LOG(INFO) << "Closing ephemeral-port reader for container[" << container_id.value() << "] at endpoint[" << iter->second->endpoint()->string() << "].";
  }
  container_to_reader.erase(iter);
}

Try<stats::UDPEndpoint> stats::EphemeralPortAssigner::_get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) {
  auto container_iter = executor_to_container.find(executor_info.executor_id());
  if (container_iter == executor_to_container.end()) {
    std::ostringstream oss;
    oss << "Unable to retrieve ephemeral-port container for executor=" << executor_info.ShortDebugString();
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>::error(oss.str());
  }
  // We only needed the executor_id->container_id for this lookup, so remove it now.
  mesos::ContainerID container_id = container_iter->second;
  executor_to_container.erase(container_iter);

  auto reader_iter = container_to_reader.find(container_id);
  if (reader_iter == container_to_reader.end()) {
    std::ostringstream oss;
    oss << "Unable to retrieve ephemeral-port endpoint for"
        << " container=" << container_id.ShortDebugString()
        << " executor=" << executor_info.ShortDebugString();
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
              << "-> endpoint[" << ret->string() << "].";
  }
  return ret;
}

// ---

stats::PortRangeAssigner::PortRangeAssigner(const mesos::Parameters& parameters)
  : InputAssigner(parameters) {
  size_t port_range_start = params::get_uint(parameters, params::LISTEN_PORT_START, params::LISTEN_PORT_START_DEFAULT);
  if (!valid_port(port_range_start)) {
    LOG(FATAL) << "Invalid " << params::LISTEN_PORT_START << " config value.";
  }
  size_t port_range_end = params::get_uint(parameters, params::LISTEN_PORT_END, params::LISTEN_PORT_END_DEFAULT);
  if (!valid_port(port_range_end)) {
    LOG(FATAL) << "Invalid " << params::LISTEN_PORT_END << " config value.";
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
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  // Reuse existing reader if available.
  auto iter = container_to_reader.find(container_id);
  if (iter != container_to_reader.end()) {
    Try<UDPEndpoint> ret = iter->second->endpoint();
    if (ret.isError()) {
      LOG(ERROR) << "Existing ephemeral-port endpoint unavailable for "
                << "container[" << container_id.value() << "] executor[" << executor_info.ShortDebugString() << "] ";
    } else {
      LOG(INFO) << "Reusing existing ephemeral-port reader for "
                << "container[" << container_id.value() << "] executor[" << executor_info.ShortDebugString() << "] "
                << "at endpoint[" << ret->string() << "].";
    }
    return ret;
  }

  // Get an unused port from the pool range.
  Try<size_t> port = range_pool->take();
  if (port.isError()) {
    std::ostringstream oss;
    oss << "Unable to monitor container " << container_id.value() << ": " << port.error();
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>::error(oss.str());
  }

  // Create/open/register a new reader against the obtained port.
  std::shared_ptr<port_reader_t> reader(new port_reader_t(
          io_service,
          writer,
          UDPEndpoint(listen_host, port.get()),
          annotations_enabled));
  Try<UDPEndpoint> endpoint = reader->open();
  if (endpoint.isError()) {
    LOG(ERROR) << "Unable to open port-range reader at endpoint[" << listen_host << ":" << port.get() << "]: " << endpoint.error();
    range_pool->put(port.get());
    return endpoint;
  }
  endpoint = reader->register_container(container_id, executor_info);
  executor_to_container[executor_info.executor_id()] = container_id;
  container_to_reader[container_id] = reader;
  LOG(INFO) << "New port-range reader for "
            << "container[" << container_id.value() << "] "
            << "executor[" << executor_info.ShortDebugString() << "] "
            << "created at endpoint[" << endpoint->string() << "].";
  return endpoint;
}

void stats::PortRangeAssigner::_unregister_container(const mesos::ContainerID& container_id) {
  auto iter = container_to_reader.find(container_id);
  if (iter == container_to_reader.end()) {
    LOG(WARNING) << "No port-range reader had been assigned to container=" << container_id.value() << ", cannot unregister";
    return;
  }
  // Return the reader's port to the pool, then close/delete the reader.
  Try<UDPEndpoint> endpoint_to_close = iter->second->endpoint();
  if (endpoint_to_close.isError()) {
    LOG(ERROR) << "Endpoint is missing from port reader for container=" << container_id.value() << ", cannot return port to range pool";
  } else {
    LOG(INFO) << "Closing port-range reader for container[" << container_id.value() << "] at endpoint[" << endpoint_to_close->string() << "].";
    range_pool->put(endpoint_to_close->port);
  }
  container_to_reader.erase(iter);
}

Try<stats::UDPEndpoint> stats::PortRangeAssigner::_get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) {
  auto container_iter = executor_to_container.find(executor_info.executor_id());
  if (container_iter == executor_to_container.end()) {
    std::ostringstream oss;
    oss << "Unable to retrieve port-range container for executor=" << executor_info.ShortDebugString();
    LOG(ERROR) << oss.str();
    return Try<UDPEndpoint>::error(oss.str());
  }
  // We only needed the executor_id->container_id for this lookup, so remove it now.
  mesos::ContainerID container_id = container_iter->second;
  executor_to_container.erase(container_iter);

  auto reader_iter = container_to_reader.find(container_id);
  if (reader_iter == container_to_reader.end()) {
    std::ostringstream oss;
    oss << "Unable to retrieve port-range endpoint for"
        << " container=" << container_id.ShortDebugString()
        << " executor=" << executor_info.ShortDebugString();
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
              << "-> endpoint[" << ret->string() << "].";
  }
  return ret;
}
