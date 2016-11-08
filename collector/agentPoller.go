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

// Agent defines the structure of the agent metrics poller and any configuration
// that might be required to run it.
type Agent struct {
	AgentIP     string
	IPCommand   string
	Port        int
	PollPeriod  time.Duration
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
	timestamp            int64
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

// NewAgent returns a new instance of a DC/OS agent poller based on the provided
// configuration and the result of the provided ipCommand script for detecting
// the agent's IP address.
func NewAgent(ipCommand string, port int, pollPeriod time.Duration, metricsChan chan<- producers.MetricsMessage) (Agent, error) {
	a := Agent{}

	if len(ipCommand) == 0 {
		return a, fmt.Errorf("Must pass ipCommand to NewAgent()")
	}
	if port < 1024 {
		return a, fmt.Errorf("Must pass port >= 1024 to NewAgent()")
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
	var err error
	if a.AgentIP, err = a.getIP(); err != nil {
		return a, err
	}

	return a, nil
}

// RunPoller periodiclly polls the HTTP APIs of a Mesos agent. This function
// should be run in its own goroutine.
func (a *Agent) RunPoller() {
	ticker := time.NewTicker(a.PollPeriod)

	// Poll once immediately
	for _, m := range a.transform(a.pollAgent()) {
		a.MetricsChan <- m
	}
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

// getIP runs the ip_detect script and returns the IP address that the agent
// is listening on.
func (a *Agent) getIP() (string, error) {
	log.Debugf("Executing ip-detect script %s", a.IPCommand)
	cmdWithArgs := strings.Split(a.IPCommand, " ")

	ipBytes, err := exec.Command(cmdWithArgs[0], cmdWithArgs[1:]...).Output()
	if err != nil {
		return "", err
	}
	ip := strings.TrimSpace(string(ipBytes))
	if len(ip) == 0 {
		return "", err
	}

	log.Debugf("getIP() returned successfully, got IP %s", ip)
	return ip, nil
}

// pollAgent queries the DC/OS agent for metrics and returns.
func (a *Agent) pollAgent() metricsMeta {
	metrics := metricsMeta{}
	now := time.Now().UTC()
	log.Infof("Polling the Mesos agent at %s:%d. Started at %s", a.AgentIP, a.Port, now.String())

	// always fetch/emit agent state first: downstream will use it for tagging metrics
	log.Debugf("Fetching state from agent %s:%d", a.AgentIP, a.Port)
	agentState, err := a.getAgentState()
	if err != nil {
		log.Errorf("Failed to get agent state from %s:%d. Error: %s", a.AgentIP, a.Port, err)
		return metrics
	}

	log.Debugf("Fetching agent metrics from agent %s:%d", a.AgentIP, a.Port)
	agentMetrics, err := a.getAgentMetrics()
	if err != nil {
		log.Errorf("Failed to get agent metrics from %s:%d. Error: %s", a.AgentIP, a.Port, err)
		return metrics
	}

	log.Debugf("Fetching container metrics from agent %s:%d", a.AgentIP, a.Port)
	containerMetrics, err := a.getContainerMetrics()
	if err != nil {
		log.Errorf("Failed to get container metrics from %s:%d. Error: %s", a.AgentIP, a.Port, err)
		return metrics
	}

	log.Infof("Finished polling agent %s:%d, took %f seconds.", a.AgentIP, a.Port, time.Since(now).Seconds())

	metrics.agentState = agentState
	metrics.agentMetricsSnapshot = agentMetrics
	metrics.containerMetrics = containerMetrics
	metrics.timestamp = now.Unix()

	return metrics
}

// transform will take metrics retrieved from the agent and perform any
// transformations necessary to make the data fit the output expected by
// producers.MetricsMessage.
func (a *Agent) transform(in metricsMeta) (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	var v reflect.Value

	t := time.Unix(in.timestamp, 0)

	// Produce agent metrics
	msg = producers.MetricsMessage{
		Name: strings.Join([]string{
			producers.AgentMetricPrefix,
			in.agentState.ID,
		}, producers.MetricNamespaceSep),
		Datapoints: []producers.Datapoint{},
		Dimensions: producers.Dimensions{
			AgentID:   in.agentState.ID,
			ClusterID: "", // TODO(roger) need to get this from the master
			Hostname:  in.agentState.Hostname,
		},
		Timestamp: t.UTC().Unix(),
	}
	v = reflect.Indirect(reflect.ValueOf(in.agentMetricsSnapshot))
	for i := 0; i < v.NumField(); i++ {
		n := strings.Join([]string{
			msg.Name,
			strings.Split(v.Type().Field(i).Tag.Get("json"), ",")[0],
		}, producers.MetricNamespaceSep)

		msg.Datapoints = append(msg.Datapoints, producers.Datapoint{
			Name:      strings.Replace(n, "/", producers.MetricNamespaceSep, -1),
			Unit:      "",                                        // TODO(roger): not currently an easy way to get units
			Value:     fmt.Sprintf("%v", v.Field(i).Interface()), // TODO(roger): everything is a string for MVP
			Timestamp: t.UTC().Format(time.RFC3339Nano),
		})
	}
	out = append(out, msg)

	// Produce container metrics
	for _, c := range in.containerMetrics {
		msg = producers.MetricsMessage{
			Name: strings.Join([]string{
				producers.ContainerMetricPrefix,
				c.ContainerID,
			}, producers.MetricNamespaceSep),
			Datapoints: []producers.Datapoint{},
			Dimensions: producers.Dimensions{},
			Timestamp:  t.UTC().Unix(),
		}
		v = reflect.Indirect(reflect.ValueOf(c.Statistics))
		for i := 0; i < v.NumField(); i++ {
			n := strings.Join([]string{
				msg.Name,
				strings.Split(v.Type().Field(i).Tag.Get("json"), ",")[0],
			}, producers.MetricNamespaceSep)

			msg.Datapoints = append(msg.Datapoints, producers.Datapoint{
				Name:      strings.Replace(n, "/", producers.MetricNamespaceSep, -1),
				Unit:      "",                                        // TODO(roger): not currently an easy way to get units
				Value:     fmt.Sprintf("%v", v.Field(i).Interface()), // TODO(roger): everything is a string for MVP
				Timestamp: t.UTC().Format(time.RFC3339Nano),
			})
		}

		fi, ok := getFrameworkInfoByFrameworkID(c.FrameworkID, in.agentState.Frameworks)
		if !ok {
			log.Warnf("Did not find FrameworkInfo for framework ID %s, skipping!", fi.ID)
			continue
		}

		msg.Dimensions.AgentID = in.agentState.ID
		msg.Dimensions.ClusterID = "" // TODO(roger) need to get this from the master
		msg.Dimensions.ContainerID = c.ContainerID
		msg.Dimensions.FrameworkID = c.FrameworkID
		msg.Dimensions.FrameworkName = fi.Name
		msg.Dimensions.FrameworkRole = fi.Role
		msg.Dimensions.FrameworkPrincipal = fi.Principal
		msg.Dimensions.ExecutorID = c.ExecutorID
		msg.Dimensions.ContainerID = c.ContainerID
		msg.Dimensions.Labels = getLabelsByContainerID(c.ContainerID, in.agentState.Frameworks)

		out = append(out, msg)
	}

	return out
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
