# test-sender
A small executable which emits metrics data, which may be used to test a metrics forwarding stack.

## Notes:

* Requires ```STATSD_UDP_HOST``` and ```STATSD_UDP_PORT``` in the environment, pointing to where stats should be sent. These environment variables are automatically provided by Mesos on DC/OS EE clusters 1.7+.
* A "```-debug```" option enables additional logs to stdout.

## Build/run instructions:

```
apt-get install golang-go
```

## Build:

```
dcos-stats/test-sender$ go build test-sender.go
```

## Run locally:

```
dcos-stats/test-sender$ ./test-sender -h
dcos-stats/test-sender$ STATSD_UDP_HOST="127.0.0.1" STATSD_UDP_PORT="8125" ./test-sender -debug
```

## Run in Marathon (with Stats module installed in Mesos):

```
/path/to/test-sender
```
