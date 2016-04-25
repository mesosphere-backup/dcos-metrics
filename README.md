# dcos-stats
Routing of metrics from Infinity services to a customer-operated dashboard.

- ```slave```: C++ code for the mesos-slave module. Advertises metrics endpoints to containers, and forwards any metrics obtained at those advertised endpoints to the Router.
- ```router```: Go code for the Router framework. Receives metrics from mesos-slaves, then manipulates/filters/forwards those metrics to one or more customer-owned endpoints. This may be abandoned in favor of just sending stats straight to a Kafka instance in the cluster.
- ```test-sender```: Sample code for a cluster process which emits some metrics.

Docs:
- **[Install/config instructions](DEMO.md)**
- [Design doc](https://docs.google.com/document/d/11XZF8600Fqfw_yY9YeSh-rX2jJVN4rjw_oQuJFkvlwM/edit#)
