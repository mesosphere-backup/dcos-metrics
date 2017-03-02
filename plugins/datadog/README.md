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
├── plugins
│   └── dcos-metrics-datadog_plugin-1.0.0-rc7
└── statsd-emitter
    └── dcos-metrics-statsd-emitter-1.0.0-rc7
```

### Install the datadog agent in your cluster
Install the `datadog` package in DC/OS:
- Visit `https://YOURCLUSTER.COM/universe/packages`
- Click the INSTALL button for the `datadog` package.
- Go into ADVANCED INSTALLATION and fill in [your datadog API_KEY](https://app.datadoghq.com/account/settings#api).
- After filling in the API_KEY, install the package by clicking REVIEW AND INSTALL then INSTALL.

After a minute or two a datadog agent will be running in the cluster at `datadog-agent.marathon.mesos:8125`. This is the default location used by the datadog plugin.

### Test the DC/OS datadog metrics plugin (agents only)
As a stopgap during testing, you may be able to manually run the datadog plugin on your agents by running it as a Marathon task. You must first upload the binary you built to a web server that's visible to your cluster, then create a Marathon application like the following (with customized `cmd`, `instances`, and `uris` to meet your needs):

```
{
  "cmd": "chmod +x ./dcos-metrics-* && ./dcos-metrics-* -dcos-role agent -auth-token <CONTENT OF 'dcos config show core.dcos_acs_token'>",
  "instances": NUMBER_OF_AGENTS,
  "uris": [ "https://YOURFILEHOST.COM/dcos-metrics-datadog_plugin-YOURBUILDVERSION" ],
  "id": "test-datadog-plugin",
  "cpus": 0.1,
  "mem": 128,
  "disk": 0,
  "acceptedResourceRoles": [ "slave_public", "*" ]
}
```

### Install the DC/OS datadog metrics plugin
Once you're happy with the result, you'll need to install the plugin into your cluster. For each host in your cluster, you'll need to transfer the binary you build for the plugin and then add a systemd unit to manage the service. This unit differs slightly between agent and master hosts.

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
