#pragma once

#include <memory>

#include <mesos/mesos.pb.h>
#include <stout/try.hpp>

#include "udp_endpoint.hpp"
#include "port_writer.hpp"

namespace stats {
  class PortReaderProcess;

  class PortReader {
   public:
    PortReader(std::shared_ptr<PortWriter> port_writer, const UDPEndpoint& requested_endpoint, bool annotations_enabled);
    virtual ~PortReader();

    Try<UDPEndpoint> open();
    Try<UDPEndpoint> endpoint();

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info,
        const std::string& source_host = "");
    bool unregister_container_check_empty(const mesos::ContainerID& container_id);

   private:
    std::unique_ptr<PortReaderProcess> impl;
  };
}
