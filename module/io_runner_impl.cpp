#include "io_runner_impl.hpp"

#ifdef LINUX_PRCTL_AVAILABLE
#include <sys/prctl.h>
#endif
#define THREAD_NAME "metrics-io-service"

#include <glog/logging.h>

//#include "collector_output_writer.hpp"
#include "container_reader_impl.hpp"
#include "statsd_output_writer.hpp"

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

  listen_host = params::get_str(parameters, params::LISTEN_HOST, params::LISTEN_HOST_DEFAULT);

  io_service.reset(new boost::asio::io_service);
  if (params::get_bool(
          parameters, params::OUTPUT_STATSD_ENABLED, params::OUTPUT_STATSD_ENABLED_DEFAULT)) {
    output_writer_ptr_t writer = StatsdOutputWriter::create(io_service, parameters);
    writers.push_back(writer);
  }
  /* TODO
  if (params::get_bool(
          parameters, params::OUTPUT_COLLECTOR_ENABLED, params::OUTPUT_COLLECTOR_ENABLED_DEFAULT)) {
    output_writer_ptr_t writer = CollectorOutputWriter::create(io_service, parameters);
    writers.push_back(writer);
  }
  */
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
    LOG(FATAL) << "IORunner::init() wasn't called before update_usage()";
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
