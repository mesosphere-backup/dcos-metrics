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
go generate # creates "metrics_schema_generated.go" in collector dir

cd cmd/sample-producer
go build # creates "sample-producer" executable
./sample-producer -h
```

If you see errors about `undefined: collector.[*]Namespace` and `undefined: collector.[*]Schema`, you forgot to run `go generate` in the `dcos-stats/collector/` directory to create `dcos-stats/collector/metrics_schema_generated.go`.

### Deploy

1. Install/configure the Kafka framework on your DCOS cluster. By default it will be named `kafka`.
2. Upload the `sample-producer` executable you built to an agent node. Ideally the agent node should have containers currently running on it. If no containers are running, `sample-producer` will send nothing to Kafka until containers have appeared.
3. Run the producer as `./sample-producer -framework <kafka-fmwk-name>` (add an `&` to launch as a background task). Assuming the targeted Kafka framework is up and running, `sample-producer` should automatically detect the brokers and start forwarding stats from `http://<agent-ip>:5051/monitor/statistics.json` to those brokers.
