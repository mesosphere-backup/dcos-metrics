# Prometheus plugin for DC/OS Metrics

This plugin exposes all data received by dcos-metrics in Prometheus format,
allowing you to channel node, container and app metrics to a central Prometheus
server.

This endpoint includes data from:
1. The local node - CPU, memory, disk usage etc
1. Each container running on this node, if it is a Mesos agent
1. Any metrics received over statsd from each container's workload

## Architecture

The Prometheus plugin uses the dcos-metrics plugin API. You run the plugin
binary on each node in your cluster. It polls its local HTTP API for metrics,
which it aggregates and offers for scraping by your Prometheus server.

## Usage

### Installing and starting the plugin

1. Download the latest prometheus plugin binary from  the [releases](https://github.com/dcos/dcos-metrics/releases) page
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

### Running a Prometheus server on a DC/OS cluster

You can deploy a [prometheus server](./marathon/prometheus-server.json) with
Marathon by running

`dcos marathon app add prometheus-server.json`

You can also deploy it manually with the DC/OS UI.

You will need to configure the Prometheus server to discover an endpoint on
each of your nodes. You should do this by adding the IP address of each node
to the `static_configs` array in the `scrape_configs` section of your
`prometheus.yml` file. Remember to specify the port you're using too.

### Configuring the plugin

The plugin serves by default on http://localhost:8088. (8088 was chosen because
8080 is the standard, but was already reserved for Marathon). If you would like
to serve on a different port, you can modify the environment file:

1. Download the [environment file](./systemd/dcos-metrics-prometheus.env)
1. Modify the `PROMETHEUS_PORT` EV to your desired value
1. Upload the environment file to `/opt/mesosphere/etc` on every node
1. Reload systemd state by running `systemctl daemon-reload` on every node
1. Restart the systemd services by running
    1. `systemctl restart dcos-metrics-prometheus-agent` on the agent nodes and
    1. `systemctl restart dcos-metrics-prometheus-master` on the master nodes

## Testing your setup

Launch the [statsd-emitter](./marathon/statsd-emitter.json) test task on
Marathon:

`dcos marathon app add statsd-emitter.json`

Then check your prometheus frontend for the 'statsd_tester_time_uptime' metric
- if it is present, you have configured everything correctly.