#!/bin/sh

cd ../../consumer
KAFKA_OVERRIDE_GROUP_ID=test KAFKA_OVERRIDE_BOOTSTRAP_SERVERS=127.0.0.1:9092 ./start-consumer.sh print

