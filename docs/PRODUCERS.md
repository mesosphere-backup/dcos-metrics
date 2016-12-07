# Metrics Producers

1. [Overview](#overview)
2. [Bundled Producers](#bundled-producers)
  1. [DataDog](#datadog)
  2. [HTTP](#http)
3. [Developing A Producer](#developing-a-producer)
  1. [Example](#example)

## Overview
In dcos-metrics,

### Terminology
A *collector* refers to code that queries the underlying operating system, Mesos,
and/or DC/OS to collect metrics. Each collector then transforms collected
metrics into a *MetricsMessage*, or common format that can be broadcast to
one or more *producers*.

A *producer* refers to code that is responsible for taking collected metrics and
shipping them to an external system, such as a time series database like
InfluxDB, a message queue like Kafka, a hosted metrics provider like DataDog,
or simply exposing metrics via a HTTP API.

## Bundled Producers
As part of the dcos-metrics project, we've included several metrics producers to
help you get metrics out of the cluster and in to the metrics store of your
choice. We hope to improve this offering over time, and gladly welcome community
and vendor contributions to this repo.

### DataDog
The DataDog producer is enabled by default and will attempt to connect to a
DataDog agent running at `127.0.0.1:8125`. If the agent isn't running, an error
message will be logged and the datapoint will be dropped. (*Note: this will be
improved in future versions of dcos-metrics.*)

You can install and configure the DataDog agent for all nodes of your DC/OS
cluster by using the package available in the
[Mesosphere Universe](https://github.com/mesosphere/universe).

### HTTP
The HTTP producer is enabled by default and exposes a JSON-formatted HTTP API
on each node in the cluster. These APIs include both metrics datapoints as well
as *dimensions*, or key/value pairs with relevant node and cluster metadata.
In DC/OS 1.9, the path to this API is `/system/v1/metrics/...`. The available
endpoints include:

* `/v0/ping` -- basic health check
* `/v0/node` -- node metrics (CPU, memory, storage, networks, etc)
* `/v0/containers` -- an array of container IDs running on the node
* `/v0/containers/{id}` -- resource allocation and usage for the given container ID
* `/v0/containers/{id}/app` -- application-level metrics from the container
(shipped in [DogStatsD format](http://docs.datadoghq.com/guides/dogstatsd/) using
the `STATSD_UDP_HOST` and `STATSD_UDP_PORT` environment variables)
* `/v0/containers/{id}/app/{metric-id}` -- similar to `/v0/containers/{id}/app`
but only contains datapoints for a single metric ID

## Developing A Producer
The code for various metrics producers resides in the [producers/](../producers/)
directory at the root of this repository. Producers are rather simple: they
receive messages (of type `producers.MetricsMessage`) off a channel, do some
processing, and ship the metric(s) to the external system.

When creating a new producer, it should implement the interface
`producers.MetricsProducer`. It's helpful to have a method, such as `New()`,
that returns both the `MetricsProducer` implementation as well as a channel
to receive metrics, such as `chan producers.MetricsMessage`.

Configuration is defined in each producer package, and can then be exposed in
`config.go`.

Finally, to get metrics on to the producer, add it to `main()` in
`dcos-metrics.go`. `main()` already has logic to distribute collected metrics
to multiple producers.

### Example
```go
type MyConfig struct {
    SomeConfigKey string
    SomeOtherConfigKey string
}

type myProducerImpl struct {
    cfg MyConfig
    mc chan producers.MetricsMessage
}

func New(cfg MyConfig) (producers.MetricsProducer, chan producers.MetricsMessage) {
    p := myProducerImpl{
        cfg: cfg,
        mc: make(chan producers.MetricsMessage),
    }
    return &p, p.mc
}

func Run() error {
    for {
        msg := <-p.mc // blocks

        // do something with msg
    }
}
```