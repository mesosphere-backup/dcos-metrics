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

package mesos_agent

import (
	"bytes"
	"fmt"
	"html/template"
	"net"
	"net/url"
	"reflect"
	"strconv"
	"strings"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/collector/http_client"
	"github.com/dcos/dcos-metrics/producers"
)

const (
	HTTPTIMEOUT = 2 * time.Second
)

var (
	maColLog = log.WithFields(log.Fields{
		"collector": "mesos-agent"})
)

func (h *MesosAgentCollector) RunPoller() {
	ticker := time.NewTicker(h.PollPeriod)

	// Poll once immediately
	h.pollMesosAgent()
	for _, m := range h.transformContainerMetrics() {
		h.MetricsChan <- m
	}

	for {
		select {
		case _ = <-ticker.C:
			h.pollMesosAgent()
			for _, m := range h.transformContainerMetrics() {
				h.MetricsChan <- m
			}
		}
	}
}

func (h *MesosAgentCollector) pollMesosAgent() {
	host := fmt.Sprintf("%s:%d", h.NodeInfo.IPAddress, h.Port)

	// always fetch/emit agent state first: downstream will use it for tagging metrics
	maColLog.Debugf("Fetching state from DC/OS host %s", host)
	if err := h.getAgentState(); err != nil {
		maColLog.Errorf("Failed to get agent state from %s. Error: %s", host, h.Port, err)
	}

	maColLog.Debugf("Fetching container metrics from DC/OS host %s", host)
	if err := h.getContainerMetrics(); err != nil {
		maColLog.Errorf("Failed to get container metrics from %s. Error: %s", host, err)
	}
}

// getContainerMetrics queries an agent for container-level metrics, such as
// CPU, memory, disk, and network usage.
func (h *MesosAgentCollector) getContainerMetrics() error {
	h.containerMetrics = []agentContainer{}
	u := url.URL{
		Scheme: "http",
		Host:   net.JoinHostPort(h.NodeInfo.IPAddress, strconv.Itoa(h.Port)),
		Path:   "/containers",
	}

	h.HTTPClient.Timeout = HTTPTIMEOUT

	return http_client.Fetch(h.HTTPClient, u, &h.containerMetrics)
}

// getAgentState fetches the state JSON from the Mesos agent, which contains
// info such as framework names and IDs, the current leader, config flags,
// container (executor) labels, and more.
func (h *MesosAgentCollector) getAgentState() error {
	h.agentState = agentState{}

	u := url.URL{
		Scheme: "http",
		Host:   net.JoinHostPort(h.NodeInfo.IPAddress, strconv.Itoa(h.Port)),
		Path:   "/state",
	}

	h.HTTPClient.Timeout = HTTPTIMEOUT

	return http_client.Fetch(h.HTTPClient, u, &h.agentState)
}

func (h *MesosAgentCollector) transformContainerMetrics() (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	now := time.Now().UTC()

	// Produce container metrics
	for _, c := range h.containerMetrics {
		msg = producers.MetricsMessage{
			Name:       producers.ContainerMetricPrefix,
			Datapoints: buildDatapoints(c, now),
			Timestamp:  now.Unix(),
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
