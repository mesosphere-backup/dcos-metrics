## DC/OS Metrics Service Plugins

DC/OS Metrics Plugins are the best way to get your metrics from DC/OS to your monitoring platform.

Plugins are binaries which you run on each node in your DC/OS cluster. There they monitor the DC/OS Metrics API,
aggregating, transforming, and pushing or offering them as appropriate.

The following plugins are available for use:
 - [Datadog](./datadog-standalone) - sends your metrics straight to DatadogHQ
 - [Librato](./librato) - sends your metrics to Librato
 - [Prometheus](./prometheus) - serves prometheus-format metrics (DC/OS 1.9 and 1.10 only)

The following plugins are not recommended for production use:
 - [Datadog Agent](./datadog) - deprecated, sends your metrics to the Datadog Agent
 - [StatsD](./statsd) - experimental, sends your metrics to a StatsD server
 - [stdout](./stdout) - for debugging only, prints your metrics to stdout

Missing a plugin? You can get in touch with the team and ask us to write a new one, or you can
[write your own](#Developing).

### Installation
Plugins can be installed on each node.

The prebuilt binaries are linked from the [README](https://github.com/dcos/dcos-metrics/tree/master/README.md) or can
be built from source according to preference. The binary should be copied to the `/opt/mesosphere/bin` directory on
each node.

Each plugin has (or will soon have) a `systemd` directory containing systemd unit files for the master and agent.
These should be downloaded, edited as appropriate - each plugin has its own flags where you may need to define API
keys or make customisations - and then uploaded to the /etc/systemd/system` directory on each node.

This done, the new systemd service should be added and started (`systemctl daemon-reload && systemctl start <plugin>`)

### Compatibility
Regardless of the release to which they are attached, all plugins are compatible with DC/OS 1.9 and above.

### The future
In 1.11, a new and improved metrics API will be available which should make plugins much easier to write and install.

When that happens, the plugins that reside in this directory will be deprecated. They will continue to work. New plugins
using the updated API will be developed and released to replace them.

In later versions of DC/OS, we expect these plugins to cease to function as the API on which they depend will no longer
be available.

### Developing
See [CONTRIBUTING.md](./CONTRIBUTING.md)

