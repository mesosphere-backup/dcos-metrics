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

// agentMetricsSnapshot defines the structure of the response expected from Mesos
// when referring to agent-level metrics.
//
// TODO(roger): the metrics available via the /metrics/snapshot endpoint DO NOT
// satisfy the requirements set forth in the original dcos-metrics design doc.
// I've included the base Mesos agent metrics here as a starting point ONLY.
// In the future, this should be replaced by something like
// github.com/shirou/gopsutil.
type agentMetricsSnapshot struct {
	// System info
	SystemLoad1Min  float64 `json:"system/load_1min"`
	SystemLoad5Min  float64 `json:"system/load_5min"`
	SystemLoad15Min float64 `json:"system/load_15min"`

	// CPU info
	CPUsTotal float64 `json:"system/cpus_total"`

	// Memory info
	MemTotalBytes float64 `json:"system/mem_total_bytes"`
	MemFreeBytes  float64 `json:"system/mem_free_bytes"`
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
func (a *Agent) getContainerMetrics() ([]agentContainer, error) {
	var containers []agentContainer

	// TODO(roger): 15sec timeout is a guess. Is there a better way to do this?
	c := NewHTTPClient(
		strings.Join([]string{a.AgentIP, strconv.Itoa(a.Port)}, ":"),
		"/containers",
		time.Duration(15*time.Second))
	if err := c.Fetch(&containers); err != nil {
		return nil, err
	}

	return containers, nil
}

// getAgentMetrics queries an agent for system-level metrics, such as CPU,
// memory, disk, and network.
//
// TODO(roger): the metrics available via the /metrics/snapshot endpoint DO NOT
// satisfy the requirements set forth in the original dcos-metrics design doc.
// I've included the base Mesos agent metrics here as a starting point ONLY.
// In the future, this should be replaced by something like
// github.com/shirou/gopsutil.
func (a *Agent) getAgentMetrics() (agentMetricsSnapshot, error) {
	metrics := agentMetricsSnapshot{}

	// TODO(roger): 5sec timeout is a guess. Is there a better way to do this?
	c := NewHTTPClient(
		strings.Join([]string{a.AgentIP, strconv.Itoa(a.Port)}, ":"),
		"/metrics/snapshot",
		time.Duration(5*time.Second))
	if err := c.Fetch(&metrics); err != nil {
		return metrics, err
	}

	return metrics, nil
}

// getAgentState fetches the state JSON from the Mesos agent, which contains
// info such as framework names and IDs, the current leader, config flags,
// container (executor) labels, and more.
func (a *Agent) getAgentState() (agentState, error) {
	state := agentState{}

	// TODO(roger): 15sec timeout is a guess. Is there a better way to do this?
	c := NewHTTPClient(
		strings.Join([]string{a.AgentIP, strconv.Itoa(a.Port)}, ":"),
		"/state",
		time.Duration(15*time.Second))
	if err := c.Fetch(&state); err != nil {
		return state, err
	}

	return state, nil
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
