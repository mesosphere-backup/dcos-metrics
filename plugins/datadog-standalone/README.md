# Datadog Metrics Service Plugin for DC/OS

This plugin sends all metrics collected by the dcos-metrics service to [DataDog][1]. 

## Installation & Usage

Refer to the [quickstart documentation][2] for instructions on installing and using this plugin.

### Building this plugin (requires a Golang environment)

1. `go get -u github.com/dcos/dcos-metrics`
1. `cd $(go env GOPATH)/src/github.com/dcos/dcos-metrics`
1. `make && make plugins`

The resulting binary (dcos-metrics-datadog-standalone-plugin), which will be built to the `build/plugins` directory
wth the dcos-metrics version appended to its filename, can then be installed on each node in the cluster.

[1]: https://datadoghq.com/
[2]: ../../docs/quickstart/datadog.md
