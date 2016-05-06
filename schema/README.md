# Schemas

JSON-formatted [Avro schemas](http://avro.apache.org/docs/current/spec.html#schemas) which define the common format used for transferring metrics to/from the [Metrics Collector](../collector/) running on every DC/OS node.

**THIS SCHEMA IS NOT FINAL AND CROSS-VERSION COMPATIBILITY IS NOT GUARANTEED**

## Usage

The schema defines a `MetricsList` type, which is used for the following dataflows:

- Sending metrics to the Collector from on-agent processes, including `mesos-agent` itself, via the [Metrics Module](../module/).
- Sending metrics to the Kafka Metrics Service from the Collector.
- Sending metrics to on-agent Partner services from the Collector.

## Transport

[Avro's RPC standard](http://avro.apache.org/docs/current/spec.html#Protocol+Wire+Format) appears to suffer a stark lack of adoption across Avro library implementations (even the official Avro C/C++ libraries lack support), so we're foregoing it for now in favor of something much more rudamentary.

### Over TCP

This is effectively just streaming an [OCF file](http://avro.apache.org/docs/current/spec.html#Object+Container+Files) over TCP. The sender starts by sending the Schema and Codec, followed by zero or more `MetricsList` records for as long as the socket remains open. If the connection is closed, the following connection must repeat the header information as if starting a new file.

The receiver doesn't have any explicit responses for acknowledging or refusing data from the sender. If the receiver encounters corrupt data (including lack of the required header information), it may simply close the connection in response.

### Over Kafka

As with each TCP session, each Kafka message is effectively treated as a self-contained [OCF file](http://avro.apache.org/docs/current/spec.html#Object+Container+Files), where each message starts with a copy of the header/schema, followed by zero or more `MetricsList` entries. This enables Kafka Consumers to quickly start successfully reading data, without needing to cross-reference with a schema retrieved elsewhere (which in turn has its own complexities). Schema overhead may be reduced by increasing the number of `MetricsList` records per Kafka message.

This format is intentionally starting off as simple as possible, but may be revisited later if it turns out to be unsustainable.

## Demo/Validation

These examples use [avro-tools-1.7.7.jar](http://www.apache.org/dyn/closer.cgi/avro/avro-1.7.7/java/avro-tools-1.7.7.jar).

Generate Java code from the schema:

```
java -jar avro-tools-1.7.7.jar compile schema metrics.avsc .
find dcos/
```

Convert sample metrics JSON to a binary file, then view info about the file:

```
java -jar avro-tools-1.7.7.jar fromjson --schema-file metrics.avsc --codec deflate sample.json > sample.avro
java -jar avro-tools-1.7.7.jar getschema sample.avro
java -jar avro-tools-1.7.7.jar tojson --pretty sample.avro
```

Generate records containing random data:

```
java -jar avro-tools-1.7.7.jar random --schema-file metrics.avsc --codec deflate --count 10 random.avro
java -jar avro-tools-1.7.7.jar getschema random.avro
java -jar avro-tools-1.7.7.jar tojson --pretty random.avro
```
