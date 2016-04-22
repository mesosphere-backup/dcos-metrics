#include "input_state_cache_impl.hpp"

stats::InputStateCacheImpl::InputStateCacheImpl(const mesos::Parameters& parameters)
  : state_path_dir(
      params::get_str(parameters, params::STATE_PATH_DIR, params::STATE_PATH_DIR_DEFAULT)) { }

stats::container_id_map<stats::UDPEndpoint> stats::InputStateCacheImpl::get_containers() {
  //TODO
  return container_id_map<UDPEndpoint>();
}

void stats::InputStateCacheImpl::add_container(
    const mesos::ContainerID& container_id, const UDPEndpoint& endpoint) {
  //TODO
}

void stats::InputStateCacheImpl::remove_container(const mesos::ContainerID& container_id) {
  //TODO
}
