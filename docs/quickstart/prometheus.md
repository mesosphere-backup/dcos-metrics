# DC/OS metrics with Prometheus and Grafana

Prometheus producers are already running on every node in your cluster, so you only have to configure Prometheus and
Grafana. This walkthrough shows you the quickest way to deploy Prometheus to monitor your DC/OS cluster. 

## Prerequisites:

* A cluster running DC/OS 1.11 or higher (see [Prometheus on DC/OS 1.9 and 1.10][quickstart-prom-dcos19] for earlier
versions of DC/OS)
* The [DC/OS CLI][docs-dcos-cli] installed

## Deploying Prometheus and Grafana

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
[quickstart-prom-dcos19]: prometheus-dcos19.md
[resource-prom-json]: ../resources/prometheus.json
[resource-graf-json]: ../resources/grafana.json
