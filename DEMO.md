# Metrics module usage

This module is included in DCOS EE (only), starting with 1.7.

In DCOS 1.7 EE, the module supports forwarding metrics in `statsd` format to `metrics.marathon.mesos`. Support for additional export destinations is planned for DCOS 1.8 EE.

Here are some examples for trying out metrics support on a 1.7+ EE cluster:

## Emitting Metrics

Here are some examples of ways to run services that support container metrics.

Emitting metrics is simple: The program just needs to look for `STATSD_UDP_HOST` and `STATSD_UDP_PORT` environment variables. When they're present, the host:port they advertise may be used as a destination for statsd-formatted metrics data. See [test-sender/test-sender.go](test-sender.go) for an example.

### Launching a test sender

A sample program that just emits various arbitrary metrics to the endpoint advertised via `STATSD_UDP_HOST`/`STATSD_UDP_PORT` environment variables. The sample program's Go source code is included in the .tgz.

In Marathon:
- ID: `test-sender` (the precise name isn't important)
- Command: `./test-sender`
- Optional settings > URIs = `https://s3-us-west-2.amazonaws.com/nick-dev/metrics-msft/test-sender.tgz`
- Extra credit: start (or reconfigure) the sender task with >1 instances to test sending metrics from multiple sources.

Or in JSON Mode:
```json
{
  "id": "test-sender",
  "cmd": "./test-sender",
  "cpus": 1,
  "mem": 128,
  "disk": 0,
  "instances": 1,
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-msft/test-sender.tgz"
  ]
}
```

### Launching a Kafka or Cassandra sender

The Kafka and Cassandra framework executors already support auto-detection of the `STATSD_UDP_HOST`/`STATSD_UDP_PORT` environment variables, configuring the underlying service to send metrics to that location. At the moment, the frameworks don't send metrics of their own, but they could do that too.

#### Kafka

As Kafka brokers start, they will automatically be configured for metrics export, and will show the following in `stdout`:

```
[2016-04-07 18:15:18,709] INFO Reporter is enabled and starting... (com.airbnb.metrics.StatsDReporter)
[2016-04-07 18:15:18,782] INFO Started Reporter with host=127.0.0.1, port=35542, polling_period_secs=10, prefix= (com.airbnb.metrics.StatsDReporter)
```

#### Cassandra

As Cassandra nodes start, they will automatically be configured for metrics export, and will show the following in `cassandra-stdout.log`:

```
INFO  18:16:42 Trying to load metrics-reporter-config from file: metrics-reporter-config.yaml
INFO  18:16:42 Enabling StatsDReporter to 127.0.0.1:50030
```

## Receiving Metrics

All agents running the metrics module periodically do an A Record lookup of `metrics.marathon.mesos` (aka a Marathon job named `metrics`). Until it resolves, metrics data will be dropped at the agents, since there's nowhere for them to go, but containers are still given `STATSD_UDP_HOST`/`STATSD_UDP_PORT` endpoints at this point, so forwarding can begin immediately once `metrics.marathon.mesos` begins to resolve.

Once `metrics.marathon.mesos` resolves to one or more A Records, the module picks one A Record at random and starts sending metrics to port `8125` (the standard statsd port) at that location. The `metrics.marathon.mesos` hostname continues to be periodically resolved, and any material changes to the returned list of records (entries changed/added/removed) will trigger a reselection of a random A Record.

If `metrics.marathon.mesos` no longer resolves after sending is begun (ie the `metrics` Marathon job is stopped), the module will continue to send metrics to its current destination, rather than dropping data. This is intended to avoid any issues/bugs with DNS itself causing metrics to stop flowing. This behavior may be revisited later.

### Launching a test receiver

This is just a shell script that runs `nc -ul 8125`. A minute or two after the job comes up, `metrics.marathon.mesos` will be resolved by the mesos-agents, at which point `nc` will start printing anything it receives to stdout.

In Marathon, create the following job in JSON Mode:
```json
{
  "id": "metrics",
  "cmd": "LD_LIBRARY_PATH=. ./nc -ul 8125",
  "cpus": 1,
  "mem": 128,
  "disk": 0,
  "instances": 1,
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-msft/test-receiver.tgz"
  ]
}
```

## Launching a sample graphite receiver

Runs a sample copy of Graphite in a Docker container. This is just a stock Docker image that someone put up on Dockerhub. Note that **using this receiver requires `annotation_mode` = `key_prefix`**, which is the default in DCOS. `tag_datadog` is NOT supported.

In Marathon, create the following job in JSON Mode:
```json
{
  "id": "/metrics",
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

Once the image has been up for a few minutes, it should start getting metrics from mesos-agents as `metrics.marathon.mesos` starts to resolve to it. In Graphite's left panel, navigate into `Metrics > stats > gauges > [fmwk_id] > [executor_id] > [container_id] > ...` to view the gauges produced by the application. Most applications seem to stick to gauge-type metrics, while the example `test-sender` produces several types.
