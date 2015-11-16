#include "port_writer.hpp"

#include <process/dispatch.hpp>
#include <process/process.hpp>

#include "params.hpp"

namespace stats {
  class PortWriterProcess : public process::Process<PortWriterProcess> {
   public:
    PortWriterProcess(const mesos::Parameters& parameters)
      : send_host(params::get_str(parameters, params::DEST_HOST, params::DEST_HOST_DEFAULT)),
        send_port(params::get_uint(parameters, params::DEST_PORT, params::DEST_PORT_DEFAULT)),
        udp_max_bytes(params::get_uint(parameters, params::DEST_UDP_MAX_BYTES, params::DEST_UDP_MAX_BYTES_DEFAULT)) {
    }
    virtual ~PortWriterProcess() {
    }

    bool open() {
      //TODO open socket
    }

    Nothing send(const std::string& metric) {
      //TODO implement sending data to socket (with aggregation)
      //TODO look into libprocess delay() for scheduling periodic flushes of aggregated data
      return Nothing();
    }

   private:
    const std::string send_host;
    const size_t send_port;
    const size_t udp_max_bytes;
  };
}

stats::PortWriter::PortWriter(const mesos::Parameters& parameters) {
  impl.reset(new PortWriterProcess(parameters));
}
stats::PortWriter::~PortWriter() {
}

bool stats::PortWriter::open() {
  process::dispatch(*impl, &PortWriterProcess::open).get();
}

void stats::PortWriter::send(const std::string& metric) {
  process::dispatch(*impl, &PortWriterProcess::send, metric).get();
}
