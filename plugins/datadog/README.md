# Datadog Metrics Service Plugin for DC/OS
This plugin supports sending metrics from the DC/OS metrics service on both master and agent hosts to a datadog agent for shipping to DatadogHQ.

## Installation
### Build this plugin (requires a Golang environment)
1. `git clone git@github.com:dcos/dcos-metrics`
1. `cd dcos-metrics && make`

Plugin is available in the build directory:
```
 tree build
build
├── collector
│   └── dcos-metrics-collector-1.0.0-rc7
├── datadog_plugin
│   └── dcos-metrics-datadog_plugin-1.0.0-rc7
└── statsd-emitter
    └── dcos-metrics-statsd-emitter-1.0.0-rc7
```

### Install the datadog agent in your cluster
Checkout your [Datadog account](https://app.datadoghq.com/account) for more info on getting and installing agent's for your specific OS. 

For example sake, here's the one we used to test this project on CoreOS:
```
docker run -d --name dd-agent -h `hostname` -v /var/run/docker.sock:/var/run/docker.sock:ro -v /proc/:/host/proc/:ro -v /sys/fs/cgroup/:/host/sys/fs/cgroup:ro -e SD_BACKEND=docker -e API_KEY=0399e2d327848d3317d15d47dcf40461 datadog/docker-dd-agent:latest
```

Note in testing, we had to add `-p 8125/8125/udp` to the `docker run` command in order to proxy the container port for UDP access. 

### Install the DC/PS datadog metrics plugin
For each host in your cluster, you'll need to transfer the binary you build for the plugin and then add a systemd unit to manage the service. This unit differs slightly between agent and master hosts.

#### Create a Valid Auth Token for DC/OS
The DC/OS docs have good info on making this auth token for [OSS](https://dcos.io/docs/1.7/administration/id-and-access-mgt/managing-authentication/) and [enterprise](https://docs.mesosphere.com/1.8/administration/id-and-access-mgt/service-auth/custom-service-auth/) DC/OS.

#### Deploy the Metrics Plugin to Every Cluster Host
1. `scp dcos-metrics-datadog-plugin-1.0.0-rc7 my.host:/usr/bin`
1. `ssh my.master "chmod 0755 /usr/bin/dcos-metrics-datadog-plugin-1.0.0-rc7"` 

#### Master Systemd Unit
Add a master systemd unit file: `cat /etc/systemd/system/dcos-metrics-datadog-plugin.service`
```
[Unit]
Description=DC/OS Datadog Metrics Plugin (master)

[Service]
ExecStart=/usr/bin/dcos-metrics-datadog-plugin-1.0.0rc7 -dcos-role master -metrics-port 80 -auth-token <MY_AUTH_TOKEN>
```

#### Agent Systemd Unit
Add a agent systemd unit file: `cat /etc/systemd/system/dcos-metrics-datadog-plugin.service`
```
[Unit]
Description=DC/OS Datadog Metrics Plugin (agent)

[Service]
ExecStart=/usr/bin/dcos-metrics-datadog-plugin-1.0.0rc7 -dcos-role agent -auth-token <MY_AUTH_TOKEN>
```

*Note:* This plugin runs on port :61001 (agent adminrouter) by default, so we don't pass the port as we did in the master version of the service.

#### Enable, Start & Verify
1. `systemctl enable dcos-metrics-datadog-plugin && systemctl start dcos-metrics-datadog-plugin`
1. `journalctl -u dcos-metrics-datadog-plugin` -> Make sure everything is OK

## That's it!
