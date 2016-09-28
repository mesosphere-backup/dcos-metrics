# Metrics module usage

This module is included in DCOS EE (only), starting with 1.7.

In DCOS 1.7 EE, the module supports forwarding metrics in `statsd` format to `metrics.marathon.mesos`. In 1.8 EE, this has been replaced with a more reliable/flexible Kafka-based system for getting metrics off of the nodes. See [the main README](README.md).

Here are some examples for trying out metrics support on a 1.7+ EE cluster:

## Emitting Metrics

Here are some examples of ways to run services that support container metrics.

Emitting metrics is simple: The program just needs to look for `STATSD_UDP_HOST` and `STATSD_UDP_PORT` environment variables. When they're present, the host:port they advertise may be used as a destination for statsd-formatted metrics data. See [examples/statsd-emitter/main.go](the reference implementation) for an example.

### Launching a StatsD Emitter

This is a reference program that just emits various arbitrary metrics to the endpoint advertised by the metrics agent module via `STATSD_UDP_HOST`/`STATSD_UDP_PORT` environment variables. The sample program's Go source code is included in the .tgz.

See the [StatsD Emitter docs](examples/statsd-emitter/) for more information on starting one or more StatsD Emitters.

### Integrating with Cassandra/HDFS/Kafka

These Infinity frameworks support auto-detection of the `STATSD_UDP_HOST`/`STATSD_UDP_PORT` environment variables. When the StatsD endpoint is detected, they automatically configure the underlying service to send metrics to that endpoint. The frameworks themselves don't yet send metrics of their own, but as Mesos-resident processes, they are likewise advertised StatsD export, so the frameworks could use the same system as the underlying services.

#### Cassandra

As Cassandra nodes start, they will automatically be configured for metrics export, and will show the following in `cassandra-stdout.log`:

```
INFO  18:16:42 Trying to load metrics-reporter-config from file: metrics-reporter-config.yaml
INFO  18:16:42 Enabling StatsDReporter to 127.0.0.1:50030
```

#### HDFS

As HDFS nodes start, they will automatically be configured for metrics export, and will show the following in `stdout`:

```
19:08:02.102 [main] INFO  o.a.m.h.executor.MetricsConfigWriter - Configuring metrics for statsd endpoint in etc/hadoop/hadoop-metrics2.properties: 127.0.0.1:37288 (period 10s)
```

Note that as of this writing there's currently [an HDFS framework bug](https://mesosphere.atlassian.net/browse/HDFS-306) which may prevent stats from reaching upstream.

#### Kafka

As Kafka brokers start, they will automatically be configured for metrics export, and will show the following in `stdout`:

```
[2016-04-07 18:15:18,709] INFO Reporter is enabled and starting... (com.airbnb.metrics.StatsDReporter)
[2016-04-07 18:15:18,782] INFO Started Reporter with host=127.0.0.1, port=35542, polling_period_secs=10, prefix= (com.airbnb.metrics.StatsDReporter)
```

## Receiving Metrics via Kafka (1.8 EE)

All agents running the metrics module periodically attempt to connect to a local TCP port at `127.0.0.1:8124`. The avro output format is disabled until the endpoint is resolved, but containers are still given `STATSD_UDP_HOST`/`STATSD_UDP_PORT` endpoints, so forwarding can begin immediately once `127.0.0.1:8124` has successfully connected.

Once `127.0.0.1:8124` has connected, the module begins sending data in Avro OCF format as described in the [schema standard](schema/). If the connection is lost, the module will periodically attempt to reconnect automatically, dropping any data that cannot be sent in the meantime.

### Launching a Collector

The Collector is the process which runs on each DC/OS agent node. They listen on a commonly-known local TCP port (8124), accepting metrics from local system processes and sending them upstream to a Kafka cluster. Collectors currently run as Mesos tasks, but this may be revisited later.

See the [Collector docs](collector/) for more information on starting the Collectors.

### Launching Consumers

The Consumers retrieve data which has been published to the Kafka cluster. One or more Consumer types may consume the same data, and more than one Consumer instances may run in each type. These are standard behavior for Kafka Consumers.

See the [Consumer docs](consumer/) for more information on starting Consumerss.

## Receiving Metrics via StatsD (1.7 EE)

Before we get started, it's worth noting that direct statsd output from the agent is meant for demo/testing purposes and is **not suitable for real everyday use**. Here are some reasons:
- Effectively zero protections against silently losing data if there's a hiccup, compared to Kafka
- No support for passing through arbitrary tag data from containers, unless the output format is manually switched to `tag_datadog` on all agents. But this in turn breaks compatibility with most statsd implementations.
- No support for sending data via a collector, so other non-Agent processes on the system need to implement their own systems for getting data upstream.

Now on with the instructions...

In addition to the above Kafka support, all agents running the metrics module also periodically do an A Record lookup of `metrics.marathon.mesos` (aka a Marathon job named `metrics`). The statsd output format is disabled until the endpoint is resolved, but containers are still given `STATSD_UDP_HOST`/`STATSD_UDP_PORT` endpoints, so forwarding can begin immediately once `metrics.marathon.mesos` begins to resolve.

Once `metrics.marathon.mesos` resolves to one or more A Records, the module picks one A Record at random and starts sending metrics to port `8125` (the standard statsd port) at that location. The `metrics.marathon.mesos` hostname continues to be periodically resolved, and any material changes to the returned list of records (entries changed/added/removed) will trigger a reselection of a random A Record.

If `metrics.marathon.mesos` no longer resolves after sending is begun (ie the `metrics` Marathon job is stopped), the module will continue to send metrics to its current destination, rather than dropping data. This is intended to avoid any issues/bugs with DNS itself causing metrics to stop flowing. This behavior may be revisited later.

### Launching a test StatsD receiver

This is just a script which runs `nc -ul 8125`. A minute or two after the job comes up, `metrics.marathon.mesos` will be resolved by the mesos agents, at which point `nc` will start printing anything it receives to stdout.

In Marathon, create the following application (in JSON Mode):
```json
{
  "id": "metrics",
  "cmd": "LD_LIBRARY_PATH=. ./nc -ul 8125",
  "cpus": 1,
  "mem": 128,
  "disk": 0,
  "instances": 1,
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/nc.tgz"
  ],
  "portDefinitions": [
    {
      "port": 8125,
      "protocol": "udp",
      "name": null,
      "labels": null
    }
  ],
  "requirePorts" : true
}
```

## Launching a sample graphite receiver

Runs a sample copy of Graphite in a Docker container. This is just a stock Docker image that someone put up on Dockerhub. It is **NOT** suitable for real production use, as it merely takes hours for it to consume gigabytes of space and then fall over. Note that **using this receiver requires `annotation_mode` = `key_prefix`**, which is the default in DCOS. `tag_datadog` is NOT supported.

In Marathon, create the following application (in JSON Mode):
```json
{
  "id": "metrics",
  "cmd": null,
  "cpus": 1,
  "mem": 512,
  "disk": 0,
  "instances": 1,
  "acceptedResourceRoles": [ "slave_public" ],
  "container": {
    "type": "DOCKER",
    "docker": {
      "image": "hopsoft/graphite-statsd",
      "network": "BRIDGE",
      "portMappings": [
        { "hostPort": 80,   "protocol": "tcp" },
        { "hostPort": 2003, "protocol": "tcp" },
        { "hostPort": 2004, "protocol": "tcp" },
        { "hostPort": 2023, "protocol": "tcp" },
        { "hostPort": 2024, "protocol": "tcp" },
        { "hostPort": 8125, "protocol": "udp" },
        { "hostPort": 8126, "protocol": "tcp" }
      ]
    }
  }
}
```

The image should deploy to a public agent instance (due to the `slave_public` resource role). Once it's up and running, you need to find the ip of the node it's running on:
1. Go to http://<your_cluster>/mesos and determine the id of the public agent.
2. SSH into that node with `dcos node ssh --master-proxy --mesos-id=<the id>`
3. Run `curl http://ipinfo.io/ip` on the node to get its public IP.

Once you have the public node IP, you may connect to the docker image with any of the following:
- Visit http://<public_agent_ip> (port 80) to view Graphite
- `telnet` into port 8126 to view the statsd daemon's console (tip: type `help`)

Once the image has been up for a few minutes, it should start getting metrics from mesos-agents as `metrics.marathon.mesos` starts to resolve to it. In Graphite's left panel, navigate into `Metrics > stats > gauges > [fmwk_id] > [executor_id] > [container_id] > ...` to view the gauges produced by the application. Most applications seem to stick to gauge-type metrics, while the example `statsd-emitter` produces several types.
