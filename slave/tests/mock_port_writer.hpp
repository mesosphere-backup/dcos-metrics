#pragma once

#include <gmock/gmock.h>
#include <stout/nothing.hpp>
#include <stout/try.hpp>

class MockPortWriter {
 public:
  MOCK_METHOD0(open, Try<Nothing>());
  MOCK_METHOD2(write, void(const char* bytes, size_t size));
};
