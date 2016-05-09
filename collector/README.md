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

### Deploy

1. Configure and deploy a Kafka instance on your DC/OS cluster. By default it will be named `kafka`.
2. Upload the `sample-producer` executable you built to somewhere that's visible to your cluster (eg S3).
3. Run `sample-producer` as a task in Marathon, by editing the following JSON config. Edit `-framework kafka` to match the name of your deployed Kafka cluster, if needed:

```json
TODO marathon task config: require port 8124 (even if sample-producer isn't using it)
```

As `sample-producer` is deployed on every node, each instance should automatically start forwarding stats from `http://<local-agent-ip>:5051/monitor/statistics.json` to the brokers it got from querying the Kafka Scheduler. If the agent isn't running any containers, or if the Kafka Scheduler isn't reachable, it should automatically retry until either situation changes.
