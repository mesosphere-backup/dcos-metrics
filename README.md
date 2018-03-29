# dcos-metrics

[![Build Status][button-build]][jenkins-master]
[![Go Report Card][button-go-report]][go-report-card]

## Overview

dcos-metrics collects, tags, and transmits metrics from every node, container, and application in your DC/OS cluster. 

![Architecture Diagram][architecture-diagram]

## Getting started

|For operators                                             |For developers                                             |
|----------------------------------------------------------|-----------------------------------------------------------|
|[Self-hosted Prometheus & Grafana][quickstart-prometheus] |[Instrumenting your code][quickstart-instrumentation]      |
|[Integrating with DataDog][quickstart-datadog]            |[Using the DC/OS HTTP API][quickstart-api]                 |
|[Integrating with Librato][quickstart-librato]            |[Contributing to DC/OS Metrics][quickstart-contributing]   |

## Download Plugins

* [DataDog][plugin-datadog]
* [Librato][plugin-librato]
* [Prometheus][plugin-prometheus] (DC/OS 1.9 and 1.10 only)

## Resources

* [Report a bug][jira-bug]
* [Suggest a feature][jira-feature]
* [Join Community Slack][dcos-slack] (#day2ops and #day2ops-wg channels for discussion of dcos-metrics)
* [Mailing List][dcos-mailing-list]

## License

Both dcos-metrics and [DC/OS][github-dcos] are open source software released under the
[Apache Software License, Version 2.0](LICENSE).

[dcos-metrics][github-dcos-metrics] is maintained by the Cluster Ops team at [Mesosphere][mesosphere-io]. 


[button-build]: https://jenkins.mesosphere.com/service/jenkins/buildStatus/icon?job=public-dcos-cluster-ops/dcos-metrics/dcos-metrics-master
[button-go-report]: https://goreportcard.com/badge/github.com/dcos/dcos-metrics

[architecture-diagram]: docs/architecture.svg

[jenkins-master]: https://jenkins.mesosphere.com/service/jenkins/job/public-dcos-cluster-ops/job/dcos-metrics/job/dcos-metrics-master/
[jira-bug]: https://jira.mesosphere.com/secure/CreateIssueDetails!init.jspa?issuetype=1&pid=14105&components=19811&summary=Issue%20on%20DC/OS%20Metrics&priority=2&labels=testing&assignee=philip&customfield_12300=4
[jira-feature]: https://jira.mesosphere.com/secure/CreateIssueDetails!init.jspa?issuetype=4&pid=14105&components=19811&summary=Issue%20on%20DC/OS%20Metrics&priority=2&labels=testing&assignee=philip&customfield_12300=4
[go-report-card]: https://goreportcard.com/report/github.com/dcos/dcos-metrics

[quickstart-prometheus]: docs/quickstart/prometheus.md
[quickstart-datadog]: docs/quickstart/datadog.md
[quickstart-librato]: docs/quickstart/librato.md
[quickstart-instrumentation]: docs/quickstart/instrumentation.md
[quickstart-api]: docs/quickstart/api.md
[quickstart-contributing]: docs/quickstart/contributing.md

[plugin-datadog]: https://downloads.mesosphere.io/dcos-metrics/plugins/datadog
[plugin-librato]: https://downloads.mesosphere.io/dcos-metrics/plugins/librato
[plugin-prometheus]: https://downloads.mesosphere.io/dcos-metrics/plugins/prometheus

[dcos-jira]: https://jira.mesosphere.com
[dcos-mailing-list]: https://groups.google.com/a/dcos.io/forum/#!forum/users
[dcos-slack]: https://dcos-community.slack.com
[github-clusterops]: https://github.com/orgs/mesosphere/teams/clusterops-team
[github-dcos]: https://github.com/dcos/dcos
[github-dcos-metrics]: https://github.com/dcos/dcos-metrics
[mesosphere-io]: https://mesosphere.io
