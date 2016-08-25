# Metrics Consumer

A set of Kafka metrics consumer implementations which grab Avro-formatted metrics data from a Kafka cluster, then do something with the records it receives.

## Project contents

- **[common](metrics-consumer-common/)**: Shared code across the following implementations. Handles everything except the output itself.
- **[graphite](metrics-consumer-graphite/)**: Outputs data to a Graphite service.
- **[influx](metrics-consumer-influx/)**: Outputs data to an InfluxDB service.
- **[kairos](metrics-consumer-kairos/)**: Outputs data to a KairosDB service.
- **[print](metrics-consumer-print/)**: Outputs consumed data to `stdout`.
- **[statsd](metrics-consumer-statsd/)**: Outputs consumed data to a statsd or dogstatsd service.

## Build

All consumers:
```
$ ./gradlew shadowJar
$ find . -iname *-uber.jar
./metrics-consumer-graphite/build/libs/metrics-consumer-graphite-uber.jar
./metrics-consumer-influx/build/libs/metrics-consumer-influx-uber.jar
./metrics-consumer-kairos/build/libs/metrics-consumer-kairos-uber.jar
./metrics-consumer-print/build/libs/metrics-consumer-print-uber.jar
./metrics-consumer-print/build/libs/metrics-consumer-statsd-uber.jar
[...]
```

Specific consumer:
```
$ ./gradlew :metrics-consumer-kairos:shadowjar
$ find . -iname *-uber.jar
./metrics-consumer-kairos/build/libs/metrics-consumer-kairos-uber.jar
```

## Configure and Deploy

Before you launch the consumers, you should already have a Kafka cluster up and accepting metrics from one or more deployed [Metrics Collectors](../collector). Depending on the consumer, you will also need to have the destination metrics service up and running as well (not applicable to the Print Consumer).

The consumers are configured via environment variables, making it easy to make configuration changes in Marathon. Each consumer type has a mix of options which can be configured this way. The consumers are themselves stateless, making it easy to update their configuration by simply rolling out a change in Marathon.

### Common

Each consumer implementation shares the following settings from `metrics-consumer-common`:

- **KAFKA_FRAMEWORK_NAME**: The Kafka Framework to consume against. If a manual list of brokers is desired, it can be provided via `KAFKA_OVERRIDE_BOOTSTRAP_SERVERS`, in which case `KAFKA_FRAMEWORK_NAME` will be ignored.  Default: `kafka`
- **KAFKA_TOPIC_PATTERN**: A regular expression for topics to be subscribed to, ignored if `KAFKA_TOPICS` is set. Default: `metrics-.*`
- **KAFKA_TOPIC_POLL_PERIOD_MS**: How frequently to refresh the available list of topics and subscribe to topics which match `KAFKA_TOPIC_PATTERN`. Default: `60000` (60s)
- **KAFKA_TOPICS**: A comma-separated list of specific topics to consume from. If this setting is populated, it disables `KAFKA_TOPIC_PATTERN` and `KAFKA_TOPIC_POLL_PERIOD_MS`. Default: `<unset>`
- **FRAMEWORK_NAMES**: A comma-separated whitelist of framework names whose metrics should be forwarded. This filtering is in addition to the topic selection provided via the `KAFKA_TOPIC` settings, which effectively provides filtering by framework ID (not framework name). If this setting is populated, only matching data will be forwarded from the selected topics. An entry named `null` is interpreted as matching values which lacks have a framework name, such as system-level data which isn't tied to a container. Default: `<unset>`
- **STATS_PRINT_PERIOD_MS**: How frequently to print statistics to stdout about the amount of records/bytes consumed. Default: `5000` (5s)
- **PRINT_RECORDS**: Enables printing the filtered records as they're received. Default: `false`
- **POLL_TIMEOUT_MS**: The timeout value to use when calling Kafka's poll() function. Default: `1000` (1s)
- **CONSUMER_THREADS**: The number of consumer threads to run in parallel. Default: `1`

In addition to the above values, any configuration variables defined by the Kafka Consumer client library can be configured via the environment. Any values of the form `KAFKA_OVERRIDE_X_Y` will be given to the Kafka library in the form `x.y`. For example, the value `KAFKA_OVERRIDE_BOOTSTRAP_SERVERS=broker1:1234,broker2:2345` will be given to Kafka as `bootstrap.servers=broker1:1234,broker2:2345`. This allows full customization of the underlying Kafka consumer.

### Graphite Consumer

Sends data to a Graphite server.

#### Options

- **OUTPUT_HOST**: The hostname or IP of the Graphite server. **Required, no default**.
- **OUTPUT_PORT**: The port of the Graphite server. Default: `2003`
- **GRAPHITE_PROTOCOL**: The connection type to use. May be `TCP`, `UDP`, `PICKLE`, or `RABBITMQ`. `RABBITMQ` requires also providing `RABBIT_USERNAME`, `RABBIT_PASSWORD`, and `RABBIT_EXCHANGE`. Default: `TCP`
- **EXIT_ON_CONNECT_FAILURE**: Whether to exit the consumer process if it fails to connect to `OUTPUT_HOST`/`OUTPUT_PORT`. Default: `true`
- **GRAPHITE_PREFIX**: A prefix string to be prepended on metric names. For example, `GRAPHITE_PREFIX=foo` will result in `some.value=5` being passed to Graphite as `foo.some.value=5`. Default: `""`
- **GRAPHITE_PREFIX_IDS**: Boolean flag which specifies whether metric names should be prefixed with the framework id, executor id, and container id tag values. For example, this will result in `some.value=5` being passed to Graphite as `<framework_id>.<executor_id>.<container_id>.some.value=5`. This may be combined with `GRAPHITE_PREFIX` in which case the `GRAPHITE_PREFIX` value will come first. Default: `true`

#### Deployment

To get a test Graphite instance up and running in DC/OS, we recommend using the Marathon template defined in [DEMO.md](../DEMO.md), which launches a self-contained Docker image. **WARNING:** This image will quickly consume all disk if left unchecked.

Example Marathon app (JSON Mode). Before deployment, `OUTPUT_HOST` **must** be manually configured to match the location of your Graphite deployment. Graphite is accepting data over UDP, so you likely won't know that data's being dropped if you configured this incorrectly, except that you won't see the metrics you were expecting in Graphite itself.

```json
{
  "id": "metrics-consumer-graphite",
  "cmd": "env && JAVA_HOME=./jre* ./jre*/bin/java -jar *.jar",
  "cpus": 1,
  "mem": 1024,
  "disk": 0,
  "instances": 1,
  "env": {
    "OUTPUT_HOST": "",
    "OUTPUT_PORT": "2004",
    "GRAPHITE_PROTOCOL": "PICKLE",
    "GRAPHITE_PREFIX": "dcos",
    "KAFKA_FRAMEWORK_NAME": "kafka",
    "KAFKA_TOPIC_PATTERN": "metrics-.*",
    "STATS_PRINT_PERIOD_MS": "5000",
    "POLL_TIMEOUT_MS": "1000",
    "CONSUMER_THREADS": "1",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer-graphite"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-graphite-uber.jar",
    "https://downloads.mesosphere.com/kafka/assets/jre-8u91-linux-x64.tar.gz"
  ]
}
```

### InfluxDB Consumer

Sends data to an InfluxDB server, which [must be version 0.9 or higher](https://github.com/influxdata/influxdb-java).

#### Options

- **OUTPUT_HOST**: The hostname or IP of the server. **Required, no default**.
- **OUTPUT_PORT**: The port of the server. Default: `8086`
- **OUTPUT_USERNAME**: Username for the server. **Required, no default**.
- **OUTPUT_PASSWORD**: Password for the server. **Required, no default**.
- **OUTPUT_DATABASE**: Database to use on the server. **Required, no default**.
- **MEASUREMENT_NAME**: Measurement name to use for inserted data. **Required, no default**.
- **BATCH_POINTS**: Number of Points to accumulate before flushing to the database. Default: `2000`
- **BATCH_MS**: Maximum duration to wait before flushing to the database, in milliseconds. Default: `100`
- **CREATE_DB**: Whether to attempt to create `OUTPUT_DATABASE` before writing data. Doesn't seem to cause problems if it's already created. Default: `true`
- **LOG_LEVEL**: The verbosity to use for communication with the server. May be one of `NONE`, `BASIC`, `HEADERS`, or `FULL`. Default: `BASIC`
- **RETENTION_POLICY**: The name of the custom retention policy to use for inserted data. Default: `default`

#### Deployment

To get a test InfluxDB instance up and running in DC/OS, we recommend checking the [Time Series Demo tutorial](https://github.com/mesosphere/time-series-demo/tree/master/online). Note that you'd want something more durable for a real system.

Example Marathon app (JSON Mode). Before deployment, `OUTPUT_HOST`, `OUTPUT_USERNAME`, `OUTPUT_PASSWORD`, `OUTPUT_DATABASE`, and `MEASUREMENT_NAME` **must** be correctly configured to match the desired configuration for InfluxDB:

```json
{
  "id": "metrics-consumer-influx",
  "cmd": "env && JAVA_HOME=./jre* ./jre*/bin/java -jar *.jar",
  "cpus": 1,
  "mem": 1024,
  "disk": 0,
  "instances": 1,
  "env": {
    "OUTPUT_HOST": "",
    "OUTPUT_PORT": "8086",
    "OUTPUT_USERNAME": "admin",
    "OUTPUT_PASSWORD": "admin",
    "OUTPUT_DATABASE": "sample_db",
    "MEASUREMENT_NAME": "sample_measurement",
    "KAFKA_FRAMEWORK_NAME": "kafka",
    "KAFKA_TOPIC_PATTERN": "metrics-.*",
    "STATS_PRINT_PERIOD_MS": "5000",
    "POLL_TIMEOUT_MS": "1000",
    "CONSUMER_THREADS": "1",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer-influx"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-influx-uber.jar",
    "https://downloads.mesosphere.com/kafka/assets/jre-8u91-linux-x64.tar.gz"
  ]
}
```

### KairosDB Consumer

Sends data to a KairosDB server, using the REST API.

#### Options

- **OUTPUT_HOST**: The hostname or IP of the server. **Required, no default**.
- **OUTPUT_PORT**: The port of the server. **Required, no default**.
- **EXIT_ON_CONNECT_FAILURE**: Whether to exit the consumer process if it fails to connect to `OUTPUT_HOST`/`OUTPUT_PORT`. Default: `true`
- **FLUSH_THRESHOLD**: Rough limit on the batch size when sending metrics to KairosDB. Default: `1000`

#### Deployment

To get a test KairosDB instance up and running in DC/OS, we recommend following the [Cassandra KairosDB Tutorial](https://github.com/mesosphere/cassandra-kairosdb-tutorial). Just get to the point where you've added the KairosDB data source to the Grafana, then return here.

Example Marathon app (JSON Mode). Before deployment, both `OUTPUT_HOST` and `OUTPUT_PORT` **must** be manually configured to match the location of KairosDB:

```json
{
  "id": "metrics-consumer-kairos",
  "cmd": "env && JAVA_HOME=./jre* ./jre*/bin/java -jar *.jar",
  "cpus": 1,
  "mem": 1024,
  "disk": 0,
  "instances": 1,
  "env": {
    "OUTPUT_HOST": "",
    "OUTPUT_PORT": "",
    "KAFKA_FRAMEWORK_NAME": "kafka",
    "KAFKA_TOPIC_PATTERN": "metrics-.*",
    "STATS_PRINT_PERIOD_MS": "5000",
    "POLL_TIMEOUT_MS": "1000",
    "CONSUMER_THREADS": "1",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer-kairos"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-kairos-uber.jar",
    "https://downloads.mesosphere.com/kafka/assets/jre-8u91-linux-x64.tar.gz"
  ]
}
```

### Print Consumer

Prints consumed data to stdout with JSON formatting. Doesn't have any additional options beyond what's listed in Common above.

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
    "KAFKA_FRAMEWORK_NAME": "kafka",
    "KAFKA_TOPIC_PATTERN": "metrics-.*",
    "STATS_PRINT_PERIOD_MS": "5000",
    "POLL_TIMEOUT_MS": "1000",
    "CONSUMER_THREADS": "1",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer-print"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-print-uber.jar",
    "https://downloads.mesosphere.com/kafka/assets/jre-8u91-linux-x64.tar.gz"
  ]
}
```

### (Dog)StatsD Consumer

Sends data to a StatsD or DogstatsD server in StatsD format over UDP.

#### Options

- **OUTPUT_HOST**: The hostname or IP of the server. **Required, no default**.
- **OUTPUT_PORT**: The port of the server. Default: `8125`
- **KEY_PREFIX**: String to include as a prefix on all outputted metrics. Default: `""`
- **ENABLE_TAGS**: Outputs metrics with [Datadog-style tags](http://docs.datadoghq.com/guides/dogstatsd/#datagram-format). This may need to be `false` depending on the receiving service. Default: `true`

#### Deployment

Install the `datadog` package in Universe. You will need to manually go into `Advanced Installation` and provide a value for the `api_key`. Once this is done, it's ready to launch.

Example Marathon app (JSON Mode). The following has been populated with defaults which should align with the default `datadog` package settings:

```json
{
  "id": "metrics-consumer-statsd",
  "cmd": "env && JAVA_HOME=./jre* ./jre*/bin/java -jar *.jar",
  "cpus": 1,
  "mem": 1024,
  "disk": 0,
  "instances": 1,
  "env": {
    "OUTPUT_HOST": "datadog-agent.marathon.mesos",
    "OUTPUT_PORT": "8125",
    "KEY_PREFIX": "",
    "ENABLE_TAGS": "true",
    "KAFKA_FRAMEWORK_NAME": "kafka",
    "KAFKA_TOPIC_PATTERN": "metrics-.*",
    "STATS_PRINT_PERIOD_MS": "5000",
    "POLL_TIMEOUT_MS": "1000",
    "CONSUMER_THREADS": "1",
    "KAFKA_OVERRIDE_GROUP_ID": "metrics-consumer-statsd"
  },
  "uris": [
    "https://s3-us-west-2.amazonaws.com/nick-dev/metrics-consumer-statsd-uber.jar",
    "https://downloads.mesosphere.com/kafka/assets/jre-8u91-linux-x64.tar.gz"
  ]
}
```
