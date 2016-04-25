# Metrics module installation and usage

In DCOS 1.7 EE (or newer), the module is already included, so these steps are mainly meant to be a reference for installing a custom build of the module, or for changing the module config.

## Installing a custom build of the module (on EACH mesos-slave system)

The metrics module must be installed on **EACH** mesos-slave system that you want to forward metrics from.
It's recommended that you try these steps end-to-end on a single mesos-slave before continuing to other mesos-slaves, to ensure that you have the configuration you want BEFORE deploying it across the cluster.

1. Build the module against a version of Mesos matches what your cluster is running.
2. Copy the customized `libstats-slave.so` (and any additional dependency libs) into `/opt/mesosphere/lib/`
3. Back up the current versions of `/opt/mesosphere/etc/mesos-slave-common` and `/opt/mesosphere/etc/mesos-slave-modules.json`.
4. Perform the following changes to `mesos-slave-common` and `mesos-slave-modules.json` as needed:
  - `mesos-slave-common`
    - Append `,com_mesosphere_StatsIsolatorModule` to the end of `MESOS_ISOLATION`.
    - REMOVE any existing line defining `MESOS_HOOKS=com_mesosphere_StatsEnvHook`. Recent builds of the module no longer need this.
  - `mesos-slave-modules.json`
    - Add a configuration block for `/opt/mesosphere/lib/libstats-slave.so` which only lists `com_mesosphere_StatsIsolatorModule`. Recent builds of the module no longer need `com_mesosphere_StatsEnvHook`.
5. Make any other changes to settings in `mesos-slave-modules.json` as needed. See below.
6. Last chance to back out! Revert `mesos-slave-common` and `mesos-slave-modules.json` to their original state if you want to abort now. The library files added to `/opt/mesosphere/lib/` are effectively unused until the configs in `/opt/mesosphere/etc/` are referencing them.
7. Restart the `mesos-slave` process, see below.
8. Verify that the module is working by checking `mesos-slave` logs, see below.

## Configuring/customizing the module

All configuration is within `/opt/mesosphere/etc/mesos-slave-modules.json`. The `mesos-slave` process must be restarted for any changes to take effect (see below for how to do this). Here are some explanations of the parameters:
- **"`dest_host`": Hostname/IP for where to forward data received from tasks.**
- "`dest_refresh_seconds`": Duration in seconds between DNS lookups of dest_host. Automatically detects changes in the DNS record and redirects output to the new destination.
- "`dest_port`": Port to use when forwarding stats to dest_host.
- "`annotation_mode`": How (or whether) to tag outgoing data with information about the Mesos task. Available modes are "key_prefix" (prefix statsd keys with the info), "tag_datadog" (use the datadog tag extension), or "none" (no tagging, data forwarded without modification). **If your statsd receiver doesn't support datadog-format statsd tags, this should be 'key_prefix' or 'none'.**
- "`chunking`": Whether to group outgoing data into a smaller number of packets. **If your statsd receiver doesn't support multiple newline-separated statsd records in the same UDP packet, this should be 'false'.**
- "`chunk_size_bytes`": Preferred chunk size for outgoing UDP packets, when "chunking" is enabled. This should be the UDP MTU.

The full list of config options is provided in [params.hpp](https://github.com/mesosphere/dcos-stats/blob/master/slave/params.hpp).

IMPORTANT: Again, any changes to these options don't take effect until the mesos-slave process is restarted using the following steps:

## Restarting `mesos-slave`

The `mesos-slave` process must be restarted whenever the module config changes for the changes to take effect.

To restart the mesos-slave process following a configuration change, perform the following steps:

1. Copy the current slave state into a backup location:
  - `$ cp -a /var/lib/mesos/slave /var/lib/mesos/slave.bak`
2. Restart the `mesos-slave` process:
  - `$ systemctl restart dcos-mesos-slave` (or `dcos-mesos-slave-public`)
3. Check `mesos-slave`'s status (any of these):
  - `$ systemctl status dcos-mesos-slave` (or `dcos-mesos-slave-public`)
  - `$ journalctl -f`
  - `$ journalctl -e -n 100`

## Verifying the module works (any of the following)

Check the mesos-slave logs for something like this near the beginning:

```input_assigner_factory.cpp:31 Creating new stats InputAssigner with parameters: [... json config content …]```

Check the mesos-slave logs for a message like this every minute (assuming a 'metrics' process hasn't been started yet, as described below):

```port_writer.cpp:180 Error when resolving host[metrics.marathon.mesos]. Dropping data and trying again in 60 seconds. err=asio.netdb:1, err2=system:22```

Start a process in marathon that just runs `env` and look for envvars named `STATSD_UDP_HOST` and `STATSD_UDP_PORT`, or run the `test-sender` process as described below.

## Launching a test sender

A sample program that just emits various arbitrary stats (eg 'currentto the endpoint advertised via `STATSD_UDP_HOST`/`STATSD_UDP_PORT` environment variables. The sample program's Go source code is included in the .tgz.

In Marathon:
- ID: `test-sender` (the precise name isn't important)
- Command: `./test-sender`
- Optional settings > URIs = `https://s3-us-west-2.amazonaws.com/nick-dev/metrics-msft/test-sender.tgz`
- Extra credit: start (or reconfigure) the sender task with >1 instances to test sending stats from multiple sources.

Or in JSON Mode:
```json
{
  "id": "test-sender",
  "cmd": "./test-sender",
  "cpus": 1,
  "mem": 128,
  "disk": 0,
  "instances": 1,
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-msft/test-sender.tgz"
  ]
}
```

## Launching a Kafka or Cassandra sender

After the module has been installed on all the slaves, the Kafka brokers and Cassandra nodes will need to be restarted in order to start using it. They will see the advertised metrics capability on startup and will automatically enable metrics export locally. In future DCOS versions, the module will be installed by default, so this restart won't be necessary.

### Kafka

As Kafka brokers start, they will automatically be configured for metrics export, and will show the following in `stdout`:

```
[2016-04-07 18:15:18,709] INFO Reporter is enabled and starting... (com.airbnb.metrics.StatsDReporter)
[2016-04-07 18:15:18,782] INFO Started Reporter with host=127.0.0.1, port=35542, polling_period_secs=10, prefix= (com.airbnb.metrics.StatsDReporter)
```

### Cassandra

As Cassandra nodes start, they will automatically be configured for metrics export, and will show the following in `cassandra-stdout.log`:

```
INFO  18:16:42 Trying to load metrics-reporter-config from file: metrics-reporter-config.yaml
INFO  18:16:42 Enabling StatsDReporter to 127.0.0.1:50030
```

## Launching a test receiver

This is just a shell script that runs `nc -ul 8125`. A minute or two after the job comes up, 'metrics.marathon.mesos' will be resolved by the mesos-slaves, at which point stats will start being printed to stdout.

In Marathon:
- ID: `metrics` (this name matters, it maps to `metrics.marathon.mesos` configured in `modules.json` on the slaves)
- Command: `./test-receiver`
- Optional settings > URIs = `https://s3-us-west-2.amazonaws.com/nick-dev/metrics-msft/test-receiver.tgz`
- Extra credit: start (or reconfigure) the 'metrics' receiver task with >1 instances. This will result in multiple DNS A records for 'metrics.marathon.mesos', and the mesos-slaves will automatically balance their load across them.

Or in JSON Mode:
```json
{
  "id": "metrics",
  "cmd": "./test-receiver",
  "cpus": 1,
  "mem": 128,
  "disk": 0,
  "instances": 1,
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-msft/test-receiver.tgz"
  ]
}
```

## Launching a sample graphite receiver

Runs a sample copy of Graphite in a Docker container. This is just a stock set of packages that someone put up on Dockerhub. Note that **using this receiver requires `annotation_mode` = `key_prefix`**. `tag_datadog` is NOT supported.

Defining the container config with all its ports via the web UI is very clunky, so lets just call the REST API.

Save the following config as `statsd-docker-marathon.json`, then push it to the Marathon API using `curl -X POST --header 'Content-Type: application/json' -d @statsd-docker-marathon.json http://$DCOS_URI/marathon/v2/apps`, or by using JSON Mode in more recent Marathon versions.

```json
{
  "id": "/metrics",
  "cmd": null,
  "cpus": 1,
  "mem": 512,
  "disk": 0,
  "instances": 1,
  "acceptedResourceRoles": [ "slave_public" ],
  "container": {
    "type": "DOCKER",
    "docker": {
      "image": "hopsoft/graphite-statsd",
      "network": "BRIDGE",
      "portMappings": [
        { "hostPort": 80,   "protocol": "tcp" },
        { "hostPort": 2003, "protocol": "tcp" },
        { "hostPort": 2004, "protocol": "tcp" },
        { "hostPort": 2023, "protocol": "tcp" },
        { "hostPort": 2024, "protocol": "tcp" },
        { "hostPort": 8125, "protocol": "udp" },
        { "hostPort": 8126, "protocol": "tcp" }
      ]
    }
  }
}
```

The image should deploy to a public slave instance (due to the `slave_public` resource role). Once it's up and running, you need to find the ip of the node it's running on, then connect to one of the following:
- Visit http://the-node-ip (port 80) to view Graphite
- `telnet` into port 8126 to view the statsd daemon's console (tip: type `help`)
Once the image has been up for a few minutes, it should start getting stats from `mesos-slaves` as `metrics.marathon.mesos` starts to resolve to it. In Graphite's left panel, navigate into `Metrics > stats > gauges > [fmwk_id] > [executor_id] > [container_id] > ...` to view the gauges produced by the application. Most applications seem to stick to gauge-type metrics, while the example `test-sender` produces several types.

## Uninstalling the module

1. Undo the changes made to `/opt/mesosphere/etc/mesos-slave-common` and `/opt/mesosphere/etc/mesos-slave-modules.json` by restoring the original files (you kept backups, right?).
2. The library files added to `/opt/mesosphere/lib/` should also be backed out.
3. Restart `mesos-slave` using the steps described before.
