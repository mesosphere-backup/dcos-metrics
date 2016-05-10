#pragma once

#include <mesos/mesos.pb.h>
#include <process/future.hpp>

namespace metrics {

  /**
   * An OutputWriter accepts data from one or more ContainerReaders, then tags and forwards it to an
   * external endpoint of some kind.
   */
  class OutputWriter {
   public:
    virtual ~OutputWriter() { }

    /**
     * Initializes the writer.
     */
    virtual void start() = 0;

    /**
     * Outputs the provided data associated with the given container information, or NULL
     * container information if none is available.
     */
    virtual void write_container_statsd(
        const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
        const char* data, size_t size) = 0;

    /**
     * Outputs the provided container resource usage data.
     */
    virtual void write_resource_usage(const process::Future<mesos::ResourceUsage>& usage) = 0;
  };

  typedef std::shared_ptr<OutputWriter> output_writer_ptr_t;
}
