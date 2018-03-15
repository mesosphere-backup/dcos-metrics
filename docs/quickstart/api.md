# Using the HTTP API

The HTTP API is designed for cases where you want to get metrics from one particular node or task, for example to build
third-party tooling. It is exposed via adminrouter for each agent:

`http://<your-dcos-master-url>/system/v1/<agent-id>/metrics/v0`

## The /node endpoint

The /node endpoint shows metrics about a node. These are equivalent to the metrics available from PSUtil, and contain
information about memory, cpu, and disk utilization. 

## The /containers endpoint

The /containers endpoint yields a list of all container IDs running on the node. A container is the lowest common
denominator for Mesos workloads. In general, one DC/OS task has one container (however some tasks can have multiple
container IDs, and sometimes a container exists with no corresponding task). 

## The /containers/container-id endpoint

The /containers/container-id endpoint gives a list of metrics related to a container’s resources. It is populated
with data from the mesos /monitor/statistics endpoint. 

Note that no data (HTTP 204) is returned for nested containers (for example tasks in a pod, or tasks created by a
framework which is running on Marathon). This is because they are missing from the mesos statistics endpoint. 

## The /containers/container-id/app endpoint

The /containers/container-id/app gives a list of metrics emitted by a workload inside a container over statsd, as
described in [Instrumenting your Code](instrumentation.md). 

Note that no data (HTTP 204) is returned for containers for which no metrics exist (for example containers which use
the Docker executor). 
