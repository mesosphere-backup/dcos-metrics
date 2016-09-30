# dcos-metrics
[![Build Status](https://jenkins.mesosphere.com/service/jenkins/buildStatus/icon?job=public-dcos-metrics/public-dcos-metrics-master)](https://jenkins.mesosphere.com/service/jenkins/job/public-dcos-metrics/job/public-dcos-metrics-master/)

_*Note:* This project is a work-in-progress. We're currently aiming to ship a completed service,
with integration hooks, as part of DC/OS 1.10. Community help is welcome and appreciated!_

1. [Overview](#overview)
2. [How this repo is organized](#how-this-repo-is-organized)
3. [Getting Started](#getting-started)
4. [Documentation](#documentation)
5. [Community](#community)
6. [Contributing](#contributing)
7. [License](#license)
8. [Acknowledgements](#acknowledgements)

## Overview

**I want to...**
  - **emit metrics from a Mesos container**: You should check for `STATSD_UDP_HOST` and `STATSD_UDP_PORT` in your application environment, then send statsd-formatted metrics to that endpoint when it's available. You may emit your own tags using the [dogstatsd tag format](http://docs.datadoghq.com/guides/dogstatsd/#datagram-format), and they'll automatically be translated into avro-formatted tags! (see also: [example code](examples/statsd-emitter/))
  - **emit metrics from a system process on the agents**: You should send avro-formatted metrics to the Collector process at `127.0.0.1:8124`. (see also: [avro schema](schema/), [example code](examples/collector-emitter/))
  - **collect and process emitted metrics**: See Quick Start above. Take a look at the available [Kafka Consumers](consumer/), and see if your format already exists. If it doesn't, a new Consumer is *very* easy. (see also: [avro schema](schema/))
  - **develop parts of the metrics stack**: You can run the whole stack on your local system, no Mesos Agent required! To get started, take a look at the [local stack launcher scripts](examples/local-stack).

## How this repo is organized
  - **[module](module/)**: C++ code for the mesos-agent module. This module is installed by default on DC/OS EE 1.7+, with updated output support added as of EE 1.8+.
    - Input: Accepts data produced by Mesos containers on the agent. All Mesos containers are given a unique StatsD endpoint, advertised via `STATSD_UDP_HOST`/`STATSD_UDP_PORT` environment variables. The module then tags and forwards upstream any metrics sent to that endpoint. (EE 1.7+)
    - Output formats:
      - Avro metrics sent to a local Collector process on TCP port `8124` (EE 1.8+)
      - StatsD to `metrics.marathon.mesos` with tags added via key prefixes or datadog tags (EE 1.7 only, disabled in EE 1.8),
  - **[collector](collector/)**: A Marathon process which runs on every agent node.
    - Inputs:
      - Listens on TCP port `8124` for Avro-formatted metrics from the mesos-agent module, as well as any other processes on the system.
      - Polls the local Mesos agent for additional information:
        - `/containers` is polled to retrieve per-container resource usage stats (this was briefly done in the Mesos module via the Oversubscription module interface). Similarly `/metrics/snapshot` is also polled for system-level information.
        - `/state` is polled to determine the local `agent_id` and to get a mapping of `framework_id` to `framework_name`. These are then used to populate `agent_id` on all outgoing metrics, and `framework_name` for metrics that have a `framework_id` (i.e. all metrics emitted by containers).
    - Output: Data is collated into topics and forwarded to a configured Kafka instance (default `kafka`).
  - **[consumer](consumer/)**: Kafka Consumer implementations which fetch Avro-formatted metrics and do something with them (print to `stdout`, write to a database, etc). By default the Consumers will consume from all topics which match the regex pattern `metrics-.*`. This expression can be customized, or alternately a single specific topic can be specified for consumption.
  - **examples**: Reference implementations of programs which integrate with the metrics stack:
    - **[collector-emitter](examples/collector-emitter/)**: A reference for DC/OS system processes which emit metrics. Sends some Avro metrics data to a local Collector process.
    - **[local-stack](examples/local-stack/)**: Helper scripts for running a full metrics stack on a dev machine. Feeds stats into itself and prints them at the end. Requires a running copy of Zookeeper (reqd by Kafka).
    - **[statsd-emitter](examples/statsd-emitter/)**: A reference for mesos tasks which emit metrics. Sends some StatsD metrics to the `STATSD_UDP_HOST`/`STATSD_UDP_PORT` endpoint advertised by the mesos-agent module.
  - **[schema](schema/)**: Avro schemas shared by most everybody that processes metrics (agent module, collector, collector clients, kafka consumers). The exception is containerized processes which only need know how to emit StatsD data.

## Getting Started
First, get a 1.8 EE cluster with at least 3 private nodes (minimum for default Kafka), then install the following:

  1. Install [**Kafka**](http://github.com/mesosphere/kafka-private/README.md): `dcos package install kafka` or install via the Universe UI
    - Note: stock settings are plenty to start with, but for production use consider increasing the default number of partitions (`num.partitions`) and replication factor (`default.replication.factor`).
  2. Run a [**Metrics Collector**](docs/COLLECTOR.md#deployment-to-a-cluster) on every node: use provided marathon jsons.
  3. One or more [**Metrics Consumers**](consumer/): see example marathon jsons for each consumer type, edit output settings as needed before launching

## Documentation
![architecture diagram](https://www.lucidchart.com/publicSegments/view/830f4c23-b2f9-4db3-9954-a947f395eae5/image.png)

  - **[Launching demo processes](docs/DEMO.md)**
  - **[Launching the Collector](docs/COLLECTOR.md)**
  - **[Launching Consumers](docs/CONSUMERS.md)**
  - [Installing custom module builds (for module dev)](docs/MESOS_MODULE.md)
  - [Slides from MesosCon EU (Aug 2016)](http://schd.ws/hosted_files/mesosconeu2016/e7/Metrics%20on%20DC-OS%20Enterprise%20%28Mesoscon%29.pdf)

## Community
This project is one component of the larger DC/OS community.
  * [DC/OS JIRA (issue tracker)][dcos-jira] (please use the `dcos-metrics` component)
  * [DC/OS mailing list][dcos-mailing-list]
  * [DC/OS Community Slack team][dcos-slack]

## Contributing
We love contributions! There's more than one way to give back, from code to documentation
and examples. To ensure we have a chance to keep up with community contributions, please
follow the guidelines in [CONTRIBUTING.md](CONTRIBUTING.md).

## License
[DC/OS][github-dcos], along with this project, are both open source software released under
the [Apache Software License, Version 2.0](LICENSE).

## Acknowledgements
  * Maintainer(s): [Jeff Malnick][github-malnick], [Roger Ignazio][github-rji]
  * Author(s): [Nicholas Parker][github-nickbp]

[dcos-jira]: https://dcosjira.atlassian.net
[dcos-mailing-list]: https://groups.google.com/a/dcos.io/forum/#!forum/users
[dcos-slack]: https://dcos-community.slack.com
[github-dcos]: https://github.com/dcos/dcos
[github-malnick]: https://github.com/malnick
[github-nickbp]: https://github.com/nickbp
[github-rji]: https://github.com/rji
