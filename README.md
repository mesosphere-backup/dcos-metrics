# dcos-stats
Routing of metrics from Infinity services to a customer-operated dashboard.

- ```slave```: C++ code for the mesos-slave module. Advertises metrics endpoints to containers, and forwards any metrics obtained at those advertised endpoints to the Router.
- ```router```: Go code for the Router framework. Receives metrics from mesos-slaves, then manipulates/filters/forwards those metrics to one or more customer-owned endpoints.
- ```test-sender```: Sample code for a cluster process which emits some metrics.

[Design doc](https://docs.google.com/document/d/11XZF8600Fqfw_yY9YeSh-rX2jJVN4rjw_oQuJFkvlwM/edit#)
