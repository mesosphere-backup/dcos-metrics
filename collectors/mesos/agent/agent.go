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
	"net/http"
	"strconv"
	"time"

	"github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-metrics/collectors"
	"github.com/dcos/dcos-metrics/producers"
)

const (
	// HTTPTIMEOUT defines the maximum duration for all requests
	HTTPTIMEOUT    = 2 * time.Second
	maxLabelLength = 128
)

// Collector defines the collector type for Mesos agent. It is
// configured from main from config file options and pass a new instance of HTTP
// client and a channel for dropping metrics onto.
type Collector struct {
	Port            int           `yaml:"port"`
	PollPeriod      time.Duration `yaml:"poll_period"`
	RequestProtocol string        `yaml:"request_protocol"`
	HTTPClient      *http.Client

	agentState       agentState
	containerMetrics []agentContainer

	// Specifying a field of type *logrus.Entry allows us to create a single
	// logger for this struct, such as logrus.WithFields(). This way, instead of
	// using a global variable for a logger instance, we can do something like
	// c.log.Errorf(). For more info, see the upstream docs at
	// https://godoc.org/github.com/sirupsen/logrus#Entry
	log *logrus.Entry

	metricsChan chan producers.MetricsMessage
	nodeInfo    collectors.NodeInfo
	timestamp   int64

	//basic auth
	Principal string `yaml:"principal"`
	Secret    string `yaml:"secret"`
}

// New creates a new instance of the Mesos agent collector (poller).
func New(cfg Collector, nodeInfo collectors.NodeInfo) (Collector, chan producers.MetricsMessage) {
	c := cfg
	c.log = logrus.WithFields(logrus.Fields{"collector": "mesos-agent"})
	c.nodeInfo = nodeInfo
	c.metricsChan = make(chan producers.MetricsMessage)
	return c, c.metricsChan
}

// RunPoller continually polls the agent on a set interval. This should be run in its own goroutine.
func (c *Collector) RunPoller() {
	for {
		c.pollMesosAgent()
		for _, m := range c.metricsMessages() {
			c.log.Debugf("Sending container metrics to metric chan:\n%+v", m)
			c.metricsChan <- m
		}
		time.Sleep(c.PollPeriod)
	}
}

func (c *Collector) pollMesosAgent() {
	now := time.Now().UTC()
	c.timestamp = now.Unix()

	host := net.JoinHostPort(c.nodeInfo.IPAddress, strconv.Itoa(c.Port))

	// always fetch/emit agent state first: downstream will use it for tagging metrics
	c.log.Debugf("Fetching state from DC/OS host %s", host)
	if err := c.getAgentState(); err != nil {
		c.log.Errorf("Failed to get agent state from %s. Error: %s", host, err)
	}

	c.log.Debugf("Fetching container metrics from host %s", host)
	if err := c.getContainerMetrics(); err != nil {
		c.log.Errorf("Failed to get container metrics from %s. Error: %s", host, err)
	}
}

// metricsMessages() transforms the []agentContainer slice into a slice of
// producers.MetricsMessage{} for ingestion by our larger metrics gathering
// system
func (c *Collector) metricsMessages() (out []producers.MetricsMessage) {
	var msg producers.MetricsMessage
	t := time.Unix(c.timestamp, 0)

	for _, cm := range c.containerMetrics {
		msg = producers.MetricsMessage{
			Name:       producers.ContainerMetricPrefix,
			Datapoints: c.createContainerDatapoints(cm),
			Timestamp:  t.UTC().Unix(),
		}

		fi, ok := getFrameworkInfoByFrameworkID(cm.FrameworkID, c.agentState.Frameworks)
		if !ok {
			c.log.Warnf("Did not find FrameworkInfo for framework ID %s, skipping!", fi.ID)
			continue
		}

		msg.Dimensions = producers.Dimensions{
			MesosID:            c.nodeInfo.MesosID,
			ClusterID:          c.nodeInfo.ClusterID,
			Hostname:           c.nodeInfo.Hostname,
			ContainerID:        cm.ContainerID,
			ExecutorID:         cm.ExecutorID,
			FrameworkID:        cm.FrameworkID,
			FrameworkName:      fi.Name,
			FrameworkRole:      fi.Role,
			FrameworkPrincipal: fi.Principal,
			Labels:             getLabelsByContainerID(cm.ContainerID, c.agentState.Frameworks, c.log),
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
func getLabelsByContainerID(containerID string, frameworks []frameworkInfo, log *logrus.Entry) map[string]string {
	labels := map[string]string{}
	for _, framework := range frameworks {
		log.Debugf("Attempting to add labels to %v framework", framework)
		for _, executor := range framework.Executors {
			log.Debugf("Found executor %v for framework %v", framework, executor)
			if executor.Container == containerID {
				log.Debugf("ContainerID %v for executor %v is a match, adding labels", containerID, executor)
				for _, pair := range executor.Labels {
					if len(pair.Value) > maxLabelLength {
						log.Warnf("Label %s is longer than %d chars; discarding label", pair.Key, maxLabelLength)
						log.Debugf("Discarded label value: %s", pair.Value)
						continue
					}
					log.Debugf("Adding label for containerID %v: %v = %+v", containerID, pair.Key, pair.Value)
					labels[pair.Key] = pair.Value
				}
				return labels
			}
		}
	}
	return labels
}
