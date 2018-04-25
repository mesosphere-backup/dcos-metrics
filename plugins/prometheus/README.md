# Prometheus Plugin for DC/OS Metrics

This plugin serves all metrics collected by the dcos-metrics service in [Prometheus format][1]. Note that this plugin is
only needed for DC/OS 1.9 and 1.10; as of DC/OS 1.11 this functionality is [built into dcos-metrics][2].

## Installation & Usage

Refer to the [quickstart documentation][3] for instructions on installing and using this plugin.

To install the plugin on DC/OS 1.9 and 1.10, it is necessary to follow the next steps:

1. Download the latest prometheus plugin binary from the [releases](https://github.com/dcos/dcos-metrics/releases) page
1. Upload the binary as `/opt/mesosphere/bin/dcos-metrics-prometheus-plugin` on every node in your cluster
1. Download the [environment file](./systemd/dcos-metrics-prometheus.env)
1. Upload the environment file to `/opt/mesosphere/etc` on every node
1. On every master node:
    1. Copy the [master systemd service](./systemd/dcos-metrics-prometheus-master.service) file to `/etc/systemd/system`
    1. Reload the systemd state by running `systemctl daemon-reload`
    1. Start the systemd service with `systemctl start dcos-metrics-prometheus-master`
1. On every agent node:
    1. Copy the [agent systemd service](./systemd/dcos-metrics-prometheus-agent.service) file to `/etc/systemd/system`
    1. Reload the systemd state by running `systemctl daemon-reload`
    1. Start the systemd service with `systemctl start dcos-metrics-prometheus-agent`


### Building this plugin (requires a Golang environment)

1. `go get -u github.com/dcos/dcos-metrics`
1. `cd $(go env GOPATH)/src/github.com/dcos/dcos-metrics`
1. `make && make plugins`

The resulting binary (dcos-metrics-prometheus-plugin), which will be built to the `build/plugins` directory
wth the dcos-metrics version appended to its filename, can then be installed on each node in the cluster.

[1]: https://prometheus.io/docs/instrumenting/writing_exporters/
[2]: https://github.com/dcos/dcos-metrics/releases/tag/1.11.0
[3]: ../../docs/quickstart/prometheus.md
