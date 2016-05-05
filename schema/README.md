# Schemas

JSON-formatted [Avro schemas](http://avro.apache.org/docs/current/spec.html#schemas) which define the common format used for transferring metrics to/from the [Metrics Collector](../collector/) running on every DC/OS node.

**THIS SCHEMA IS NOT FINAL AND CROSS-VERSION COMPATIBILITY IS NOT GUARANTEED**

## Usage

The schema defines a `MetricsList` type, which is used for the following dataflows:

- Sending metrics to the Collector from on-agent processes, including `mesos-agent` itself, via the [Metrics Module](../module/).
- Sending metrics to the Kafka Metrics Service from the Collector.
- Sending metrics to on-agent Partner services from the Collector.

## Transport

### Over TCP

For each socket connection, the communication MUST begin with the sender sending the binary schema, followed by zero or more binary MetricsList records. A binary schema is valid for as long as the socket is open. The schema must be re-sent if the socket is reconnected.

The receiver doesn't have any explicit responses for accepting or refusing data from the sender. If the receiver encounters corrupt data (including lack of the required schema), it may close the connection.

### Over Kafka

Each Kafka message MUST begin with the binary schema, followed by zero or more binary MetricsList records. Schema overhead may be reduced by increasing the number of MetricsList records per Kafka message.

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
