# Metrics Collectors
The metrics collectors run on every node in the cluster and provide the following methods of ingesting metrics:

- Periodically polling the Mesos HTTP APIs for cluster state and metrics about running containers and cluster services
- Periodically polling the node's operating system for system-level resource utilization (CPU, memory, disk, networks)
- Listening on TCP port 8124 for metrics being sent from the Mesos metrics module

The metrics collectors then transform their metrics to fit a common message format, at which point they are broadcast
to one or more configured *producers*. More information about the available producers and their implementation can be
found in [PRODUCERS.md](PRODUCERS.md).

## Terminology
A *collector* refers to code that queries the underlying operating system, Mesos, and/or DC/OS to collect metrics.
Each collector then transforms collected metrics into a `MetricsMessage`, or common format that can be broadcast to one
or more producers.

## Included Collectors
As part of the dcos-metrics project, we've included several metrics collectors to help you get metrics from various
sources on the agent into a common format, and eventually off the node and into the metrics store of your choice.

### Framework
Listens on TCP port 8124 for metrics being sent from the Mesos metrics module. Allows applications running in
containers to ship metrics (in DogStatsD format) to the listener by using the environment variables
`STATSD_UDP_HOST` and `STATSD_UDP_PORT`.

### Mesos Agent
Periodically polls the Mesos HTTP APIs for cluster state and resource utilization of running containers.

### Node (via gopsutil)
Periodically polling the node's operating system for system-level resource utilization (CPU, memory, disk, networks).

## Developing A Collector
The code for various metrics collectors resides in the [collectors/](../collectors) directory at the root of this
repository. Collectors run on the node, and given various data sources, put metrics (of type producers.MetricsMessage)
on a channel.

When creating a new collector, it should implement the interface `collectors.MetricsCollector`. It's helpful to have a
method, such as `New()`, that returns both the `MetricsCollector` implementation as well as a channel to put metrics,
such as `chan producers.MetricsMessage`.

Configuration is defined in each collector package, and can then be exposed in `config.go`.

Finally, to get metrics from the collector to one or more producers, add the collector to `main()` in `dcos-metrics.go`.
Note that `main()` already has logic to distribute collected metrics to multiple producers.

### Example

```go
type MyConfig struct {
    SomeConfigKey string
    SomeOtherConfigKey string
}

type collectorImpl struct {
    cfg MyConfig
    mc chan producers.MetricsMessage
}

func New(cfg MyConfig, nodeInfo collectors.NodeInfo) (collectors.MetricsCollector, chan producers.MetricsMessage) {
    p := myProducerImpl{
        cfg: cfg,
        mc: make(chan producers.MetricsMessage),
    }
    return &p, p.mc
}

func (c *collectorImpl) Run() error {
    for {
        // collector logic
    }
}
```
