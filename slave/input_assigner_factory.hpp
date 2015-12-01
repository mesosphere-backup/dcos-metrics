#pragma once

#include <list>
#include <memory>

#include <mesos/mesos.pb.h>
#include <mesos/slave/isolator.pb.h>
#include <stout/try.hpp>

#include "udp_endpoint.hpp"

namespace stats {
  class InputAssigner;

  /**
   * Creates InputAssigners according to the provided mesos module parameters, or reuses a
   * previously-created InputAssigner if one had previously been created.
   *
   * The retrieval function is thread-safe. See input_assigner.hpp to determine thread-safety within
   * returned InputAssigners.
   */
  class InputAssignerFactory {
   public:
    static std::shared_ptr<InputAssigner> get(const mesos::Parameters& parameters);

    virtual ~InputAssignerFactory() { }

   private:
    InputAssignerFactory() { }
  };
}
