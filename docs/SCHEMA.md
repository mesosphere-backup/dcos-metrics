# Metrics Schema
This document refers to the schema used when transferring metrics from the
[Mesos module](MESOS_MODULE.md) to the [metrics collector](COLLECTORS.md)
running on every DC/OS node. Currently, this project uses JSON-formatted
[Avro schemas](http://avro.apache.org/docs/current/spec.html#schemas) which are
defined in the [schema](../schema/) directory.

**NOTE:** this document does _not_ define the message format implemented
internally in the DC/OS metrics service, nor does it define the JSON-formatted
HTTP API.

## Usage
The schema defines a `MetricsList` type, which is used for the following
dataflows:
  - Sending metrics to the Collector from on-agent processes, including
  `mesos-agent` itself, via the [metrics module](MESOS_MODULE.md).

## Transport
[Avro's RPC standard](http://avro.apache.org/docs/current/spec.html#Protocol+Wire+Format)
appears to suffer a stark lack of adoption across Avro library implementations
(even the official Avro C/C++ libraries lack support), so we're foregoing it for
now in favor of something much more rudimentary.

### Over TCP
This is effectively just streaming an [OCF file](http://avro.apache.org/docs/current/spec.html#Object+Container+Files)
over a TCP session. The sender starts by sending the OCF header containing the
schema, followed by one or more `MetricsList` records inside OCF blocks for as
long as the socket remains open. If the connection is closed, the following
connection must repeat the header information as if starting a new file.

The receiver doesn't have any explicit responses for acknowledging or refusing
data from the sender, other than standard TCP ACKs. If the receiver encounters
corrupt data (including lack of the required header information), it may simply
close the connection in response, at which point the sender may reconnect and
resume sending.

## Code generation
Whenever the schema is changed, clients which use preprocessed code MUST be
updated to reflect the changes.

### C++
C++ code, such as the [Agent Module](../module), is generated using `avrogencpp`,
which may be built from
[avro-cpp.tar.gz](http://www.apache.org/dyn/closer.cgi/avro/avro-1.8.0/cpp/avro-cpp-1.8.0.tar.gz).

```bash
avrogencpp -i metrics.avsc -n avro -o metrics.hpp
cat metrics.hpp
```

### Go
Go code, such as the [collector](../collector) or the [examples](../examples/),
is generated at build time by running `make` in the root of this repo.

```bash
make
cat schema/metrics_schema/schema.go
```
