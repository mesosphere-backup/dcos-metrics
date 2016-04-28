#pragma once

#include <thread>

#include <boost/asio.hpp>
#include <mesos/mesos.pb.h>

#include "io_runner.hpp"
#include "port_writer.hpp"

namespace stats {
  /**
   * The IORunner runs the async scheduler which powers the PortWriter and all PortReaders, while
   * also acting as a factory for PortReaders.
   */
  class IORunnerImpl : public IORunner {
   public:
    /**
     * Creates an uninitialized instance. init() must be called before anything else.
     */
    IORunnerImpl();

    virtual ~IORunnerImpl();

    /**
     * Configures the IORunner with the provided parameters.
     * This must be called before any other methods.
     */
    void init(const mesos::Parameters& parameters);

    /**
     * Utility function to dispatch the provided method against the enclosed async scheduler.
     */
    void dispatch(std::function<void()> func);

    /**
     * Creates a new PortReader which is powered by an internal async scheduler for the provided
     * port. The returned PortReader won't have been open()ed yet.
     */
    std::shared_ptr<PortReader> create_port_reader(size_t port);

    /**
     * Submits an update to container resource state to the internal async scheduler.
     */
    void update_usage(process::Future<mesos::ResourceUsage> usage);

   private:
    void run_io_service();

    std::string listen_host;
    params::annotation_mode::Value annotation_mode;

    std::shared_ptr<boost::asio::io_service> io_service;
    std::shared_ptr<PortWriter> writer;
    std::unique_ptr<std::thread> io_service_thread;
  };
}
