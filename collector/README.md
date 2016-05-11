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

## Sample Producer

The sample producer is a Collector implementation which just grabs some data from the mesos agent and forwards it to a Kafka cluster. It's meant to allow Kafka Consumers to be implemented while the real Collector is still being built.

### Build

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

cd cmd/sample-producer/
go build
./sample-producer -h
```

If you see errors about `cannot find package "github.com/.../metrics-schema"`, you forgot to perform `go generate` in the `dcos-stats/collector/` directory.

#### Updating vendor dependencies

This project uses [`govendor`](https://github.com/kardianos/govendor) to manage library dependencies in the `vendor/` directory. This is used for adding new dependencies, removing old dependencies, and updating dependency versions.

```bash
go get github.com/kardianos/govendor
cd $CODEPATH/dcos-stats/collector
govendor add +external
govendor status
govendor list
```

### Deploy

1. Configure and deploy a Kafka instance on your DC/OS cluster. By default it will be named `kafka`.
2. Upload the `sample-producer` executable you built to somewhere that's visible to your cluster (eg S3).
3. Run `sample-producer` as a task in Marathon, by editing the following JSON config:
  - Set `instances` to the number of instances to run. At most one instance will run on each agent node. If you have 5 nodes and you launch 6 instances, the 6th instance will stay in an "Unscheduled" state in Marathon.
  - Edit `kafka` in the env to match the name of your deployed Kafka cluster, if needed.
  - If you want to run the producer on your Public nodes, you must create a separate additional task in Marathon, with a different `id`, which also includes `acceptedResourceRoles": [ "slave_public" ]` in its config.

```json
{
  "instances": 100,
  "env": {
    "KAFKA_FRAMEWORK": "kafka"
  },
  "id": "sample-producer",
  
  "cmd": "env && chmod +x ./sample-producer && ./sample-producer -framework $KAFKA_FRAMEWORK",
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

If the Kafka framework isn't reachable (not deployed yet? wrong name passed to `-framework` arg?), then `sample-producer` will loop until it comes up (complaining to `stderr` every few seconds).

Once `sample-producer` is up and running, the data it's producing may be viewed by running this task (see `stdout` once it's launched):

```
{
  "id": "sample-consumer",
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
