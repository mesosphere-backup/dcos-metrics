#pragma once

#include <gmock/gmock.h>

#include "output_writer.hpp"

class MockOutputWriter : public stats::OutputWriter {
 public:
  MOCK_METHOD0(start, void());
  MOCK_METHOD4(write_container_statsd, void(
          const mesos::ContainerID* container_id, const mesos::ExecutorInfo* executor_info,
          const char* data, size_t size));
  MOCK_METHOD1(write_resource_usage, void(const process::Future<mesos::ResourceUsage>& usage));
};
