#pragma once

#include <gmock/gmock.h>

#include "io_runner.hpp"

class MockIORunner : public metrics::IORunner {
 public:
  MOCK_METHOD1(dispatch, void(std::function<void()> func));
  MOCK_METHOD1(create_container_reader, std::shared_ptr<metrics::ContainerReader>(size_t port));
  MOCK_METHOD1(update_usage, void(process::Future<mesos::ResourceUsage> usage));
};
