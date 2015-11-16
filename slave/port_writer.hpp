#pragma once

#include <memory>

#include <mesos/mesos.pb.h>

namespace stats {
  class PortWriterProcess;

  class PortWriter {
   public:
    PortWriter(const mesos::Parameters& parameters);
    virtual ~PortWriter();

    bool open();
    void send(const std::string& metric);

   private:
    std::unique_ptr<PortWriterProcess> impl;
  };
}
