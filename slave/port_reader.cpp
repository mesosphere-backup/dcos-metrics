#include "port_reader.hpp"

#include <process/dispatch.hpp>
#include <process/process.hpp>
namespace stats {
  class PortReaderProcess : public process::Process<PortReaderProcess> {//TODO implement reading data from a socket, tagging, passing to writer
   public:
    PortReaderProcess(std::shared_ptr<PortWriter> port_writer, const UDPEndpoint& requested_endpoint, bool annotations_enabled)
      : port_writer(port_writer), requested_endpoint(requested_endpoint), annotations_enabled(annotations_enabled) { }
    virtual ~PortReaderProcess() { }

    Try<UDPEndpoint> open();
    Try<UDPEndpoint> endpoint();

    Try<UDPEndpoint> register_container(
        const mesos::ContainerID& container_id,
        const mesos::ExecutorInfo& executor_info,
        const std::string& source_host = "");
    bool unregister_container_check_empty(const mesos::ContainerID& container_id);

   private:
    const std::shared_ptr<PortWriter> port_writer;
    const UDPEndpoint requested_endpoint;
    const bool annotations_enabled;
    std::unique_ptr<UDPEndpoint> endpoint_;
  };
}

stats::PortReader::PortReader(std::shared_ptr<PortWriter> port_writer, const UDPEndpoint& requested_endpoint, bool annotations_enabled) {
  impl.reset(new PortReaderProcess(port_writer, requested_endpoint, annotations_enabled));
}
stats::PortReader::~PortReader() {
}

Try<stats::UDPEndpoint> stats::PortReader::open() {
  return process::dispatch(*impl, &PortReaderProcess::open).get();
}
Try<stats::UDPEndpoint> stats::PortReader::endpoint() {
  return process::dispatch(*impl, &PortReaderProcess::endpoint).get();
}

Try<stats::UDPEndpoint> stats::PortReader::register_container(
  const mesos::ContainerID& container_id,
  const mesos::ExecutorInfo& executor_info,
  const std::string& source_host/*=""*/) {
  return process::dispatch(*impl, &PortReaderProcess::register_container, container_id, executor_info, source_host).get();
}
bool stats::PortReader::unregister_container_check_empty(const mesos::ContainerID& container_id) {
  return process::dispatch(*impl, &PortReaderProcess::unregister_container_check_empty, container_id).get();
}
