#!/bin/sh

sudo su

cd /opt/mesosphere/lib
rm -f libmetrics-module.so
curl -O https://s3-us-west-2.amazonaws.com/nick-dev/libmetrics-module.so
chmod +x libmetrics-module.so
curl -O https://s3-us-west-2.amazonaws.com/nick-dev/libs.tgz
tar xzvf libs.tgz
rm libs.tgz

cd /opt/mesosphere/etc
sed -i 's/StatsIsolator/MetricsIsolator/g' mesos-slave-common
sed -i 's/MESOS_HOOKS=.*/MESOS_RESOURCE_ESTIMATOR=com_mesosphere_MetricsResourceEstimatorModule/g' mesos-slave-common

sed -i 's/libstats-slave/libmetrics-module/g' mesos-slave-modules.json
sed -i 's/StatsEnvHook/MetricsResourceEstimatorModule/g' mesos-slave-modules.json
sed -i 's/StatsIsolatorModule/MetricsIsolatorModule/g' mesos-slave-modules.json

echo "Now run:     systemctl restart dcos-mesos-slave; journalctl -f"
