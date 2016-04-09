#pragma once

namespace stats {
  /**
   * Writes container state to disk, so that it can be recovered if the agent is restarted.
   */
  class InputStateCache {
   public:
    InputStateCache(const mesos::Parameters& parameters);

    container_id_map<UDPEndpoint> get_containers();
    void add_container(const mesos::ContainerID& container_id, const UDPEndpoint& endpoint);
    void remove_container(const mesos::ContainerID& container_id);

   private:
    const std::string state_path_dir;
  };
}
