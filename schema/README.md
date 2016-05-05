# Schemas

JSON-formatted [Avro schemas](http://avro.apache.org/docs/current/spec.html#schemas) which define the common format used for transferring metrics to/from the [Metrics Collector](../collector/) running on every DC/OS node.

**THIS SCHEMA IS NOT FINAL AND CROSS-VERSION COMPATIBILITY IS NOT GUARANTEED**

The schemas define the `MetricsList` message used for the following dataflows:
- Sending metrics to the Collector from on-agent processes, including `mesos-agent` itself, via the [Metrics Module](../module/).
- Sending metrics to the Kafka Metrics Service from the Collector.
- Sending metrics to on-agent Partner services from the Collector.

## Usage/Validation

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
