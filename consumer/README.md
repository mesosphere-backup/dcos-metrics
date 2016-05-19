# Metrics Consumer

A set of Kafka metrics consumer implementations which grab Avro-formatted metrics data from a Kafka cluster, then do something with the records it receives.

## Project contents

- **[common](metrics-consumer-common/)**: Shared code across consumer implementations. Handles everything except the output itself.
- **[graphite](metrics-consumer-graphite/)**: Outputs data to a Graphite service which is running at a provided `OUTPUT_HOST`/`OUTPUT_PORT`.
- **[kairos](metrics-consumer-kairos/)**: Outputs data to a KairosDB service which is running at a provided `OUTPUT_HOST`/`OUTPUT_PORT`.
- **[print](metrics-consumer-print/)**: Outputs consumed data to `stdout`.

## Build

All consumers:
```
$ ./gradlew shadowJar
$ find . -iname *-uber.jar
./metrics-consumer-graphite/build/libs/metrics-consumer-graphite-uber.jar
./metrics-consumer-print/build/libs/metrics-consumer-print-uber.jar
./metrics-consumer-kairos/build/libs/metrics-consumer-kairos-uber.jar
[...]
```

Specific consumer:
```
$ ./gradlew :metrics-consumer-graphite:shadowjar
$ find . -iname *-uber.jar
./metrics-consumer-graphite/build/libs/metrics-consumer-graphite-uber.jar
```

## Configure and Deploy

Before you launch the consumers, you should already have a Kafka cluster up and accepting metrics from one or more deployed [Metrics Collectors](../collector). Depending on the consumer, you will also need to have the destination metrics service up and running as well (not applicable to the Print Consumer).

The consumers are configured via environment variables, making it easy to make configuration changes in Marathon. Each consumer type has a mix of options which can be configured this way. The consumers are themselves stateless, making it easy to update their configuration by simply rolling out a change in Marathon.

### Common

Each consumer implementation shares the following settings from `metrics-consumer-common`:

- **FRAMEWORK_NAME**: The Kafka Framework to consume against. If a manual broker list is desired, it can be provided via `KAFKA_OVERRIDE_BOOTSTRAP_SERVERS`, in which case `FRAMEWORK_NAME` will be ignored.  Default: `kafka`
- **TOPIC**: The topic to be consumed from. Default: `sample_metrics` (TODO support wildcard subscription)
- **STATS_PRINT_PERIOD_MS**: How frequently to print statistics about the amount of records/bytes consumed to stdout. Default: `500`
- **POLL_TIMEOUT_MS**: The timeout value to use when calling Kafka's poll() function. Default: `1000`
- **CONSUMER_THREADS**: The number of consumer threads to run in parallel. Default: `1`

In addition to the above values, any configuration variables defined by the Kafka Consumer client library can be configured via the environment. Any values of the form `KAFKA_OVERRIDE_X_Y` will be given to the Kafka library in the form `x.y`. For example, the value `KAFKA_OVERRIDE_BOOTSTRAP_SERVERS=broker1:1234,broker2:2345` will be given to Kafka as `bootstrap.servers=broker1:1234,broker2:2345`. This allows full customization of the underlying Kafka consumer.

### Print Consumer

Prints consumed data to stdout with JSON formatting. Doesn't have any additional options beyond what's provided in Common above.

#### Deployment

Example Marathon app (JSON Mode):

```json
{
  "id": "metrics-consumer-print",
  "cmd": "env && JAVA_HOME=./jre* ./jre*/bin/java -jar *.jar",
  "cpus": 1,
  "mem": 512,
  "disk": 0,
  "instances": 1,
  "env": {
    "FRAMEWORK_NAME": "kafka",
    "TOPIC": "sample_metrics",
    "STATS_PRINT_PERIOD_MS": "500",
    "POLL_TIMEOUT_MS": "1000",
    "CONSUMER_THREADS": "1",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer-print"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-print-uber.jar",
    "https://s3.amazonaws.com/downloads.mesosphere.io/kafka/assets/jre-8u72-linux-x64.tar.gz"
  ]
}
```

### Graphite Consumer

Sends data to a Graphite server.

#### Options

- **OUTPUT_HOST**: The hostname or IP of the Graphite server. Required, no default.
- **OUTPUT_PORT**: The port of the Graphite server. Default: `2003`
- **GRAPHITE_PROTOCOL**: The connection type to use. May be `TCP`, `UDP`, `PICKLE`, or `RABBITMQ`. `RABBITMQ` requires also providing `RABBIT_USERNAME`, `RABBIT_PASSWORD`, and `RABBIT_EXCHANGE`. Default: `TCP`
- **EXIT_ON_CONNECT_FAILURE**: Whether to exit the consumer process if it fails to connect to `OUTPUT_HOST`/`OUTPUT_PORT`. Default: `true`
- **GRAPHITE_PREFIX**: A prefix string to be prepended on metric names. For example, `GRAPHITE_PREFIX=foo` will result in `some.value=5` being passed to Graphite as `foo.some.value=5`. Default: `""`
- **GRAPHITE_PREFIX_IDS**: Boolean flag which specifies whether metric names should be prefixed with the framework id, executor id, and container id tag values. For example, this will result in `some.value=5` being passed to Graphite as `<framework_id>.<executor_id>.<container_id>.some.value=5`. This may be combined with `GRAPHITE_PREFIX` in which case the `GRAPHITE_PREFIX` value will come first. Default: `true`

#### Deployment

To get a Graphite instance up and running in DC/OS, we recommend using the Marathon template defined in [DEMO.md](../DEMO.md), which launches a self-contained Docker image.

Example Marathon app (JSON Mode). Before deployment, `OUTPUT_HOST` **must** be manually configured to match the location of your Graphite deployment. Graphite is accepting data over UDP, so you likely won't know that data's being dropped if you configured this incorrectly, except that you won't see the metrics you were expecting in Graphite itself.

```json
{
  "id": "metrics-consumer-graphite",
  "cmd": "env && JAVA_HOME=./jre* ./jre*/bin/java -jar *.jar",
  "cpus": 1,
  "mem": 512,
  "disk": 0,
  "instances": 1,
  "env": {
    "OUTPUT_HOST": "",
    "OUTPUT_PORT": "2004",
    "GRAPHITE_PROTOCOL": "PICKLE",
    "GRAPHITE_PREFIX": "dcos",
    "FRAMEWORK_NAME": "kafka",
    "TOPIC": "sample_metrics",
    "STATS_PRINT_PERIOD_MS": "500",
    "POLL_TIMEOUT_MS": "1000",
    "CONSUMER_THREADS": "1",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer-graphite"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-graphite-uber.jar",
    "https://s3.amazonaws.com/downloads.mesosphere.io/kafka/assets/jre-8u72-linux-x64.tar.gz"
  ]
}
```

### KairosDB Consumer

Sends data to a KairosDB server, using the REST API.

#### Options

- **OUTPUT_HOST**: The hostname or IP of the Graphite server. Required, no default.
- **OUTPUT_PORT**: The port of the Graphite server. Required, no default.
- **EXIT_ON_CONNECT_FAILURE**: Whether to exit the consumer process if it fails to connect to `OUTPUT_HOST`/`OUTPUT_PORT`. Default: `true`

#### Deployment

To get a KairosDB instance up and running in DC/OS, we recommend following the [Cassandra KairosDB Tutorial](https://github.com/mesosphere/cassandra-kairosdb-tutorial). Just get to the point where you've added the KairosDB data source to the Grafana, then return here.

Example Marathon app (JSON Mode). Before deployment, both `OUTPUT_HOST` and `OUTPUT_PORT` **must** be manually configured to match the location of Graphite:

```json
{
  "id": "metrics-consumer-kairos",
  "cmd": "env && JAVA_HOME=./jre* ./jre*/bin/java -jar *.jar",
  "cpus": 1,
  "mem": 512,
  "disk": 0,
  "instances": 1,
  "env": {
    "OUTPUT_HOST": "",
    "OUTPUT_PORT": "",
    "FRAMEWORK_NAME": "kafka",
    "TOPIC": "sample_metrics",
    "STATS_PRINT_PERIOD_MS": "500",
    "POLL_TIMEOUT_MS": "1000",
    "CONSUMER_THREADS": "1",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer-kairos"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-kairos-uber.jar",
    "https://s3.amazonaws.com/downloads.mesosphere.io/kafka/assets/jre-8u72-linux-x64.tar.gz"
  ]
}
```
