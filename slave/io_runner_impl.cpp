#include "io_runner_impl.hpp"

#ifdef LINUX_PRCTL_AVAILABLE
#include <sys/prctl.h>
#endif
#define THREAD_NAME "stats-io-service"

#include <glog/logging.h>

#include "port_reader_impl.hpp"

stats::IORunnerImpl::IORunnerImpl() { }

stats::IORunnerImpl::~IORunnerImpl() {
  // Clean shutdown in a specific order.
  if (io_service) {
    writer.reset();
    io_service->stop();
    io_service_thread->join();
    io_service_thread.reset();
    io_service->reset();
    io_service.reset();
  }
}

void stats::IORunnerImpl::init(const mesos::Parameters& parameters) {
  if (io_service) {
    LOG(FATAL) << "IORunner::init() was called twice";
    return;
  }

  listen_host = params::get_str(parameters, params::LISTEN_HOST, params::LISTEN_HOST_DEFAULT);
  annotation_mode = params::to_annotation_mode(
      params::get_str(parameters, params::ANNOTATION_MODE, params::ANNOTATION_MODE_DEFAULT));
  if (annotation_mode == params::annotation_mode::Value::UNKNOWN) {
    LOG(FATAL) << "Unknown " << params::ANNOTATION_MODE << " config value: "
               << params::get_str(parameters, params::ANNOTATION_MODE, params::ANNOTATION_MODE_DEFAULT);
  }

  io_service.reset(new boost::asio::io_service);
  //TODO one or more PortWriter implementations, each for a different output type
  writer.reset(new PortWriter(io_service, parameters));
  writer->start();
  io_service_thread.reset(new std::thread(std::bind(&IORunnerImpl::run_io_service, this)));
}

void stats::IORunnerImpl::dispatch(std::function<void()> func) {
  if (!io_service) {
    LOG(FATAL) << "IORunner::init() wasn't called before dispatch()";
    return;
  }
  io_service->dispatch(func);
}

std::shared_ptr<stats::PortReader> stats::IORunnerImpl::create_port_reader(size_t port) {
  if (!io_service) {
    LOG(FATAL) << "IORunner::init() wasn't called before create_port_reader()";
    return std::shared_ptr<stats::PortReader>();
  }
  return std::shared_ptr<PortReader>(
      new PortReaderImpl<PortWriter>(
          io_service, writer, UDPEndpoint(listen_host, port), annotation_mode));
}

void stats::IORunnerImpl::update_usage(process::Future<mesos::ResourceUsage> usage) {
  if (!io_service) {
    LOG(FATAL) << "IORunner::init() wasn't called before update_usage()";
    return;
  }
  //TODO dispatch: get usage proto, print
  LOG(INFO) << "USAGE DUMP:\n" << usage.get().DebugString();
}

void stats::IORunnerImpl::run_io_service() {
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
