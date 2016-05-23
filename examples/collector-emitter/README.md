# Collector Client

A reference implementation of a system process which emits Avro Metrics data to a Collector.

* Connects to ```127.0.0.1:64113``` and sends various arbitrary data to it.
* Automatically recovers if the connection is lost.

## Prerequisites:

```
apt-get install golang-go
```

## Build:

```
$ go generate # creates 'metrics-schema' package
$ go build
```

If you see errors about `cannot find package "github.com/.../metrics-schema"`, you forgot to perform `go generate`.

## Run locally or on a DC/OS node:

```
$ ./collector-emitter -h # view help
$ ./collector-emitter -log-record-output # print records in json form
```

## Get sent data

`nc -l 64113` can be run on the same system to view raw emitted data or save it to disk.

The data will follow the standard [Avro OCF format](http://avro.apache.org/docs/current/spec.html#Object+Container+Files) and can be parsed using using [avro-tools.jar](http://www.apache.org/dyn/closer.cgi/avro/avro-1.8.0/java/avro-tools-1.8.0.jar):

```
$ nc -l 64113 > data.avro
# <send some stats, then close collector-emitter process to close nc>
$ java -jar avro-tools-1.8.0.jar getschema data.avro
$ java -jar avro-tools-1.8.0.jar tojson --pretty data.avro
```
