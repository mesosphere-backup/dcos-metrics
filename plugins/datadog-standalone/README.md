# Standalone Datadog Metrics Service Plugin for DC/OS

This plugin is an alternative to the [regular Datadog plugin](https://github.com/dcos/dcos-metrics/plugins/datadog) for
those who do not wish to make use of the Datadog Agent.

## Installation

### Build this plugin (requires a Golang environment)
1. `git clone git@github.com:dcos/dcos-metrics`
1. `cd dcos-metrics && make test && make plugins`

As detailed in the Datadog plugin docs, the resulting binary (dcos-metrics-datadog-standalone-plugin), which will be
built to the `build/plugins` directory wth the dcos-metrics version appended to its filename, can then be installed on
each node in the cluster.

This plugin takes a `datadog-key` flag in addition to the normal plugin args; a sample call looks like this:

```bash
./dcos-metrics-datadog-standalone-plugin \
  --dcos-role    agent \
  --metrics-host localhost \
  --metrics-port 9000 \
  --auth-token   aaZaaZaaZaZZZaZ1ZaZaZaaaZZZ1ZaZaZ1ZaaZZaZaZ1aZZ1ZaaZZZZ1... \
  --datadog-key  11a1aaaa1aa11a111aaa1aa111a11111
```