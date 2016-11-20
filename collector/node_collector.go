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
	"bytes"
	"fmt"
	"net/http"
	"reflect"
	"strings"
	"text/template"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/dcos"
	"github.com/dcos/dcos-metrics/producers"
)

var nodeColLog = log.WithFields(log.Fields{
	"collector": "node",
})

// Agent defines the structure of the agent metrics poller and any configuration
// that might be required to run it.
// This entire struct should be the AgentCollectorConfig
// TODO(malnick) decompose configs so this is
// all set in main
type DCOSHost struct {
	Port        int
	PollPeriod  time.Duration
	MetricsChan chan<- producers.MetricsMessage
	DCOSRole    string
	IPAddress   string
	MesosID     string
	ClusterID   string
	Hostname    string
	HTTPClient  *http.Client
}

// metricsMeta is a high-level struct that contains data structures with the
// various metrics we're collecting from the agent. By implementing this
// "meta struct", we're able to more easily handle the transformation of
// metrics from the structs in this file to the MetricsMessage struct expected
// by the producer(s).
type metricsMeta struct {
	agentState       agentState
	nodeMetrics      nodeMetrics
	containerMetrics []agentContainer
	timestamp        int64
}

// NewAgent returns a new instance of a DC/OS agent poller based on the provided
// configuration and the result of the provided ipCommand script for detecting
// the agent's IP address.
func NewDCOSHost(
	dcosRole string,
	ipAddress string,
	mesosID string,
	clusterID string,
	port int,
	pollPeriod time.Duration,
	httpClient *http.Client,
	metricsChan chan<- producers.MetricsMessage) (DCOSHost, error) {
	h := DCOSHost{}

	if port < 1024 {
		return h, fmt.Errorf("Must pass port >= 1024 to DCOSHost()")
	}
	if pollPeriod == 0 {
		return h, fmt.Errorf("Must pass pollPeriod to DCOSHost()")
	}

	h.Port = port
	h.PollPeriod = pollPeriod
	h.MetricsChan = metricsChan
	h.DCOSRole = dcosRole
	h.IPAddress = ipAddress
	h.MesosID = mesosID
	h.ClusterID = clusterID
	h.Hostname = ipAddress
	h.HTTPClient = httpClient

	return h, nil
}

// RunPoller periodiclly polls the HTTP APIs of a Mesos agent. This function
// should be run in its own goroutine.
func (h *DCOSHost) RunPoller() {
	ticker := time.NewTicker(h.PollPeriod)

	// Poll once immediately
	for _, m := range h.transform(h.pollHost()) {
		h.MetricsChan <- m
	}

	for {
		select {
		case _ = <-ticker.C:
			for _, m := range h.transform(h.pollHost()) {
				h.MetricsChan <- m
			}
		}
	}
}

// pollHost queries the DC/OS hsot for metrics and returns.
func (h *DCOSHost) pollHost() metricsMeta {
	metrics := metricsMeta{}
	now := time.Now().UTC()
	metrics.timestamp = now.Unix()

	// Fetch node-level metrics for all DC/OS roles
	nodeColLog.Debugf("Fetching node-level metrics from DC/OS host %s:%d", h.IPAddress, h.Port)
	nm, err := h.getNodeMetrics()
	if err != nil {
		nodeColLog.Errorf("Failed to get node-level metrics. %s", err)
	} else {
		metrics.nodeMetrics = nm
	}

	if h.DCOSRole == dcos.RoleAgent {
		// always fetch/emit agent state first: downstream will use it for tagging metrics
		nodeColLog.Debugf("Fetching state from DC/OS host %s:%d", h.IPAddress, h.Port)
		agentState, err := h.getAgentState()

		if err != nil {
			nodeColLog.Errorf("Failed to get agent state from %s:%d. Error: %s", h.IPAddress, h.Port, err)
		} else {
			metrics.agentState = agentState
		}

		nodeColLog.Debugf("Fetching container metrics from DC/OS host %s:%d", h.IPAddress, h.Port)
		containerMetrics, err := h.getContainerMetrics()
		if err != nil {
			nodeColLog.Errorf("Failed to get container metrics from %s:%d. Error: %s", h.IPAddress, h.Port, err)
		} else {
			metrics.containerMetrics = containerMetrics
		}
	}

	nodeColLog.Infof("Finished polling DC/OS host %s:%d, took %f seconds.", h.IPAddress, h.Port, time.Since(now).Seconds())

	return metrics
}

// transform will take metrics retrieved from the agent and perform any
// transformations necessary to make the data fit the output expected by
// producers.MetricsMessage.
func (h *DCOSHost) transform(in metricsMeta) (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	t := time.Unix(in.timestamp, 0)

	// Produce node metrics
	msg = producers.MetricsMessage{
		Name:       producers.NodeMetricPrefix,
		Datapoints: buildDatapoints(in.nodeMetrics, t),
		Dimensions: producers.Dimensions{
			MesosID:   h.MesosID,
			ClusterID: h.ClusterID,
			Hostname:  h.IPAddress,
		},
		Timestamp: t.UTC().Unix(),
	}
	out = append(out, msg)

	// Produce container metrics
	for _, c := range in.containerMetrics {
		msg = producers.MetricsMessage{
			Name:       producers.ContainerMetricPrefix,
			Datapoints: buildDatapoints(c, t),
			Timestamp:  t.UTC().Unix(),
		}

		fi, ok := getFrameworkInfoByFrameworkID(c.FrameworkID, in.agentState.Frameworks)
		if !ok {
			nodeColLog.Warnf("Did not find FrameworkInfo for framework ID %s, skipping!", fi.ID)
			continue
		}

		msg.Dimensions = producers.Dimensions{
			MesosID:            h.MesosID,
			ClusterID:          h.ClusterID,
			Hostname:           h.Hostname,
			ContainerID:        c.ContainerID,
			ExecutorID:         c.ExecutorID,
			FrameworkID:        c.FrameworkID,
			FrameworkName:      fi.Name,
			FrameworkRole:      fi.Role,
			FrameworkPrincipal: fi.Principal,
			Labels:             getLabelsByContainerID(c.ContainerID, in.agentState.Frameworks),
		}
		out = append(out, msg)
	}

	return out
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
			nodeColLog.Warn("Unable to build datapoint for metric with name %s, skipping", jsonName)
			continue
		}
		if err := tmpl.Execute(&parsed, v.Interface()); err != nil {
			nodeColLog.Warn("Unable to build datapoint for metric with name %s, skipping", jsonName)
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
