# dcos-stats
Routing of metrics from Infinity services to a customer-operated dashboard.

- ```slave```: C++ code for the mesos-slave module. Advertises metrics endpoints to containers, and tags/forwards any metrics obtained at those advertised endpoints upstream. This module is installed  by default on DCOS EE 1.7+.
- ```router```: Go code for a Router framework. Receives metrics from mesos-slaves, then manipulates/filters/forwards those metrics to one or more customer-owned endpoints. This may be abandoned in favor of just sending stats straight to a Kafka instance in the cluster.
- ```test-sender```: Sample code for a process which emits some metrics to an endpoint advertised by the mesos-slave module.

Docs:
- **[Using the module](DEMO.md)**
- [Installing custom module builds (for module dev)](slave/README.md)
- [Design doc](https://docs.google.com/document/d/11XZF8600Fqfw_yY9YeSh-rX2jJVN4rjw_oQuJFkvlwM/edit#)
