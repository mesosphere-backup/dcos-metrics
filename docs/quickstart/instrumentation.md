# Instrumenting your code

DC/OS metrics listens for <statsd metrics> from every app running with the <Mesos containerizer>. We expose a statsd server for each container, which allows us to tag all metrics by origin. We expose them via the standard environment variables STATSD_UDP_HOST and STATSD_UDP_PORT. 

## Quickstart:

```
import os, statsd
c = statsd.StatsClient(os.getenv('STATSD_UDP_HOST'), os.getenv('STATSD_UDP_PORT'), prefix='example.app')
c.incr('foo.bar') # Will be ‘example.app.foo.bar' in statsd/graphite. 
```

## Limitiations:
Workloads running with the docker containerizer do not have a statsd server made available to them. Consider running a <statsd sidecar> if you need to get metrics from an application running with the docker containerizer.

Statsd has no concept of a histogram; all metrics are therefore reported as gauges.
