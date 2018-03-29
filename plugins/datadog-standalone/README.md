# Datadog Metrics Service Plugin for DC/OS

This plugin sends all metrics accumulated by the dcos-metrics service to DataDog. 

## Installation & Usage

Refer to the [quickstart documentation][1] for instructions on installing and using this plugin.

### Building this plugin (requires a Golang environment)

1. `go get -u github.com/dcos/dcos-metrics`
1. `cd $(go env GOPATH)/src/github.com/dcos/dcos-metrics`
1. `make && make plugins`

The resulting binary (dcos-metrics-datadog-standalone-plugin), which will be built to the `build/plugins` directory
wth the dcos-metrics version appended to its filename, can then be installed on each node in the cluster.
