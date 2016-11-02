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
	"fmt"
	"os/exec"
	"reflect"
	"strconv"
	"strings"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/producers"
)

const (
	containerMetricPrefix = "dcos.metrics.container"
	agentMetricPrefix     = "dcos.metrics.agent"
)

// Agent defines the structure of the agent metrics poller and any configuration
// that might be required to run it.
type Agent struct {
	AgentIP     string
	IPCommand   string
	Port        int
	PollPeriod  int
	MetricsChan chan<- producers.MetricsMessage
}

// metricsMeta is a high-level struct that contains data structures with the
// various metrics we're collecting from the agent. By implementing this
// "meta struct", we're able to more easily handle the transformation of
// metrics from the structs in this file to the MetricsMessage struct expected
// by the producer(s).
type metricsMeta struct {
	agentState           agentState
	agentMetricsSnapshot agentMetricsSnapshot
	containerMetrics     []agentContainer
	timestamp            string
}

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
// *for all cluster state* when polling the /monitor/state endpoint.
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
	Frameworks []frameworkInfo `json:"frameworks"`
}

type frameworkInfo struct {
	ID        string         `json:"id"`
	Name      string         `json:"name"`
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
	CPUTotal float64 `json:"system/cpu_total"`

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
	Timestamp float64 `json:"timestamp,omitempty"`

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

// NewAgent ...
func NewAgent(ipCommand string, port int, pollPeriod int, metricsChan chan<- producers.MetricsMessage) (Agent, error) {
	var a Agent
	var err error

	if len(ipCommand) == 0 {
		return a, fmt.Errorf("Must pass ipAddress to NewAgent()")
	}
	if port < 1024 {
		return a, fmt.Errorf("Must pass port > 1024 to NewAgent()")
	}
	if pollPeriod == 0 {
		return a, fmt.Errorf("Must pass pollPeriod to NewAgent()")
	}

	a.IPCommand = ipCommand
	a.Port = port
	a.PollPeriod = pollPeriod
	a.MetricsChan = metricsChan

	// Detect the agent's IP address once. Per the DC/OS docs (at least as of
	// November 2016), changing a node's IP address is not supported.
	if a.AgentIP, err = a.getIP(); err != nil {
		log.Error(err)
	}

	return a, nil
}

// RunPoller periodiclly polls the HTTP APIs of a Mesos agent. This function
// should be run in its own goroutine.
func (a *Agent) RunPoller() {
	ticker := time.NewTicker(time.Duration(a.PollPeriod) * time.Second)
	for {
		select {
		case _ = <-ticker.C:
			for _, m := range a.transform(a.pollAgent()) {
				a.MetricsChan <- m
			}
		}
	}
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
		log.Error(err)
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
	var metrics agentMetricsSnapshot

	// TODO(roger): 5sec timeout is a guess. Is there a better way to do this?
	c := NewHTTPClient(
		strings.Join([]string{a.AgentIP, strconv.Itoa(a.Port)}, ":"),
		"/metrics/snapshot",
		time.Duration(5*time.Second))
	if err := c.Fetch(&metrics); err != nil {
		log.Error(err)
		return agentMetricsSnapshot{}, err
	}

	return metrics, nil
}

// getAgentState fetches the state JSON from the Mesos agent, which contains
// info such as framework names and IDs, the current leader, config flags,
// container (executor) labels, and more.
func (a *Agent) getAgentState() (agentState, error) {
	var state agentState

	// TODO(roger): 15sec timeout is a guess. Is there a better way to do this?
	c := NewHTTPClient(
		strings.Join([]string{a.AgentIP, strconv.Itoa(a.Port)}, ":"),
		"/monitor/state",
		time.Duration(15*time.Second))
	if err := c.Fetch(&state); err != nil {
		log.Error(err)
		return agentState{}, err
	}

	return state, nil
}

// getIP runs the ip_detect script and returns the IP address that the agent
// is listening on.
func (a *Agent) getIP() (string, error) {
	cmdWithArgs := strings.Split(a.IPCommand, " ")
	ipBytes, err := exec.Command(cmdWithArgs[0], cmdWithArgs[1:]...).Output()
	if err != nil {
		return "", err
	}
	ip := strings.TrimSpace(string(ipBytes))
	if len(ip) == 0 {
		return "", err
	}

	return ip, nil
}

// pollAgent queries the DC/OS agent for metrics and returns.
func (a *Agent) pollAgent() metricsMeta {
	var metrics metricsMeta
	now := time.Now().Format(time.RFC3339Nano)

	// always fetch/emit agent state first: downstream will use it for tagging metrics
	agentState, err := a.getAgentState()
	if err != nil {
		log.Errorf("Failed to get agent state from %s:%d. Error: %s", a.AgentIP, a.Port, err)
		return metrics
	}

	agentMetrics, err := a.getAgentMetrics()
	if err != nil {
		log.Errorf("Failed to get agent metrics from %s:%d. Error: %s", a.AgentIP, a.Port, err)
		return metrics
	}

	containerMetrics, err := a.getContainerMetrics()
	if err != nil {
		log.Errorf("Failed to get container metrics from %s:%d. Error: %s", a.AgentIP, a.Port, err)
		return metrics
	}

	metrics.agentState = agentState
	metrics.agentMetricsSnapshot = agentMetrics
	metrics.containerMetrics = containerMetrics
	metrics.timestamp = now

	return metrics
}

// transform will take metrics retrieved from the agent and perform any
// transformations necessary to make the data fit the output expected by
// producers.MetricsMessage.
func (a *Agent) transform(in metricsMeta) (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	var v reflect.Value

	// Produce agent metrics
	msg = producers.MetricsMessage{
		Name:       "agent",
		Datapoints: []producers.Datapoint{},
		// TODO(roger): Dimensions: producers.Dimensions{},
	}
	v = reflect.ValueOf(in.agentMetricsSnapshot)
	for i := 0; i < v.NumField(); i++ {
		msg.Datapoints = append(msg.Datapoints, producers.Datapoint{
			Name:      v.Type().Field(i).Name,
			Unit:      "",                  // TODO(roger): not currently an easy way to get units
			Value:     v.Field(i).String(), // TODO(roger): everything is a string for MVP
			Timestamp: in.timestamp,
		})
	}
	out = append(out, msg)

	// Produce container metrics
	for _, c := range in.containerMetrics {
		msg = producers.MetricsMessage{
			Name:       "container",
			Datapoints: []producers.Datapoint{},
			Dimensions: producers.Dimensions{},
		}
		v = reflect.ValueOf(c.Statistics)
		for i := 0; i < v.NumField(); i++ {
			msg.Datapoints = append(msg.Datapoints, producers.Datapoint{
				Name:      v.Type().Field(i).Name,
				Unit:      "",                  // TODO(roger): not currently an easy way to get units
				Value:     v.Field(i).String(), // TODO(roger): everything is a string for MVP
				Timestamp: in.timestamp,
			})
		}

		msg.Dimensions.AgentID = "" // TODO(roger)
		msg.Dimensions.ContainerID = c.ContainerID
		msg.Dimensions.FrameworkID = c.FrameworkID
		msg.Dimensions.FrameworkName = getFrameworkNameByFrameworkID(c.FrameworkID, in.agentState.Frameworks)
		msg.Dimensions.FrameworkRole = ""      // TODO(roger)
		msg.Dimensions.FrameworkPrincipal = "" // TODO(roger)
		msg.Dimensions.ExecutorID = c.ExecutorID
		msg.Dimensions.ContainerID = c.ContainerID
		msg.Dimensions.Labels = getLabelsByContainerID(c.ContainerID, in.agentState.Frameworks)

		out = append(out, msg)
	}

	return out
}

// getFrameworkNameByFrameworkID returns the human-readable framework name.
// For example: "5349f49b-68b3-4638-aab2-fc4ec845f993-0000" => "marathon"
func getFrameworkNameByFrameworkID(frameworkID string, frameworks []frameworkInfo) string {
	for _, framework := range frameworks {
		if framework.ID == frameworkID {
			return framework.Name
		}
	}
	return ""
}

// getLabelsByContainerID returns a map of labels, as specified by the framework
// that created the executor. In the case of Marathon, the framework allows the
// user to specify their own arbitrary labels per application.
func getLabelsByContainerID(containerID string, frameworks []frameworkInfo) (labels map[string]string) {
	for _, framework := range frameworks {
		for _, executor := range framework.Executors {
			if executor.Container == containerID {
				for _, pair := range executor.Labels {
					labels[pair.Key] = pair.Value
				}
				return
			}
		}
	}
	return
}
