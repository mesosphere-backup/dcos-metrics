#pragma once

#include <gmock/gmock.h>

#include "port_runner.hpp"

class MockPortRunner : public stats::PortRunner {
 public:
  MOCK_METHOD1(dispatch, void(std::function<void()> func));
  MOCK_METHOD1(create_port_reader, std::shared_ptr<stats::PortReader>(size_t port));
};
