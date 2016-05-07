# Metrics Collector

Lightweight standalone process which runs on mesos-agent nodes.

Inputs metrics [sent over TCP](../schema/) from local sources:
- The [mesos-agent module](../module/), for metrics relating to containers
- Other DCOS processes on the system

Outputs metrics to the following destinations:
- Kafka metrics service
- Local partner processes, if any

A basic HTTP server provides:
- Returning input/output TCP endpoints for local processes to use
- On-the-fly configuration changes
