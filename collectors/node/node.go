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

package node

import (
	"bytes"
	"reflect"
	"strings"
	"text/template"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
)

var nodeColLog = log.WithFields(log.Fields{
	"collector": "node",
})

// Collector defines the collector type for system-level metrics.
type Collector struct {
	PollPeriod time.Duration `yaml:"poll_period,omitempty"`

	MetricsChan chan producers.MetricsMessage
	NodeInfo    collectors.NodeInfo

	nodeMetrics nodeMetrics
	timestamp   int64
}

// RunPoller periodiclly polls the HTTP APIs of a Mesos agent. This function
// should be run in its own goroutine.
func (h *Collector) RunPoller() {
	for {
		h.pollHost()
		for _, m := range h.transform() {
			h.MetricsChan <- m
		}
		time.Sleep(h.PollPeriod * time.Second)
	}
}

// pollHost queries the DC/OS hsot for metrics and returns.
func (h *Collector) pollHost() {
	now := time.Now().UTC()
	h.timestamp = now.Unix()

	// Fetch node-level metrics for all DC/OS roles
	nodeColLog.Debugf("Fetching node-level metrics from DC/OS host %s", h.NodeInfo.Hostname)
	nm, err := h.getNodeMetrics()
	if err != nil {
		nodeColLog.Errorf("Failed to get node-level metrics. %s", err)
	} else {
		h.nodeMetrics = nm
	}

	nodeColLog.Infof("Finished polling DC/OS host %s, took %f seconds.", h.NodeInfo.Hostname, time.Since(now).Seconds())
}

// transform will take metrics retrieved from the agent and perform any
// transformations necessary to make the data fit the output expected by
// producers.MetricsMessage.
func (h *Collector) transform() (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	t := time.Unix(h.timestamp, 0)

	// Produce node metrics
	msg = producers.MetricsMessage{
		Name:       producers.NodeMetricPrefix,
		Datapoints: buildDatapoints(h.nodeMetrics, t),
		Dimensions: producers.Dimensions{
			MesosID:   h.NodeInfo.MesosID,
			ClusterID: h.NodeInfo.ClusterID,
			Hostname:  h.NodeInfo.IPAddress,
		},
		Timestamp: t.UTC().Unix(),
	}
	out = append(out, msg)

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
