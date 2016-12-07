# Architecture and Design
  * Mesosphere, DC/OS Open Source
  * October 16, 2016 (Updated December 7, 2016)
  * Team Lead: Jeff Malnick 
  * Tech Lead: Roger Ignazio 

## Motivation
As an end-user of DC/OS, I want to be able to pull discrete metrics about my application and my cluster infrastructure
in order to make informative charts, dashboards and alerts in my metrics analysis stack of choice. Our primary goal with
this MVP is give our end users this base which can be seamlessly integrated with any metrics analysis stack, whether
that’s Data Dog with Kafka or Graphana on top of Influx, we aim to be un-opinionated about what you do with the metrics
once they’re out of the system. Our goal, is to make getting those metrics mind numbingly simple. 

## Terminology
  * *Metric* - a discrete piece of telemetry
  * *DC/OS* - the Datacenter Operating System
  * *Containerizer* - a method to contain a process using Linux cgroups & namespaces
  * *Mesos Module* - Mesos code loaded on demand, <http://mesos.apache.org/documentation/latest/modules/>

## Overview
There are three layers of metrics identified in DC/OS: host, container and application.

  * Host metrics are metrics about the specific node which is part of the DC/OS cluster. 
  * Container metrics are metrics regarding cgroup allocations from tasks running in Mesos or Docker containerizers. 
  * Application metrics are metrics which are part of a specific application running inside a Mesos or Docker containerizer.

The first API that we’ll be building for DC/OS metrics is an HTTP API which is capable of exposing these three core
areas. Further development will include a DataDog-style statsd "exhaust" or exports of metrics for other metrics stacks.
We’re really excited about enabling integration with everyone’s existing metrics tools.

All three metrics layers (host, container and application) will be aggregated by a collector which is shipped as part of
the DC/OS distribution. This will enable us to run it on every host in the cluster, both masters and agents. It will
be the main entrypoint to the metrics ecosystem, aggregating metrics sent to it by the Metrics Mesos module, or
gathering host and container level metrics on the box which is runs. 

The Mesos Metrics module will be bundled with every Mesos agent in the cluster. This module will enable applications to
publish metrics from applications running on top of DC/OS to the collector by exposing a statsd port and host
environment variable inside every container. We will then decorate these metrics with some structured data such as
agent-id, framework-id and task-id.

![architecture diagram](https://www.lucidchart.com/publicSegments/view/30f4c23-b2f9-4db3-9954-a947f395eae5/image.png)

Per-container metrics tags will enable you to arbitrarily group metrics on e.g. a per-framework or per-system/agent
basis. The full list is:
  * `agent_id`
  * `container_id`
  * `executor_id`
  * `framework_id`
  * `framework_name`
  * `framework_principal`
  * `hostname`
  * `labels`

Your applications will discover the endpoint via an environment variable (`STATSD_UDP_HOST` / `STATSD_UDP_PORT`). They
will be able to leverage this statsd interface to send custom profiling metrics to the system.

In addition, we collect some metrics automatically for you. These are:
  * Per-container resource resource utilization (metrics named 'usage.*')
  * Agent/system-level resource utilization (metrics named 'node.*', not tied to a specific container, so only tagged with `agent_id`)

## Implementation Plan
### Mesos Metrics Module
The Mesos Metrics Module shall provide the following push-based statsd UDP API for metrics:

  1. A per-task statsd UDP endpoint for the collection of task-generated metrics shall be opened by the module for each
     Mesos-containerized container launched on the agents. This endpoint shall be advertised to the container via the
     following environment variables: `STATSD_UDP_HOST` and `STATSD_UDP_PORT`.
  2. The task may emit well-formed metrics to this endpoint. The Mesos Metrics Module will ensure that it can map each
     incoming metric to its originating container. Any malformed metrics will be discarded. Multiple metrics may be sent
     in a single packet provided they are delimited with a newline per the statsd convention. Metrics may be annotated
     with Datadog-format tags, and those tags will be passed through to the module’s output.
  3. Any metrics received by the Mesos Metrics Module shall be annotated (tagged) with the following basic information
     to identify the originating container. Any additional annotations may be inserted downstream by the Collector:
      * `container_id`
      * `executor_id`
      * `framework_id`
  4. The Mesos Metrics Module shall then forward the resulting annotated (tagged) metrics in the form of repeated Avro
     MetricLists, serialized to follow the Avro OCF format. This data is sent to a well known, configurable TCP port
     (configured in `modules.json`). Exactly one Collector may listen on this port to ship metrics to an off-agent
     aggregator.
  5. The Mesos agent shall not offer this host port for framework reservation.

## Metrics Collector 
### Motivation & Reasoning
Our original prototype for this used Avro and Kafka for sending metrics out. This required that everyone run a Kafka
cluster for integration. After listening to initial feedback, we decided to build an HTTP API to enable integration with
existing tools. This was the easiest way for integrations to be built out. In time, we’d like to add statsd exhaust to
allow for something more push based.

### Configuration & Installation
The metrics collector is the only configurable piece of the metrics system. The Mesos Module is not configurable at this
time. The MVP will be configured as part of the normal DC/OS configuration process. The implications of this is that
right now you’ll need to upgrade your cluster to change these configuration options.

In the very near future, we would like to  have a configuration API in place. That API will allow us to update
configuration in a more real-time fashion.. It will be particularly useful for re-configuring the metrics collector.

The collector itself will run as a systemd unit that is distributed via the normal DC/OS distribution. It will be built
and packaged via pkgpanda. It’s final form is of a Golang compiled binary for Linux 64 bit architectures. 

## API (Additions and Changes)
### Metrics Producers HTTP API Producer Specification (MVP)
Base Structure

```
    http://<hostname>:<port>/system/v<version>/metrics/v<version>/<request>
```

Node Endpoints
  * `/system/metrics/v0/node`

Containers and App Endpoints
  * `/system/metrics/v0/containers`
  * `/system/metrics/v0/containers/<id>`
  * `/system/metrics/v0/containers/<id>/app`
  * `/system/metrics/v0/containers/<id>/app/<metric-id>`

## Users
The main user for metrics are cluster super users who are administering DC/OS. They will be the "front line" user for
this feature, though every user of DC/OS will inevitably consume metrics, whether directly from the API’s we implement
here or by proxy via the dashboards, graphs and other metrics analysis stacks which administrators send the metrics to. 

## Security Plan
Since metrics are mainly an "exhaust" from the cluster, i.e., most metrics are sent to other service stacks and not
consumed by DC/OS users, we will not implement any role based access control for them. The caveat is that our HTTP
producer does expose and API endpoint, which can be consumed by DC/OS users. In this case and this case only, we will
implement coarse grained ACLs via the adminrouter proxy to ensure only DC/OS super users have access to this HTTP API
endpoint. 

## Upgrade/Downgrade Plan
This is an additional feature which does not touch any currently working DC/OS code. In the future we’ll need to ensure
our API is reverse compatible or implement a notion of cababilities. 

## Third-Party Integration Plan
We will POC a few integrations with metrics stacks which are widely used, mainly, InfluxDB/Grafana and Kafka/DataDog to
start.

## Test Plan
### Unit
We will aim for 80% coverage on the codebase itself. We will leverage Jenkins to automate running these tests plus
golang metalinter to ensure code quality as well as blocking broken code from merging to master accidentally. 

### Integration/Acceptance 
We will use a client to query the metrics collector HTTP API in a running cluster and test for expected data to be
returned. This test will be a part of the DC/OS integration test package.

### Soak
We will do a similar client-based query of the HTTP API from inside the Soak cluster, ensuring durability in a long
running cluster. 

## Documentation and Examples Plan
The documentation for the metrics feature will be written by both tech lead with copy writing by the team lead and the
docs team.