#!/bin/bash

gunzip sample-*.txt.gz

trap "echo exiting" SIGINT
AGENT_TEST_STATE_FILE=sample-state-response.txt \
AGENT_TEST_SYSTEM_FILE=sample-metrics-snapshot-response.txt \
AGENT_TEST_CONTAINERS_FILE=sample-containers-response.txt \
KAFKA=false \
RECORD_OUTPUT_LOG=true \
KAFKA_PRODUCE_COUNT=1 \
./start-collector.sh

gzip sample-*.txt
