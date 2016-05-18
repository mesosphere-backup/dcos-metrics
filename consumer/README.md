# Metrics Consumer

A set of Kafka metrics consumer implementations which grab Avro-formatted metrics data from a Kafka cluster, then do something with the records it receives.

## Project contents

- **[common](common/)**: Shared code across consumer implementations. Handles everything except the output itself.
- **[graphite](graphite/)**: Outputs data to a Graphite service which is running at a provided `OUTPUT_HOST`/`OUTPUT_PORT`.
- **[kairos](kairos/)**: Outputs data to a KairosDB service which is running at a provided `OUTPUT_HOST`/`OUTPUT_PORT`.
- **[print](print/)**: Outputs consumed data to `stdout`.

## Usage

**TODO new instructions for each output type**

First, get a [Metrics Collector](../collector) producing data into a Kafka cluster, then launch one or more Consumers as Marathon tasks (JSON Mode):

```json
{
  "id": "metrics-consumer",
  "cmd": "env && JAVA_HOME=./jre* ./jre*/bin/java -jar *.jar",
  "cpus": 1,
  "mem": 512,
  "disk": 0,
  "instances": 1,
  "env": {
    "FRAMEWORK_NAME": "kafka",
    "TOPIC": "sample_metrics",
    "PRINT_RECORDS": "true",
    "CONSUMER_THREADS": "1",
    "POLL_TIMEOUT_MS": "1000",
    "STATS_PRINT_PERIOD_MS": "500",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-uber.jar",
    "https://s3.amazonaws.com/downloads.mesosphere.io/kafka/assets/jre-8u72-linux-x64.tar.gz"
  ]
}
```

The provided environment variables may be adjusted to fit your deployment. The Kafka configuration can also be adjusted using environment variables. Values of the form `KAFKA_OVERRIDE_SOME_VALUE` will automatically be forwarded to the Kafka consumer library as `some.value`.
