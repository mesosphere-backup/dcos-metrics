#!/bin/sh

if [ $# -eq 0 -o $# -gt 2 -o "$1" = "-h" -o "$1" = "--help" ]; then
  echo "Starts a consumer instance on the local machine, for testing."
  echo "Syntax: $(basename $0) <type> [-nobuild]"
  echo "Examples:"
  echo "KAFKA_OVERRIDE_GROUP_ID=test KAFKA_OVERRIDE_BOOTSTRAP_SERVERS=127.0.0.1:9092 $0 print -nobuild"
  echo "KAFKA_OVERRIDE_GROUP_ID=test KAFKA_OVERRIDE_BOOTSTRAP_SERVERS=127.0.0.1:9092 $0 graphite"
  exit 1
fi

cd $(dirname $0)
if [ "$2" != "-nobuild" ]; then
    ./gradlew shadowjar :metrics-consumer-$1:shadowjar
fi
java -jar ./metrics-consumer-$1/build/libs/metrics-consumer-$1-uber.jar
