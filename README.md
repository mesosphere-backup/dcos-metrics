# dcos-metrics
[![Build Status](https://jenkins.mesosphere.com/service/jenkins/buildStatus/icon?job=public-dcos-metrics/public-dcos-metrics-master)][jenkins-master]
[![Go Report Card](https://goreportcard.com/badge/github.com/dcos/dcos-metrics)][go-report-card]

**Table of Contents:**
  1. [Overview](#overview)
  2. [How This Repo Is Organized](#how-this-repo-is-organized)
  3. [Getting Started](#getting-started)
  4. [Documentation](#documentation)
  5. [Community](#community)
  6. [Contributing](#contributing)
  7. [License](#license)
  8. [Acknowledgements](#acknowledgements)

## Overview
The metrics component provides operational insight to your DC/OS cluster, providing discrete metrics about your applications and deployments. This can include charts, dashboards, and alerts based on cluster, node, container, and application-level statistics. 

This project provides a metrics service for all DC/OS clusters which can be integrated with any timeseries data store or
hosted metrics service. We aim to be un-opinionated about what you do with the metrics once they’re out of the system.
However you look at it, getting those metrics should be mind-numbingly simple. 


## How This Repo Is Organized
  - **[collectors](collectors/)**: The collector (input) portion of the metrics service that runs on every DC/OS node.
      - Periodically polls the local machine for:
        - OS-level metrics: polls the underlying OS for system-wide metrics such as CPU, memory, disk, and
        network utilization. 
        - Mesos agent metrics: polls the Mesos agent APIs for agent statistics, state, and dimensions.
        - Container metrics: polls the Mesos agent APIs for per-container metrics.
      - Listens on TCP port `8124` for Avro-formatted metrics from the mesos-agent module for application-level metrics.
  - **[docs](docs/)**: Project documentation.
  - **[examples](examples/)**: Reference implementations of programs which integrate with the metrics stack.
    - **[collector-emitter](examples/collector-emitter/)**: A reference for DC/OS system processes which emit metrics.
    Sends some Avro metrics data to a local Collector process.
    - **[configs](examples/configs/)**: Example configuration files.
    - **[datadog](examples/datadog/)**: Scripts and dashboards to be used with the DataDog service.
    - **[statsd-emitter](examples/statsd-emitter/)**: A reference for mesos tasks which emit metrics. Sends some StatsD
    metrics to the `STATSD_UDP_HOST` / `STATSD_UDP_PORT` endpoint advertised by the mesos-agent module.
  - **[mesos_module](mesos_module/)**: Mesos module (written in C++) to support application-level metrics. Exposes
  two environment variables to each container: `STATSD_UDP_HOST` and `STATSD_UDP_PORT`.
  - **[producers](producers/)**: The producer (output) portion of the metrics service that runs on every DC/OS node.
      - HTTP: exposes a JSON-formatted HTTP API on the local node to be queried by user-provided tooling.
  - **[schema](schema/)**: Schemas shared between the Mesos module and the collector daemon.
  - **[scripts](scripts/)**: Helper scripts used to build and test this project.
  - **[util](util/)**: Helper utilities used across Go packages in this repo.
  - **[vendor](vendor/)**: Vendored libraries (uses [govendor][github-govendor])


## Getting Started
The dcos-metrics component is natively integrated with DC/OS version 1.9 and later. No additional setup is required.

### Application-level metrics
This service exposes two new environment variables to every Mesos container: `STATSD_UDP_HOST` and `STATSD_UDP_PORT`. The process is to simply send StatsD-formatted metrics to the host and port provided to your container via those environment variables, and the metrics service will take it from there. Incoming metrics are automatically decorated with dimensions about the host and cluster that the container or application is running on.


## Documentation
All documentation for this project is located in the [docs](docs/) directory at the root of this repository.


## Community
This project is one component of the larger DC/OS community.
  * [DC/OS JIRA (issue tracker)][dcos-jira] (please use the `dcos-metrics` component)
  * [DC/OS mailing list][dcos-mailing-list]
  * [DC/OS Community Slack team][dcos-slack]


## Contributing
We love contributions! There's more than one way to give back, from code to documentation and examples. To ensure we
have a chance to keep up with community contributions, please follow the guidelines in [CONTRIBUTING.md](CONTRIBUTING.md).


## License
[DC/OS][github-dcos], along with this project, are both open source software released under the
[Apache Software License, Version 2.0](LICENSE).


## Acknowledgements
  * Current maintainer(s): [Jeff Malnick][github-malnick], [Roger Ignazio][github-rji]
  * Original author: [Nicholas Parker][github-nickbp]


[dcos-jira]: https://dcosjira.atlassian.net
[dcos-mailing-list]: https://groups.google.com/a/dcos.io/forum/#!forum/users
[dcos-slack]: https://dcos-community.slack.com
[github-dcos]: https://github.com/dcos/dcos
[github-govendor]: https://github.com/kardianos/govendor
[github-malnick]: https://github.com/malnick
[github-nickbp]: https://github.com/nickbp
[github-rji]: https://github.com/rji
[github-universe]: https://github.com/mesosphere/universe
[go-report-card]: https://goreportcard.com/report/github.com/dcos/dcos-metrics]
[jenkins-master]: https://jenkins.mesosphere.com/service/jenkins/job/public-dcos-metrics/job/public-dcos-metrics-master/
