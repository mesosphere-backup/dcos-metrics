#pragma once

#include <thread>

#include <boost/asio.hpp>
#include <mesos/mesos.pb.h>

#include "port_runner.hpp"
#include "port_writer.hpp"

namespace stats {
  /**
   * The PortRunner runs the async scheduler which powers the PortWriter and all PortReaders.
   */
  class PortRunnerImpl : public PortRunner {
   public:
    static std::shared_ptr<PortRunner> create(const mesos::Parameters& parameters);
    virtual ~PortRunnerImpl();

    /**
     * Utility function to dispatch the provided method against the enclosed async scheduler.
     */
    void dispatch(std::function<void()> func);

    /**
     * Creates a new PortReader against the provided port which is powered by an internal async
     * scheduler, and which hasn't been open()ed yet.
     */
    std::shared_ptr<PortReader> create_port_reader(size_t port);

   private:
    PortRunnerImpl(const mesos::Parameters& parameters);
    void run_io_service();

    const std::string listen_host;
    const params::annotation_mode::Value annotation_mode;

    std::shared_ptr<boost::asio::io_service> io_service;
    std::shared_ptr<PortWriter> writer;
    std::unique_ptr<std::thread> io_service_thread;
  };
}
