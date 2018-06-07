#!/bin/bash

set -o errexit -o nounset -o pipefail

MODULE_CONFIG=$(cat << EOF
{
  "libraries": [
  {
    "file": "/usr/lib/mesos/libmetrics-module.so",
    "modules": [{
        "name": "com_mesosphere_MetricsIsolatorModule",
        "parameters": [
            {"key": "container_limit_amount_kbytes", "value": "10240"},
            {"key": "container_limit_period_secs", "value": "60"},
            {"key": "listen_interface", "value": "lo"},
            {"key": "listen_port_mode", "value": "ephemeral"},
            {"key": "output_collector_enabled", "value": "true"},
            {"key": "output_collector_ip", "value": "127.0.0.1"},
            {"key": "output_collector_port", "value": "8124"},
            {"key": "output_collector_chunking", "value": "true"},
            {"key": "output_collector_chunk_size_datapoints", "value": "100"},
            {"key": "output_collector_chunk_timeout_seconds", "value": "10"},
            {"key": "state_path_dir", "value": "/run/mesos/isolators/com_mesosphere_MetricsIsolatorModule/"}
        ]
    }]
  }]
}
EOF
)

if [ "$(docker ps -a -q -f 'name=mesos-zookeeper')" != "" ]; then
	docker rm -f mesos-zookeeper
fi
docker run -d --net=host --name=mesos-zookeeper \
  netflixoss/exhibitor:1.5.2

if [ "$(docker ps -a -q -f 'name=mesos-master')" != "" ]; then
	docker rm -f mesos-master
fi
docker run -d --net=host --name=mesos-master \
  -e LIBPROCESS_IP=127.0.0.1 \
  -e MESOS_PORT=5050 \
  -e MESOS_ZK=zk://127.0.0.1:2181/mesos \
  -e MESOS_QUORUM=1 \
  -e MESOS_REGISTRY=in_memory \
  -e MESOS_LOG_DIR=/var/log/mesos \
  -e MESOS_WORK_DIR=/var/tmp/mesos \
  -v "$(pwd)/mesos_module/build/log/mesos:/var/log/mesos" \
  -v "$(pwd)/mesos_module/build/tmp/mesos:/var/tmp/mesos" \
  mesosphere/mesos-master:1.5.0

if [ "$(docker ps -a -q -f 'name=mesos-agent')" != "" ]; then
	docker rm -f mesos-agent
fi
docker run -d --net=host --privileged --name=mesos-agent \
  -e LD_LIBRARY_PATH="/usr/lib/mesos/avro-cpp-1.8.0:/usr/lib/mesos/boost_1_53_0" \
  -e LIBPROCESS_IP=127.0.0.1 \
  -e MESOS_PORT=5051 \
  -e MESOS_MASTER=zk://127.0.0.1:2181/mesos \
  -e MESOS_SWITCH_USER=0 \
  -e MESOS_SYSTEMD_ENABLE_SUPPORT=false \
  -e MESOS_CONTAINERIZERS=docker,mesos \
  -e MESOS_IMAGE_PROVIDERS=docker \
  -e MESOS_ISOLATION="docker/runtime,filesystem/linux,com_mesosphere_MetricsIsolatorModule" \
  -e MESOS_LOG_DIR=/var/log/mesos \
  -e MESOS_WORK_DIR=/var/tmp/mesos \
  -e MESOS_MODULES="${MODULE_CONFIG}" \
  -v "$(pwd)/mesos_module/build/avro-cpp-1.8.0/build/:/usr/lib/mesos/avro-cpp-1.8.0" \
  -v "$(pwd)/mesos_module/build/boost_1_53_0/stage/lib/:/usr/lib/mesos/boost_1_53_0" \
  -v "$(pwd)/mesos_module/build/libmetrics-module.so:/usr/lib/mesos/libmetrics-module.so" \
  -v "$(pwd)/mesos_module/build/log/mesos:/var/log/mesos" \
  -v "$(pwd)/mesos_module/build/tmp/mesos:/var/tmp/mesos" \
  -v /var/run/docker.sock:/var/run/docker.sock \
  -v /cgroup:/cgroup \
  -v /sys:/sys \
  -v /usr/local/bin/docker:/usr/local/bin/docker \
  mesosphere/mesos-agent:1.5.0
