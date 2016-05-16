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

## Build

Prerequisites:
- git
- [Go 1.5+](https://golang.org/dl/)

First, get the code and set up the environment (edit `/YOUR/CODEPATH` and `/YOUR/GOPATH` as needed):

```bash
export CODEPATH=/YOUR/CODEPATH # eg ~/code
export GOPATH=/YOUR/GOPATH # eg ~/go
export GO15VENDOREXPERIMENT=1 # only required if using Go 1.5. for Go 1.6+ this step can be skipped

mkdir -p $CODEPATH
cd $CODEPATH

git clone git@github.com:mesosphere/dcos-stats
mkdir -p $GOPATH/src/github.com/mesosphere
ln -s $CODEPATH/dcos-stats $GOPATH/src/github.com/mesosphere/dcos-stats
```

Then preprocess the Avro schema and build the code:

```bash
cd $GOPATH/src/github.com/mesosphere/dcos-stats/collector
go generate # creates 'metrics-schema' package

cd collector/
go build
./collector -h
```

If you see errors about `cannot find package "github.com/.../metrics-schema"`, you forgot to perform `go generate` in the `dcos-stats/collector/` directory.

### Updating vendor dependencies

This project uses [`govendor`](https://github.com/kardianos/govendor) to manage library dependencies in the `vendor/` directory. This is used for adding new dependencies, removing old dependencies, and updating dependency versions.

```bash
go get github.com/kardianos/govendor
cd $CODEPATH/dcos-stats/collector
govendor add +external
govendor status
govendor list
```

## Collector

The Collector runs on each Mesos agent, listening on TCP port 8124 for data from the mesos module (and any other processes on the system). The Collector then sends any avro-formatted data it receives into a Kafka cluster.

### Run Locally

The Collector's Kafka export can be disabled for local use. Just run it with `-kafka-enabled=false` (and optionally `-log-record-input` and/or `-log-record-output`).

With the Collector running in this mode, sample data can be sent to it using the reference [collector-emitter](../examples/collector-emitter/).

### Deploy To Cluster

1. Configure and deploy a Kafka instance on your DC/OS cluster. By default it will be named `kafka`. If you use a different name, you'll need to customize the `KAFKA_FRAMEWORK` value below.
2. Run `collector` as a Marathon task by editing and submitting the following JSON config:
  - Set `instances` to the number of instances to run, or just leave it as-is 100. At most one instance will run on each agent node due to the port requirement. If you have 5 nodes and you launch 6 instances, the 6th instance will stay in an "Unscheduled" state in Marathon. This doesn't hurt anything.
  - If you named your Kafka cluster something other than the default `kafka`, edit `KAFKA_FRAMEWORK` in the env config to match the name of your deployed Kafka cluster.
  - If you're wanting to use a custom build, upload the `collector` executable you built to somewhere that's visible to your cluster (eg S3), then modify the `uris` value below.
  - If you want to run the producer on your Public nodes, you must create a separate additional task in Marathon, with a different `id`, which also includes `acceptedResourceRoles": [ "slave_public" ]` in its config.

```json
{
  "id": "metrics-collector",
  "instances": 100,
  "env": {
    "KAFKA_FRAMEWORK": "kafka",
    "KAFKA_SINGLE_TOPIC": "sample_metrics",
    "LOG_RECORD_INPUT": "true",
    "LOG_RECORD_OUTPUT": "true"
  },

  "cmd": "env && chmod +x ./collector && ./collector",
  "cpus": 1,
  "mem": 128,
  "disk": 0,
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/collector"
  ],
  "portDefinitions": [
    {
      "port": 8124,
      "protocol": "tcp",
      "name": null,
      "labels": null
    }
  ],
  "requirePorts" : true
}
```

See `collector -h` for the many additional options which may be configured via the system environment or via commandline flags. Each option is associated with both a system environment variable and a commandline argument.

As `collector` is deployed on every node, each instance should automatically start forwarding stats from `http://<local-agent-ip>:5051/monitor/statistics.json` to the brokers it got from querying the Kafka Scheduler. Once all nodes have been filled, the Marathon task will enter a `Waiting` state, but all nodes will have a running copy at this point.

If the Kafka framework isn't reachable (not deployed yet? wrong name passed to `KAFKA_FRAMEWORK` envvar?), then `collector` will loop until it comes up (complaining to `stderr` every few seconds).

### Consuming collected data

Once `collector` is up and running, the raw binary data it's passing to Kafka may be viewed by running this task (see `stdout` once it's launched). This assumes that the collector was started with `KAFKA_SINGLE_TOPIC=sample_metrics` to force all Kafka data into a single topic named `sample_metrics`. By default without this parameter, the collector will send data to multiple source-specific topics.

```json
{
  "id": "console-consumer",
  "cmd": "JAVA_HOME=./jre* ./kafka_2.10-0.9.0.1/bin/kafka-console-consumer.sh --topic sample_metrics --zookeeper master.mesos:2181/kafka",
  "cpus": 1,
  "mem": 512,
  "disk": 0,
  "instances": 1,
  "uris": [
    "https://s3.amazonaws.com/downloads.mesosphere.io/kafka/assets/kafka_2.10-0.9.0.1.tgz",
    "https://s3.amazonaws.com/downloads.mesosphere.io/kafka/assets/jre-8u72-linux-x64.tar.gz"
  ]
}
```

## Sample Producer

The sample producer is an alternate Collector implementation which just grabs some data from the mesos agent and forwards it to a Kafka cluster.

It's meant to allow testing of metrics Kafka Consumers without requiring that the DC/OS cluster have the latest Metrics Agent Module.

### Deploy

1. Configure and deploy a Kafka instance on your DC/OS cluster. By default it will be named `kafka`.
2. Upload the `sample-producer` executable you built to somewhere that's visible to your cluster (eg S3).
3. Run `sample-producer` as a task in Marathon, by editing the following JSON config:
  - Set `instances` to the number of instances to run. At most one instance will run on each agent node. If you have 5 nodes and you launch 6 instances, the 6th instance will stay in an "Unscheduled" state in Marathon.
  - Edit `kafka` in the env to match the name of your deployed Kafka cluster, if needed.
  - If you want to run the producer on your Public nodes, you must create a separate additional task in Marathon, with a different `id`, which also includes `acceptedResourceRoles": [ "slave_public" ]` in its config.

```json
{
  "id": "sample-producer",
  "instances": 100,
  "env": {
    "KAFKA_FRAMEWORK": "kafka"
  },

  "cmd": "env && chmod +x ./sample-producer && ./sample-producer",
  "cpus": 1,
  "mem": 128,
  "disk": 0,
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/sample-producer"
  ],
  "portDefinitions": [
    {
      "port": 8124,
      "protocol": "tcp",
      "name": null,
      "labels": null
    }
  ],
  "requirePorts" : true
}
```

As `sample-producer` is deployed on every node, each instance should automatically start forwarding stats from `http://<local-agent-ip>:5051/monitor/statistics.json` to the brokers it got from querying the Kafka Scheduler. Once all nodes have been filled, the Marathon task will enter a `Waiting` state, but all nodes will have a running copy at this point.

If the Kafka framework isn't reachable (not deployed yet? wrong name passed to `KAFKA_FRAMEWORK` envvar?), then `sample-producer` will loop until it comes up (complaining to `stderr` every few seconds).

Data sent by `sample-producer` is running, its data can be accessed by running a `sample-consumer` task as described above.
