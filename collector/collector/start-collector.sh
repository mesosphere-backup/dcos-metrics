#!/bin/sh

if [ $# -gt 1 -o "$1" = "-h" -o "$1" = "--help" ]; then
  echo "Starts a collector instance on the local machine, for testing."
  echo "Syntax: $(basename $0) [-nobuild]"
  echo "Examples:"
  echo "KAFKA_BROKERS=127.0.0.1:9092 $0 -nobuild"
  echo "STATSD_UDP_HOST=127.0.0.1 STATSD_UDP_PORT=8125 KAFKA_BROKERS=127.0.0.1:9092 $0"
  exit 1
fi

cd $(dirname $0)
if [ "$1" != "-nobuild" ]; then
    go build || exit 1
fi
./collector
