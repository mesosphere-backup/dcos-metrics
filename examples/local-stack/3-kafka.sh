KAFKA_VER="0.9.0.1"
PKG_URL="https://www.apache.org/dyn/closer.cgi?path=/kafka/${KAFKA_VER}/kafka_2.11-${KAFKA_VER}.tgz"

KAFKA_DIR="$(ls -d ./kafka_2.*-${KAFKA_VER})"
if [ $? -ne 0 ]; then
  echo "Download/unpack kafka into this directory: $PKG_URL"
  exit 1
fi

# download statsd support libs, if needed
JAR_URL_DIR="https://s3-us-west-2.amazonaws.com/infinity-artifacts/kafka/container-hook"
JAR_LIB_DIR="$KAFKA_DIR/libs"

STATSD_CLIENT_JAR="java-dogstatsd-client-2.0.13.jar"
if [ ! -f "$JAR_LIB_DIR/$STATSD_CLIENT_JAR" ]; then
    curl -O $JAR_URL_DIR/$STATSD_CLIENT_JAR || exit 1
    mv -v $STATSD_CLIENT_JAR $JAR_LIB_DIR || exit 1
fi
STATSD_METRICS_JAR="kafka-statsd-metrics2-0.4.1.jar"
if [ ! -f "$JAR_LIB_DIR/$STATSD_METRICS_JAR" ]; then
    curl -O $JAR_URL_DIR/$STATSD_METRICS_JAR || exit 1
    mv -v $STATSD_METRICS_JAR $JAR_LIB_DIR || exit 1
fi

CONFIG_FILE=$KAFKA_DIR/config/server.properties
# put zookeeper stuff under a /kafka-local-stack path:
sed -i 's,localhost:2181$,localhost:2181/kafka-local-stack,g' $CONFIG_FILE

# use a tag to tell if we've already added to server.properties
ADDED_TAG="Automatically added by local-stack script"
grep "$ADDED_TAG" $CONFIG_FILE > /dev/null
if [ $? -ne 0 ]; then
    echo "Adding statsd metrics settings to $CONFIG_FILE"
    cat <<EOF >> $CONFIG_FILE

# $ADDED_TAG
kafka.metrics.reporters=com.airbnb.kafka.KafkaStatsdMetricsReporter
external.kafka.statsd.reporter.enabled=true
external.kafka.statsd.host=127.0.0.1
external.kafka.statsd.port=8125
external.kafka.statsd.tag.enabled=true
external.kafka.statsd.metrics.exclude_regex=""
EOF
else
    echo "Kafka config already has statsd metrics settings: $CONFIG_FILE"
fi

rm -rf /tmp/kafka-logs/
$KAFKA_DIR/bin/kafka-server-start.sh ./kafka_*/config/server.properties
