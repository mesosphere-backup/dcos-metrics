#include "input_assigner.hpp"

#ifdef LINUX_PRCTL_AVAILABLE
#include <sys/prctl.h>
#endif

#include <mutex>
#include <thread>
#include <unordered_map>

#include <boost/asio.hpp>
#include <glog/logging.h>

#include "params.hpp"
#include "port_reader.hpp"
#include "port_writer.hpp"
#include "range_pool.hpp"

#define MAX_PORT 65535

namespace stats {
  bool valid_port(size_t port) {
    return port > 0 && port < MAX_PORT;
  }

  typedef std::shared_ptr<PortReader> port_reader_ptr_t;

  /**
   * Base class for a port assignment strategy. See below for implementations.
   */
  class InputAssignerImpl {
   public:
    InputAssignerImpl(const mesos::Parameters& parameters)
      : listen_host(params::get_str(parameters, params::LISTEN_HOST, params::LISTEN_HOST_DEFAULT)),
        annotations_enabled(params::get_bool(parameters, params::ANNOTATIONS, params::ANNOTATIONS_DEFAULT)),
        io_service(),
        writer(new PortWriter(io_service, parameters)) {
      io_service_thread.reset(new std::thread(std::bind(&InputAssignerImpl::run_io_service, this)));
    }
    virtual ~InputAssignerImpl() {
      // Clean shutdown of all sockets, followed by the IO Service thread itself
      std::unique_lock<std::mutex> lock(mutex);
      writer.reset();
      io_service.stop();
      io_service_thread->join();
    }

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) {
      std::unique_lock<std::mutex> lock(mutex);
      return _register_container(container_id, executor_info);
    }

    std::list<Try<stats::UDPEndpoint>> register_containers(
        const std::list<mesos::slave::ContainerState>& containers) {
      std::unique_lock<std::mutex> lock(mutex);
      std::list<Try<UDPEndpoint>> list;
      LOG(ERROR) << "STATSM RECOVER (" << containers.size() << "):";
      for (const mesos::slave::ContainerState& container : containers) {
        list.push_back(_register_container(container.container_id(), container.executor_info()));
      }
      return list;
    }

    void unregister_container(const mesos::ContainerID& container_id) {
      std::unique_lock<std::mutex> lock(mutex);
      _unregister_container(container_id);
    }

    Try<UDPEndpoint> get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) {
      std::unique_lock<std::mutex> lock(mutex);
      return _get_statsd_endpoint(executor_info);
    }

   protected:
    virtual Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) = 0;
    virtual void _unregister_container(const mesos::ContainerID& container_id) = 0;
    virtual Try<UDPEndpoint> _get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) = 0;

    const std::string listen_host;
    const bool annotations_enabled;

    boost::asio::io_service io_service;
    std::shared_ptr<PortWriter> writer;

   private:
    void run_io_service() {
#if defined(LINUX_PRCTL_AVAILABLE) && defined(PR_SET_NAME)
      // Set the thread name to help with any debugging/tracing (uses Linux-specific API)
      LOG(ERROR) << "SET thread name";
      prctl(PR_SET_NAME, "stats-io-service", 0, 0, 0);
#endif
      try {
        LOG(ERROR) << "START io_service";
        io_service.run();
        LOG(ERROR) << "EXITED io_service.run()";
      } catch (const std::exception& e) {
        LOG(ERROR) << "io_service.run() THREW: " << e.what();
      }
    }

    std::unique_ptr<std::thread> io_service_thread;
    std::mutex mutex;
  };

  /**
   * Listen on a single port across all containers in the slave.
   * IP-per-container should use this, then register individual container hosts inside the Reader.
   */
  class SinglePortAssignerImpl : public InputAssignerImpl {
   public:
    SinglePortAssignerImpl(const mesos::Parameters& parameters)
      : InputAssignerImpl(parameters),
        single_port_value(params::get_uint(parameters, params::LISTEN_PORT, params::LISTEN_PORT_DEFAULT)) {
      if (!valid_port(single_port_value)) {
        LOG(FATAL) << "Invalid " << params::LISTEN_PORT << " config value.";
      }
    }
    virtual ~SinglePortAssignerImpl() { }

    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) {
      if (!single_port_reader) {
        // Create/open/register a new port reader only if one doesn't exist.
        port_reader_ptr_t reader(new PortReader(
                io_service,
                writer,
                UDPEndpoint(listen_host, single_port_value),
                annotations_enabled));
        Try<UDPEndpoint> endpoint = reader->open();
        if (endpoint.isError()) {
          return endpoint;
        }
        single_port_reader = reader;
      }

      // Assign the container to the sole port reader.
      return single_port_reader->register_container(container_id, executor_info);
    }

    void _unregister_container(const mesos::ContainerID& container_id) {
      if (!single_port_reader) {
        LOG(WARNING) << "No single-port reader had been initialized, cannot unregister container=" << container_id.value();
        return;
      }
      // Unassign this container from the reader, but leave the reader itself (and its port) open.
      single_port_reader->unregister_container(container_id);
    }

    Try<UDPEndpoint> _get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) {
      if (!single_port_reader) {
        std::ostringstream oss;
        oss << "Unable to retrieve single-port endpoint for executor=" << executor_info.ShortDebugString();
        LOG(ERROR) << oss.str();
        return Try<UDPEndpoint>::error(oss.str());
      }
      return single_port_reader->endpoint();
    }

   private:
    // The port to listen on, passed to all containers.
    const size_t single_port_value;
    // The sole reader shared by all containers. Any per-container mapping (eg per-IP) is done internally.
    port_reader_ptr_t single_port_reader;
  };

  /**
   * Listen on ephemeral ports, dynamically-allocated and assigned by the kernel, one per container.
   * Use this unless you have some kind of localhost firewall to worry about.
   */
  class EphemeralPortAssignerImpl : public InputAssignerImpl {
   public:
    EphemeralPortAssignerImpl(const mesos::Parameters& parameters)
      : InputAssignerImpl(parameters) {
      // No extra params to validate
    }
    virtual ~EphemeralPortAssignerImpl() { }

    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) {
      // Reuse existing reader if available.
      auto iter = container_to_reader.find(container_id);
      if (iter != container_to_reader.end()) {
        return iter->second->endpoint();
      }

      // Create/open/register a new reader against an ephemeral port.
      port_reader_ptr_t reader(new PortReader(
              io_service,
              writer,
              UDPEndpoint(listen_host, 0),
              annotations_enabled));
      Try<UDPEndpoint> endpoint = reader->open();
      if (endpoint.isError()) {
        return endpoint;
      }
      endpoint = reader->register_container(container_id, executor_info);
      executor_to_container[executor_info.executor_id()] = container_id;
      container_to_reader[container_id] = reader;
      return endpoint;
    }

    void _unregister_container(const mesos::ContainerID& container_id) {
      auto iter = container_to_reader.find(container_id);
      if (iter == container_to_reader.end()) {
        LOG(WARNING) << "No ephemeral-port reader had been assigned to container=" << container_id.value() << ", cannot unregister";
        return;
      }
      // Delete the reader (which closes its socket)
      container_to_reader.erase(iter);
    }

    Try<UDPEndpoint> _get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) {
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
      return reader_iter->second->endpoint();
    }

   private:
    // Temporary mapping of executor_id to container_id. Used to bridge a call to register_container(), followed by a call to get_statsd_endpoint().
    executor_id_map<mesos::ContainerID> executor_to_container;
    // Long-term mapping of container_id to the port reader assigned to that container. This mapping exists for the lifespan of the container.
    container_id_map<port_reader_ptr_t> container_to_reader;
  };

  /**
   * Listen on a limited range of predefined ports, one per container.
   * Use this if you need to whitelist a specific range of ports.
   */
  class PortRangeAssignerImpl : public InputAssignerImpl {
   public:
    PortRangeAssignerImpl(const mesos::Parameters& parameters)
      : InputAssignerImpl(parameters) {
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
    virtual ~PortRangeAssignerImpl() { }

    Try<UDPEndpoint> _register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info) {
      // Reuse existing reader if available.
      auto iter = container_to_reader.find(container_id);
      if (iter != container_to_reader.end()) {
        return iter->second->endpoint();
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
      port_reader_ptr_t reader(new PortReader(
              io_service,
              writer,
              UDPEndpoint(listen_host, port.get()),
              annotations_enabled));
      Try<UDPEndpoint> endpoint = reader->open();
      if (endpoint.isError()) {
        return endpoint;
      }
      endpoint = reader->register_container(container_id, executor_info);
      executor_to_container[executor_info.executor_id()] = container_id;
      container_to_reader[container_id] = reader;
      return endpoint;
    }

    void _unregister_container(const mesos::ContainerID& container_id) {
      auto iter = container_to_reader.find(container_id);
      if (iter == container_to_reader.end()) {
        LOG(WARNING) << "No port-range reader had been assigned to container=" << container_id.value() << ", cannot unregister";
        return;
      }
      // Return the reader's port to the pool, then close/delete the reader.
      Try<UDPEndpoint> endpoint_to_close = iter->second->endpoint();
      if (endpoint_to_close.isError()) {
        LOG(WARNING) << "Endpoint is missing from port reader for container=" << container_id.value() << ", cannot return port to range pool";
      } else {
        range_pool->put(endpoint_to_close->port);
      }
      container_to_reader.erase(iter);
    }

    Try<UDPEndpoint> _get_statsd_endpoint(const mesos::ExecutorInfo& executor_info) {
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
      return reader_iter->second->endpoint();
    }

   private:
    // Temporary mapping of executor_id to container_id. Used to bridge a call to register_container(), followed by a call to get_statsd_endpoint().
    executor_id_map<mesos::ContainerID> executor_to_container;
    // Long-term mapping of container_id to the port reader assigned to that container. This mapping exists for the lifespan of the container.
    container_id_map<port_reader_ptr_t> container_to_reader;
    // Allocator of ports within a range.
    std::shared_ptr<RangePool> range_pool;
  };
}

namespace {
  std::mutex global_assigner_mutex;
  std::shared_ptr<stats::InputAssigner> global_assigner;
}

std::shared_ptr<stats::InputAssigner> stats::InputAssigner::get(const mesos::Parameters& parameters) {
  std::unique_lock<std::mutex> lock(global_assigner_mutex);
  if (!global_assigner) {
    LOG(ERROR) << "STATS MGR GET CONSTRUCT" << parameters.DebugString();
    global_assigner.reset(new InputAssigner(parameters));
  } else {
    LOG(ERROR) << "STATS MGR GET IGNORE" << parameters.DebugString();
  }
  return global_assigner;
}

stats::InputAssigner::InputAssigner(const mesos::Parameters& parameters) {
  std::string port_mode_str = params::get_str(parameters, params::LISTEN_PORT_MODE, params::LISTEN_PORT_MODE_DEFAULT);
  params::PortMode port_mode = params::to_port_mode(port_mode_str);
  switch (port_mode) {
    case params::PortMode::SINGLE_PORT:
      impl.reset(new SinglePortAssignerImpl(parameters));
      break;
    case params::PortMode::EPHEMERAL_PORTS:
      impl.reset(new EphemeralPortAssignerImpl(parameters));
      break;
    case params::PortMode::PORT_RANGE:
      impl.reset(new PortRangeAssignerImpl(parameters));
      break;
    case params::PortMode::UNKNOWN_PORT_MODE:
      LOG(FATAL) << "Unknown " << params::LISTEN_PORT_MODE << " config value: " << port_mode_str;
      break;
  }
}

stats::InputAssigner::~InputAssigner() { }

Try<stats::UDPEndpoint> stats::InputAssigner::register_container(
    const mesos::ContainerID& container_id,
    const mesos::ExecutorInfo& executor_info) {
  return impl->register_container(container_id, executor_info);
}

std::list<Try<stats::UDPEndpoint>> stats::InputAssigner::register_containers(
    const std::list<mesos::slave::ContainerState>& containers) {
  return impl->register_containers(containers);
}

void stats::InputAssigner::unregister_container(
    const mesos::ContainerID& container_id) {
  impl->unregister_container(container_id);
}

Try<stats::UDPEndpoint> stats::InputAssigner::get_statsd_endpoint(
    const mesos::ExecutorInfo& executor_info) {
  return impl->get_statsd_endpoint(executor_info);
}
