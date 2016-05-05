# dcos-stats
Routing of metrics from Infinity services to a customer-operated dashboard.

- ```module```: C++ code for the mesos-agent module. Advertises metrics endpoints to containers, and tags/forwards any metrics obtained at those advertised endpoints upstream. This module is installed  by default on DCOS EE 1.7+.
- ```collector```: Go code for the mesos-agent metrics collector. Receives metrics from the mesos-agent module, as well as other processes on the system, and forwards that data to a Kafka instance and/or exposes the data to a local partner metrics agent.
- ```test-sender```: Sample code for a containerized process which emits arbitrary statsd metrics to an endpoint advertised by the mesos-agent module. Reference for service development on DC/OS.

Docs:
- **[Using the module](DEMO.md)**
- [Installing custom module builds (for module dev)](module/README.md)
- [Design doc](https://docs.google.com/document/d/11XZF8600Fqfw_yY9YeSh-rX2jJVN4rjw_oQuJFkvlwM/edit#)
