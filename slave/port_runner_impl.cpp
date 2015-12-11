#include "port_runner_impl.hpp"

#ifdef LINUX_PRCTL_AVAILABLE
#include <sys/prctl.h>
#endif
#define THREAD_NAME "stats-io-service"

#include <glog/logging.h>

#include "port_reader_impl.hpp"

std::shared_ptr<stats::PortRunner> stats::PortRunnerImpl::create(
    const mesos::Parameters& parameters) {
  return std::shared_ptr<PortRunner>(new PortRunnerImpl(parameters));
}

stats::PortRunnerImpl::PortRunnerImpl(const mesos::Parameters& parameters)
  : listen_host(params::get_str(parameters, params::LISTEN_HOST, params::LISTEN_HOST_DEFAULT)),
    annotations_enabled(
        params::get_bool(parameters, params::ANNOTATIONS, params::ANNOTATIONS_DEFAULT)),
    io_service(new boost::asio::io_service),
    writer(new PortWriter(io_service, parameters)) {
  writer->start();
  io_service_thread.reset(new std::thread(std::bind(&PortRunnerImpl::run_io_service, this)));
}

stats::PortRunnerImpl::~PortRunnerImpl() {
  // Clean shutdown in a specific order.
  writer.reset();
  io_service->stop();
  io_service_thread->join();
  io_service_thread.reset();
  io_service->reset();
  io_service.reset();
}

void stats::PortRunnerImpl::dispatch(std::function<void()> func) {
  io_service->dispatch(func);
}

std::shared_ptr<stats::PortReader> stats::PortRunnerImpl::create_port_reader(size_t port) {
  return std::shared_ptr<PortReader>(
      new PortReaderImpl<PortWriter>(
          io_service, writer, UDPEndpoint(listen_host, port), annotations_enabled));
}

void stats::PortRunnerImpl::run_io_service() {
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
