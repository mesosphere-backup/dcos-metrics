#include "io_runner_impl.hpp"

#ifdef LINUX_PRCTL_AVAILABLE
#include <sys/prctl.h>
#endif
#define THREAD_NAME "metrics-io-service"

#include <ifaddrs.h>
#define IFACE_TYPE AF_INET // ipv4
#define IFACE_ADDR_STRUCT struct sockaddr_in // ipv4

#include <glog/logging.h>

#include "collector_output_writer.hpp"
#include "container_reader_impl.hpp"
#include "statsd_output_writer.hpp"

namespace {
  std::string get_iface_host(const std::string& iface) {
    struct ifaddrs* ifaces = NULL;
    if (getifaddrs(&ifaces) < 0) {
      int errnum = errno;
      LOG(FATAL) << "Unable to get list of network interfaces, "
                 << "errno=" << errnum << " => " << strerror(errnum);
    }

    char host[512];
    memset(host, 0, sizeof(host));

    LOG(INFO) << "Network interfaces (searching for '" << iface << "'):";
    for (struct ifaddrs* cur = ifaces; cur != NULL; cur = cur->ifa_next) {
      std::string cur_name(cur->ifa_name);
      if (cur_name != iface) {
        LOG(INFO) << "- found '" << cur_name << "' (!= '" << iface << "')";
        continue;
      }
      if (cur->ifa_addr == NULL) {
        LOG(INFO) << "- found '" << cur_name << "' has null address";
        continue;
      }
      if (cur->ifa_addr->sa_family != IFACE_TYPE) {
        LOG(INFO) << "- found '" << cur_name << "', but "
                  << "family is '" << cur->ifa_addr->sa_family << "' (!= '" << IFACE_TYPE << "')";
        continue;
      }
      int result = getnameinfo(
          cur->ifa_addr, sizeof(IFACE_ADDR_STRUCT), host, sizeof(host), NULL, 0, NI_NUMERICHOST);
      if (result != 0) {
        LOG(FATAL) << "Failed to get host for interface name '" << cur_name << "': "
                   << gai_strerror(result);
      }
      LOG(INFO) << "Interface '" << cur_name << "' (family '" << cur->ifa_addr->sa_family << "') "
                << "is using host '" << host << "'";
      break;
    }
    freeifaddrs(ifaces);
    size_t hostlen = strnlen(host, sizeof(host));
    if (hostlen == 0) {
      LOG(FATAL) << "Interface named '" << iface << "' was not found, see list above. "
                 << "Check configuration of '" << metrics::params::LISTEN_INTERFACE << "'.";
    }
    return std::string(host, hostlen);
  }
}

metrics::IORunnerImpl::IORunnerImpl() { }

metrics::IORunnerImpl::~IORunnerImpl() {
  // Clean shutdown in a specific order.
  if (io_service) {
    writers.clear();
    io_service->stop();
    io_service_thread->join();
    io_service_thread.reset();
    io_service->reset();
    io_service.reset();
  }
}

void metrics::IORunnerImpl::init(const mesos::Parameters& parameters) {
  if (io_service) {
    LOG(FATAL) << "IORunner::init() was called twice";
    return;
  }

  // do this once, up-front.
  // - fail early if requested iface isn't found (don't wait for a container to arrive)
  // - this shouldn't change, and any existing container streams would be lost if it changed anyway
  listen_host = get_iface_host(params::get_str(
          parameters, params::LISTEN_INTERFACE, params::LISTEN_INTERFACE_DEFAULT));

  io_service.reset(new boost::asio::io_service);
  if (params::get_bool(
          parameters, params::OUTPUT_STATSD_ENABLED, params::OUTPUT_STATSD_ENABLED_DEFAULT)) {
    output_writer_ptr_t writer = StatsdOutputWriter::create(io_service, parameters);
    writers.push_back(writer);
  }
  if (params::get_bool(
          parameters, params::OUTPUT_COLLECTOR_ENABLED, params::OUTPUT_COLLECTOR_ENABLED_DEFAULT)) {
    output_writer_ptr_t writer = CollectorOutputWriter::create(io_service, parameters);
    writers.push_back(writer);
  }
  if (writers.empty()) {
    LOG(FATAL) << "At least one writer must be enabled in preferences: "
               << params::OUTPUT_STATSD_ENABLED << " or " << params::OUTPUT_COLLECTOR_ENABLED
               << " must be true";
  }
  // Writers must start before the io thread starts. The writers will configure timers that will
  // prevent the io thread from exiting immediately.
  for (output_writer_ptr_t writer : writers) {
    writer->start();
  }
  io_service_thread.reset(new std::thread(std::bind(&IORunnerImpl::run_io_service, this)));
}

void metrics::IORunnerImpl::dispatch(std::function<void()> func) {
  if (!io_service) {
    LOG(FATAL) << "IORunner::init() wasn't called before dispatch()";
    return;
  }
  io_service->dispatch(func);
}

std::shared_ptr<metrics::ContainerReader> metrics::IORunnerImpl::create_container_reader(size_t port) {
  if (!io_service) {
    LOG(FATAL) << "IORunner::init() wasn't called before create_container_reader()";
    return std::shared_ptr<metrics::ContainerReader>();
  }
  return std::shared_ptr<ContainerReader>(
      new ContainerReaderImpl(io_service, writers, UDPEndpoint(listen_host, port)));
}

void metrics::IORunnerImpl::update_usage(process::Future<mesos::ResourceUsage> usage) {
  if (!io_service) {
    LOG(FATAL) << "IORunner::init() wasn't called before update_usage(). "
               << "Bad mesos agent config?";
    return;
  }
  // Run the resource usage handling within the IO thread.
  for (output_writer_ptr_t writer : writers) {
    dispatch(std::bind(&OutputWriter::write_resource_usage, writer.get(), usage));
  }
}

void metrics::IORunnerImpl::run_io_service() {
#if defined(LINUX_PRCTL_AVAILABLE) && defined(PR_SET_NAME)
  // Set the thread name to help with any debugging/tracing (uses Linux-specific API)
  prctl(PR_SET_NAME, THREAD_NAME, 0, 0, 0);
#endif
  try {
    LOG(INFO) << "Starting io_service";
    io_service->run();
    LOG(INFO) << "Exited io_service.run()";
  } catch (const std::exception& e) {
    LOG(ERROR) << "io_service.run() threw exception, exiting: " << e.what();
  }
}
