// Copyright 2016 Mesosphere, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package agent

import (
	"net"
	"net/url"
	"strconv"

	"github.com/dcos/dcos-metrics/util/http/client"
)

// agentState defines the structure of the response expected from Mesos
// *for all cluster state* when polling the /state endpoint.
// Specifically, this struct exists for the following purposes:
//
//   * collect labels from individual containers (executors) since labels are
//     NOT available via the /containers endpoint in v1.
//   * map framework IDs to a human-readable name
//
// For more information, see the upstream protobuf in Mesos v1:
//
//   * Framework info: https://github.com/apache/mesos/blob/1.0.1/include/mesos/v1/mesos.proto#L207-L307
//   * Executor info:  https://github.com/apache/mesos/blob/1.0.1/include/mesos/v1/mesos.proto#L474-L522
//
// An important note on the difference between master and agent state:
//
// On the agent, both `frameworks` and `completed_frameworks` will list both
// `executors` and `completed_executors`, which each may list `tasks`,
// `queued_tasks` (not yet started), and `completed_tasks` (although
// completed frameworks/executors should only list completed tasks). On the
// master, both `frameworks` and `completed_frameworks` will list `tasks`,
// `unreachable_tasks` (agent unreachable), and `completed_tasks`.
// `orphan_tasks` are no longer possible as of Mesos 1.2.
//

type agentState struct {
	ID         string          `json:"id"`
	Hostname   string          `json:"hostname"`
	Frameworks []frameworkInfo `json:"frameworks"`
}

type frameworkInfo struct {
	ID        string         `json:"id"`
	Name      string         `json:"name"`
	Principal string         `json:"principal,omitempty"`
	Role      string         `json:"role"`
	Executors []executorInfo `json:"executors,omitempty"`
}

type executorInfo struct {
	ID        string     `json:"id"`
	Name      string     `json:"name"`
	Container string     `json:"container"`
	Labels    []keyValue `json:"labels,omitempty"` // labels are optional
	Tasks     []TaskInfo `json:"tasks,omitempty"`
}

type keyValue struct {
	Key   string `json:"key"`
	Value string `json:"value"`
}

// TaskInfo describes a Mesos task
type TaskInfo struct {
	ID       string           `json:"id"`
	Name     string           `json:"name"`
	Labels   []keyValue       `json:"labels,omitempty"`
	Statuses []taskStatusInfo `json:"statuses,omitempty"`
}

type taskStatusInfo struct {
	ContainerStatusInfo containerStatusInfo `json:"container_status"`
}

type containerStatusInfo struct {
	ID containerStatusID `json:"container_id"`
}

type containerStatusID struct {
	Value string `json:"value"`
}

// getAgentState fetches the state JSON from the Mesos agent, which contains
// info such as framework names and IDs, the current leader, config flags,
// container (executor) labels, and more.
func (c *Collector) getAgentState() error {
	c.agentState = agentState{}

	u := url.URL{
		Scheme: c.RequestProtocol,
		Host:   net.JoinHostPort(c.nodeInfo.IPAddress, strconv.Itoa(c.Port)),
		Path:   "/state",
	}

	c.HTTPClient.Timeout = HTTPTIMEOUT
	return client.Fetch(c.HTTPClient, u, &c.agentState, c.Principal, c.Secret)
}
