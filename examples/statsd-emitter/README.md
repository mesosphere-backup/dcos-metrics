# statsd-emitter

A reference implementation of a containerized process which emits StatsD data to an advertised StatsD UDP endpoint.

* Looks for ```STATSD_UDP_HOST``` and ```STATSD_UDP_PORT``` in the environment, pointing to where metrics should be sent. These environment variables are automatically provided by Mesos on DC/OS EE clusters 1.7+.
* A ```-debug``` option enables additional logs to stdout.

## Prerequisites:

```
apt-get install golang-go
```

## Build:

```
$ go build
```

## Run locally:

```
$ ./statsd-emitter -h
$ STATSD_UDP_HOST="127.0.0.1" STATSD_UDP_PORT="8125" ./statsd-emitter -debug
```

## Run in Marathon:

See [DEMO.md](../../DEMO.md#launching-a-statsd-emitter) for usage.
