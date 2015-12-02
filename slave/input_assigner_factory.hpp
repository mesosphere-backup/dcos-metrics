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
    /**
     * Returns an InputAssigner using the provided params, or recycles a previous InputAssigner
     * while ignoring the provided params.
     */
    static std::shared_ptr<InputAssigner> get(const mesos::Parameters& parameters);

    /**
     * Resets the internal state, wiping any locally cached InputAssigners which were previously
     * built via get(). Meant for testing only.
     */
    static void reset_for_test();

    virtual ~InputAssignerFactory() { }

   private:
    InputAssignerFactory() { }
  };
}
