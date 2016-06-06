# Metrics Collector

Lightweight standalone process which runs on mesos-agent nodes.

Inputs metrics [sent over TCP](../schema/) from local sources:
- The [mesos-agent module](../module/), for metrics relating to containers
- Other DCOS processes on the system

Outputs metrics to the following destinations:
- Kafka cluster
- Local partner processes, if any

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

## Run

The Collector runs on each Mesos agent, listening on TCP port 8124 for data from the mesos module (and any other processes on the system). The Collector then sends any avro-formatted data it receives into a Kafka cluster.

### Local Test

The Collector's Kafka export can be disabled for local use. Just run it with `-kafka=false -agent-polling=false`, and optionally with `-record-input-log` and/or `-record-output-log`.

With the Collector running in this mode, sample data can be sent to it using the reference [collector-emitter](../examples/collector-emitter/), or by sending it a [generated .avro file](../schema) using netcat:

```bash
# generate some random data (see schema/):
java -jar avro-tools-1.8.0.jar random --schema-file metrics.avsc --count 1000 random.avro

# start a test collector with kafka and agent polling disabled:
./collector -kafka=false -agent-polling=false

# then in another window, pipe the random data to the collector:
cat random.avro | nc 127.0.0.1 8124
```

### Deployment to a Cluster

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
    "KAFKA_TOPIC_PREFIX": "metrics-"
  },

  "cmd": "env && chmod +x ./collector && ./collector",
  "cpus": 1,
  "mem": 128,
  "disk": 0,
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/collector.tgz"
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

Once `collector` is up and running, the raw binary data that it's passing to Kafka may be viewed by running one or more [Kafka metrics consumers](../consumer/).
