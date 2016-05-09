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

This start-to-finish example assumes `dcos-stats` is placed in `~/code/dcos-stats` and that `GOPATH` is placed in `~/go`. Adjust these assumptions to fit your preferences.

```bash
mkdir -p ~/code
cd ~/code
git clone git@github.com:mesosphere/dcos-stats
export GO15VENDOREXPERIMENT=1 # required only if using Go 1.5. for Go 1.6+ this step can be skipped
export GOPATH=/YOUR/GOPATH # eg ~/go
mkdir -p $GOPATH/src/github.com/mesosphere
ln -s ~/code/dcos-stats $GOPATH/src/github.com/mesosphere/dcos-stats
cd $GOPATH/src/github.com/mesosphere/dcos-stats/collector/cmd/sample-producer
go build
./sample-producer -h
```

### Deploy

1. Install/configure the Kafka framework on your DCOS cluster.
2. Upload the `sample-producer` executable you built to an agent node. Ideally the agent node should have containers currently running on it. If no containers are running, `sample-producer` will send nothing to Kafka until containers have appeared.
3. Run the producer as `./sample-producer -framework <kafka-fmwk-name>` (add an `&` to launch as a background task). Assuming the targeted Kafka framework is up and running, `sample-producer` should automatically detect the brokers and start forwarding stats from `http://<agent-ip>:5051/monitor/statistics.json` to those brokers.
