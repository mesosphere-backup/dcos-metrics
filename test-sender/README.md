# test-sender
Small executable which emits metrics data, which may be used to test a metrics forwarding stack.

## Notes:

* Requires STATSD_UDP_HOST and STATSD_UDP_PORT in the environment, pointing to where stats should be sent. In Mesos, these should be provided to the container when the Stats slave module is installed.
* A "-debug" option enables additional logs to stdout.

## Build instructions:

```
host:dcos-stats/test-sender$ go build test-sender.go
host:dcos-stats/test-sender$ ./test-sender -h
host:dcos-stats/test-sender$ STATSD_UDP_HOST="127.0.0.1" STATSD_UDP_PORT="8125" ./test-sender -debug
```

