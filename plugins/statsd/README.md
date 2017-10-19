# Statsd plugin for DC/OS Metrics

This plugin will forward all data received by dcos-metrics to a statsd
server, as defined by STATSD_HOST and STATSD_PORT environment variables.

## Architecture

The statsd plugin uses the dcos-metrics plugin API. You run the 'plugin' (a
slight misnomer; it is in fact a separate binary) on each node in your cluster.
It polls its local HTTP API for metrics, which it re-transmits to your statsd
server.

### Limitations

 - statsd has no units, therefore *units are not sent*
 - statsd has no tags, therefore *dimensions are not sent*
 - statsd only accepts integer values, therefore *metrics are rounded to the
   nearest integer*
 - statsd assigns its own timestamps when metrics are received, therefore
   *timestamps do not precisely reflect the moment the measurement occurred*
 - dcos-metrics has no concept of timers or counters, therefore *all datapoints
   are sent as gauges*

## Usage

### Installing and starting the plugin

1. Download the latest statsd plugin binary from the [releases](https://github.com/dcos/dcos-metrics/releases) page
1. Upload the binary as `/opt/mesosphere/bin/dcos-metrics-statsd-plugin` on every node in your cluster
1. Download the [environment file](./systemd/dcos-metrics-statsd.env)
1. Upload the environment file to `/opt/mesosphere/etc` on every node
1. On every master node:
    1. Copy the [master systemd service](./systemd/dcos-metrics-statsd-master.service) file to `/etc/systemd/system`
    1. Reload the systemd state by running `systemctl daemon-reload`
    1. Start the systemd service with `systemctl start dcos-metrics-statsd-master`
1. On every agent node:
    1. Copy the [agent systemd service](./systemd/dcos-metrics-statsd-agent.service) file to `/etc/systemd/system`
    1. Reload the systemd state by running `systemctl daemon-reload`
    1. Start the systemd service with `systemctl start dcos-metrics-statsd-agent`

### Running a statsd server on a DC/OS cluster

If you installed the plugin according to the steps above, you can simply deploy
the [statsd server](./marathon/statsd-server.json) application with Marathon.
The simplest way to do that is using the CLI command

`dcos marathon app add statsd-server.json`

You can also deploy it manually with the DC/OS UI.

This will start a Docker container with a statsd server configured and ready to
go. You can configure your own routing using marathon-haproxy or edge-LB, or
you can VPN into your cluster using the [dcos tunnel](https://dcos.io/docs/administration/access-node/tunnel/)
command, after which you can find the Graphite UI at:

http://statsd-server.marathon.l4lb.thisdcos.directory

Please note that this configuration is neither highly available nor persistent;
you will need to adapt it if you want those attributes.

### Configuring the plugin to use your own statsd server

If you'd prefer to configure your own statsd server (or already have one set
up), you'll need to pass its address to the plugin. This requires modifying the
systemd environment file on every node.

1. Download the [environment file](./systemd/dcos-metrics-statsd.env)
1. Modify the `STATSD_UDP_HOST` and `STATSD_UDP_PORT` EVs to your desired value
1. Upload the environment file to `/opt/mesosphere/etc` on every node
1. Reload systemd state by running `systemctl daemon-reload` on every node
1. Restart the systemd services by running
    1. `systemctl restart dcos-metrics-statsd-agent` on the agent nodes and
    1. `systemctl restart dcos-metrics-statsd-master` on the master nodes

## Testing your setup

Followed the steps above, but not sure you've done it right?

Launch the [statsd-emitter](./marathon/statsd-emitter.json) test task on
Marathon:

`dcos marathon app add statsd-emitter.json`

Then check your statsd frontend for the 'statsd_tester.time.uptime' metric - if
it is present, you have configured it correctly.

