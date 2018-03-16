# Instrumenting your code

DC/OS metrics listens for [statsd metrics][statsd-spec] from every app running with the
[Mesos containerizer][mesos-ucr]. We expose a statsd server for each container, which allows us to tag all metrics by
origin. We expose them via the standard environment variables `STATSD_UDP_HOST` and `STATSD_UDP_PORT`. 

## Quickstart:

```
import os, statsd
c = statsd.StatsClient(os.getenv('STATSD_UDP_HOST'), os.getenv('STATSD_UDP_PORT'), prefix='example.app')
c.incr('foo.bar') # Will be ‘example.app.foo.bar' in statsd/graphite. 
```

## Under the hood

The mesos instance on every agent in DC/OS has several modules plugged into it to adapt the way it works. One of those
is a dedicated [isolator module][mesos-isolator] for metrics. When a container is started, the module reserves a port 
for statsd metrics, starts a statsd server process, and injects two variables into the container environment with the
host and port for that server. Metrics received by that server are tagged with the container ID and sent through to the
dcos-metrics collector. 

## Limitations

Workloads running with the docker containerizer do not have a statsd server made available to them. Consider running a
statsd sidecar if you need to get metrics from an application running with the docker containerizer.

Statsd has no concept of a histogram; all metrics are therefore reported as gauges.

[statsd-spec]: https://github.com/b/statsd_spec
[mesos-ucr]: http://mesos.apache.org/documentation/latest/container-image/
[mesos-isolator]: http://mesos.apache.org/documentation/latest/modules/#isolator
