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
	"os/exec"
	"reflect"
	"strings"
	"text/template"
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
	agentState       agentState
	nodeMetrics      nodeMetrics
	containerMetrics []agentContainer
	timestamp        int64
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

	log.Debugf("Fetching node-level metrics from agent %s:%d", a.AgentIP, a.Port)
	nm, err := a.getNodeMetrics()
	if err != nil {
		log.Errorf("Failed to get node-level metrics from %s:%d. Error: %s", a.AgentIP, a.Port, err)
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
	metrics.nodeMetrics = nm
	metrics.containerMetrics = containerMetrics
	metrics.timestamp = now.Unix()

	return metrics
}

// transform will take metrics retrieved from the agent and perform any
// transformations necessary to make the data fit the output expected by
// producers.MetricsMessage.
func (a *Agent) transform(in metricsMeta) (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	t := time.Unix(in.timestamp, 0)

	// Produce node metrics
	msg = producers.MetricsMessage{
		Name:       producers.AgentMetricPrefix,
		Datapoints: buildDatapoints(in.nodeMetrics, t),
		Dimensions: producers.Dimensions{
			AgentID:   in.agentState.ID,
			ClusterID: "", // TODO(roger) need to get this from the master
			Hostname:  in.agentState.Hostname,
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
			log.Warnf("Did not find FrameworkInfo for framework ID %s, skipping!", fi.ID)
			continue
		}

		msg.Dimensions = producers.Dimensions{
			AgentID:            in.agentState.ID,
			ClusterID:          "", // TODO(roger) need to get this from the master
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
			log.Warn("Unable to build datapoint for metric with name %s, skipping", jsonName)
			continue
		}
		if err := tmpl.Execute(&parsed, v.Interface()); err != nil {
			log.Warn("Unable to build datapoint for metric with name %s, skipping", jsonName)
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
