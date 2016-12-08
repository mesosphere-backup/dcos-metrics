# Metrics Collectors
The metrics collectors run on every node in the cluster and provide the following methods of ingesting metrics:
  - periodically polling the Mesos HTTP APIs for cluster state and metrics about running containers and cluster services
  - periodically polling the node's operating system for system-level resource utilization (CPU, memory, disk, networks)
  - listening on TCP port 8124 for metrics being sent from the Mesos metrics module

The metrics collectors then transform their metrics to fit a common message format, at which point they are broadcast
to one or more configured *producers*. More information about the available producers and their implementation can be
found in [PRODUCERS.md](PRODUCERS.md).
