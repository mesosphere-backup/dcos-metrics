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
	"bytes"
	"html/template"
	"net"
	"net/http"
	"net/url"
	"reflect"
	"strconv"
	"strings"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
	"github.com/dcos/dcos-metrics/util/http/client"
)

const (
	// HTTPTIMEOUT defines the maximum duration for all requests
	HTTPTIMEOUT = 2 * time.Second
)

var (
	maColLog = log.WithFields(log.Fields{
		"collector": "mesos-agent"})
)

// Collector defines the collector type for Mesos agent. It is
// configured from main from config file options and pass a new instance of HTTP
// client and a channel for dropping metrics onto.
type Collector struct {
	Port            int           `yaml:"port"`
	PollPeriod      time.Duration `yaml:"poll_period"`
	RequestProtocol string        `yaml:"request_protocol"`

	MetricsChan chan producers.MetricsMessage
	NodeInfo    collectors.NodeInfo
	HTTPClient  *http.Client

	agentState       agentState
	containerMetrics []agentContainer
	timestamp        int64
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

// RunPoller continually polls the agent on a set interval.
func (h *Collector) RunPoller() {
	for {
		h.pollMesosAgent()
		for _, m := range h.transformContainerMetrics() {
			h.MetricsChan <- m
		}
		time.Sleep(h.PollPeriod * time.Second)
	}
}

func (h *Collector) pollMesosAgent() {
	now := time.Now().UTC()
	h.timestamp = now.Unix()

	host := net.JoinHostPort(h.NodeInfo.IPAddress, strconv.Itoa(h.Port))

	// always fetch/emit agent state first: downstream will use it for tagging metrics
	maColLog.Debugf("Fetching state from DC/OS host %s", host)
	if err := h.getAgentState(); err != nil {
		maColLog.Errorf("Failed to get agent state from %s. Error: %s", host, err)
	}

	maColLog.Debugf("Fetching container metrics from DC/OS host %s", host)
	if err := h.getContainerMetrics(); err != nil {
		maColLog.Errorf("Failed to get container metrics from %s. Error: %s", host, err)
	}
}

// getContainerMetrics queries an agent for container-level metrics, such as
// CPU, memory, disk, and network usage.
func (h *Collector) getContainerMetrics() error {
	h.containerMetrics = []agentContainer{}
	u := url.URL{
		Scheme: h.RequestProtocol,
		Host:   net.JoinHostPort(h.NodeInfo.IPAddress, strconv.Itoa(h.Port)),
		Path:   "/containers",
	}

	h.HTTPClient.Timeout = HTTPTIMEOUT

	return client.Fetch(h.HTTPClient, u, &h.containerMetrics)
}

// getAgentState fetches the state JSON from the Mesos agent, which contains
// info such as framework names and IDs, the current leader, config flags,
// container (executor) labels, and more.
func (h *Collector) getAgentState() error {
	h.agentState = agentState{}

	u := url.URL{
		Scheme: h.RequestProtocol,
		Host:   net.JoinHostPort(h.NodeInfo.IPAddress, strconv.Itoa(h.Port)),
		Path:   "/state",
	}

	h.HTTPClient.Timeout = HTTPTIMEOUT

	return client.Fetch(h.HTTPClient, u, &h.agentState)
}

func (h *Collector) transformContainerMetrics() (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	t := time.Unix(h.timestamp, 0)

	// Produce container metrics
	for _, c := range h.containerMetrics {
		msg = producers.MetricsMessage{
			Name:       producers.ContainerMetricPrefix,
			Datapoints: buildDatapoints(c, t),
			Timestamp:  t.UTC().Unix(),
		}

		fi, ok := getFrameworkInfoByFrameworkID(c.FrameworkID, h.agentState.Frameworks)
		if !ok {
			maColLog.Warnf("Did not find FrameworkInfo for framework ID %s, skipping!", fi.ID)
			continue
		}

		msg.Dimensions = producers.Dimensions{
			MesosID:            h.NodeInfo.MesosID,
			ClusterID:          h.NodeInfo.ClusterID,
			Hostname:           h.NodeInfo.Hostname,
			ContainerID:        c.ContainerID,
			ExecutorID:         c.ExecutorID,
			FrameworkID:        c.FrameworkID,
			FrameworkName:      fi.Name,
			FrameworkRole:      fi.Role,
			FrameworkPrincipal: fi.Principal,
			Labels:             getLabelsByContainerID(c.ContainerID, h.agentState.Frameworks),
		}
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

// buildDatapoints takes an incoming structure and builds Datapoints
// for a MetricsMessage. It uses a normalized version of the JSON tag
// as the datapoint name.
func buildDatapoints(in interface{}, t time.Time) []producers.Datapoint {
	pts := []producers.Datapoint{}
	v := reflect.Indirect(reflect.ValueOf(in))

	for i := 0; i < v.NumField(); i++ { // Iterate over fields in the struct
		f := v.Field(i)
		typ := v.Type().Field(i)

		switch f.Kind() { // Handle nested data
		case reflect.Ptr:
			pts = append(pts, buildDatapoints(f.Elem().Interface(), t)...)
			continue
		case reflect.Map:
			// Ignore maps when building datapoints
			continue
		case reflect.Slice:
			for j := 0; j < f.Len(); j++ {
				for _, ndp := range buildDatapoints(f.Index(j).Interface(), t) {
					pts = append(pts, ndp)
				}
			}
			continue
		case reflect.Struct:
			pts = append(pts, buildDatapoints(f.Interface(), t)...)
			continue
		}

		// Get the underlying value; see https://golang.org/pkg/reflect/#Kind
		var uv interface{}
		switch f.Kind() {
		case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
			uv = f.Int()
		case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
			uv = f.Uint()
		case reflect.Float32, reflect.Float64:
			uv = f.Float()
		case reflect.String:
			continue // strings aren't valid values for our metrics
		}

		// Parse JSON name (with or without templating)
		var parsed bytes.Buffer
		jsonName := strings.Join([]string{strings.Split(typ.Tag.Get("json"), ",")[0]}, producers.MetricNamespaceSep)
		tmpl, err := template.New("_nodeMetricName").Parse(jsonName)
		if err != nil {
			maColLog.Warn("Unable to build datapoint for metric with name %s, skipping", jsonName)
			continue
		}
		if err := tmpl.Execute(&parsed, v.Interface()); err != nil {
			maColLog.Warn("Unable to build datapoint for metric with name %s, skipping", jsonName)
			continue
		}

		pts = append(pts, producers.Datapoint{
			Name:      parsed.String(),
			Unit:      "", // TODO(roger): not currently an easy way to get units
			Value:     uv,
			Timestamp: t.UTC().Format(time.RFC3339Nano),
		})
	}
	return pts
}
