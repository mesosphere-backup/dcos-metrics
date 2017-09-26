# Librato Service Plugin for DC/OS
This plugin supports sending metrics from the DC/OS metrics service on both master and agent hosts to [Librato](https://metrics.librato.com).

## Installation

### Build this plugin (requires a Golang environment)
1. `go get -u github.com/dcos/dcos-metrics`
1. `cd $(go env GOPATH)/src/github.com/dcos/dcos-metrics`
1. `make && make plugins`

The plugin will be available in the build directory:

```
$ tree build
build/
|-- collector
|   `-- dcos-metrics-collector-1.1.0-44-ga9f1e73
|-- plugins
|   `-- dcos-metrics-librato-plugin@1.1.0-44-ga9f1e73
`-- statsd-emitter
    `-- dcos-metrics-statsd-emitter-1.1.0-44-ga9f1e73
```

### Install the DC/OS Librato metrics plugin
For each host in your cluster, you'll need to transfer the binary you build for the plugin and then add a systemd unit to manage the service. This unit differs slightly between agent and master hosts.

#### Create a Valid Auth Token for DC/OS
The DC/OS docs have good info on making this auth token for [OSS](https://dcos.io/docs/1.7/administration/id-and-access-mgt/managing-authentication/) and [enterprise](https://docs.mesosphere.com/1.8/administration/id-and-access-mgt/service-auth/custom-service-auth/) DC/OS.

#### Deploy the Metrics Plugin to Every Cluster Host
1. `scp build/plugins/dcos-metrics-librato-plugin@1.1.0-44-ga9f1e73 my.host:/usr/bin`
1. `ssh my.master "chmod 0755 /usr/bin/dcos-metrics-librato-plugin@1.1.0-44-ga9f1e73"`

#### Master Systemd Unit
Add a master systemd unit file: `cat /etc/systemd/system/dcos-metrics-librato-plugin.service`

```
[Unit]
Description=DC/OS Librato Metrics Plugin (master)

[Service]
ExecStart=/usr/bin/dcos-metrics-librato-plugin@1.1.0-44-ga9f1e73 \
	-dcos-role master \
	-auth-token <MY_AUTH_TOKEN> \
	-metrics-port 80 \
	-librato-email "your librato email" \
	-librato-token "your librato API token with record permissions" \
	-librato-metric-prefix "dcos"
```

#### Agent Systemd Unit
Add a agent systemd unit file: `cat /etc/systemd/system/dcos-metrics-librato-plugin.service`

```
[Unit]
Description=DC/OS Librato Metrics Plugin (agent)

[Service]
ExecStart=/usr/bin/dcos-metrics-librato-plugin@1.1.0-44-ga9f1e73 \
	-dcos-role agent \
	-auth-token <MY_AUTH_TOKEN> \
	-librato-email "your librato email" \
	-librato-token "your librato API token with record permissions" \
	-librato-metric-prefix "dcos"
```

*Note:* This plugin runs on port :61001 (agent adminrouter) by default, so we don't pass the port as we did in the master version of the service.

#### Enable, Start & Verify
1. `systemctl enable dcos-metrics-librato-plugin && systemctl start dcos-metrics-librato-plugin`
1. `journalctl -u dcos-metrics-librato-plugin` -> Make sure everything is OK

## That's it!
