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

package collector

import (
	"net/url"
	"strconv"
	"strings"
	"time"
)

// agentContainer defines the structure of the response expected from Mesos
// *for a single container* when polling the '/containers' endpoint in API v1.
// Note that agentContainer is actually in a top-level array. For more info, see
// https://github.com/apache/mesos/blob/1.0.1/include/mesos/v1/agent/agent.proto#L161-L168
type agentContainer struct {
	FrameworkID     string                 `json:"framework_id"`
	ExecutorID      string                 `json:"executor_id"`
	ExecutorName    string                 `json:"executor_name"`
	Source          string                 `json:"source"`
	ContainerID     string                 `json:"container_id"`
	ContainerStatus map[string]interface{} `json:"container_status"`
	Statistics      *resourceStatistics    `json:"statistics"`
}

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
type agentState struct {
	ID         string          `json:"id"`
	Hostname   string          `json:"hostname"`
	Frameworks []frameworkInfo `json:"frameworks"`
}

type frameworkInfo struct {
	ID        string         `json:"id"`
	Name      string         `json:"name"`
	Principal string         `json:"principal"`
	Role      string         `json:"role"`
	Executors []executorInfo `json:"executors"`
}

type executorInfo struct {
	ID        string           `json:"id"`
	Name      string           `json:"name"`
	Container string           `json:"container"`
	Labels    []executorLabels `json:"labels,omitempty"` // labels are optional
}

type executorLabels struct {
	Key   string `json:"key"`
	Value string `json:"value"`
}

// resourceStatistics defines the structure of the response expected from Mesos
// when referring to container and/or executor metrics. In Mesos, the
// ResourceStatistics message is very large; it defines many fields that are
// dependent on a feature being enabled in Mesos, and not all of those features
// are enabled in DC/OS.
//
// Therefore, we redefine the resourceStatistics struct here with only the fields
// dcos-metrics currently cares about, which should be stable for Mesos API v1.
//
// For a complete reference, see:
// https://github.com/apache/mesos/blob/1.0.1/include/mesos/v1/mesos.proto#L921-L1022
type resourceStatistics struct {
	// CPU usage info
	CpusUserTimeSecs      float64 `json:"cpus_user_time_secs,omitempty"`
	CpusSystemTimeSecs    float64 `json:"cpus_system_time_secs,omitempty"`
	CpusLimit             float64 `json:"cpus_limit,omitempty"`
	CpusThrottledTimeSecs float64 `json:"cpus_throttled_time_secs,omitempty"`

	// Memory info
	MemTotalBytes uint64 `json:"mem_total_bytes,omitempty"`
	MemLimitBytes uint64 `json:"mem_limit_bytes,omitempty"`

	// Disk info
	DiskLimitBytes uint64 `json:"disk_limit_bytes,omitempty"`
	DiskUsedBytes  uint64 `json:"disk_used_bytes,omitempty"`

	// Network info
	NetRxPackets uint64 `json:"net_rx_packets,omitempty"`
	NetRxBytes   uint64 `json:"net_rx_bytes,omitempty"`
	NetRxErrors  uint64 `json:"net_rx_errors,omitempty"`
	NetRxDropped uint64 `json:"net_rx_dropped,omitempty"`
	NetTxPackets uint64 `json:"net_tx_packets,omitempty"`
	NetTxBytes   uint64 `json:"net_tx_bytes,omitempty"`
	NetTxErrors  uint64 `json:"net_tx_errors,omitempty"`
	NetTxDropped uint64 `json:"net_tx_dropped,omitempty"`
}

// getContainerMetrics queries an agent for container-level metrics, such as
// CPU, memory, disk, and network usage.
func (h *DCOSHost) getContainerMetrics() ([]agentContainer, error) {
	var containers []agentContainer
	u := url.URL{
		Scheme: "http",
		Host:   strings.Join([]string{h.IPAddress, strconv.Itoa(h.Port)}, ":"),
		Path:   "/containers",
	}

	h.HTTPClient.Timeout = time.Duration(2 * time.Second)

	return containers, Fetch(h.HTTPClient, u, &containers)
}

// getAgentState fetches the state JSON from the Mesos agent, which contains
// info such as framework names and IDs, the current leader, config flags,
// container (executor) labels, and more.
func (h *DCOSHost) getAgentState() (agentState, error) {
	state := agentState{}

	u := url.URL{
		Scheme: "http",
		Host:   strings.Join([]string{h.IPAddress, strconv.Itoa(h.Port)}, ":"),
		Path:   "/state",
	}

	h.HTTPClient.Timeout = time.Duration(2 * time.Second)

	return state, Fetch(h.HTTPClient, u, &state)
}

// getFrameworkInfoByFrameworkID returns the FrameworkInfo struct given its ID.
func getFrameworkInfoByFrameworkID(frameworkID string, frameworks []frameworkInfo) (frameworkInfo, bool) {
	for _, framework := range frameworks {
		if framework.ID == frameworkID {
			return framework, true
		}
	}
	return frameworkInfo{}, false
}

// getLabelsByContainerID returns a map of labels, as specified by the framework
// that created the executor. In the case of Marathon, the framework allows the
// user to specify their own arbitrary labels per application.
func getLabelsByContainerID(containerID string, frameworks []frameworkInfo) map[string]string {
	labels := map[string]string{}
	for _, framework := range frameworks {
		for _, executor := range framework.Executors {
			if executor.Container == containerID {
				for _, pair := range executor.Labels {
					labels[pair.Key] = pair.Value
				}
				return labels
			}
		}
	}
	return labels
}
