#pragma once

#include <unordered_map>
#include <unordered_set>

namespace stats {
  struct ContainerIDComparer {
    size_t operator()(const mesos::ContainerID& a) const {
      return std::hash<std::string>()(a.value());
    }

    bool operator()(const mesos::ContainerID& a, const mesos::ContainerID& b) const {
      return a.value() == b.value();
    }
  };

  typedef std::unordered_set<mesos::ContainerID, ContainerIDComparer, ContainerIDComparer> container_id_set;

  template <typename T>
  class container_id_map : public std::unordered_map<mesos::ContainerID, T, ContainerIDComparer, ContainerIDComparer> {
  };

  struct ExecutorIDComparer {
    size_t operator()(const mesos::ExecutorID& a) const {
      return std::hash<std::string>()(a.value());
    }

    bool operator()(const mesos::ExecutorID& a, const mesos::ExecutorID& b) const {
      return a.value() == b.value();
    }
  };

  typedef std::unordered_set<mesos::ExecutorID, ExecutorIDComparer, ExecutorIDComparer> executor_id_set;

  template <typename T>
  class executor_id_map : public std::unordered_map<mesos::ExecutorID, T, ExecutorIDComparer, ExecutorIDComparer> {
  };
}
