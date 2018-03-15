# DC/OS metrics with Prometheus and Grafana on DC/OS 1.9 and 1.10

This walkthrough is broken into two parts: first deploying the Prometheus plugin on every node in your cluster, and
second configuring Prometheus and Grafana on DC/OS. 

## Prerequisites:

* A cluster running DC/OS 1.9 or 1.10 (see [Prometheus on DC/OS][quickstart-prom] for newer versions of DC/OS)
* The [DC/OS CLI][docs-dcos-cli] installed

## Part 1: Deploying the Prometheus plugin

SSH into every node and become root:
```
$ dcos node ssh --master-proxy --private-ip=<ip-address>
$ sudo su
```

Download the plugin and make sure it's executable
```
$ curl -OL https://downloads.dcos.io/dcos-metrics/plugins/prometheus /opt/mesosphere/bin/dcos-metrics-prometheus
$ chmod +x /opt/mesosphere/bin/dcos-metrics-prometheus
```

Download the systemd configuration for your plugin
```
$ curl -OL https://downloads.dcos.io/dcos-metrics/plugins/prometheus.service /etc/systemd/system/dcos-metrics-prometheus.service
```

Change the --dcos-role flag to ‘agent’, ‘agent_public' or ‘master'
```
$ vim /etc/systemd/system/dcos-metrics-prometheus.service
```

Load the new configuration and start the plugin
```
$ systemctl daemon-reload
$ systemctl start dcos-metrics-prometheus.service
```

## Part 2: Configuring Prometheus and Grafana

Download the following json resources:
* [prometheus.json][resource-prom-json]
* [grafana.json][resource-graf-json]

Deploy Prometheus using the dcos-metrics prometheus docker image:

`$ dcos marathon app add prometheus.json`

Wait for Prometheus to become healthy, then deploy Grafana using the dcos-metrics grafana docker image:

`$ dcos marathon app add grafana.json`

Wait for Grafana to become healthy, then open the Grafana UI at 
https://your-dcos-master-url/service/grafana

By default, a simple DC/OS dashboard is included. If you create a DC/OS-specific dashboard (for example, for Kafka on
DC/OS) please consider contributing it to the [DC/OS Labs grafana dashboard repository][dcos-labs-grafana]. 

[docs-dcos-cli]: https://docs.mesosphere.com/latest/cli/
[dcos-labs-grafana]: https://github.com/dcos-labs/grafana-dashboards
[quickstart-prom]: prometheus.md
[resource-prom-json]: ../resources/prometheus.json
[resource-graf-json]: ../resources/grafana.json
