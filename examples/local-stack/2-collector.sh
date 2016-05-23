#!/bin/sh

cd ../../collector/collector
go build
if [ $? -ne 0 ]; then
  exit 1
fi
LOG_RECORD_INPUT=true STATSD_PERIOD=3 STATSD_UDP_HOST=127.0.0.1 STATSD_UDP_PORT=8125 KAFKA_BROKERS=127.0.0.1:9092 ./start-collector.sh
