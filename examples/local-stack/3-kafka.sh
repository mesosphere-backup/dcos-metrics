KAFKA_VER="0.9.0.1"
PKG_URL="https://www.apache.org/dyn/closer.cgi?path=/kafka/${KAFKA_VER}/kafka_2.11-${KAFKA_VER}.tgz"

START_SCRIPT="$(ls ./kafka_*-$KAFKA_VER/bin/kafka-server-start.sh)"
if [ $? -ne 0 ]; then
  echo "Download/unpack kafka into this directory: $PKG_URL"
  exit 1
fi

rm -rf /tmp/kafka-logs/
$START_SCRIPT ./kafka_*/config/server.properties
