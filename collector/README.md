# Metrics Collector

Lightweight standalone process which runs on mesos-agent nodes.

Performs the following functions:
- Accepts metrics via an HTTP endpoint from the [mesos-agent module](../module/) as well as other local processes on the system. (Application interface)
- Publishes received metrics to a Kafka streaming service.
- (soon) Provides a Partner Interface for other local processes to retrieve collected metrics directly
- Supports live configuration updates without interrupting the flow of metrics (without requiring an agent restart, unlike the agent module)
